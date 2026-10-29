/*
 * WiFi 网页配网 + 网页固件升级 (fw-mini-sdk, AC791x)
 *
 * 流程(用户规格):
 *   上电 → wifi_on → 读存储(wifi_get_mode_stored_info):
 *     有 STA 存储 → 连接, 最多 PROV_STA_ATTEMPTS 次 × 超时 PROV_STA_TIMEOUT_SEC
 *       └ 全部失败 → wifi_enter_ap_mode 切 AP
 *     无存储 → 直接切 AP
 *   AP 模式: SSID=WL82-XXXXXX(尾缀MAC) 密码 12345678, 网页后台 http://192.168.1.1
 *     - 首页: 状态/连接表单/扫描链接/固件上传表单
 *     - /scan: AP 模式下扫描空中SSID(wifi_scan_req 官方支持 AP 扫描)
 *     - /connect: 保存(wifi_store_mode_info→VM 持久化) + wifi_enter_sta_mode 切 STA
 *     - /upload: multipart 上传 update-ota.ufw → 先入 SDRAM 缓存(目标 4MB,
 *       头解析后尽量扩到 file_size 整包落盘后再烧; 不够则分块烧且烧写间隙
 *       MSG_DONTWAIT 抽 TCP 防窗口饿死) → net_update→dual_bank; 流式回传进度
 *       (与 MSC U盘升级同一条通道: jl_file_head_valid_check + CRC16 + 2s 复位)
 *   STA 模式: 网页后台同样在局域网可用。
 *
 * 事件钩子: bt_wifi_app.c 的 wifi_event_callback(MODULE_INIT → 默认模式选择;
 * 其余事件 → wifi_prov_notify 置状态位)。回调注册 + wifi_on 在 bt_wifi_app.c
 * 的 wifi_app_task(wifi_start() 创建), 本文件 wifi_prov_run() 为其状态机主体。
 */
#include "app_config.h"
#include "typedef.h"
#include "os/os_api.h"
#include "system/malloc.h"
#include "lwip.h"
#include "lwip/sockets.h"
#include "wifi/wifi_connect.h"
#include "net_update.h"                     /* CONFIG_UPGRADE_OTA_FILE_NAME */
#include "asm/wdt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 网页 OTA: 整包缓存目标 5MB(当前 update-ota.ufw ~4.24MB, 2026-10-29 用户要求
 * 4MB→5MB —— 文件已超 4MiB, 整包进缓存可完全避开 spill 模式的 CRC 风险;
 * 实测双 CDC 删 3MB RAM 盘后 SDRAM 堆空闲 ~7.8MB, 5MB 放得下)。失败对半降级。
 * 中途刷 flash 若不抽 TCP, 对端窗口堵死 → recv stall → net_update err。 */
#define OTA_CACHE_TARGET   (5u * 1024u * 1024u)
#define OTA_CACHE_MIN      (64u * 1024u)
#define OTA_BURN_CHUNK     (4u * 1024u)
#define OTA_SPILL_SIZE     (256u * 1024u)

#if MINI_WIFI_SUPPORT

/* 0=关掉 [prov] 刷屏; [ota-http] 保留。排障配网时改 1 */
#ifndef MINI_WIFI_VERBOSE_LOG
#define MINI_WIFI_VERBOSE_LOG  0
#endif
#if MINI_WIFI_VERBOSE_LOG
#define prov_log(...)  printf(__VA_ARGS__)
#else
#define prov_log(...)  ((void)0)
#endif

#define PROV_AP_PWD            "12345678"
#define PROV_STA_TIMEOUT_SEC   25          /* 单次连接超时 */
#define PROV_STA_ATTEMPTS      3           /* STA 重试次数, 全败转 AP */
#define PROV_HTTP_PORT         80

/* JL_FILE_HEAD: update.a 加密头解密后的明文布局(32B), 与 cdc_acm_msc_app.c 同款 */
typedef struct {
    u16 crc; u16 data_crc; u32 addr; u32 length;
    u8 attr; u8 res; u16 idx; char name[16];
} __attribute__((packed)) JL_FILE_HEAD_PLAIN;

extern int jl_file_head_valid_check(void *head);             /* update.a(任务上下文) */
extern uint16_t calc_crc16_with_init_val(uint16_t init_crc, uint8_t *ptr, uint16_t len);
extern int init_net_device_mac_addr(char *macaddr, char ap_mode);
extern void wifi_set_lan_setting_info(void);                 /* bt_wifi_app.c(AP IP/DHCP池) */
/* 升级诊断: 用官方 BootInfo 布局, 避免栈上自拟结构与库写宽不一致 */
#include "dual_bank_updata_api.h"
extern u32 get_target_udate_addr(void);
extern u32 dual_bank_update_read_data(u32 offset, u8 *read_buf, u32 read_len);

/* OTA 期间挂起 LCD/LVGL 刷屏(EMI 高频写会与 flash/WiFi 抢总线 → misalign) */
volatile u8 g_ui_suspend;
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
extern void disp_disable_update(void);
extern void disp_enable_update(void);
#endif

/* ---- 状态(事件回调置位, 任务轮询) ---- */
static volatile u8 prov_dhcp_ok;         /* STA: DHCP 成功 */
static volatile u8 prov_sta_fail;        /* STA: 超时/关联失败 */
static volatile u8 prov_ap_active;       /* AP 已启动 */
static volatile u8 prov_scan_done;       /* 扫描完成 */
static volatile u8 portal_sta_req;       /* 网页发起的 STA 切换(失败要回 AP) */
static u8 prov_has_stored;
static char prov_stored_ssid[33];
static char prov_stored_pwd[64];
static char prov_ap_ssid[33];

/* ================ 事件钩子(bt_wifi_app.c wifi_event_callback 转发) ================ */
void wifi_prov_notify(enum WIFI_EVENT event)
{
    switch (event) {
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        prov_dhcp_ok = 1;
        break;
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        prov_sta_fail = 1;
        break;
    case WIFI_EVENT_AP_START:
        prov_ap_active = 1;
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        prov_scan_done = 1;
        break;
    default:
        break;
    }
}

/* ================ MODULE_INIT 默认模式选择(库回调上下文, 不可阻塞) ================
 * 读存储: 有 STA 配置 → 强制 STA 连它; 无 → 强制 AP(正式 AP 名由任务再切)。 */
void wifi_prov_default_mode_setup(void)
{
    struct wifi_store_info parm;
    struct wifi_mode_info stored;

    wifi_set_lan_setting_info();         /* AP IP=192.168.1.1 / DHCP 池 */
    wifi_set_sta_connect_timeout(PROV_STA_TIMEOUT_SEC);

    memset(&parm, 0, sizeof(parm));
    memset(&stored, 0, sizeof(stored));
    stored.mode = STA_MODE;
    if (wifi_get_mode_stored_info(&stored) == 0 &&
        stored.mode == STA_MODE && stored.ssid && stored.ssid[0]) {
        prov_has_stored = 1;
        strncpy(prov_stored_ssid, stored.ssid, sizeof(prov_stored_ssid) - 1);
        if (stored.pwd) {
            strncpy(prov_stored_pwd, stored.pwd, sizeof(prov_stored_pwd) - 1);
        }
        parm.mode = STA_MODE;
        strncpy((char *)parm.ssid[0], prov_stored_ssid, sizeof(parm.ssid[0]) - 1);
        strncpy((char *)parm.pwd[0], prov_stored_pwd, sizeof(parm.pwd[0]) - 1);
        parm.connect_best_network = 0;
        wifi_set_default_mode(&parm, 1, 0);
        prov_log("[prov] stored wifi '%s'\n", prov_stored_ssid);
    } else {
        prov_has_stored = 0;
        parm.mode = AP_MODE;
        strncpy((char *)parm.ssid[0], "WL82-SETUP", sizeof(parm.ssid[0]) - 1);
        strncpy((char *)parm.pwd[0], PROV_AP_PWD, sizeof(parm.pwd[0]) - 1);
        wifi_set_default_mode(&parm, 1, 0);
        prov_log("[prov] no stored wifi, boot to AP\n");
    }
}

/* ================ STA 单次尝试等待 ================ */
static int prov_wait_sta_once(void)
{
    int waited = 0;

    prov_dhcp_ok = 0;
    prov_sta_fail = 0;
    while (waited < (PROV_STA_TIMEOUT_SEC + 10) * 100) {    /* 单位: 10ms tick */
        if (prov_dhcp_ok ||
            wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
            return 0;
        }
        if (prov_sta_fail) {
            return -1;
        }
        os_time_dly(5);                  /* 50ms */
        waited += 5;
        wdt_clear();
    }
    return -1;
}

/* ================ HTTP 基础 ================ */
#define HTML_HDR_200 \
    "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
#define JSON_HDR_200 \
    "HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n"

#define PROV_SCAN_MAX   24
#define PROV_SCAN_WAIT  300             /* 300×50ms = 15s, 覆盖 13 信道 */

static struct {
    char ssid[33];
    s8   rssi;
    u8   ch;
} prov_scan_list[PROV_SCAN_MAX];
static u32 prov_scan_cnt;

static int http_send(int fd, const char *hdr, const char *body)
{
    if (send(fd, hdr, strlen(hdr), 0) < 0) {
        return -1;
    }
    int len = strlen(body);
    int off = 0;
    while (off < len) {
        int ret = send(fd, body + off, len - off, 0);
        if (ret <= 0) {
            return -1;
        }
        off += ret;
    }
    return 0;
}

/* JSON 字符串转义 → dst, 返回写入长度(不含 NUL) */
static int json_escape(char *dst, int dst_sz, const char *src)
{
    int o = 0;
    if (dst_sz < 2) {
        return 0;
    }
    for (int i = 0; src[i] && o < dst_sz - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (o >= dst_sz - 3) {
                break;
            }
            dst[o++] = '\\';
            dst[o++] = c;
        } else if ((u8)c < 0x20) {
            continue;
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = 0;
    return o;
}

/* HTML attribute 转义(双引号属性) */
static int html_attr_escape(char *dst, int dst_sz, const char *src)
{
    int o = 0;
    for (int i = 0; src[i] && o < dst_sz - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '<' || c == '>' || c == '&' || (u8)c < 0x20) {
            continue;
        }
        dst[o++] = c;
    }
    dst[o] = 0;
    return o;
}

/*
 * AP/STA 扫描空中 SSID.
 * 旧实现问题:
 *  1) wifi_scan_req()!=0 时直接跳过等待 → 清空后立刻取结果必为 0
 *  2) AP_START 曾把 store_ssid_cnt 置 0 + 未开 rxfilter, 列表异常
 *  3) 未 free(wifi_get_scan_result) 泄漏
 * 做法: 不过滤 → 清缓存 → 最多 2 轮 scan(轮间累积) → 总是等 COMPLETED/超时 → 拷贝到本地表
 */
static u32 prov_do_scan(void)
{
    wifi_rxfilter_cfg(prov_ap_active ? 1 : 0);
    wifi_clear_scan_result();
    prov_scan_cnt = 0;

    for (int round = 0; round < 2; round++) {
        prov_scan_done = 0;
        int req = wifi_scan_req();
        prov_log("[prov] scan round=%d req=%d (0=started,-1=busy/in-progress)\n",
               round, req);
        /* req==-1 也可能是「正在扫」, 仍要等 COMPLETED; 不可因非 0 直接放弃 */
        int waited = 0;
        for (; waited < PROV_SCAN_WAIT && !prov_scan_done; waited++) {
            os_time_dly(5);
            wdt_clear();
        }
        prov_log("[prov] scan round=%d done=%d waited=%dms\n",
               round, prov_scan_done, waited * 50);
        if (!prov_scan_done && round == 0) {
            /* 首轮无事件: 再给一轮机会 */
            continue;
        }
        /* 首轮成功也再扫一轮累积更多 SSID(官方 demo 同款策略) */
        if (round == 0) {
            os_time_dly(10);
        }
    }

    u32 cnt = 0;
    struct wifi_scan_ssid_info *r = wifi_get_scan_result(&cnt);
    prov_log("[prov] scan raw cnt=%u\n", (unsigned)cnt);

    for (u32 i = 0; r && i < cnt && prov_scan_cnt < PROV_SCAN_MAX; i++) {
        u32 len = r[i].ssid_len;
        if (len == 0) {
            len = 0;
            while (len < 32 && r[i].ssid[len]) {
                len++;
            }
        }
        if (len == 0 || len > 32) {
            continue;                    /* 隐藏 SSID / 异常跳过 */
        }
        /* 去重 */
        char tmp[33];
        memcpy(tmp, r[i].ssid, len);
        tmp[len] = 0;
        u8 dup = 0;
        for (u32 j = 0; j < prov_scan_cnt; j++) {
            if (!strcmp(prov_scan_list[j].ssid, tmp)) {
                if (r[i].rssi > prov_scan_list[j].rssi) {
                    prov_scan_list[j].rssi = r[i].rssi;
                    prov_scan_list[j].ch = (u8)r[i].channel_number;
                }
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        memcpy(prov_scan_list[prov_scan_cnt].ssid, tmp, len + 1);
        prov_scan_list[prov_scan_cnt].rssi = r[i].rssi;
        prov_scan_list[prov_scan_cnt].ch = (u8)r[i].channel_number;
        prov_scan_cnt++;
    }
    if (r) {
        free(r);
    }
    prov_log("[prov] scan unique cnt=%u\n", (unsigned)prov_scan_cnt);
    return prov_scan_cnt;
}

/* 共享样式: 离线可用(AP 模式无外网), 石板+青绿, 避免依赖外部字体 */
static const char *HTML_CSS =
    ":root{--bg:#0f172a;--card:#1e293b;--line:#334155;--tx:#e2e8f0;--mut:#94a3b8;"
    "--acc:#14b8a6;--acc2:#0d9488;--warn:#f59e0b;--danger:#f87171;--ok:#34d399}"
    "*{box-sizing:border-box}body{margin:0;font:15px/1.5 system-ui,-apple-system,Segoe UI,sans-serif;"
    "background:radial-gradient(1200px 600px at 10% -10%,#134e4a 0%,transparent 55%),"
    "radial-gradient(900px 500px at 100% 0%,#1e3a5f 0%,transparent 50%),var(--bg);color:var(--tx);min-height:100vh}"
    ".wrap{max-width:520px;margin:0 auto;padding:20px 16px 40px}"
    ".brand{display:flex;align-items:center;gap:12px;margin-bottom:18px}"
    ".logo{width:40px;height:40px;border-radius:12px;background:linear-gradient(135deg,var(--acc),#38bdf8);"
    "display:grid;place-items:center;font-weight:700;color:#042f2e;box-shadow:0 8px 24px #14b8a655}"
    "h1{font-size:1.25rem;margin:0} .sub{color:var(--mut);font-size:.85rem;margin:2px 0 0}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px 16px 14px;"
    "margin:0 0 14px;box-shadow:0 10px 30px #00000040}"
    ".card h2{margin:0 0 10px;font-size:1rem;letter-spacing:.02em}"
    ".pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;"
    "background:#0f766e33;color:var(--acc);font-size:.8rem;border:1px solid #14b8a644}"
    ".pill::before{content:'';width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 8px var(--ok)}"
    "label{display:block;color:var(--mut);font-size:.8rem;margin:10px 0 4px}"
    "input[type=text],input[type=password],input[type=file]{width:100%;padding:11px 12px;border-radius:10px;"
    "border:1px solid var(--line);background:#0b1220;color:var(--tx);outline:none}"
    "input:focus{border-color:var(--acc);box-shadow:0 0 0 3px #14b8a633}"
    ".row{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}"
    "button,.btn{appearance:none;border:0;border-radius:10px;padding:11px 16px;font-weight:600;cursor:pointer;"
    "background:linear-gradient(180deg,var(--acc),var(--acc2));color:#042f2e;text-decoration:none;display:inline-block}"
    "button.secondary,.btn.secondary{background:#334155;color:var(--tx)}"
    "button:disabled{opacity:.55;cursor:wait}"
    ".list{list-style:none;margin:0;padding:0;max-height:280px;overflow:auto}"
    ".list li{display:flex;align-items:center;gap:10px;padding:10px 10px;border-radius:10px;cursor:pointer;"
    "border:1px solid transparent;margin:0 0 6px;background:#0b1220}"
    ".list li:hover,.list li.on{border-color:var(--acc);background:#134e4a44}"
    ".list .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:600}"
    ".list .m{color:var(--mut);font-size:.78rem;white-space:nowrap}"
    ".bars{width:18px;height:14px;display:flex;align-items:flex-end;gap:2px}"
    ".bars i{display:block;width:3px;background:var(--mut);border-radius:1px}"
    ".bars i.on{background:var(--acc)}"
    ".tip{color:var(--mut);font-size:.8rem;margin:8px 0 0}"
    ".err{color:var(--danger)}.ok{color:var(--ok)}"
    ".prog{display:none;margin-top:12px}.prog.on{display:block}"
    ".prog .lab{display:flex;justify-content:space-between;font-size:.78rem;color:var(--mut);margin:6px 0 2px}"
    ".bar{height:10px;background:#0b1220;border-radius:999px;overflow:hidden;border:1px solid var(--line)}"
    ".bar>i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--acc),#38bdf8);"
    "transition:width .12s linear}"
    ".foot{text-align:center;color:var(--mut);font-size:.75rem;margin-top:18px}";

static int http_render_portal(int fd)
{
    char *body = malloc(10240);
    if (!body) {
        return -1;
    }
    const char *mode = prov_ap_active ? "AP 配网" : (prov_dhcp_ok ? "STA 已联网" : "STA 连接中");
    const char *ip = prov_ap_active ? "192.168.1.1" : "路由器 DHCP";
    snprintf(body, 10240,
        "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>"
        "<title>WL82 设备后台</title><style>%s</style></head><body><div class=wrap>"
        "<div class=brand><div class=logo>W</div><div>"
        "<h1>WL82 设备后台</h1><div class=sub>配网 · 固件升级</div></div></div>"
        "<div class=card><span class=pill>%s</span>"
        "<p class=tip style=margin-top:10px>访问地址 <b>%s</b> · AP 密码 %s</p></div>"
        "<div class=card><h2>WiFi 连接</h2>"
        "<div class=row><button type=button id=btnScan>搜索附近 WiFi</button>"
        "<span id=scanMsg class=tip></span></div>"
        "<ul class=list id=apList></ul>"
        "<form method=POST action=/connect id=fConn>"
        "<label>SSID</label><input name=ssid id=ssid maxlength=32 placeholder='选择上方网络或手动输入'>"
        "<label>密码</label><input name=pwd type=password maxlength=64 placeholder='开放网络可留空'>"
        "<div class=row><button type=submit>连接 WiFi</button></div>"
        "<p class=tip>连接后热点会断开；失败约 60 秒自动回到配网模式。</p></form></div>"
        "<div class=card><h2>固件升级</h2>"
        "<form id=fUp method=POST action=/upload enctype='multipart/form-data'>"
        "<label>升级文件 (update-ota.ufw)</label>"
        "<input type=file name=fw id=fw accept='.ufw,application/octet-stream'>"
        "<div class=row><button type=submit id=btnUp>上传并升级</button></div>"
        "<div class=prog id=otaProg>"
        "<div class=lab><span>上传</span><span id=upPct>0%%</span></div><div class=bar><i id=upBar></i></div>"
        "<div class=lab><span>烧录</span><span id=burnPct>0%%</span></div><div class=bar><i id=burnBar></i></div>"
        "<p class=tip id=otaMsg>准备中…</p></div>"
        "<p class=tip>先整包进 SDRAM 再烧录。浏览器进度≠设备进度；软AP上接收约需数十秒，收齐后才开始烧录条。</p></form></div>"
        "<div class=foot>fw-mini-sdk · CherryUSB / WiFi portal</div></div>"
        "<script>"
        "function bars(r){var n=r>-55?4:r>-65?3:r>-75?2:1,h=[4,7,10,14],s='<span class=bars>',i;"
        "for(i=0;i<4;i++)s+='<i class=\"'+(i<n?'on':'')+'\" style=\"height:'+h[i]+'px\"></i>';"
        "return s+'</span>';}"
        "function doScan(){"
        "var b=document.getElementById('btnScan'),m=document.getElementById('scanMsg'),ul=document.getElementById('apList');"
        "b.disabled=true;m.className='tip';m.textContent='扫描中，约需数秒…';ul.innerHTML='';"
        "fetch('/api/scan',{cache:'no-store'}).then(function(t){return t.json()}).then(function(j){"
        "m.textContent='找到 '+j.n+' 个网络';m.className=j.n?'tip ok':'tip err';ul.innerHTML='';"
        "if(!j.n){var e=document.createElement('li');e.style.cursor='default';"
        "e.innerHTML='<span class=n>未扫到网络，请靠近路由器后重试</span>';ul.appendChild(e);return;}"
        "(j.list||[]).forEach(function(a){var li=document.createElement('li');"
        "li.innerHTML=bars(a.r)+'<span class=n></span><span class=m></span>';"
        "li.querySelector('.n').textContent=a.s;"
        "li.querySelector('.m').textContent='CH'+a.c+' '+a.r+'dB';"
        "li.onclick=function(){var x=ul.querySelectorAll('li'),k;for(k=0;k<x.length;k++)x[k].classList.remove('on');"
        "li.classList.add('on');document.getElementById('ssid').value=a.s;};ul.appendChild(li);});"
        "}).catch(function(){m.textContent='扫描失败(AP扫信道时手机可能短暂掉线，请重试)';m.className='tip err';})"
        ".finally(function(){b.disabled=false});}"
        "document.getElementById('btnScan').onclick=doScan;"
        "function setBar(bar,pct,lab){pct=Math.max(0,Math.min(100,pct|0));"
        "document.getElementById(bar+'Bar').style.width=pct+'%%';"
        "document.getElementById(bar+'Pct').textContent=pct+'%%';"
        "if(lab)document.getElementById('otaMsg').textContent=lab;}"
        "document.getElementById('fUp').onsubmit=function(ev){"
        "ev.preventDefault();var f=document.getElementById('fw').files[0];"
        "if(!f){alert('请选择 update-ota.ufw');return;}"
        "var btn=document.getElementById('btnUp'),prog=document.getElementById('otaProg');"
        "btn.disabled=true;prog.classList.add('on');setBar('up',0,'上传中…');setBar('burn',0);"
        "var fd=new FormData(document.getElementById('fUp')),xhr=new XMLHttpRequest(),seen=0;"
        "xhr.open('POST','/upload');"
        "xhr.upload.onprogress=function(e){if(e.lengthComputable)"
        "setBar('up',e.loaded*100/e.total,'上传 '+(e.loaded/1048576).toFixed(2)+' / '+(e.total/1048576).toFixed(2)+' MB');};"
        "xhr.upload.onload=function(){setBar('up',100,'浏览器已发完，设备接收中…');};"
        "function parseOta(t){t.split('\\n').forEach(function(line){line=line.trim();if(!line)return;"
        "if(line.charAt(0)==='B')setBar('burn',parseInt(line.slice(1),10)||0,'烧录中…');"
        "else if(line.indexOf('OK')===0){setBar('burn',100,'校验通过，即将重启…');"
        "document.getElementById('otaMsg').className='tip ok';}"
        "else if(line.indexOf('ERR')===0){document.getElementById('otaMsg').textContent=line;"
        "document.getElementById('otaMsg').className='tip err';btn.disabled=false;}});}"
        "xhr.onprogress=function(){var t=xhr.responseText||'',chunk=t.slice(seen);seen=t.length;parseOta(chunk);};"
        "xhr.onload=function(){parseOta((xhr.responseText||'').slice(seen));"
        "if(xhr.status!==200){document.getElementById('otaMsg').textContent='HTTP '+xhr.status;"
        "document.getElementById('otaMsg').className='tip err';btn.disabled=false;}};"
        "xhr.onerror=function(){document.getElementById('otaMsg').textContent='网络中断';"
        "document.getElementById('otaMsg').className='tip err';btn.disabled=false;};"
        "xhr.send(fd);};"
        "</script></body></html>",
        HTML_CSS, mode, ip, PROV_AP_PWD);
    int ret = http_send(fd, HTML_HDR_200, body);
    free(body);
    return ret;
}

static int http_render_scan_json(int fd)
{
    char *body = malloc(4096);
    if (!body) {
        return -1;
    }
    u32 n = prov_do_scan();
    int off = snprintf(body, 4096, "{\"n\":%u,\"list\":[", (unsigned)n);
    for (u32 i = 0; i < n && off < 4096 - 96; i++) {
        char esc[80];
        json_escape(esc, sizeof(esc), prov_scan_list[i].ssid);
        off += snprintf(body + off, 4096 - off, "%s{\"s\":\"%s\",\"r\":%d,\"c\":%u}",
                        i ? "," : "", esc,
                        (int)prov_scan_list[i].rssi,
                        (unsigned)prov_scan_list[i].ch);
    }
    if (off < 4096 - 3) {
        snprintf(body + off, 4096 - off, "]}");
    }
    int ret = http_send(fd, JSON_HDR_200, body);
    free(body);
    return ret;
}

static int http_render_scan(int fd)
{
    /* 兼容旧 /scan 链接: 扫完后回现代列表页 */
    char *body = malloc(6144);
    if (!body) {
        return -1;
    }
    u32 n = prov_do_scan();
    int off = snprintf(body, 6144,
        "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>WiFi 列表</title><style>%s</style></head><body><div class=wrap>"
        "<div class=brand><div class=logo>W</div><div><h1>扫描结果</h1>"
        "<div class=sub>共 %u 个网络</div></div></div>"
        "<div class=card><form method=POST action=/connect>",
        HTML_CSS, (unsigned)n);
    for (u32 i = 0; i < n && off < 6144 - 220; i++) {
        char esc[40];
        html_attr_escape(esc, sizeof(esc), prov_scan_list[i].ssid);
        off += snprintf(body + off, 6144 - off,
            "<label class=list style=display:block><li>"
            "<input type=radio name=ssid value=\"%s\" %s>"
            "<span class=n>%s</span>"
            "<span class=m>CH%u %ddB</span></li></label>",
            esc, i == 0 ? "checked" : "", esc,
            (unsigned)prov_scan_list[i].ch, (int)prov_scan_list[i].rssi);
    }
    if (n == 0) {
        off += snprintf(body + off, 6144 - off,
            "<p class='tip err'>未扫到网络。AP 扫描会短暂离信道，请重试；"
            "也可在首页手动输入 SSID。</p>"
            "<label>SSID</label><input name=ssid maxlength=32>");
    }
    off += snprintf(body + off, 6144 - off,
        "<label>密码</label><input name=pwd type=password maxlength=64>"
        "<div class=row><button type=submit>连接</button>"
        "<a class='btn secondary' href=/>返回</a></div></form></div>"
        "<div class=foot><a href=/api/scan style=color:var(--mut)>JSON</a></div>"
        "</div></body></html>");
    int ret = http_send(fd, HTML_HDR_200, body);
    free(body);
    return ret;
}

/* ================ POST /connect: 保存 + 切 STA ================ */
static int urldecode(char *dst, const char *src, int maxlen)
{
    int o = 0;
    for (int i = 0; src[i] && o < maxlen - 1; i++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hx[3] = {src[i+1], src[i+2], 0};
            dst[o++] = (char)strtol(hx, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[o++] = ' ';
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = 0;
    return o;
}

static void http_handle_connect(int conn, const char *body)
{
    char field[160], ssid[33], pwd[64];
    const char *s = strstr(body, "ssid=");
    const char *w = strstr(body, "pwd=");
    const char *e;

    if (!s || !w) {
        goto bad;
    }
    s += 5;
    e = s;
    while (*e && *e != '&' && e - s < (int)sizeof(field) - 1) {
        e++;
    }
    memcpy(field, s, e - s);
    field[e - s] = 0;
    urldecode(ssid, field, sizeof(ssid));
    urldecode(pwd, w + 4, sizeof(pwd));

    if (!ssid[0]) {
        goto bad;
    }
    prov_log("[prov] connect req ssid='%s' pwd_len=%d\n", ssid, (int)strlen(pwd));

    /* 先回包再切模式(切 STA 会断开 AP, 手机立即失联) */
    {
        char *resp = malloc(4600);
        if (resp) {
            snprintf(resp, 4600,
                "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
                "<meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>连接中</title><style>%s</style></head><body><div class=wrap>"
                "<div class=card><h2>正在连接</h2>"
                "<p><b>%s</b></p>"
                "<p class=tip>设备已切换到 STA，热点会断开。若约 1 分钟未连上，"
                "将自动回到 WL82 配网模式，请重新连接热点后重试。</p>"
                "</div></div></body></html>", HTML_CSS, ssid);
            http_send(conn, HTML_HDR_200, resp);
            free(resp);
        }
    }
    os_time_dly(30);                     /* 让响应完全发出 */

    wifi_store_mode_info(STA_MODE, ssid, pwd);
    wifi_enter_sta_mode(ssid, pwd);
    portal_sta_req = 1;                  /* 状态机监视: 连不上自动回 AP */
    return;
bad:
    send(conn, "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n", 46, 0);
}

/* ================ POST /upload: SDRAM 缓存 → dual_bank 烧写 ================ */
static struct {
    void *fd;
    u32  recv_total;                     /* 已收文件字节(含双层头) */
    u32  burn_total;                     /* 已写入 flash 字节 */
    u32  file_size;                      /* 头声明长度 */
    u16  data_crc;
    u16  calc_crc;
    u8   head_ok;
    u8   crc_skip;                       /* CRC 跳过文件头 64B */
    u8   resp_hdr;                       /* 已发 text/plain 响应头 */
} upl;

#define OTA_RESP_HDR \
    "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n"

static void ota_ui_resume(void)
{
    g_ui_suspend = 0;
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
    disp_enable_update();
#endif
}

static void ota_send_line(int conn, const char *line)
{
    if (!upl.resp_hdr) {
        send(conn, OTA_RESP_HDR, strlen(OTA_RESP_HDR), 0);
        upl.resp_hdr = 1;
    }
    send(conn, line, strlen(line), 0);
}

static void ota_send_err(int conn, const char *msg)
{
    char line[192];
    snprintf(line, sizeof(line), "ERR %s\n", msg);
    ota_send_line(conn, line);
}

static void ota_send_burn_pct(int conn, u32 burned, u32 total)
{
    char line[24];
    u32 pct = total ? (burned * 100u / total) : 0;
    if (pct > 100) {
        pct = 100;
    }
    snprintf(line, sizeof(line), "B%u\n", (unsigned)pct);
    ota_send_line(conn, line);
}

/* 申请 OTA 缓存: 先目标大小, 失败则对半降级(4K 对齐)。
 * 不用 xPortGetFreeHeapSize: 本工程链入的是杰理自定义堆, 该符号未导出。 */
static u8 *ota_cache_alloc(u32 *out_size)
{
    u32 try_sz = OTA_CACHE_TARGET;
    while (try_sz >= OTA_CACHE_MIN) {
        u8 *p = (u8 *)malloc(try_sz);
        if (p) {
            *out_size = try_sz;
            printf("[ota-http] cache %u KB%s\n",
                   (unsigned)(try_sz / 1024),
                   try_sz == OTA_CACHE_TARGET ? " (target)" : " (fallback)");
            return p;
        }
        printf("[ota-http] malloc %u KB fail, halve\n", (unsigned)(try_sz / 1024));
        try_sz = (try_sz / 2) & ~0xFFFu;
    }
    *out_size = 0;
    return NULL;
}

/* 头解析后尽量把缓存扩到整包, 避免边收边烧堵死 TCP */
static int ota_cache_grow(u8 **cache, u32 *cap, u32 need)
{
    u32 ncap;
    u8 *p;

    if (!cache || !*cache || !cap || *cap >= need) {
        return 0;
    }
    ncap = (need + 0xFFFu) & ~0xFFFu;
    if (ncap < need) {
        ncap = need;
    }
    p = (u8 *)realloc(*cache, ncap);
    if (!p) {
        printf("[ota-http] grow cache %u→%u KB fail, keep %u KB (spill mode)\n",
               (unsigned)(*cap / 1024), (unsigned)(ncap / 1024),
               (unsigned)(*cap / 1024));
        return -1;
    }
    printf("[ota-http] grow cache %u→%u KB (fit file)\n",
           (unsigned)(*cap / 1024), (unsigned)(ncap / 1024));
    *cache = p;
    *cap = ncap;
    return 0;
}

/* 烧写间隙非阻塞抽 TCP → spill, 防止对端发送窗口耗尽断链 */
static u8 *ota_spill;
static u32 ota_spill_cap;
static u32 ota_spill_fill;

static void ota_spill_free(void)
{
    if (ota_spill) {
        free(ota_spill);
        ota_spill = NULL;
    }
    ota_spill_cap = 0;
    ota_spill_fill = 0;
}

static int ota_spill_ensure(void)
{
    if (ota_spill) {
        return 0;
    }
    ota_spill = (u8 *)malloc(OTA_SPILL_SIZE);
    if (!ota_spill) {
        printf("[ota-http] spill malloc %u KB fail\n",
               (unsigned)(OTA_SPILL_SIZE / 1024));
        return -1;
    }
    ota_spill_cap = OTA_SPILL_SIZE;
    ota_spill_fill = 0;
    printf("[ota-http] spill %u KB ready\n", (unsigned)(OTA_SPILL_SIZE / 1024));
    return 0;
}

/* 对刚收到的文件字节做 CRC(跳过双头 64B), 并累加 recv_total。
 * 2026-10-29 修: 字节计入钳在 file_size —— 文件尾之后漏进的多余字节(实测
 * +61B, 恰为 multipart 结束边界长度)不再污染 CRC/计数, 那是 spill 模式
 * 大文件(>缓存)升级 crc 校验失败的根因 */
static void ota_account_bytes(const u8 *data, int len)
{
    int cs = 0;
    int left;
    int po;
    u32 take;

    if (len <= 0) {
        return;
    }
    take = (u32)len;
    if (upl.file_size) {
        if (upl.recv_total >= upl.file_size) {
            take = 0;                    /* 已收满: 之后的字节全部丢弃 */
        } else {
            u32 remain = upl.file_size - upl.recv_total;
            if (take > remain) {
                take = remain;
            }
        }
    }
    while (take > (u32)cs && upl.crc_skip) {
        cs++;
        upl.crc_skip--;
    }
    if (take > (u32)cs) {
        left = (int)(take - (u32)cs);
        po = cs;
        while (left > 0) {
            u16 chunk = left > 4096 ? 4096 : (u16)left;
            upl.calc_crc = calc_crc16_with_init_val(upl.calc_crc,
                                                    (u8 *)(data + po), chunk);
            po += chunk;
            left -= chunk;
        }
    }
    upl.recv_total += take;
}

static void ota_tcp_drain(int conn)
{
    int r;
    u32 remain;
    u32 space;
    u32 want;

    if (conn < 0 || !ota_spill || ota_spill_fill >= ota_spill_cap) {
        return;
    }
    if (!upl.file_size || upl.recv_total >= upl.file_size) {
        return;
    }
    while (ota_spill_fill < ota_spill_cap && upl.recv_total < upl.file_size) {
        remain = upl.file_size - upl.recv_total;
        space = ota_spill_cap - ota_spill_fill;
        want = remain < space ? remain : space;
        r = recv(conn, ota_spill + ota_spill_fill, (int)want, MSG_DONTWAIT);
        if (r <= 0) {
            break;
        }
        ota_account_bytes(ota_spill + ota_spill_fill, r);
        ota_spill_fill += (u32)r;
        wdt_clear();
    }
}

/* 把 cache[0..fill) 写入 flash; report=1 时推送烧录进度。
 * 每写一块穿插 TCP drain, 避免长 erase/program 期间连接被掐。 */
static int ota_flush_cache(int conn, u8 *cache, u32 fill, int report)
{
    u32 off = 0;
    if (!fill) {
        return 0;
    }
    if (!upl.fd) {
        upl.fd = net_fopen(CONFIG_UPGRADE_OTA_FILE_NAME, "w");
        if (!upl.fd) {
            return -1;
        }
    }
    /* 分块烧写路径才需要 spill; 整包烧(report=1 且已收完)可不抽 */
    if (conn >= 0 && upl.recv_total < upl.file_size) {
        (void)ota_spill_ensure();
    }
    while (off < fill) {
        u32 n = fill - off;
        if (n > OTA_BURN_CHUNK) {
            n = OTA_BURN_CHUNK;
        }
        int wret = net_fwrite(upl.fd, cache + off, (int)n, 0);
        if (wret != (int)n) {
            printf("[ota-http] net_fwrite fail @%u ret=%d\n",
                   upl.burn_total, wret);
            return -2;
        }
        off += n;
        upl.burn_total += n;
        if (report && upl.file_size) {
            ota_send_burn_pct(conn, upl.burn_total, upl.file_size);
        }
        ota_tcp_drain(conn);
        wdt_clear();
    }
    return 0;
}

/* 先消化 spill, 再写 cache; 缓存满则刷 flash */
static int ota_cache_feed(int conn, u8 *cache, u32 cap, u32 *fill,
                          const u8 *data, int len)
{
    int off = 0;

    /* spill 优先并入主缓存, 保持字节序 */
    while (ota_spill_fill) {
        u32 space = cap - *fill;
        u32 n = ota_spill_fill;
        if (n > space) {
            n = space;
        }
        memcpy(cache + *fill, ota_spill, n);
        *fill += n;
        if (n < ota_spill_fill) {
            memmove(ota_spill, ota_spill + n, ota_spill_fill - n);
        }
        ota_spill_fill -= n;
        if (*fill >= cap) {
            int fr = ota_flush_cache(conn, cache, *fill, 0);
            if (fr) {
                return fr;
            }
            *fill = 0;
        }
    }

    while (off < len) {
        u32 space = cap - *fill;
        u32 n = (u32)(len - off);
        if (n > space) {
            n = space;
        }
        memcpy(cache + *fill, data + off, n);
        *fill += n;
        off += (int)n;
        if (*fill >= cap) {
            int fr = ota_flush_cache(conn, cache, *fill, 0);
            if (fr) {
                return fr;
            }
            *fill = 0;
        }
    }
    return 0;
}

/* 喂文件字节: 入缓存 + CRC/计数(spill 里已 account 过的不再重复)。
 * 入缓存字节同样钳在 file_size —— 尾部多余字节不入 flash(multipart 尾由
 * 收尾循环丢弃) */
static int ota_feed_file(int conn, u8 *cache, u32 cap, u32 *fill,
                         const u8 *data, int len)
{
    int fr;

    if (len <= 0) {
        return 0;
    }
    if (upl.file_size) {
        if (upl.recv_total >= upl.file_size) {
            return 0;                    /* 已收满 */
        }
        u32 remain = upl.file_size - upl.recv_total;
        if ((u32)len > remain) {
            len = (int)remain;
        }
        if (len <= 0) {
            return 0;
        }
    }
    fr = ota_cache_feed(conn, cache, cap, fill, data, len);
    if (fr) {
        return fr;
    }
    ota_account_bytes(data, len);
    return 0;
}

static void http_handle_upload(int conn, const char *boundary,
                               const char *residue, int residue_len)
{
    static u8 window[4096] __attribute__((aligned(4)));
    static u8 recvbuf[8192] __attribute__((aligned(4)));
    static u8 head64[64] __attribute__((aligned(4)));
    (void)boundary;                      /* 已知 file_size 后按精确长度收, 不再扫边界 */

    const char *body = NULL;
    for (int i = 0; i + 4 <= residue_len; i++) {
        if (residue[i] == '\r' && residue[i + 1] == '\n' &&
            residue[i + 2] == '\r' && residue[i + 3] == '\n') {
            body = residue + i + 4;
            break;
        }
    }
    if (!body) {
        goto bad_req;
    }
    int slen = residue_len - (int)(body - residue);

    {
        int on = 1;
        int rcv = 64 * 1024;             /* 烧写间隙靠 drain+大窗撑住对端 */
        int to_ms = 15000;
        setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        setsockopt(conn, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &to_ms, sizeof(to_ms));
    }

    g_ui_suspend = 1;
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
    disp_disable_update();
#endif
    os_time_dly(5);

    memset(&upl, 0, sizeof(upl));
    upl.crc_skip = 64;

    u32 cache_cap = 0;
    u8 *cache = ota_cache_alloc(&cache_cap);
    if (!cache) {
        ota_ui_resume();
        ota_send_err(conn, "SDRAM 缓存申请失败");
        return;
    }
    u32 cache_fill = 0;

    if (net_update_request()) {
        free(cache);
        ota_ui_resume();
        ota_send_err(conn, "OTA 忙(其他升级进行中)");
        return;
    }
    printf("[ota-http] upload start (cache %u KB)\n", (unsigned)(cache_cap / 1024));

    const u8 *stream = (const u8 *)body;
    int wlen = 0;
    int head_got = 0;
    int fail_fr = 0;
    u32 last_log = 0;
    struct BootInfo bi;
    memset(&bi, 0, sizeof(bi));
    int biret = get_current_boot_info(&bi);
    printf("[ota-http] boot info ret=%d ver=%u base=0x%08x len=0x%08x\n",
           biret, bi.version, bi.baseAddress, bi.codeLength);

    /* ---- 阶段1: 滑窗只用于凑齐 64B 双层头 ---- */
    while (head_got < 64) {
        while (wlen < 64 && head_got + wlen < 64) {
            int r;
            if (slen > 0) {
                r = slen > (int)sizeof(window) - wlen ? (int)sizeof(window) - wlen : slen;
                memcpy(window + wlen, stream, r);
                stream += r;
                slen -= r;
                wlen += r;
            } else {
                r = recv(conn, window + wlen, (int)sizeof(window) - wlen, 0);
                if (r <= 0) {
                    free(cache);
                    ota_spill_free();
                    ota_ui_resume();
                    ota_send_err(conn, "收头时连接断开/超时");
                    return;
                }
                wlen += r;
            }
            wdt_clear();
        }
        int need = 64 - head_got;
        int t = wlen < need ? wlen : need;
        memcpy(head64 + head_got, window, t);
        head_got += t;
        memmove(window, window + t, wlen - t);
        wlen -= t;

        if (head_got >= 32 && !upl.head_ok) {
            upl.head_ok = (jl_file_head_valid_check(head64) == 0);
            if (!upl.head_ok) {
                free(cache);
                ota_spill_free();
                ota_ui_resume();
                ota_send_err(conn, "文件头校验失败, 请上传 update-ota.ufw");
                return;
            }
            upl.file_size = ((JL_FILE_HEAD_PLAIN *)head64)->length;
            printf("[ota-http] head ok: len %u\n", upl.file_size);
            /* 尽量整包缓存, 避免边收边烧堵 TCP */
            (void)ota_cache_grow(&cache, &cache_cap, upl.file_size);
        }
        if (head_got == 64) {
            JL_FILE_HEAD_PLAIN *h2 = (JL_FILE_HEAD_PLAIN *)(head64 + 32);
            printf("[ota-http] inner head: %.16s len %u addr 0x%x\n",
                   h2->name, h2->length, h2->addr);
            if (h2->length != upl.file_size - 64 ||
                memcmp(h2->name, "app_core", 8) != 0) {
                free(cache);
                ota_spill_free();
                ota_ui_resume();
                ota_send_err(conn, "内层头异常, 请用构建产出的 update-ota.ufw");
                return;
            }
            upl.data_crc = h2->data_crc;
        }
    }

    /* 头 64B 已从流解析, 仍须计入文件流(recv_total/CRC 约定含双头) */
    fail_fr = ota_feed_file(conn, cache, cache_cap, &cache_fill, head64, 64);
    if (fail_fr) {
        goto feed_fail;
    }

    if (wlen > 0) {
        u32 remain = upl.file_size - upl.recv_total;
        int feed = wlen < (int)remain ? wlen : (int)remain;
        if (feed > 0) {
            fail_fr = ota_feed_file(conn, cache, cache_cap, &cache_fill, window, feed);
            if (fail_fr) {
                goto feed_fail;
            }
            memmove(window, window + feed, wlen - feed);
            wlen -= feed;
        }
    }
    if (slen > 0 && upl.recv_total < upl.file_size) {
        u32 remain = upl.file_size - upl.recv_total;
        int feed = slen < (int)remain ? slen : (int)remain;
        fail_fr = ota_feed_file(conn, cache, cache_cap, &cache_fill, stream, feed);
        if (fail_fr) {
            goto feed_fail;
        }
        stream += feed;
        slen -= feed;
    }

    /* ---- 阶段2: 精确再收 (file_size - recv_total) 字节, 不再留 keep 尾巴 ---- */
    while (upl.recv_total < upl.file_size || ota_spill_fill || cache_fill >= cache_cap) {
        /* 先把 drain 进 spill 的字节并入主缓存(已计 recv_total) */
        fail_fr = ota_cache_feed(conn, cache, cache_cap, &cache_fill, NULL, 0);
        if (fail_fr) {
            goto feed_fail;
        }
        if (upl.recv_total >= upl.file_size && !ota_spill_fill) {
            break;
        }
        u32 remain = upl.file_size - upl.recv_total;
        u32 space = cache_cap - cache_fill;
        if (space == 0) {
            fail_fr = ota_flush_cache(conn, cache, cache_fill, 0);
            if (fail_fr) {
                goto feed_fail;
            }
            cache_fill = 0;
            space = cache_cap;
            /* flush 期间可能又 drain 了 spill */
            continue;
        }
        if (!remain) {
            break;
        }
        u32 bulk = remain;
        if (bulk > space) {
            bulk = space;
        }
        if (bulk > sizeof(recvbuf)) {
            bulk = sizeof(recvbuf);
        }
        int r = recv(conn, recvbuf, (int)bulk, 0);
        if (r <= 0) {
            printf("[ota-http] recv stall/timeout at %u/%u spill=%u\n",
                   (unsigned)upl.recv_total, (unsigned)upl.file_size,
                   (unsigned)ota_spill_fill);
            /* 再试几次: STA/软AP 偶发空窗; 顺带再 drain */
            int retry;
            for (retry = 0; retry < 12 && r <= 0; retry++) {
                wdt_clear();
                ota_tcp_drain(conn);
                if (ota_spill_fill) {
                    break;
                }
                os_time_dly(10);
                r = recv(conn, recvbuf, (int)bulk, 0);
            }
            if (r <= 0 && !ota_spill_fill) {
                free(cache);
                ota_spill_free();
                if (upl.fd) {
                    net_fclose(upl.fd, 1);
                    upl.fd = NULL;
                }
                ota_ui_resume();
                ota_send_err(conn, "接收超时/中断, 请靠近路由器后重试");
                return;
            }
            if (r <= 0) {
                continue;                /* 仅 spill 有货, 下一轮并入 cache */
            }
        }
        fail_fr = ota_feed_file(conn, cache, cache_cap, &cache_fill, recvbuf, r);
        if (fail_fr) {
            goto feed_fail;
        }
        if (upl.recv_total - last_log >= 128 * 1024 ||
            upl.recv_total >= upl.file_size) {
            last_log = upl.recv_total;
            printf("[ota-http] recv %u / %u (%u%%)\n",
                   (unsigned)upl.recv_total, (unsigned)upl.file_size,
                   (unsigned)(upl.recv_total * 100u / upl.file_size));
        }
        wdt_clear();
    }

    /* 排空 multipart 尾(\r\n--boundary--), 不解析内容 */
    {
        int to_ms = 200;
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &to_ms, sizeof(to_ms));
        for (int i = 0; i < 16; i++) {
            int r = recv(conn, recvbuf, sizeof(recvbuf), 0);
            if (r <= 0) {
                break;
            }
            wdt_clear();
        }
    }

    /* 收齐后把 spill 并入再 CRC 判定 */
    fail_fr = ota_cache_feed(conn, cache, cache_cap, &cache_fill, NULL, 0);
    if (fail_fr) {
        goto feed_fail;
    }

    printf("[ota-http] recv %u / expect %u, crc %04x vs %04x, burned %u, cache_fill %u\n",
           (unsigned)upl.recv_total, (unsigned)upl.file_size,
           upl.calc_crc, upl.data_crc,
           (unsigned)upl.burn_total, (unsigned)cache_fill);

    if (!upl.head_ok || upl.recv_total != upl.file_size ||
        upl.calc_crc != upl.data_crc) {
        char diag[128];
        snprintf(diag, sizeof(diag),
                 "CRC失败 recv=%u/%u crc=%04x/%04x",
                 (unsigned)upl.recv_total, (unsigned)upl.file_size,
                 upl.calc_crc, upl.data_crc);
        if (upl.fd) {
            net_fclose(upl.fd, 1);
            upl.fd = NULL;
        }
        free(cache);
        ota_spill_free();
        ota_ui_resume();
        ota_send_err(conn, diag);
        return;
    }

    ota_send_line(conn, "U100\n");
    if (upl.burn_total && upl.file_size) {
        ota_send_burn_pct(conn, upl.burn_total, upl.file_size);
    }
    if (cache_fill) {
        /* 收齐后再烧: recv_total 已满, drain 会自行跳过 */
        fail_fr = ota_flush_cache(conn, cache, cache_fill, 1);
        cache_fill = 0;
        if (fail_fr) {
            goto feed_fail;
        }
    }
    free(cache);
    cache = NULL;
    ota_spill_free();

    if (!upl.fd) {
        ota_ui_resume();
        ota_send_err(conn, "内部错误: 无 OTA 句柄");
        return;
    }

    printf("[ota-http] crc ok, burn boot info, reboot in 2s\n");
    ota_send_burn_pct(conn, upl.file_size, upl.file_size);
    ota_send_line(conn, "OK\n");
    net_fclose(upl.fd, 0);
    upl.fd = NULL;
    while (1) {
        wdt_clear();
        os_time_dly(100);
    }

feed_fail:
    if (upl.fd) {
        net_fclose(upl.fd, 1);
        upl.fd = NULL;
    }
    if (cache) {
        free(cache);
    }
    ota_spill_free();
    ota_ui_resume();
    ota_send_err(conn, fail_fr == -1 ? "OTA 忙" : "写入 flash 失败");
    return;

bad_req:
    send(conn, "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n", 46, 0);
}

/* ================ HTTP 服务器任务(AP/STA 通用) ================ */
static void http_server_task(void *arg)
{
    (void)arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        prov_log("[prov] socket fail\n");
        return;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROV_HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 2) < 0) {
        prov_log("[prov] bind/listen fail\n");
        close(srv);
        return;
    }
    prov_log("[prov] http server on :%d\n", PROV_HTTP_PORT);

    static char req[4096];
    while (1) {
        int conn = accept(srv, NULL, NULL);
        if (conn < 0) {
            os_time_dly(10);
            continue;
        }
        int got = 0, hdr_end = 0;
        while (got < (int)sizeof(req) - 1) {
            int r = recv(conn, req + got, sizeof(req) - 1 - got, 0);
            if (r <= 0) {
                break;
            }
            got += r;
            req[got] = 0;
            for (int i = 0; i + 4 <= got; i++) {
                if (req[i] == '\r' && req[i+1] == '\n' &&
                    req[i+2] == '\r' && req[i+3] == '\n') {
                    hdr_end = i + 4;
                    goto hdr_done;
                }
            }
        }
hdr_done:
        if (!hdr_end) {
            close(conn);
            continue;
        }

        if (!strncmp(req, "GET /api/scan", 13)) {
            http_render_scan_json(conn);
        } else if (!strncmp(req, "GET /scan", 9)) {
            http_render_scan(conn);
        } else if (!strncmp(req, "POST /connect", 13)) {
            /* 表单体很小, 与头同包到达(req[hdr_end..got]); 不满足时按坏请求 */
            http_handle_connect(conn, req + hdr_end);
        } else if (!strncmp(req, "POST /upload", 12)) {
            const char *b = strstr(req, "boundary=");
            char boundary[64] = "";
            if (b) {
                b += 9;
                int i = 0;
                while (b[i] && b[i] != '\r' && b[i] != '\n' && b[i] != ';' && i < 60) {
                    boundary[i] = b[i];
                    i++;
                }
                boundary[i] = 0;
                if (boundary[0] == '"' && i >= 2) {
                    memmove(boundary, boundary + 1, i - 2);
                    boundary[i - 2] = 0;
                }
            }
            if (boundary[0]) {
                http_handle_upload(conn, boundary, req + hdr_end, got - hdr_end);
            } else {
                send(conn, "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n", 46, 0);
            }
        } else {
            http_render_portal(conn);
        }
        close(conn);
        os_time_dly(1);
    }
}

/* ================ 配网状态机主体(wifi_app_task 调用, 不返回) ================ */
void wifi_prov_run(void)
{
    if (!prov_has_stored) {
        prov_log("[prov] no stored wifi, goto AP\n");
        goto enter_ap;
    }
    prov_log("[prov] stored '%s', connecting...\n", prov_stored_ssid);

    for (int attempt = 1; attempt <= PROV_STA_ATTEMPTS; attempt++) {
        if (prov_wait_sta_once() == 0) {
            prov_log("[prov] STA connected, DHCP ok\n");
            goto http_start;
        }
        prov_log("[prov] attempt %d/%d failed\n", attempt, PROV_STA_ATTEMPTS);
        if (attempt < PROV_STA_ATTEMPTS) {
            wifi_enter_sta_mode(prov_stored_ssid, prov_stored_pwd);
        }
    }

enter_ap:
    {
        u8 mac[6];
        init_net_device_mac_addr((char *)mac, 1);
        snprintf(prov_ap_ssid, sizeof(prov_ap_ssid), "WL82-%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    }
    prov_log("[prov] enter AP: %s pwd %s\n", prov_ap_ssid, PROV_AP_PWD);
    prov_dhcp_ok = 0;
    wifi_enter_ap_mode(prov_ap_ssid, PROV_AP_PWD);
    for (int i = 0; i < 100 && !prov_ap_active; i++) {   /* 最长 10s */
        os_time_dly(10);
    }
    prov_log("[prov] AP active, portal: http://192.168.1.1\n");

http_start:
    os_task_create(http_server_task, NULL, 11, 8192, 0, "prov_http");
    while (1) {
        /* 网页发起的 STA 切换: 60s 内没拿到 DHCP(密码错/超时/拒绝) → 自动回 AP,
         * 让用户能重新进后台。prov_http 任务独立存活, 回 AP 后门户恢复 192.168.1.1 */
        if (portal_sta_req) {
            int wait = 0;
            while (wait++ < 120) {       /* 60s */
                if (prov_dhcp_ok ||
                    wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
                    prov_log("[prov] portal wifi connected\n");
                    portal_sta_req = 0;
                    break;
                }
                os_time_dly(5);
                wdt_clear();
            }
            if (portal_sta_req) {        /* 仍未成功 */
                prov_log("[prov] portal wifi failed 60s, back to AP\n");
                portal_sta_req = 0;
                prov_dhcp_ok = 0;
                prov_ap_active = 0;
                wifi_enter_ap_mode(prov_ap_ssid, PROV_AP_PWD);
                for (int i = 0; i < 100 && !prov_ap_active; i++) {
                    os_time_dly(10);
                }
                prov_log("[prov] AP re-active\n");
            }
        }
        os_time_dly(100);
    }
}

#endif  /* MINI_WIFI_SUPPORT */
