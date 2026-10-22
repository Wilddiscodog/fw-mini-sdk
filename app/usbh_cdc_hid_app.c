#include "usbh_core.h"
#include "usbh_serial.h"
#include "usbh_hid.h"
#include "usb_osal.h"
#include "usb_config.h"
#include <stdio.h>

#define CDC_RX_CHUNK_SIZE 64
#define HID_REPORT_BUFFER_SIZE 512
#define HID_REPORT_LOG_LIMIT 16

struct cdc_reader_context {
    struct usbh_serial *serial;
    usb_osal_thread_t thread;
    usb_osal_sem_t stopped;
    volatile bool running;
};

struct hid_reader_context {
    struct usbh_hid *hid;
    volatile bool running;
    uint32_t report_count;
    uint8_t report[HID_REPORT_BUFFER_SIZE];
};

static struct cdc_reader_context g_cdc_reader[CONFIG_USBHOST_MAX_SERIAL_CLASS];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct hid_reader_context g_hid_reader[CONFIG_USBHOST_MAX_HID_CLASS];

static void cdc_reader_thread(void *argument)
{
    struct cdc_reader_context *context = argument;
    uint8_t buffer[CDC_RX_CHUNK_SIZE];

    while (context->running && context->serial && context->serial->hport->connected) {
        int ret = usbh_serial_read(context->serial, buffer, sizeof(buffer));

        if (ret > 0) {
            printf("[usbh-cdc] rx %d bytes:", ret);
            for (int index = 0; index < ret; index++) {
                printf(" %02x", buffer[index]);
            }
            printf("\n");
        } else if (ret < 0 && ret != -USB_ERR_NAK && context->running) {
            printf("[usbh-cdc] read error %d\n", ret);
            break;
        }

        usb_osal_msleep(20);
    }

    context->running = false;
    usb_osal_sem_give(context->stopped);
    usb_osal_thread_delete(NULL);
}

void usbh_serial_run(struct usbh_serial *serial)
{
    struct cdc_reader_context *context;
    struct usbh_serial_termios line_coding = { 0 };
    char device_name[CONFIG_USBHOST_DEV_NAMELEN];
    int ret;

    if (!serial || serial->cdc_minor < 0 || serial->cdc_minor >= CONFIG_USBHOST_MAX_SERIAL_CLASS) {
        return;
    }

    context = &g_cdc_reader[serial->cdc_minor];
    if (context->running) {
        return;
    }

    snprintf(device_name, sizeof(device_name), "/dev/ttyACM%d", serial->cdc_minor);
    context->serial = usbh_serial_open(device_name, USBH_SERIAL_O_RDWR | USBH_SERIAL_O_NONBLOCK);
    if (!context->serial) {
        printf("[usbh-cdc] open %s failed\n", device_name);
        return;
    }

    line_coding.baudrate = 115200;
    line_coding.databits = 8;
    line_coding.stopbits = USBH_SERIAL_STOPBITS_1;
    line_coding.parity = USBH_SERIAL_PARITY_NONE;
    ret = usbh_serial_control(context->serial, USBH_SERIAL_CMD_SET_ATTR, &line_coding);
    if (ret < 0) {
        printf("[usbh-cdc] configure %s failed: %d\n", device_name, ret);
        usbh_serial_close(context->serial);
        context->serial = NULL;
        return;
    }

    context->stopped = usb_osal_sem_create(0);
    if (!context->stopped) {
        usbh_serial_close(context->serial);
        context->serial = NULL;
        return;
    }

    context->running = true;
    context->thread = usb_osal_thread_create("usbh_cdc_rx", 3072, 6, cdc_reader_thread, context);
    if (!context->thread) {
        context->running = false;
        usb_osal_sem_delete(context->stopped);
        context->stopped = NULL;
        usbh_serial_close(context->serial);
        context->serial = NULL;
        return;
    }

    printf("[usbh-cdc] %s open, 115200 8N1\n", device_name);
}

void usbh_serial_stop(struct usbh_serial *serial)
{
    struct cdc_reader_context *context;

    if (!serial || serial->cdc_minor < 0 || serial->cdc_minor >= CONFIG_USBHOST_MAX_SERIAL_CLASS) {
        return;
    }

    context = &g_cdc_reader[serial->cdc_minor];
    if (context->serial != serial) {
        return;
    }

    context->running = false;
    if (context->stopped) {
        usb_osal_sem_take(context->stopped, 1000);
        usb_osal_sem_delete(context->stopped);
    }
    context->serial = NULL;
    context->thread = NULL;
    context->stopped = NULL;
    printf("[usbh-cdc] %d disconnected\n", serial->cdc_minor);
}

static void hid_submit(struct hid_reader_context *context);

static void hid_report_complete(void *argument, int nbytes)
{
    struct hid_reader_context *context = argument;

    if (!context || !context->running || !context->hid) {
        return;
    }

    if (nbytes > 0) {
        context->report_count++;
        if (context->report_count <= HID_REPORT_LOG_LIMIT) {
            printf("[usbh-hid] report %lu (%d bytes):", (unsigned long)context->report_count, nbytes);
            for (int index = 0; index < nbytes; index++) {
                printf(" %02x", context->report[index]);
            }
            printf("\n");
        }
    } else if (nbytes != -USB_ERR_NAK) {
        printf("[usbh-hid] transfer error %d\n", nbytes);
        context->running = false;
        return;
    }

    hid_submit(context);
}

static void hid_submit(struct hid_reader_context *context)
{
    struct usbh_hid *hid = context->hid;
    uint16_t packet_size;

    if (!context->running || !hid || !hid->hport->connected || !hid->intin) {
        return;
    }

    packet_size = USB_GET_MAXPACKETSIZE(hid->intin->wMaxPacketSize);
    if (packet_size == 0 || packet_size > sizeof(context->report)) {
        printf("[usbh-hid] unsupported report packet size %u\n", packet_size);
        context->running = false;
        return;
    }

    usbh_int_urb_fill(&hid->intin_urb, hid->hport, hid->intin,
                      context->report, packet_size, 0, hid_report_complete, context);
    if (usbh_submit_urb(&hid->intin_urb) < 0) {
        context->running = false;
    }
}

void usbh_hid_run(struct usbh_hid *hid)
{
    struct hid_reader_context *context;

    if (!hid || hid->minor >= CONFIG_USBHOST_MAX_HID_CLASS || !hid->intin) {
        return;
    }

    context = &g_hid_reader[hid->minor];
    context->hid = hid;
    context->running = true;
    context->report_count = 0;
    hid->user_data = context;

    printf("[usbh-hid] interface %u protocol %u packet %u\n",
           hid->intf, hid->protocol,
           USB_GET_MAXPACKETSIZE(hid->intin->wMaxPacketSize));
    hid_submit(context);
}

void usbh_hid_stop(struct usbh_hid *hid)
{
    struct hid_reader_context *context;

    if (!hid || hid->minor >= CONFIG_USBHOST_MAX_HID_CLASS) {
        return;
    }

    context = &g_hid_reader[hid->minor];
    context->running = false;
    if (hid->intin_urb.hcpriv) {
        usbh_kill_urb(&hid->intin_urb);
    }
    context->hid = NULL;
    hid->user_data = NULL;
}