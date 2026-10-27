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
 *     - /upload: multipart 流式上传 update-ota.ufw → net_update→dual_bank 烧写
 *       (与 MSC U盘升级同一条通道: jl_file_head_valid_check 解密头 + CRC16 + 2s 复位)
 *   STA 模式: 网页后台同样在局域网可用。
 *
 * 事件钩子: bt_wifi_app.c 的 wifi_event_callback(MODULE_INIT → 默认模式选择;
 * 其余事件 → wifi_prov_notify 置状态位)。回调注册 + wifi_on 在 bt_wifi_app.c
 * 的 wifi_app_task(wifi_start() 创建), 本文件 wifi_prov_run() 为其状态机主体。
 */
#include "app_config.h"
#include "typedef.h"
#include "os/os_api.h"
#include "lwip.h"
#include "lwip/sockets.h"
#include "wifi/wifi_connect.h"
#include "net_update.h"                     /* CONFIG_UPGRADE_OTA_FILE_NAME */
#include "asm/wdt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if MINI_WIFI_SUPPORT

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
        printf("[prov] stored wifi '%s'\n", prov_stored_ssid);
    } else {
        prov_has_stored = 0;
        parm.mode = AP_MODE;
        strncpy((char *)parm.ssid[0], "WL82-SETUP", sizeof(parm.ssid[0]) - 1);
        strncpy((char *)parm.pwd[0], PROV_AP_PWD, sizeof(parm.pwd[0]) - 1);
        wifi_set_default_mode(&parm, 1, 0);
        printf("[prov] no stored wifi, boot to AP\n");
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
        printf("[prov] scan round=%d req=%d (0=started,-1=busy/in-progress)\n",
               round, req);
        /* req==-1 也可能是「正在扫」, 仍要等 COMPLETED; 不可因非 0 直接放弃 */
        int waited = 0;
        for (; waited < PROV_SCAN_WAIT && !prov_scan_done; waited++) {
            os_time_dly(5);
            wdt_clear();
        }
        printf("[prov] scan round=%d done=%d waited=%dms\n",
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
    printf("[prov] scan raw cnt=%u\n", (unsigned)cnt);

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
    printf("[prov] scan unique cnt=%u\n", (unsigned)prov_scan_cnt);
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
    ".foot{text-align:center;color:var(--mut);font-size:.75rem;margin-top:18px}";

static int http_render_portal(int fd)
{
    char *body = malloc(7200);
    if (!body) {
        return -1;
    }
    const char *mode = prov_ap_active ? "AP 配网" : (prov_dhcp_ok ? "STA 已联网" : "STA 连接中");
    const char *ip = prov_ap_active ? "192.168.1.1" : "路由器 DHCP";
    snprintf(body, 7200,
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
        "<form method=POST action=/upload enctype='multipart/form-data'>"
        "<label>升级文件 (update-ota.ufw)</label>"
        "<input type=file name=fw accept='.ufw,application/octet-stream'>"
        "<div class=row><button type=submit>上传并升级</button></div>"
        "<p class=tip>校验通过后设备自动重启进入新固件。</p></form></div>"
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
    printf("[prov] connect req ssid='%s' pwd_len=%d\n", ssid, (int)strlen(pwd));

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

/* ================ POST /upload: multipart 流式 → OTA ================ */
static struct {
    void *fd;
    u32  recv_total;                     /* 已收文件字节(含 32B 头) */
    u32  file_size;                      /* 头声明长度 */
    u16  data_crc;
    u16  calc_crc;
    u8   head_ok;
    u8   crc_skip;                       /* CRC 跳过文件头 32B */
} upl;

static const char *upload_finish_page(const char *msg)
{
    static char page[640];
    snprintf(page, sizeof(page),
        "<html><meta charset=utf-8><body>%s <a href=/>返回</a></body></html>", msg);
    return page;
}

static void ota_ui_resume(void)
{
    g_ui_suspend = 0;
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
    disp_enable_update();
#endif
}

static void http_handle_upload(int conn, const char *boundary,
                               const char *residue, int residue_len)
{
    /* 大缓冲放静态: 原栈上 window+recvbuf≈1.5KB, 叠加 get_current_boot_info
     * 内部打印易撑破 prov_http 4K 栈 → 返回后 misalign_err */
    static u8 window[512] __attribute__((aligned(4)));
    static u8 recvbuf[1024] __attribute__((aligned(4)));
    static u8 head64[64] __attribute__((aligned(4)));

    /* part 头结束 "\r\n\r\n" 之后的字节才是文件内容 */
    const char *body = NULL;
    for (int i = 0; i + 4 <= residue_len; i++) {
        if (residue[i] == '\r' && residue[i+1] == '\n' &&
            residue[i+2] == '\r' && residue[i+3] == '\n') {
            body = residue + i + 4;
            break;
        }
    }
    if (!body) {
        goto bad_req;                    /* part 头 > 首包: 极罕见, 拒绝 */
    }
    int slen = residue_len - (int)(body - residue);

    /* 先挂起 UI, 再开 OTA: 避免 LVGL/EMI 与 norflash 并发 */
    g_ui_suspend = 1;
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
    disp_disable_update();
#endif
    os_time_dly(20);                     /* ~200ms 等 lcd_test 停刷 */

    memset(&upl, 0, sizeof(upl));
    upl.crc_skip = 64;                    /* CRC 覆盖载荷 [64B 双头之后, 文件尾) */
    upl.fd = net_fopen(CONFIG_UPGRADE_OTA_FILE_NAME, "w");
    if (!upl.fd) {
        ota_ui_resume();
        http_send(conn, HTML_HDR_200,
                  upload_finish_page("OTA 忙(其他升级进行中), 稍后再试。"));
        return;
    }
    printf("[ota-http] upload start\n");

    /* 滑动尾窗: 只投喂能排除结束边界(\r\n--BOUNDARY--)的数据 */
    const u8 *stream = (const u8 *)body;
    int wlen = 0;
    int keep = (int)strlen(boundary) + 8;
    int head_got = 0;
    int done = 0;
    struct BootInfo bi;
    memset(&bi, 0, sizeof(bi));
    int biret = get_current_boot_info(&bi);
    int ctx_ok = 0;                       /* 已过首次 4K 落盘(update ctx 有效) */
    printf("[ota-http] boot info ret=%d ver=%u base=0x%08x len=0x%08x\n",
           biret, bi.version, bi.baseAddress, bi.codeLength);

    while (!done) {
        while (wlen < keep) {            /* 窗口不足: 补数据 */
            int r;
            if (slen > 0) {
                r = slen > (int)sizeof(window) - wlen ? (int)sizeof(window) - wlen : slen;
                memcpy(window + wlen, stream, r);
                stream += r;
                slen -= r;
                wlen += r;
            } else {
                r = recv(conn, recvbuf, sizeof(recvbuf), 0);
                if (r <= 0) {
                    done = 2;            /* 对端断开 */
                    break;
                }
                stream = recvbuf;
                slen = r;
            }
            wdt_clear();
        }
        if (done == 2) {
            break;
        }

        int emit = wlen - keep;          /* 窗口中确定不属于结束边界的部分 */
        if (emit > 0) {
            /* 文件前 64B: 外层头(32B,校验一次) + 内层 app_core 头(诊断) */
            if (head_got < 64) {
                int t = (64 - head_got) < emit ? (64 - head_got) : emit;
                memcpy(head64 + head_got, window, t);
                head_got += t;
                if (head_got >= 32 && !upl.head_ok) {
                    upl.head_ok = (jl_file_head_valid_check(head64) == 0);
                    if (!upl.head_ok) {
                        upload_fail_head:
                        net_fclose(upl.fd, 1);
                        upl.fd = NULL;
                        ota_ui_resume();
                        http_send(conn, HTML_HDR_200,
                                  upload_finish_page("升级文件头校验失败, 请上传 update-ota.ufw。"));
                        return;
                    }
                    JL_FILE_HEAD_PLAIN *h = (JL_FILE_HEAD_PLAIN *)head64;
                    upl.file_size = h->length;   /* 外层头: 整包长度 */
                    printf("[ota-http] head ok: len %u\n", upl.file_size);
                }
                if (head_got == 64) {
                    /* 内层 app_core 头: dataCrc = 写镜像 CRC(包内约定), 内层长度须吻合 */
                    JL_FILE_HEAD_PLAIN *h2 = (JL_FILE_HEAD_PLAIN *)(head64 + 32);
                    printf("[ota-http] inner head: %.16s len %u addr 0x%x\n",
                           h2->name, h2->length, h2->addr);
                    if (h2->length != upl.file_size - 64 ||
                        memcmp(h2->name, "app_core", 8) != 0) {
                        net_fclose(upl.fd, 1);
                        upl.fd = NULL;
                        ota_ui_resume();
                        http_send(conn, HTML_HDR_200,
                                  upload_finish_page("升级文件内层头异常, 请用构建产出的 update-ota.ufw。"));
                        return;
                    }
                    upl.data_crc = h2->data_crc;
                }
            }
            /* 投喂(文件流含 32B 头), CRC 跳过头 32B, 超出声明长度的丢弃 */
            u32 want = upl.file_size > upl.recv_total ?
                       (upl.file_size - upl.recv_total) : 0;
            if (want == 0) {
                done = 1;                /* 文件收满, 剩余全是尾边界 */
            } else {
                int feed = emit < (int)want ? emit : (int)want;
                int wret = net_fwrite(upl.fd, (u8 *)window, feed, 0);
                if (wret != feed) {
                    /* 详细诊断页: 偏移/返回码/目标地址/回读/boot/双层头 */
                    u32 tgt = 0;
                    int rb = -1;
                    if (ctx_ok) {
                        tgt = get_target_udate_addr();
                        u8 rbbuf[64];
                        u32 rn = dual_bank_update_read_data(0, rbbuf, 64);
                        rb = (rn != 64) ? -2 : memcmp(rbbuf, head64, 64);
                    }
                    char n1[17], n2[17], diag[420];
                    memcpy(n1, head64 + 16, 16);  n1[16] = 0;
                    memcpy(n2, head64 + 48, 16);  n2[16] = 0;
                    JL_FILE_HEAD_PLAIN *h2 = (JL_FILE_HEAD_PLAIN *)(head64 + 32);
                    snprintf(diag, sizeof(diag),
                        "写入 flash 失败(offset=%u ret=%d)<br>"
                        "tgt=%08x rb=%d (rb:0=首4K已落盘 1=未写入 -1=未到落盘 -2=读失败)<br>"
                        "boot: ret=%d ver=%u base=%08x len=%08x<br>"
                        "heads: [%s len=%u] [%s len=%u addr=%u]",
                        upl.recv_total, wret, tgt, rb,
                        biret, bi.version, bi.baseAddress, bi.codeLength,
                        n1, upl.file_size, n2, h2->length, h2->addr);
                    printf("[ota-http] net_fwrite fail @%u ret=%d tgt=0x%x rb=%d\n",
                           upl.recv_total, wret, tgt, rb);
                    net_fclose(upl.fd, 1);
                    upl.fd = NULL;
                    ota_ui_resume();
                    http_send(conn, HTML_HDR_200, upload_finish_page(diag));
                    return;
                }
                upl.recv_total += feed;
                if (upl.recv_total >= 4096) {
                    ctx_ok = 1;         /* 首 4K 已成功过, update ctx 有效 */
                }
                /* CRC: 跳过文件头 32B */
                int cs = 0;
                while (feed > cs && upl.crc_skip) {
                    cs++;
                    upl.crc_skip--;
                }
                if (feed > cs) {
                    upl.calc_crc = calc_crc16_with_init_val(upl.calc_crc,
                                                            window + cs, (u16)(feed - cs));
                }
            }
            memmove(window, window + emit, wlen - emit);
            wlen -= emit;
        } else {
            /* 窗口全是"疑似边界": 再收一字节推进(理论上 keep>边界长, 不会卡死) */
            int r = recv(conn, recvbuf, 1, 0);
            if (r <= 0) {
                done = 2;
            } else if (wlen < (int)sizeof(window)) {
                window[wlen++] = recvbuf[0];
            }
        }
    }

    printf("[ota-http] recv %u / expect %u, crc %04x vs %04x\n",
           (unsigned)upl.recv_total, (unsigned)upl.file_size,
           upl.calc_crc, upl.data_crc);
    if (done != 1 || !upl.head_ok || upl.recv_total != upl.file_size ||
        upl.calc_crc != upl.data_crc) {
        char diag[160];
        snprintf(diag, sizeof(diag),
                 "CRC 校验失败: done=%d recv=%u/%u crc=%04x/%04x, 请重试。",
                 done, (unsigned)upl.recv_total, (unsigned)upl.file_size,
                 upl.calc_crc, upl.data_crc);
        net_fclose(upl.fd, 1);           /* 不烧写不复位 */
        upl.fd = NULL;
        ota_ui_resume();
        http_send(conn, HTML_HDR_200, upload_finish_page(diag));
        return;
    }

    printf("[ota-http] crc ok, burn boot info, reboot in 2s\n");
    net_fclose(upl.fd, 0);               /* 烧 boot info + 2s 后复位 */
    upl.fd = NULL;
    /* 成功后保持 UI 挂起, 即将复位 */
    http_send(conn, HTML_HDR_200,
              upload_finish_page("校验通过, 设备即将重启升级..."));
    while (1) {                          /* 等复位 */
        wdt_clear();
        os_time_dly(100);
    }

bad_req:
    send(conn, "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n", 46, 0);
}

/* ================ HTTP 服务器任务(AP/STA 通用) ================ */
static void http_server_task(void *arg)
{
    (void)arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        printf("[prov] socket fail\n");
        return;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROV_HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 2) < 0) {
        printf("[prov] bind/listen fail\n");
        close(srv);
        return;
    }
    printf("[prov] http server on :%d\n", PROV_HTTP_PORT);

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
        printf("[prov] no stored wifi, goto AP\n");
        goto enter_ap;
    }
    printf("[prov] stored '%s', connecting...\n", prov_stored_ssid);

    for (int attempt = 1; attempt <= PROV_STA_ATTEMPTS; attempt++) {
        if (prov_wait_sta_once() == 0) {
            printf("[prov] STA connected, DHCP ok\n");
            goto http_start;
        }
        printf("[prov] attempt %d/%d failed\n", attempt, PROV_STA_ATTEMPTS);
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
    printf("[prov] enter AP: %s pwd %s\n", prov_ap_ssid, PROV_AP_PWD);
    prov_dhcp_ok = 0;
    wifi_enter_ap_mode(prov_ap_ssid, PROV_AP_PWD);
    for (int i = 0; i < 100 && !prov_ap_active; i++) {   /* 最长 10s */
        os_time_dly(10);
    }
    printf("[prov] AP active, portal: http://192.168.1.1\n");

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
                    printf("[prov] portal wifi connected\n");
                    portal_sta_req = 0;
                    break;
                }
                os_time_dly(5);
                wdt_clear();
            }
            if (portal_sta_req) {        /* 仍未成功 */
                printf("[prov] portal wifi failed 60s, back to AP\n");
                portal_sta_req = 0;
                prov_dhcp_ok = 0;
                prov_ap_active = 0;
                wifi_enter_ap_mode(prov_ap_ssid, PROV_AP_PWD);
                for (int i = 0; i < 100 && !prov_ap_active; i++) {
                    os_time_dly(10);
                }
                printf("[prov] AP re-active\n");
            }
        }
        os_time_dly(100);
    }
}

#endif  /* MINI_WIFI_SUPPORT */
