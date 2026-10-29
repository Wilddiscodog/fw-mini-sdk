/*
 * CherryUSB 设备 —— 双 CDC-ACM 虚拟串口(无 MSC)
 *
 * 端点规划(HUSB, bulk 512 @ HS):
 *   CDC1: EP2(0x02 OUT / 0x82 IN) 数据 + EP5(0x85 INT) 通知
 *   CDC2: EP3(0x03 OUT / 0x83 IN) 数据 + EP4(0x84 INT) 通知
 * 接口: IAD 复合, intf0/1=CDC1(comm/data), intf2/3=CDC2(comm/data)
 *
 * 行为: 两口独立, 均为数据串口 —— OUT 收到即回显(echo); DTR 打开时每 5s
 * 各发一行状态。历史注: 旧形态为 CDC(日志口)+MSC(3MB RAM 盘拖拽升级),
 * 2026-10 改双 CDC, 日志镜像(CONFIG_USB_DEBUG_ENABLE)与 U 盘升级
 * (TCFG_VIR_UPDATE_ENABLE)链路一并移除, 升级走 WiFi 网页 OTA(双备份保留)。
 */
#include "app_config.h"

#include "os/os_api.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define CDC1_IN_EP    0x82
#define CDC1_OUT_EP   0x02
#define CDC1_INT_EP   0x85
#define CDC2_IN_EP    0x83
#define CDC2_OUT_EP   0x03
#define CDC2_INT_EP   0x84

#define USBD_VID           0xFFFF
#define USBD_PID           0x0004    /* 形态变更递增 PID, 主机免残留旧枚举 */
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
    "WL82 Dual-CDC",              /* Product */
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

static void usbd_cdc_send_serial_state(uint8_t busid, uint8_t ep, uint8_t intf, uint16_t state);

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

/* ---- echo: OUT 收到即回显(数据口); 写复用各自 write_buf, IN 完成后重新武装 OUT ---- */
static void usbd_cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    struct cdc_port *p = NULL;
    for (int i = 0; i < CDC_PORT_NUM; i++) {
        if (cdc_ports[i].out_ep == ep) {
            p = &cdc_ports[i];
            break;
        }
    }
    if (!p || nbytes > CDC_BUF_SIZE) {
        if (p) {
            usbd_ep_start_read(busid, p->out_ep, p->read_buf, CDC_BUF_SIZE);
        }
        return;
    }
    memcpy(p->write_buf, p->read_buf, nbytes);
    p->tx_busy = true;
    usbd_ep_start_write(busid, p->in_ep, p->write_buf, nbytes);
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
        usbd_ep_start_read(busid, p->out_ep, p->read_buf, CDC_BUF_SIZE);
    }
}

/*!< 端点回调表 */
struct usbd_endpoint cdc1_out_ep = { .ep_addr = CDC1_OUT_EP, .ep_cb = usbd_cdc_bulk_out };
struct usbd_endpoint cdc1_in_ep  = { .ep_addr = CDC1_IN_EP,  .ep_cb = usbd_cdc_bulk_in };
struct usbd_endpoint cdc2_out_ep = { .ep_addr = CDC2_OUT_EP, .ep_cb = usbd_cdc_bulk_out };
struct usbd_endpoint cdc2_in_ep  = { .ep_addr = CDC2_IN_EP,  .ep_cb = usbd_cdc_bulk_in };

/* ---- 通知端点(INT): 注册完成 ep_open 硬件配置; DTR 变化发 SERIAL_STATE ---- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_notify_buf[16];

static void usbd_cdc_notify_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
}

static void usbd_cdc_send_serial_state(uint8_t busid, uint8_t ep, uint8_t intf, uint16_t state)
{
    cdc_notify_buf[0] = 0xA1;      /* bmRequestType: D2H | Class | Interface */
    cdc_notify_buf[1] = 0x20;      /* SERIAL_STATE */
    cdc_notify_buf[2] = 0;         /* wValue */
    cdc_notify_buf[3] = 0;
    cdc_notify_buf[4] = intf;      /* wIndex = comm 接口号 */
    cdc_notify_buf[5] = 0;
    cdc_notify_buf[6] = 2;         /* wLength */
    cdc_notify_buf[7] = 0;
    cdc_notify_buf[8] = (uint8_t)state;   /* UART 状态位图: bit0 DCD, bit1 DSR */
    cdc_notify_buf[9] = (uint8_t)(state >> 8);
    usbd_ep_start_write(busid, ep, cdc_notify_buf, 10);
}

struct usbd_endpoint cdc1_notify_ep = { .ep_addr = CDC1_INT_EP, .ep_cb = usbd_cdc_notify_in };
struct usbd_endpoint cdc2_notify_ep = { .ep_addr = CDC2_INT_EP, .ep_cb = usbd_cdc_notify_in };

struct usbd_interface intf0; /* CDC1 comm */
struct usbd_interface intf1; /* CDC1 data */
struct usbd_interface intf2; /* CDC2 comm */
struct usbd_interface intf3; /* CDC2 data */

/* ---- DTR 弱回调覆盖: intf0=CDC1 comm, intf2=CDC2 comm ---- */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    if (intf == 0) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x01) : (dtr_intf_mask & ~0x01);
        usbd_cdc_send_serial_state(busid, CDC1_INT_EP, 0, dtr ? 0x0003 : 0x0000);
        printf("[cdc1] DTR=%d\n", dtr);
    } else if (intf == 2) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x02) : (dtr_intf_mask & ~0x02);
        usbd_cdc_send_serial_state(busid, CDC2_INT_EP, 2, dtr ? 0x0003 : 0x0000);
        printf("[cdc2] DTR=%d\n", dtr);
    }
}

/* ---- 状态行: DTR 打开的口每 5s 各发一行(标签区分两口) ---- */
static void usb_cdc_status_task(void *arg)
{
    (void)arg;
    os_time_dly(300);                    /* 等枚举稳定 */
    while (1) {
        for (int i = 0; i < CDC_PORT_NUM; i++) {
            if (!cdc_configured || !(dtr_intf_mask & (1u << i))) {
                continue;
            }
            struct cdc_port *p = &cdc_ports[i];
            int n = snprintf((char *)p->write_buf, CDC_BUF_SIZE,
                             "[wl82] cdc%d alive\r\n", i + 1);
            if (n <= 0) {
                continue;
            }
            int wait = 0;
            while (p->tx_busy && ++wait < 300) {
                os_time_dly(1);
            }
            if (p->tx_busy) {
                continue;                /* 主机暂停读取: 本轮跳过 */
            }
            p->tx_busy = true;
            usbd_ep_start_write(0, p->in_ep, p->write_buf, (uint32_t)n);
        }
        os_time_dly(500);                /* 5s */
    }
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

    os_task_create(usb_cdc_status_task, NULL, 9, 1024, 0, "cdc_status");
}
