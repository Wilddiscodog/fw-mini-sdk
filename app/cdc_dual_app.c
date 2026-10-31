/*
 * CherryUSB 设备 —— 双 CDC-ACM 虚拟串口(无 MSC)
 *
 * 端点规划(HUSB, bulk 512 @ HS; 避开 EP3 bulk=MSC 大缓冲特殊路径):
 *   CDC1: EP1(0x01 OUT / 0x81 IN) 数据 + EP5(0x85 INT) 通知 —— 数据口(echo)
 *   CDC2: EP2(0x02 OUT / 0x82 IN) 数据 + EP6(0x86 INT) 通知 —— 用户专用日志
 * 接口: IAD 复合, intf0/1=CDC1(comm/data), intf2/3=CDC2(comm/data)
 *
 * CDC2 不对接系统 UART/printf; 仅通过 user_cdc_puts()/user_cdc_printf() 输出。
 * 系统日志仍只走 UART0(PB2)。
 */
#include "app_config.h"

#include "os/os_api.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "user_cdc_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>


#define CDC1_IN_EP    0x81
#define CDC1_OUT_EP   0x01
#define CDC1_INT_EP   0x85
#define CDC2_IN_EP    0x82
#define CDC2_OUT_EP   0x02
#define CDC2_INT_EP   0x86

#define USBD_VID           0xFFFF
#define USBD_PID           0x0006    /* clear-halt 修复后再递增, 强制主机重枚举 */
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< 4 个接口: CDC1(comm+data) + CDC2(comm+data), IAD 复合 */
#define USB_CONFIG_SIZE (9 + 2 * CDC_ACM_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
    CDC_ACM_DESCRIPTOR_INIT(0x02, CDC2_INT_EP, CDC2_OUT_EP, CDC2_IN_EP, USB_BULK_EP_MPS_HS, 0x00),
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
    CDC_ACM_DESCRIPTOR_INIT(0x02, CDC2_INT_EP, CDC2_OUT_EP, CDC2_IN_EP, USB_BULK_EP_MPS_FS, 0x00),
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
    CDC_ACM_DESCRIPTOR_INIT(0x02, CDC2_INT_EP, CDC2_OUT_EP, CDC2_IN_EP, USB_BULK_EP_MPS_FS, 0x00),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
    CDC_ACM_DESCRIPTOR_INIT(0x02, CDC2_INT_EP, CDC2_OUT_EP, CDC2_IN_EP, USB_BULK_EP_MPS_HS, 0x00),
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "fw-mini-sdk",                /* Manufacturer */
    "WL82 CDC1+UserLog",          /* Product */
    "WL82-0001",                  /* Serial Number */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return config_descriptor_fs;
    }
    return NULL;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return other_speed_config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return other_speed_config_descriptor_fs;
    }
    return NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_descriptor_dual_cdc = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

/* ---- 每口独立缓冲(读/写各 2KB; RAM0 .usb_h_bss, 栈为 CPU 搬运无 DMA 属性要求)。
 * section 属性只能挂全局变量, 故缓冲为独立数组, struct 内存指针 ---- */
#define CDC_BUF_SIZE  2048u
#define CDC_PORT_NUM  2u

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_read_buf[CDC_BUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_write_buf[CDC_BUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc2_read_buf[CDC_BUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc2_write_buf[CDC_BUF_SIZE];

struct cdc_port {
    uint8_t in_ep;
    uint8_t out_ep;
    uint8_t int_ep;
    uint8_t *read_buf;
    uint8_t *write_buf;
    volatile bool tx_busy;
};

static struct cdc_port cdc_ports[CDC_PORT_NUM] = {
    { .in_ep = CDC1_IN_EP, .out_ep = CDC1_OUT_EP, .int_ep = CDC1_INT_EP,
      .read_buf = cdc1_read_buf, .write_buf = cdc1_write_buf },
    { .in_ep = CDC2_IN_EP, .out_ep = CDC2_OUT_EP, .int_ep = CDC2_INT_EP,
      .read_buf = cdc2_read_buf, .write_buf = cdc2_write_buf },
};

/* bit0=CDC1 DTR, bit1=CDC2 DTR(主机打开串口) */
volatile uint8_t dtr_intf_mask = 0;
volatile uint8_t cdc_configured = 0;     /* SET_CONFIGURATION 完成 */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        cdc_configured = 1;
        for (int i = 0; i < CDC_PORT_NUM; i++) {
            cdc_ports[i].tx_busy = false;
            usbd_ep_start_read(busid, cdc_ports[i].out_ep,
                               cdc_ports[i].read_buf, CDC_BUF_SIZE);
        }
        break;
    case USBD_EVENT_DISCONNECTED:
        cdc_configured = 0;
        dtr_intf_mask = 0;
        for (int i = 0; i < CDC_PORT_NUM; i++) {
            cdc_ports[i].tx_busy = false;
        }
        break;
    default:
        break;
    }
}

/* ---- 两口均不 echo: 避免 EP IN 主动发送触发 JL clear-halt 风暴。
 * CDC1=静默数据口; CDC2=仅 user_cdc_* 主动写。OUT 丢弃后重新武装。 ---- */
static void usbd_cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    struct cdc_port *p = NULL;
    (void)nbytes;
    for (int i = 0; i < CDC_PORT_NUM; i++) {
        if (cdc_ports[i].out_ep == ep) {
            p = &cdc_ports[i];
            break;
        }
    }
    if (!p) {
        return;
    }
    usbd_ep_start_read(busid, p->out_ep, p->read_buf, CDC_BUF_SIZE);
}

static void usbd_cdc_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    struct cdc_port *p = NULL;
    for (int i = 0; i < CDC_PORT_NUM; i++) {
        if (cdc_ports[i].in_ep == ep) {
            p = &cdc_ports[i];
            break;
        }
    }
    if (!p) {
        return;
    }
    if ((nbytes % 512) == 0 && nbytes) {
        usbd_ep_start_write(busid, p->in_ep, NULL, 0);   /* ZLP 收尾 */
    } else {
        p->tx_busy = false;
    }
}

/*!< 端点回调表 */
struct usbd_endpoint cdc1_out_ep = { .ep_addr = CDC1_OUT_EP, .ep_cb = usbd_cdc_bulk_out };
struct usbd_endpoint cdc1_in_ep  = { .ep_addr = CDC1_IN_EP,  .ep_cb = usbd_cdc_bulk_in };
struct usbd_endpoint cdc2_out_ep = { .ep_addr = CDC2_OUT_EP, .ep_cb = usbd_cdc_bulk_out };
struct usbd_endpoint cdc2_in_ep  = { .ep_addr = CDC2_IN_EP,  .ep_cb = usbd_cdc_bulk_in };

/* ---- 通知端点: 仅完成 ep_open; 暂不主动发 SERIAL_STATE ---- */
static void usbd_cdc_notify_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
}

struct usbd_endpoint cdc1_notify_ep = { .ep_addr = CDC1_INT_EP, .ep_cb = usbd_cdc_notify_in };
struct usbd_endpoint cdc2_notify_ep = { .ep_addr = CDC2_INT_EP, .ep_cb = usbd_cdc_notify_in };

struct usbd_interface intf0; /* CDC1 comm */
struct usbd_interface intf1; /* CDC1 data */
struct usbd_interface intf2; /* CDC2 comm */
struct usbd_interface intf3; /* CDC2 data */

/* ---- DTR: 只记掩码, 不发 SERIAL_STATE(通知 EP MPS=8, 10B 通知会截断且易搅乱 IN) ---- */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    if (intf == 0) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x01) : (dtr_intf_mask & ~0x01);
    } else if (intf == 2) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x02) : (dtr_intf_mask & ~0x02);
    }
}

/* ---- CDC2 用户日志 API(与 printf/UART0 无关) ----
 * 不在此 os_time_dly: 常从 LVGL 定时器调用, 忙则直接丢弃本条。
 * 需主机打开 CDC2 并拉高 DTR(多数串口助手默认会)。 */
static int user_cdc2_write(const uint8_t *data, uint32_t len)
{
    struct cdc_port *p = &cdc_ports[1];

    if (!data || !len) {
        return 0;
    }
    if (!cdc_configured || !(dtr_intf_mask & 0x02)) {
        return -1;                       /* 主机未打开 CDC2 */
    }
    if (len > CDC_BUF_SIZE) {
        len = CDC_BUF_SIZE;
    }
    if (p->tx_busy) {
        return -2;                       /* 上一帧未完成: 丢弃, 避免堵 UI */
    }
    memcpy(p->write_buf, data, len);
    p->tx_busy = true;
    usbd_ep_start_write(0, p->in_ep, p->write_buf, len);
    return (int)len;
}

int user_cdc_puts(const char *s)
{
    if (!s) {
        return -1;
    }
    return user_cdc2_write((const uint8_t *)s, (uint32_t)strlen(s));
}

int user_cdc_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

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
    return user_cdc2_write((const uint8_t *)buf, (uint32_t)n);
}

void usb_dual_cdc_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &usb_descriptor_dual_cdc);

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf2));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf3));
    usbd_add_endpoint(busid, &cdc1_out_ep);
    usbd_add_endpoint(busid, &cdc1_in_ep);
    usbd_add_endpoint(busid, &cdc1_notify_ep);
    usbd_add_endpoint(busid, &cdc2_out_ep);
    usbd_add_endpoint(busid, &cdc2_in_ep);
    usbd_add_endpoint(busid, &cdc2_notify_ep);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}
