/*
 * CherryUSB CDC-ACM 设备应用 (JL WL82 HUSB 高速口)
 *
 * 功能：
 *  - 枚举为 USB 串行口 (CDC ACM)；Windows10+/Linux 免驱 (usbser)
 *  - 回显：主机发什么，原样回什么
 *  - DTR 打开时，每 5 秒主动发一行状态信息
 *
 * 端点规划（JL HUSB EP1 双向 512 字节 FIFO 已验证）：
 *  0x81 = EP1 IN  bulk (数据上行)   0x01 = EP1 OUT bulk (数据下行)
 *  0x82 = EP2 IN  int  (通知, 保留)
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define CDC_IN_EP   0x81
#define CDC_OUT_EP  0x02
#define CDC_INT_EP  0x83

#define USBD_VID           0xFFFF
#define USBD_PID           0x8888
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },       /* Langid */
    "fw-mini-sdk",                      /* Manufacturer */
    "CherryUSB CDC ACM",                /* Product */
    "WL82-0001",                        /* Serial Number */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
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
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv1_descriptor = NULL,
    .msosv2_descriptor = NULL,
    .webusb_url_descriptor = NULL,
    .bos_descriptor = NULL,
};

/* EP 缓冲: 按 HS bulk MPS=512 分配 */
USB_MEM_ALIGNX uint8_t cdc_read_buffer[512];
USB_MEM_ALIGNX uint8_t cdc_write_buffer[512];

volatile bool ep_tx_busy_flag = false;

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            break;
        case USBD_EVENT_CONFIGURED:
            ep_tx_busy_flag = false;
            /* 启动第一次 OUT 读传输 */
            usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, sizeof(cdc_read_buffer));
            break;
        case USBD_EVENT_SUSPEND:
        case USBD_EVENT_DISCONNECTED:
            break;
        default:
            break;
    }
}

void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    // /* 回显: 收到多少, 原样发回 */
    // memcpy(cdc_write_buffer, cdc_read_buffer, nbytes);
    // ep_tx_busy_flag = true;
    // usbd_ep_start_write(busid, CDC_IN_EP, cdc_write_buffer, nbytes);
    /* 启动下一次 OUT 读传输 */
    usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, sizeof(cdc_read_buffer));
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (nbytes && (nbytes % usbd_get_ep_mps(busid, ep)) == 0) {
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0); /* 补 ZLP */
    } else {
        ep_tx_busy_flag = false;
    }
}

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out,
};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in,
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

void cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    const uint8_t data[10] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30 };

    memcpy(&cdc_write_buffer[0], data, 10);
    memset(&cdc_write_buffer[10], 'a', 502);

    usbd_desc_register(busid, &cdc_descriptor);

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

volatile uint8_t dtr_enable = 0;

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    if (dtr) {
        dtr_enable = 1;
    } else {
        dtr_enable = 0;
    }
}

void cdc_acm_data_send_with_dtr_test(uint8_t busid)
{
    if (dtr_enable) {
        ep_tx_busy_flag = true;
        usbd_ep_start_write(busid, CDC_IN_EP, cdc_write_buffer, 512);
        while (ep_tx_busy_flag) {
        }
    }
}