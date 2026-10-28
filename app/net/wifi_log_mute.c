/*
 * 静音预编译 lwip / wifi / DLNA 库里的裸 printf 刷屏。
 *
 * 注意: 库常把一行拆成多行打印, 或把无换行的 "IP"/"()" 粘在下一行前,
 * 因此除前缀匹配外还做: 去粘连噪声、续行丢弃、子串匹配。
 */
#include "app_config.h"
#include "generic/typedef.h"
#include "asm/cpu.h"
#include <string.h>

#if defined(CONFIG_DEBUG_ENABLE)

#ifndef MINI_WIFI_LOG_MUTE
#define MINI_WIFI_LOG_MUTE  1
#endif

extern void uart_putbyte(char c);

#if MINI_WIFI_LOG_MUTE

/* 行首匹配 */
static const char *const s_drop_pfx[] = {
    "etharp_",
    "dhcp_",
    "DHCP_",
    "USER_PROVIDE_",
    "udp_bind",
    "udp_connect",
    "netif:",
    "netif_set_",
    "Default network",
    "ip4 address:",
    "gw address:",
    "net mask",
    "[RECV ",
    "[SEND ",
    "wpas_",
    "wpa_supplicant",
    "CNTL -",
    "!!! LINK",
    "Set_SSID",
    "******************ssid",
    "ThisBulkSize",
    "Current MAC",
    "RTMPSetPhyMode",
    "MCS Set",
    "skip do auto",
    "netdev_set_mac",
    "|Init_LwIP",
    "|lwip_renew",
    "HT_FBK_",
    "LG_FBK_",
    "wl_set_fallback",
    "wifi_module_",
    "stored pmk",
    "pmk_info",
    "netlink not",
    "cur_wifi_vm",
    "wl sta_mode",
    "wl ap_mode",
    "threat_create:",
    "put_ncmsg",
    "kill for:",
    "Subscription request",
    "UDN :",
    "*******gena_notify",
    "Action request",
    "Action_name",
    "---dlna url---",
    "curr_play_info",
    "ServiceId :",
    "SSDP ",
    "GET /",
    "Host:",
    "Connection:",
    "Cache-Control:",
    "net_download_dev_open",
    "net_download cbuf",
    "dac open end",
    "dac sample rate",
    "sr = ",
    /* udp_bind 拆行续行 */
    "0.0.0.0",
    ", port ",
    NULL,
};

/* 行内子串(防 IP 粘连导致前缀失配) */
static const char *const s_drop_sub[] = {
    "udp_bind",
    "[RECV ",
    "[SEND ",
    "Action request",
    "Action_name",
    "GetPositionInfo",
    "GetVolume",
    "Subscription request",
    "gena_notify",
    "SSDP recvd",
    "SSDP ",
    "---dlna url---",
    "etharp_",
    "threat_create:",
    "net_download",
    NULL,
};

static int starts_with(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++) {
            return 0;
        }
    }
    return 1;
}

static int contains(const char *s, const char *sub)
{
    for (; *s; s++) {
        const char *a = s;
        const char *b = sub;
        while (*b && *a == *b) {
            a++;
            b++;
        }
        if (*b == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_noise_char(char c)
{
    return (c == 'I' || c == 'P' || c == '(' || c == ')');
}

/* 缓冲当前是否只含 IP/() 噪声 */
static int buf_only_noise(const char *s, u16 n)
{
    int has = 0;
    for (u16 i = 0; i < n; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        has = 1;
        if (!is_noise_char(c)) {
            return 0;
        }
    }
    return has;
}

/* 去掉行首粘连的 IP / () 碎片 */
static const char *strip_leading_noise(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (p[0] == 'I' && p[1] == 'P') {
            p += 2;
            continue;
        }
        if (p[0] == '(' && p[1] == ')') {
            p += 2;
            continue;
        }
        if (p[0] == 'L' && p[1] == 'A' && (p[2] == 0 || p[2] == ' ' || p[2] == '[')) {
            p += 2;
            continue;
        }
        break;
    }
    return p;
}

static int line_drop(const char *line)
{
    const char *p = strip_leading_noise(line);
    int i;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!*p) {
        return 1; /* 剥净后空行 */
    }
    if (buf_only_noise(p, (u16)strlen(p))) {
        return 1;
    }

    for (i = 0; s_drop_pfx[i]; i++) {
        if (starts_with(p, s_drop_pfx[i])) {
            return 1;
        }
    }
    for (i = 0; s_drop_sub[i]; i++) {
        if (contains(p, s_drop_sub[i])) {
            return 1;
        }
    }
    return 0;
}

void mini_log_filter_putbyte(char c, void (*emit)(char))
{
    static char buf[192];
    static u16 len;
    static u8 dropping;

    if (!emit) {
        return;
    }

    local_irq_disable();

    if (c == '\r') {
        local_irq_enable();
        return;
    }

    if (c == '\n') {
        char out[192];
        u16 n = len;
        u8 drop;
        if (n >= sizeof(buf)) {
            n = sizeof(buf) - 1;
        }
        buf[n] = 0;
        drop = dropping || line_drop(buf);
        if (!drop) {
            for (u16 i = 0; i < n; i++) {
                out[i] = buf[i];
            }
        }
        len = 0;
        dropping = 0;
        local_irq_enable();

        if (!drop) {
            for (u16 i = 0; i < n; i++) {
                emit(out[i]);
            }
            emit('\n');
        }
        return;
    }

    /* 新字符是正常内容, 但缓冲里还粘着 IP/() → 先清空噪声再收 */
    if (!is_noise_char(c) && c != ' ' && c != '\t' &&
        len > 0 && buf_only_noise(buf, len)) {
        len = 0;
        dropping = 0;
        buf[0] = 0;
    }

    if (len < sizeof(buf) - 1) {
        buf[len++] = c;
        buf[len] = 0;
        if (len >= 4 && buf_only_noise(buf, len) && len >= 16) {
            len = 0;
            dropping = 0;
        }
    } else {
        dropping = 1;
    }
    local_irq_enable();
}

#if !defined(CONFIG_USB_DEBUG_ENABLE) \
    && !defined(CONFIG_NETWORK_DEBUG_ENABLE) \
    && !defined(CONFIG_SDCARD_DEBUG_ENABLE)

void putbyte(char c)
{
    mini_log_filter_putbyte(c, uart_putbyte);
}

#endif

#else /* !MINI_WIFI_LOG_MUTE */

void mini_log_filter_putbyte(char c, void (*emit)(char))
{
    if (emit) {
        emit(c);
    }
}

#if !defined(CONFIG_USB_DEBUG_ENABLE) \
    && !defined(CONFIG_NETWORK_DEBUG_ENABLE) \
    && !defined(CONFIG_SDCARD_DEBUG_ENABLE)

void putbyte(char c)
{
    uart_putbyte(c);
}

#endif

#endif /* MINI_WIFI_LOG_MUTE */
#endif /* CONFIG_DEBUG_ENABLE */
