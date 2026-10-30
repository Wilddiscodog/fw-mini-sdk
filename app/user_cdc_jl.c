/*
 * 原厂 USB 栈下的 user_cdc_*：映射到 CDC2(port1, EP2)。
 * 主机打开 CDC2 并拉 DTR 后才有数据；未连接返回 -1。
 * CherryUSB 路径下本文件不参与编译(见 Makefile MINI_USB_STACK_CHERRYUSB)。
 */
#include "app_config.h"

#if !MINI_USB_STACK_CHERRYUSB

#include "usb/device/cdc.h"
#include "user_cdc_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef MINI_USER_CDC_PORT
#define MINI_USER_CDC_PORT  1
#endif

/* user_setup / cfg_tool 升级跳转依赖；未编 msd_upgrade 时提供空实现 */
void go_mask_usb_updata(void)
{
}

void __attribute__((weak)) ram_protect_close(void)
{
}

void __attribute__((weak)) hw_mmu_disable(void)
{
}

int user_cdc_puts(const char *s)
{
    u32 n;

    if (!s) {
        return -1;
    }
    n = (u32)strlen(s);
    if (!n) {
        return 0;
    }
    n = cdc_write_data_port((usb_dev)MINI_USB_JL_ID, (u8)MINI_USER_CDC_PORT, (u8 *)s, n);
    return n ? (int)n : -1;
}

int user_cdc_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;
    u32 w;

    if (!fmt) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return n;
    }
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    w = cdc_write_data_port((usb_dev)MINI_USB_JL_ID, (u8)MINI_USER_CDC_PORT, (u8 *)buf, (u32)n);
    return w ? (int)w : -1;
}

#endif /* !MINI_USB_STACK_CHERRYUSB */
