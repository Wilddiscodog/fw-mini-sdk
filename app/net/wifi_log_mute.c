/*
 * 静音预编译 lwip.a / wpas / wifi 库里的裸 printf 刷屏。
 * putbyte 在 cpu.a 为弱符号; 本文件提供强符号, 按行匹配前缀则丢弃。
 * 与 CONFIG_USB_DEBUG_ENABLE / NETWORK / SDCARD debug(也会定义 putbyte) 互斥。
 */
#include "app_config.h"
#include "generic/typedef.h"
#include "asm/cpu.h"

#if defined(CONFIG_DEBUG_ENABLE) && !defined(CONFIG_USB_DEBUG_ENABLE) \
    && !defined(CONFIG_NETWORK_DEBUG_ENABLE) && !defined(CONFIG_SDCARD_DEBUG_ENABLE)

#ifndef MINI_WIFI_LOG_MUTE
#define MINI_WIFI_LOG_MUTE  1
#endif

extern void uart_putbyte(char c);

#if MINI_WIFI_LOG_MUTE

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
    NULL,
};

static int line_drop(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!*p) {
        return 1;
    }
    /* 库偶发单独打出 "LA" */
    if (p[0] == 'L' && p[1] == 'A' && p[2] == 0) {
        return 1;
    }
    for (int i = 0; s_drop_pfx[i]; i++) {
        const char *pfx = s_drop_pfx[i];
        const char *a = p;
        const char *b = pfx;
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

void putbyte(char c)
{
    static char buf[192];
    static u16 len;
    static u8 dropping;

    local_irq_disable();

    if (c == '\r') {
        local_irq_enable();
        return;
    }
    if (c == '\n') {
        char out[192];
        u16 n = len;
        if (n >= sizeof(buf)) {
            n = sizeof(buf) - 1;
        }
        buf[n] = 0;
        u8 drop = dropping || line_drop(buf);
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
                uart_putbyte(out[i]);
            }
            uart_putbyte('\n');
        }
        return;
    }

    if (len < sizeof(buf) - 1) {
        buf[len++] = c;
        buf[len] = 0;
    } else {
        dropping = 1;
    }
    local_irq_enable();
}

#else

void putbyte(char c)
{
    uart_putbyte(c);
}

#endif /* MINI_WIFI_LOG_MUTE */
#endif
