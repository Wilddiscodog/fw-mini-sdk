#include "app_config.h"
#include "usb/device/usb_stack.h"
#include "usb/usb_config.h"
#include "usb/device/cdc.h"
#include "os/os_api.h"
#include "cdc_defs.h"

#define LOG_TAG_CONST       USB
#define LOG_TAG             "[USB]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_USB_SLAVE_CDC_ENABLE

/*
 * CDC 从机:
 *   CDC_DUAL_ENABLE=0: 原厂单 CDC(数据 EP=CDC_DATA_EP_*, 描述符可带 INT)
 *   CDC_DUAL_ENABLE=1: 双 CDC, 无 INT 通知端点
 *     CDC1(port0)=EP1 bulk, CDC2(port1)=EP2 bulk
 */

struct usb_cdc_gadget {
    u8 *cdc_buffer;
    u8 *bulk_ep_in_buffer;
    u8 *bulk_ep_out_buffer;
    void *priv;
    int (*output)(void *priv, u8 *obuf, u32 olen);
    void (*wakeup_handler)(struct usb_device_t *usb_device);
    OS_MUTEX mutex_data;
#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
    OS_MUTEX mutex_intr;
    u8 *intr_ep_in_buffer;
#endif
    u8 data_ep;
    u8 bmTransceiver;
    u8 subtype_data[8];
};

static struct usb_cdc_gadget *cdc_hdl[USB_MAX_HW_NUM][CDC_PORT_NUM];
/* wakeup 口在 OUT ISR 里先读走, 任务侧 cdc_read 取 pending, 避免延后 usb_g_bulk_read 得 0 */
static volatile u16 cdc_rx_pending[USB_MAX_HW_NUM][CDC_PORT_NUM];

#if USB_MALLOC_ENABLE
#else
static u8 _cdc_buffer[USB_MAX_HW_NUM][CDC_PORT_NUM][MAXP_SIZE_CDC_BULKOUT]
SEC(.cdc_var) __attribute__((aligned(4)));
static struct usb_cdc_gadget _cdc_hdl[USB_MAX_HW_NUM][CDC_PORT_NUM] SEC(.cdc_var);
#endif

#if CDC_DUAL_ENABLE
static const u8 cdc_data_eps[CDC_PORT_NUM] = { 1, 2 };
#else
static const u8 cdc_data_eps[CDC_PORT_NUM] = { CDC_DATA_EP_IN };
#endif

static u8 cdc_itf_to_port(u32 itf)
{
    u8 port = (u8)(itf / 2);
    if (port >= CDC_PORT_NUM) {
        port = 0;
    }
    return port;
}

static struct usb_cdc_gadget *cdc_get(const usb_dev usb_id, u8 port)
{
    if (usb_id >= USB_MAX_HW_NUM || port >= CDC_PORT_NUM) {
        return NULL;
    }
    return cdc_hdl[usb_id][port];
}

static u16 cdc_bulk_mps(const usb_dev usb_id)
{
    /* 对齐原厂: 即便 HUSB 也用 64 字节 bulk MPS(见 AC79 cdc_desc_config)。
     * 原先对 usb_id!=0 用 512 与原厂 EQ 路径不一致, 易导致收发包异常。 */
    (void)usb_id;
    return MAXP_SIZE_CDC_BULKIN;
}

/* ---- 描述符: 无 INT 的 ACM IAD(双 CDC 用) ---- */
#if CDC_DUAL_ENABLE
static u32 cdc_build_iad_no_intr(u8 *tptr, u8 itf, u8 data_ep, u16 mps)
{
    u8 *p = tptr;

    /* IAD */
    *p++ = 0x08;
    *p++ = 0x0b;
    *p++ = itf;
    *p++ = 0x02;
    *p++ = 0x02;
    *p++ = 0x02;
    *p++ = 0x01;
    *p++ = 0x00;

    /* Comm interface: 0 endpoint (no notify) */
    *p++ = 0x09;
    *p++ = 0x04;
    *p++ = itf;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x02;
    *p++ = 0x02;
    *p++ = 0x01;
    *p++ = 0x00;

    /* Header */
    *p++ = 0x05;
    *p++ = 0x24;
    *p++ = 0x00;
    *p++ = 0x10;
    *p++ = 0x01;

    /* Call Management */
    *p++ = 0x05;
    *p++ = 0x24;
    *p++ = 0x01;
    *p++ = 0x00;
    *p++ = (u8)(itf + 1);

    /* ACM: support line coding / control line state; no Serial_State notify */
    *p++ = 0x04;
    *p++ = 0x24;
    *p++ = 0x02;
    *p++ = 0x02;

    /* Union */
    *p++ = 0x05;
    *p++ = 0x24;
    *p++ = 0x06;
    *p++ = itf;
    *p++ = (u8)(itf + 1);

    /* Data interface */
    *p++ = 0x09;
    *p++ = 0x04;
    *p++ = (u8)(itf + 1);
    *p++ = 0x00;
    *p++ = 0x02;
    *p++ = 0x0a;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00;

    /* Bulk OUT */
    *p++ = 0x07;
    *p++ = 0x05;
    *p++ = data_ep;
    *p++ = 0x02;
    *p++ = mps & 0xff;
    *p++ = (mps >> 8) & 0xff;
    *p++ = 0x00;

    /* Bulk IN */
    *p++ = 0x07;
    *p++ = 0x05;
    *p++ = (u8)(USB_DIR_IN | data_ep);
    *p++ = 0x02;
    *p++ = mps & 0xff;
    *p++ = (mps >> 8) & 0xff;
    *p++ = 0x00;

    return (u32)(p - tptr);
}
#else
/* 原厂单 CDC 模板(含 INT EP 描述符项; 实际配置受 CDC_INTR_EP_ENABLE 控制) */
static const u8 cdc_virtual_comport_desc[] = {
    0x08, 0x0b, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x03, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,
    0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,
};
#endif

static void cdc_endpoint_init(struct usb_device_t *usb_device, u32 itf);
static u32 cdc_setup_rx(struct usb_device_t *usb_device, struct usb_ctrlrequest *ctrl_req);

static void usb_cdc_line_coding_init(struct usb_cdc_line_coding *lc)
{
    lc->dwDTERate = 460800;
    lc->bCharFormat = USB_CDC_1_STOP_BITS;
    lc->bParityType = USB_CDC_NO_PARITY;
    lc->bDataBits = 8;
}

static void dump_line_coding(struct usb_cdc_line_coding *lc)
{
    log_debug("dtw rate      : %d", lc->dwDTERate);
    log_debug("stop bits     : %d", lc->bCharFormat);
    log_debug("verify bits   : %d", lc->bParityType);
    log_debug("data bits     : %d", lc->bDataBits);
}

static u32 cdc_setup(struct usb_device_t *usb_device, struct usb_ctrlrequest *ctrl_req)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    u8 port = cdc_itf_to_port(ctrl_req->wIndex & 0xff);
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);
    int recip_type;
    u32 len;

    recip_type = ctrl_req->bRequestType & USB_TYPE_MASK;

    switch (recip_type) {
    case USB_TYPE_CLASS:
        switch (ctrl_req->bRequest) {
        case USB_CDC_REQ_SET_LINE_CODING:
            log_debug("set line coding p%d", port);
            usb_set_setup_recv(usb_device, cdc_setup_rx);
            break;
        case USB_CDC_REQ_GET_LINE_CODING:
            log_debug("get line coding p%d", port);
            len = ctrl_req->wLength < sizeof(struct usb_cdc_line_coding) ?
                  ctrl_req->wLength : sizeof(struct usb_cdc_line_coding);
            if (!g) {
                usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
                break;
            }
            usb_set_data_payload(usb_device, ctrl_req, g->subtype_data, len);
            dump_line_coding((struct usb_cdc_line_coding *)g->subtype_data);
            break;
        case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
            /* 对齐原厂: 收到本请求即视为口已打开(BIT0/1/4),
             * 不依赖工具是否拉高 DTR(EQ 工具常发 state=0)。 */
            log_debug("set control line p%d state=%d", port, ctrl_req->wValue);
            if (g) {
                g->bmTransceiver |= BIT(0) | BIT(1) | BIT(4);
            }
            usb_set_setup_phase(usb_device, USB_EP0_STAGE_SETUP);
            cdc_endpoint_init(usb_device, (ctrl_req->wIndex & 0xff));
            break;
        default:
            log_error("unsupported class req");
            usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
            break;
        }
        break;
    default:
        log_error("unsupported req type");
        usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
        break;
    }
    return 0;
}

static u32 cdc_setup_rx(struct usb_device_t *usb_device, struct usb_ctrlrequest *ctrl_req)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    u8 port = cdc_itf_to_port(ctrl_req->wIndex & 0xff);
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);
    int recip_type;
    struct usb_cdc_line_coding *lc;
    u32 len;
    u8 read_ep[8];

    len = ctrl_req->wLength;
    usb_read_ep0(usb_id, read_ep, len);
    recip_type = ctrl_req->bRequestType & USB_TYPE_MASK;
    switch (recip_type) {
    case USB_TYPE_CLASS:
        switch (ctrl_req->bRequest) {
        case USB_CDC_REQ_SET_LINE_CODING:
            if (!g) {
                break;
            }
            if (len > sizeof(struct usb_cdc_line_coding)) {
                len = sizeof(struct usb_cdc_line_coding);
            }
            memcpy(g->subtype_data, read_ep, len);
            lc = (struct usb_cdc_line_coding *)g->subtype_data;
            dump_line_coding(lc);
            break;
        }
        break;
    }
    return USB_EP0_STAGE_SETUP;
}

static void cdc_reset(struct usb_device_t *usb_device, u32 itf)
{
    log_error("%s() itf=%d", __func__, itf);
    const usb_dev usb_id = usb_device2id(usb_device);
    u8 port = cdc_itf_to_port(itf);
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);

#if USB_ROOT2
    if (g) {
        usb_disable_ep(usb_id, g->data_ep);
    }
#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
    usb_disable_ep(usb_id, CDC_INTR_EP_IN);
#endif
#else
    cdc_endpoint_init(usb_device, itf);
#endif
}

u32 cdc_desc_config(const usb_dev usb_id, u8 *ptr, u32 *itf)
{
    u8 *tptr = ptr;
    u8 port;

#if CDC_DUAL_ENABLE
    u16 mps = cdc_bulk_mps(usb_id);
    for (port = 0; port < CDC_PORT_NUM; port++) {
        u8 i0 = (u8)(*itf);
        tptr += cdc_build_iad_no_intr(tptr, i0, cdc_data_eps[port], mps);
        if (usb_set_interface_hander(usb_id, i0, cdc_setup) != i0) {
            ASSERT(0, "cdc set interface_hander fail");
        }
        if (usb_set_reset_hander(usb_id, i0, cdc_reset) != i0) {
        }
        *itf = i0 + 2;
    }
    log_info("cdc dual desc: EP1+EP2 no-notify, itf done=%d", *itf);
#else
    memcpy(tptr, cdc_virtual_comport_desc, sizeof(cdc_virtual_comport_desc));
    tptr[2] = *itf;
    tptr[8 + 2] = *itf;
    tptr[8 + 9 + 5 + 4] = *itf + 1;
    tptr[8 + 9 + 5 + 5 + 4 + 3] = *itf;
    tptr[8 + 9 + 5 + 5 + 4 + 4] = *itf + 1;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 2] = USB_DIR_IN | CDC_INTR_EP_IN;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 4] = MAXP_SIZE_CDC_INTRIN & 0xff;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 5] = (MAXP_SIZE_CDC_INTRIN >> 8) & 0xff;
#if defined(CONFIG_CPU_WL82)
    if (usb_id != 0) {
        tptr[8 + 9 + 5 + 5 + 4 + 5 + 6] = 4;
    }
#endif
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 2] = *itf + 1;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 2] = CDC_DATA_EP_OUT;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 4] = MAXP_SIZE_CDC_BULKOUT & 0xff;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 5] = (MAXP_SIZE_CDC_BULKOUT >> 8) & 0xff;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 2] = USB_DIR_IN | CDC_DATA_EP_IN;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 4] = MAXP_SIZE_CDC_BULKIN & 0xff;
    tptr[8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 5] = (MAXP_SIZE_CDC_BULKIN >> 8) & 0xff;
    tptr += sizeof(cdc_virtual_comport_desc);

    if (usb_set_interface_hander(usb_id, *itf, cdc_setup) != *itf) {
        ASSERT(0, "cdc set interface_hander fail");
    }
    if (usb_set_reset_hander(usb_id, *itf, cdc_reset) != *itf) {
    }
    *itf += 2;
    (void)port;
#endif
    return (u32)(tptr - ptr);
}

void cdc_set_wakeup_handler(const usb_dev usb_id, void (*handle)(struct usb_device_t *usb_device))
{
    u8 port;
    for (port = 0; port < CDC_PORT_NUM; port++) {
        if (cdc_hdl[usb_id][port]) {
            cdc_hdl[usb_id][port]->wakeup_handler = handle;
        }
    }
}

static void cdc_out_handler(struct usb_device_t *usb_device, u32 ep)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    u8 port;
    struct usb_cdc_gadget *g = NULL;
    u32 len;
    u8 ep_n = ep & 0x0f;

    for (port = 0; port < CDC_PORT_NUM; port++) {
        if (cdc_hdl[usb_id][port] && cdc_hdl[usb_id][port]->data_ep == ep_n) {
            g = cdc_hdl[usb_id][port];
            break;
        }
    }
    if (!g) {
        return;
    }

    /*
     * 有 output: ISR 读 + 回调。
     * 有 wakeup: ISR 预读进 pending 再通知任务侧 cdc_read。
     * 无 reader: 读走丢弃, 避免 OUT 堵死(双 CDC 空闲口)。
     */
    if (g->output) {
        len = usb_g_bulk_read(usb_id, g->data_ep, g->cdc_buffer, cdc_bulk_mps(usb_id), 0);
        if (len) {
            g->output(g->priv, g->cdc_buffer, len);
        }
    } else if (g->wakeup_handler) {
        len = usb_g_bulk_read(usb_id, g->data_ep, g->cdc_buffer, cdc_bulk_mps(usb_id), 0);
        cdc_rx_pending[usb_id][port] = (u16)len;
        g->wakeup_handler(usb_device);
    } else {
        (void)usb_g_bulk_read(usb_id, g->data_ep, g->cdc_buffer, cdc_bulk_mps(usb_id), 0);
    }
}

void cdc_set_output_handle(const usb_dev usb_id, void *priv, int (*output_handler)(void *priv, u8 *buf, u32 len))
{
    cdc_set_output_handle_port(usb_id, 0, priv, output_handler);
}

void cdc_set_output_handle_port(const usb_dev usb_id, u8 port, void *priv,
                                int (*output_handler)(void *priv, u8 *buf, u32 len))
{
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);
    if (g) {
        g->output = output_handler;
        g->priv = priv;
    }
}

static void cdc_endpoint_init(struct usb_device_t *usb_device, u32 itf)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    u8 port = cdc_itf_to_port(itf);
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);
    u16 mps_in = MAXP_SIZE_CDC_BULKIN;
    u16 mps_out = MAXP_SIZE_CDC_BULKOUT;

    ASSERT(g, "cdc not register");

    cdc_rx_pending[usb_id][port] = 0;
    mps_in = mps_out = cdc_bulk_mps(usb_id);

    usb_g_ep_config(usb_id, g->data_ep | USB_DIR_IN, USB_ENDPOINT_XFER_BULK,
                    0, g->bulk_ep_in_buffer, mps_in);
    usb_g_ep_config(usb_id, g->data_ep | USB_DIR_OUT, USB_ENDPOINT_XFER_BULK,
                    1, g->bulk_ep_out_buffer, mps_out);
    usb_g_set_intr_hander(usb_id, g->data_ep | USB_DIR_OUT, cdc_out_handler);
    usb_enable_ep(usb_id, g->data_ep);

#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
    usb_g_ep_config(usb_id, CDC_INTR_EP_IN | USB_DIR_IN, USB_ENDPOINT_XFER_INT,
                    0, g->intr_ep_in_buffer, MAXP_SIZE_CDC_INTRIN);
    usb_enable_ep(usb_id, CDC_INTR_EP_IN);
#endif
}

u32 cdc_read_data_port(const usb_dev usb_id, u8 port, u8 *buf, u32 len)
{
    u32 rxlen;
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);

    if (!g) {
        return 0;
    }
    os_mutex_pend(&g->mutex_data, 0);
    if (cdc_rx_pending[usb_id][port]) {
        rxlen = cdc_rx_pending[usb_id][port];
        cdc_rx_pending[usb_id][port] = 0;
    } else {
        rxlen = usb_g_bulk_read(usb_id, g->data_ep, g->cdc_buffer, cdc_bulk_mps(usb_id), 0);
    }
    rxlen = rxlen > len ? len : rxlen;
    if (rxlen > 0) {
        memcpy(buf, g->cdc_buffer, rxlen);
    }
    os_mutex_post(&g->mutex_data);
    return rxlen;
}

u32 cdc_read_data(const usb_dev usb_id, u8 *buf, u32 len)
{
    return cdc_read_data_port(usb_id, 0, buf, len);
}

u32 cdc_write_data_port(const usb_dev usb_id, u8 port, u8 *buf, u32 len)
{
    u32 txlen, offset;
    struct usb_cdc_gadget *g = cdc_get(usb_id, port);
    u16 mps;

    if (!g) {
        return 0;
    }
    /* 对齐原厂 cdc_write_data: 需 RTS(BIT1) + cfg done(BIT4) */
    if ((g->bmTransceiver & (BIT(1) | BIT(4))) != (BIT(1) | BIT(4))) {
        return 0;
    }
    mps = cdc_bulk_mps(usb_id);
    offset = 0;
    os_mutex_pend(&g->mutex_data, 0);
    while (offset < len) {
        txlen = len - offset > mps ? mps : len - offset;
        txlen = usb_g_bulk_write(usb_id, g->data_ep, buf + offset, txlen);
        if (txlen == 0) {
            break;
        }
        if ((g->bmTransceiver & (BIT(1) | BIT(4))) != (BIT(1) | BIT(4))) {
            break;
        }
        offset += txlen;
    }
    if (offset && !(offset % mps)) {
        usb_g_bulk_write(usb_id, g->data_ep, NULL, 0);
    }
    os_mutex_post(&g->mutex_data);
    return offset;
}

u32 cdc_write_data(const usb_dev usb_id, u8 *buf, u32 len)
{
    return cdc_write_data_port(usb_id, 0, buf, len);
}

u32 cdc_write_inir(const usb_dev usb_id, u8 *buf, u32 len)
{
#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
    u32 txlen, offset;
    struct usb_cdc_gadget *g = cdc_get(usb_id, 0);
    if (!g) {
        return 0;
    }
    if ((g->bmTransceiver & BIT(4)) == 0) {
        return 0;
    }
    offset = 0;
    os_mutex_pend(&g->mutex_intr, 0);
    while (offset < len) {
        txlen = len - offset > MAXP_SIZE_CDC_INTRIN ?
                MAXP_SIZE_CDC_INTRIN : len - offset;
        txlen = usb_g_intr_write(usb_id, CDC_INTR_EP_IN, buf + offset, txlen);
        if (txlen == 0) {
            break;
        }
        if ((g->bmTransceiver & BIT(4)) == 0) {
            break;
        }
        offset += txlen;
    }
    os_mutex_post(&g->mutex_intr);
    return offset;
#else
    (void)usb_id;
    (void)buf;
    (void)len;
    return 0;
#endif
}

void cdc_register(const usb_dev usb_id)
{
    u8 port;
    struct usb_cdc_line_coding *lc;
    u16 mps_in = cdc_bulk_mps(usb_id);
    u16 mps_out = mps_in;

    for (port = 0; port < CDC_PORT_NUM; port++) {
        if (cdc_hdl[usb_id][port]) {
            continue;
        }
#if USB_MALLOC_ENABLE
        cdc_hdl[usb_id][port] = zalloc(sizeof(struct usb_cdc_gadget));
        if (!cdc_hdl[usb_id][port]) {
            log_error("cdc_register err port=%d", port);
            return;
        }
        cdc_hdl[usb_id][port]->cdc_buffer = malloc(mps_out);
        if (!cdc_hdl[usb_id][port]->cdc_buffer) {
            log_error("cdc_register buf err port=%d", port);
            free(cdc_hdl[usb_id][port]);
            cdc_hdl[usb_id][port] = NULL;
            return;
        }
#else
        memset(&_cdc_hdl[usb_id][port], 0, sizeof(struct usb_cdc_gadget));
        cdc_hdl[usb_id][port] = &_cdc_hdl[usb_id][port];
        cdc_hdl[usb_id][port]->cdc_buffer = _cdc_buffer[usb_id][port];
#endif
        cdc_hdl[usb_id][port]->data_ep = cdc_data_eps[port];
        lc = (struct usb_cdc_line_coding *)cdc_hdl[usb_id][port]->subtype_data;
        usb_cdc_line_coding_init(lc);
        os_mutex_create(&cdc_hdl[usb_id][port]->mutex_data);

        cdc_hdl[usb_id][port]->bulk_ep_in_buffer =
            usb_alloc_ep_dmabuffer(usb_id, cdc_data_eps[port] | USB_DIR_IN, mps_in);
        cdc_hdl[usb_id][port]->bulk_ep_out_buffer =
            usb_alloc_ep_dmabuffer(usb_id, cdc_data_eps[port] | USB_DIR_OUT, mps_out);

#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
        if (port == 0) {
            os_mutex_create(&cdc_hdl[usb_id][port]->mutex_intr);
            cdc_hdl[usb_id][port]->intr_ep_in_buffer =
                usb_alloc_ep_dmabuffer(usb_id, CDC_INTR_EP_IN | USB_DIR_IN, MAXP_SIZE_CDC_INTRIN);
        }
#endif
        log_info("cdc_register id=%d port=%d ep=%d", usb_id, port, cdc_data_eps[port]);
    }
}

void cdc_release(const usb_dev usb_id)
{
    u8 port;

    for (port = 0; port < CDC_PORT_NUM; port++) {
        struct usb_cdc_gadget *g = cdc_hdl[usb_id][port];
        if (!g) {
            continue;
        }
        if (g->bulk_ep_in_buffer) {
            usb_free_ep_dmabuffer(usb_id, g->bulk_ep_in_buffer);
            g->bulk_ep_in_buffer = NULL;
        }
        if (g->bulk_ep_out_buffer) {
            usb_free_ep_dmabuffer(usb_id, g->bulk_ep_out_buffer);
            g->bulk_ep_out_buffer = NULL;
        }
#if CDC_INTR_EP_ENABLE && !CDC_DUAL_ENABLE
        if (g->intr_ep_in_buffer) {
            usb_free_ep_dmabuffer(usb_id, g->intr_ep_in_buffer);
            g->intr_ep_in_buffer = NULL;
        }
#endif
#if USB_MALLOC_ENABLE
        free(g->cdc_buffer);
        free(g);
#endif
        cdc_hdl[usb_id][port] = NULL;
    }
}

#endif /* TCFG_USB_SLAVE_CDC_ENABLE */
