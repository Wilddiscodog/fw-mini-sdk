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
} JL_FILE_HEAD_PLAIN;

extern int jl_file_head_valid_check(void *head);             /* update.a(任务上下文) */
extern uint16_t calc_crc16_with_init_val(uint16_t init_crc, uint8_t *ptr, uint16_t len);
extern int init_net_device_mac_addr(char *macaddr, char ap_mode);
extern void wifi_set_lan_setting_info(void);                 /* bt_wifi_app.c(AP IP/DHCP池) */
/* 升级诊断(update.a): boot_info 状态 / 升级目标地址 / 升级区回读 */
struct upl_boot_info { u32 code_len; u32 base; u16 ver; };
extern int get_current_boot_info(void *info);
extern u32 get_target_udate_addr(void);
extern u32 dual_bank_update_read_data(u32 offset, u8 *read_buf, u32 read_len);

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
#define HTML_HDR_200 "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"

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

static int http_render_portal(int fd)
{
    char body[1600];
    const char *mode = prov_ap_active ? "AP(配网模式)" : (prov_dhcp_ok ? "STA(已联网)" : "STA(连接中)");
    const char *ip = prov_ap_active ? "192.168.1.1" : "(见路由器DHCP)";
    snprintf(body, sizeof(body),
        "<html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>WL82 后台</title></head><body>"
        "<h2>WL82 设备后台</h2><p>模式: %s &nbsp; 地址: %s</p>"
        "<h3>WiFi 连接</h3>"
        "<form method=POST action=/connect>"
        "SSID: <input name=ssid maxlength=32><br>"
        "密码: <input name=pwd type=password maxlength=64><br>"
        "<input type=submit value='连接WiFi'> <a href=/scan>扫描WiFi</a></form>"
        "<h3>固件升级</h3>"
        "<form method=POST action=/upload enctype='multipart/form-data'>"
        "<input type=file name=fw accept='.ufw'>"
        "<input type=submit value='上传并升级'></form>"
        "<p>上传 update-ota.ufw, 设备自动校验 CRC 后重启。</p>"
        "</body></html>", mode, ip);
    return http_send(fd, HTML_HDR_200, body);
}

static int http_render_scan(int fd)
{
    u32 cnt = 0;
    char *body = malloc(4096);
    if (!body) {
        return -1;
    }
    prov_scan_done = 0;
    wifi_clear_scan_result();
    if (wifi_scan_req() == 0) {
        for (int i = 0; i < 160 && !prov_scan_done; i++) {   /* 最多等 8s */
            os_time_dly(5);
            wdt_clear();
        }
    }
    struct wifi_scan_ssid_info *r = wifi_get_scan_result(&cnt);

    int off = snprintf(body, 4096,
        "<html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>WiFi 列表</title></head><body><h2>扫描到 %u 个 WiFi</h2>"
        "<p>选择要连接的 WiFi, 输入密码后提交:</p>"
        "<form method=POST action=/connect>", (unsigned)cnt);
    for (u32 i = 0; r && i < cnt && off < 4096 - 200; i++) {
        off += snprintf(body + off, 4096 - off,
            "<input type=radio name=ssid value='%s'>%s (CH%u %ddB)<br>",
            r[i].ssid, r[i].ssid,
            (unsigned)r[i].channel_number, (int)r[i].rssi);
    }
    off += snprintf(body + off, 4096 - off,
        "密码: <input name=pwd type=password maxlength=64><br>"
        "<input type=submit value='连接'></form>"
        "<p><a href=/>返回首页</a></p></body></html>");
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
    char field[160], ssid[33], pwd[64], resp[512];
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
    snprintf(resp, sizeof(resp),
        "<html><meta charset=utf-8><body>正在连接 %s ...<br>"
        "设备切换到 STA 模式, AP 会断开。若 1 分钟内未连上, 设备自动回到 "
        "WL82 配网模式, 请重试。</body></html>", ssid);
    http_send(conn, HTML_HDR_200, resp);
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

static void http_handle_upload(int conn, const char *boundary,
                               const char *residue, int residue_len)
{
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

    memset(&upl, 0, sizeof(upl));
    upl.crc_skip = 64;                    /* CRC 覆盖载荷 [64B 双头之后, 文件尾) */
    upl.fd = net_fopen(CONFIG_UPGRADE_OTA_FILE_NAME, "w");
    if (!upl.fd) {
        http_send(conn, HTML_HDR_200,
                  upload_finish_page("OTA 忙(其他升级进行中), 稍后再试。"));
        return;
    }
    printf("[ota-http] upload start\n");

    /* 滑动尾窗: 只投喂能排除结束边界(\r\n--BOUNDARY--)的数据 */
    const u8 *stream = (const u8 *)body;
    u8 window[512];
    int wlen = 0;
    int keep = strlen(boundary) + 8;
    u8 head64[64];                        /* 前 64B = 外层 update_data 头 + 内层 app_core 头 */
    int head_got = 0;
    int done = 0;
    u8 recvbuf[1024];
    struct upl_boot_info bi = {0, 0, 0};
    int biret = get_current_boot_info(&bi);
    int ctx_ok = 0;                       /* 已过首次 4K 落盘(update ctx 有效) */
    printf("[ota-http] boot info ret=%d ver=%u base=0x%08x len=0x%08x\n",
           biret, bi.ver, bi.base, bi.code_len);

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
                        biret, bi.ver, bi.base, bi.code_len,
                        n1, upl.file_size, n2, h2->length, h2->addr);
                    printf("[ota-http] net_fwrite fail @%u ret=%d tgt=0x%x rb=%d\n",
                           upl.recv_total, wret, tgt, rb);
                    net_fclose(upl.fd, 1);
                    upl.fd = NULL;
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
        http_send(conn, HTML_HDR_200, upload_finish_page(diag));
        return;
    }

    printf("[ota-http] crc ok, burn boot info, reboot in 2s\n");
    net_fclose(upl.fd, 0);               /* 烧 boot info + 2s 后复位 */
    upl.fd = NULL;
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

        if (!strncmp(req, "GET /scan", 9)) {
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
    os_task_create(http_server_task, NULL, 11, 2048, 0, "prov_http");
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
