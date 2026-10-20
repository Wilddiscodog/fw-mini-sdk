/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * JL WL82 (AC791x) HUSB(USB1) 适配：
 *  - SIE 寄存器布局与 CherryUSB 默认 MUSB 布局一致（FADDR=0x00, FIFO=0x20+4n,
 *    端点平铺窗口 0x100+0x10n），基址固定 0x16000。
 *  - JL 无 TXFIFOSZ/RXFIFOSZ/FIFOADD 寄存器：端点 FIFO 硬件固定，跳过 FIFO 配置。
 *  - IS/TXIS/RXIS 为读清除, 且必须把桥接器(HUSB_SIE_CON) bit12(HUSB_INT_MODE)
 *    拉低形成窗口后再读才生效(闭源 usb_read_intr 还原); EP0 FIFO 读同理。
 *    bulk FIFO 读写 / EP0 FIFO 写 / CSR 寄存器均直接访问。
 */
#include "usbd_core.h"
#include "usb_musb_reg.h"

#define HWREG(x) \
    (*((volatile uint32_t *)(x)))
#define HWREGH(x) \
    (*((volatile uint16_t *)(x)))
#define HWREGB(x) \
    (*((volatile uint8_t *)(x)))

#ifdef CONFIG_USB_MUSB_JL82
#define USB_BASE        (0x16000UL) /* lsfr_base(0x10000) + map_adr(0x60,0) */
#define JL_HUSB_BRIDGE  (*(volatile uint32_t *)0x16800UL) /* map_adr(0x68,0) = HUSB_SIE_CON */
#define JL_BRIDGE_INT_MODE  0x1000  /* HUSB_INT_MODE: 置0窗口内读 IS/TXIS/RXIS/EP0FIFO 才会清除 */

/* 闭源 usb_read_intr 还原: 清 INT_MODE → 读三组状态(读即清除) → 恢复 INT_MODE */
static inline void jl_usb_int_mode(uint8_t enable)
{
    uint32_t v = JL_HUSB_BRIDGE;
    JL_HUSB_BRIDGE = enable ? (v | JL_BRIDGE_INT_MODE) : (v & ~JL_BRIDGE_INT_MODE);
    __asm__ volatile("csync");
}

static inline void jl_usb_read_intr(uint8_t *is, uint16_t *txis, uint16_t *rxis)
{
    jl_usb_int_mode(0);
    *is   = HWREGB(USB_BASE + 0x0A); /* MUSB_IS_OFFSET(默认布局) */
    *txis = HWREGH(USB_BASE + 0x02); /* MUSB_TXIS_OFFSET */
    *rxis = HWREGH(USB_BASE + 0x04); /* MUSB_RXIS_OFFSET */
    jl_usb_int_mode(1);
}
#else
#define USB_BASE (g_usbdev_bus[0].reg_base)
#endif

#if defined(CONFIG_USB_MUSB_SUNXI)
#define MUSB_FADDR_OFFSET 0x98
#define MUSB_POWER_OFFSET 0x40
#define MUSB_TXIS_OFFSET  0x44
#define MUSB_RXIS_OFFSET  0x46
#define MUSB_TXIE_OFFSET  0x48
#define MUSB_RXIE_OFFSET  0x4A
#define MUSB_IS_OFFSET    0x4C
#define MUSB_IE_OFFSET    0x50
#define MUSB_EPIDX_OFFSET 0x42

#define MUSB_IND_TXMAP_OFFSET      0x80
#define MUSB_IND_TXCSRL_OFFSET     0x82
#define MUSB_IND_TXCSRH_OFFSET     0x83
#define MUSB_IND_RXMAP_OFFSET      0x84
#define MUSB_IND_RXCSRL_OFFSET     0x86
#define MUSB_IND_RXCSRH_OFFSET     0x87
#define MUSB_IND_RXCOUNT_OFFSET    0x88
#define MUSB_IND_TXTYPE_OFFSET     0x8C
#define MUSB_IND_TXINTERVAL_OFFSET 0x8D
#define MUSB_IND_RXTYPE_OFFSET     0x8E
#define MUSB_IND_RXINTERVAL_OFFSET 0x8F

#define MUSB_FIFO_OFFSET 0x00

#define MUSB_DEVCTL_OFFSET 0x41

#define MUSB_TXFIFOSZ_OFFSET  0x90
#define MUSB_RXFIFOSZ_OFFSET  0x94
#define MUSB_TXFIFOADD_OFFSET 0x92
#define MUSB_RXFIFOADD_OFFSET 0x96

#define MUSB_TXFUNCADDR0_OFFSET 0x98
#define MUSB_TXHUBADDR0_OFFSET  0x9A
#define MUSB_TXHUBPORT0_OFFSET  0x9B
#define MUSB_TXFUNCADDRx_OFFSET 0x98
#define MUSB_TXHUBADDRx_OFFSET  0x9A
#define MUSB_TXHUBPORTx_OFFSET  0x9B
#define MUSB_RXFUNCADDRx_OFFSET 0x9C
#define MUSB_RXHUBADDRx_OFFSET  0x9E
#define MUSB_RXHUBPORTx_OFFSET  0x9F

#define USB_TXMAP_BASE(ep_idx)      (USB_BASE + MUSB_IND_TXMAP_OFFSET)
#define USB_TXCSRL_BASE(ep_idx)     (USB_BASE + MUSB_IND_TXCSRL_OFFSET)
#define USB_TXCSRH_BASE(ep_idx)     (USB_BASE + MUSB_IND_TXCSRH_OFFSET)
#define USB_RXMAP_BASE(ep_idx)      (USB_BASE + MUSB_IND_RXMAP_OFFSET)
#define USB_RXCSRL_BASE(ep_idx)     (USB_BASE + MUSB_IND_RXCSRL_OFFSET)
#define USB_RXCSRH_BASE(ep_idx)     (USB_BASE + MUSB_IND_RXCSRH_OFFSET)
#define USB_RXCOUNT_BASE(ep_idx)    (USB_BASE + MUSB_IND_RXCOUNT_OFFSET)
#define USB_TXTYPE_BASE(ep_idx)     (USB_BASE + MUSB_IND_TXTYPE_OFFSET)
#define USB_TXINTERVAL_BASE(ep_idx) (USB_BASE + MUSB_IND_TXINTERVAL_OFFSET)
#define USB_RXTYPE_BASE(ep_idx)     (USB_BASE + MUSB_IND_RXTYPE_OFFSET)
#define USB_RXINTERVAL_BASE(ep_idx) (USB_BASE + MUSB_IND_RXINTERVAL_OFFSET)

#define USB_TXADDR_BASE(ep_idx)    (USB_BASE + MUSB_TXFUNCADDRx_OFFSET)
#define USB_TXHUBADDR_BASE(ep_idx) (USB_BASE + MUSB_TXHUBADDRx_OFFSET)
#define USB_TXHUBPORT_BASE(ep_idx) (USB_BASE + MUSB_TXHUBPORTx_OFFSET)
#define USB_RXADDR_BASE(ep_idx)    (USB_BASE + MUSB_RXFUNCADDRx_OFFSET)
#define USB_RXHUBADDR_BASE(ep_idx) (USB_BASE + MUSB_RXHUBADDRx_OFFSET)
#define USB_RXHUBPORT_BASE(ep_idx) (USB_BASE + MUSB_RXHUBPORTx_OFFSET)

#elif defined(CONFIG_USB_MUSB_CUSTOM)
#include "musb_custom.h"
#else
#define MUSB_FADDR_OFFSET 0x00
#define MUSB_POWER_OFFSET 0x01
#define MUSB_TXIS_OFFSET  0x02
#define MUSB_RXIS_OFFSET  0x04
#define MUSB_TXIE_OFFSET  0x06
#define MUSB_RXIE_OFFSET  0x08
#define MUSB_IS_OFFSET    0x0A
#define MUSB_IE_OFFSET    0x0B

#define MUSB_EPIDX_OFFSET 0x0E

#define MUSB_IND_TXMAP_OFFSET      0x10
#define MUSB_IND_TXCSRL_OFFSET     0x12
#define MUSB_IND_TXCSRH_OFFSET     0x13
#define MUSB_IND_RXMAP_OFFSET      0x14
#define MUSB_IND_RXCSRL_OFFSET     0x16
#define MUSB_IND_RXCSRH_OFFSET     0x17
#define MUSB_IND_RXCOUNT_OFFSET    0x18
#define MUSB_IND_TXTYPE_OFFSET     0x1A
#define MUSB_IND_TXINTERVAL_OFFSET 0x1B
#define MUSB_IND_RXTYPE_OFFSET     0x1C
#define MUSB_IND_RXINTERVAL_OFFSET 0x1D

#define MUSB_FIFO_OFFSET 0x20

#define MUSB_DEVCTL_OFFSET 0x60

#define MUSB_TXFIFOSZ_OFFSET  0x62
#define MUSB_RXFIFOSZ_OFFSET  0x63
#define MUSB_TXFIFOADD_OFFSET 0x64
#define MUSB_RXFIFOADD_OFFSET 0x66

#define MUSB_TXFUNCADDR0_OFFSET 0x80
#define MUSB_TXHUBADDR0_OFFSET  0x82
#define MUSB_TXHUBPORT0_OFFSET  0x83
#define MUSB_TXFUNCADDRx_OFFSET 0x88
#define MUSB_TXHUBADDRx_OFFSET  0x8A
#define MUSB_TXHUBPORTx_OFFSET  0x8B
#define MUSB_RXFUNCADDRx_OFFSET 0x8C
#define MUSB_RXHUBADDRx_OFFSET  0x8E
#define MUSB_RXHUBPORTx_OFFSET  0x8F

#define MUSB_TXMAP0_OFFSET          0x100

// do not use EPIDX
#define USB_TXMAP_BASE(ep_idx)      (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx)
#define USB_TXCSRL_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 2)
#define USB_TXCSRH_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 3)
#define USB_RXMAP_BASE(ep_idx)      (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 4)
#define USB_RXCSRL_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 6)
#define USB_RXCSRH_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 7)
#define USB_RXCOUNT_BASE(ep_idx)    (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 8)
#define USB_TXTYPE_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 0x0A)
#define USB_TXINTERVAL_BASE(ep_idx) (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 0x0B)
#define USB_RXTYPE_BASE(ep_idx)     (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 0x0C)
#define USB_RXINTERVAL_BASE(ep_idx) (USB_BASE + MUSB_TXMAP0_OFFSET + 0x10 * ep_idx + 0x0D)

#define USB_TXADDR_BASE(ep_idx)    (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx)
#define USB_TXHUBADDR_BASE(ep_idx) (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx + 2)
#define USB_TXHUBPORT_BASE(ep_idx) (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx + 3)
#define USB_RXADDR_BASE(ep_idx)    (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx + 4)
#define USB_RXHUBADDR_BASE(ep_idx) (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx + 6)
#define USB_RXHUBPORT_BASE(ep_idx) (USB_BASE + MUSB_TXFUNCADDR0_OFFSET + 0x8 * ep_idx + 7)
#endif

#define USB_FIFO_BASE(ep_idx) (USB_BASE + MUSB_FIFO_OFFSET + 0x4 * ep_idx)

typedef enum {
    USB_EP0_STATE_SETUP = 0x0,      /**< SETUP DATA */
    USB_EP0_STATE_IN_DATA = 0x1,    /**< IN DATA */
    USB_EP0_STATE_OUT_DATA = 0x3,   /**< OUT DATA */
    USB_EP0_STATE_IN_STATUS = 0x4,  /**< IN status */
    USB_EP0_STATE_OUT_STATUS = 0x5, /**< OUT status */
    USB_EP0_STATE_IN_ZLP = 0x6,     /**< OUT status */
    USB_EP0_STATE_STALL = 0x7,      /**< STALL status */
} ep0_state_t;

/* Endpoint state */
struct musb_ep_state {
    uint16_t ep_mps;    /* Endpoint max packet size */
    uint8_t ep_type;    /* Endpoint type */
    uint8_t ep_stalled; /* Endpoint stall flag */
    uint8_t ep_enable;  /* Endpoint enable */
    uint8_t *xfer_buf;
    uint32_t xfer_len;
    uint32_t actual_xfer_len;
};

/* Driver state */
struct musb_udc {
    volatile uint8_t dev_addr;
    __attribute__((aligned(32))) struct usb_setup_packet setup;
    struct musb_ep_state in_ep[CONFIG_USB_MUSB_EP_NUM];  /*!< IN endpoint parameters*/
    struct musb_ep_state out_ep[CONFIG_USB_MUSB_EP_NUM]; /*!< OUT endpoint parameters */
} g_musb_udc;

static volatile uint8_t usb_ep0_state = USB_EP0_STATE_SETUP;

/* get current active ep */
static uint8_t musb_get_active_ep(void)
{
    return HWREGB(USB_BASE + MUSB_EPIDX_OFFSET);
}

/* set the active ep */
static void musb_set_active_ep(uint8_t ep_index)
{
    HWREGB(USB_BASE + MUSB_EPIDX_OFFSET) = ep_index;
}

static void musb_write_packet(uint8_t ep_idx, uint8_t *buffer, uint16_t len)
{
    uint32_t *buf32;
    uint8_t *buf8;
    uint32_t count32;
    uint32_t count8;
    int i;

#ifdef CONFIG_USB_MUSB_JL82
    /* JL HUSB FIFO 口只支持字节访问(闭源 usb_write_ep0 为逐字节 store) */
    {
        buf8 = buffer;
        for (i = 0; i < len; i++) {
            HWREGB(USB_FIFO_BASE(ep_idx)) = *buf8++;
        }
    }
#else
    if ((uint32_t)buffer & 0x03) {
        buf8 = buffer;
        for (i = 0; i < len; i++) {
            HWREGB(USB_FIFO_BASE(ep_idx)) = *buf8++;
        }
    } else {
        count32 = len >> 2;
        count8 = len & 0x03;

        buf32 = (uint32_t *)buffer;

        while (count32--) {
            HWREG(USB_FIFO_BASE(ep_idx)) = *buf32++;
        }

        buf8 = (uint8_t *)buf32;

        while (count8--) {
            HWREGB(USB_FIFO_BASE(ep_idx)) = *buf8++;
        }
    }
#endif
}

static void musb_read_packet(uint8_t ep_idx, uint8_t *buffer, uint16_t len)
{
    uint32_t *buf32;
    uint8_t *buf8;
    uint32_t count32;
    uint32_t count8;
    int i;

#ifdef CONFIG_USB_MUSB_JL82
    /* 闭源 usb_read_ep0: EP0 FIFO 读须在 INT_MODE=0 窗口内; bulk EP 无需 */
    if (ep_idx == 0) {
        jl_usb_int_mode(0);
    }
#endif

#ifdef CONFIG_USB_MUSB_JL82
    /* JL HUSB FIFO 口只支持字节访问(闭源 usb_read_ep0 为逐字节 load);
     * 32 位宽读每字仅低字节有效, 会把 setup 包读成 80 00 00 00 ... */
    {
        buf8 = buffer;
        for (i = 0; i < len; i++) {
            *buf8++ = HWREGB(USB_FIFO_BASE(ep_idx));
        }
    }
#else
    if ((uint32_t)buffer & 0x03) {
        buf8 = buffer;
        for (i = 0; i < len; i++) {
            *buf8++ = HWREGB(USB_FIFO_BASE(ep_idx));
        }
    } else {
        count32 = len >> 2;
        count8 = len & 0x03;

        buf32 = (uint32_t *)buffer;

        while (count32--) {
            *buf32++ = HWREG(USB_FIFO_BASE(ep_idx));
        }

        buf8 = (uint8_t *)buf32;

        while (count8--) {
            *buf8++ = HWREGB(USB_FIFO_BASE(ep_idx));
        }
    }
#endif

#ifdef CONFIG_USB_MUSB_JL82
    if (ep_idx == 0) {
        jl_usb_int_mode(1);
    }
#endif
}

static uint32_t musb_get_fifo_size(uint16_t mps, uint16_t *used)
{
    uint32_t size;

    for (uint8_t i = USB_TXFIFOSZ_SIZE_8; i <= USB_TXFIFOSZ_SIZE_2048; i++) {
        size = (8 << i);
        if (mps <= size) {
            *used = size;
            return i;
        }
    }

    *used = 0;
    return USB_TXFIFOSZ_SIZE_8;
}

static uint32_t usbd_musb_fifo_config(struct musb_fifo_cfg *cfg, uint32_t offset)
{
    uint16_t fifo_used;
    uint8_t c_size;
    uint16_t c_off;

    c_off = offset >> 3;
    c_size = musb_get_fifo_size(cfg->maxpacket, &fifo_used);

    musb_set_active_ep(cfg->ep_num);

    switch (cfg->style) {
        case FIFO_TX:
            HWREGB(USB_BASE + MUSB_TXFIFOSZ_OFFSET) = c_size & 0x0f;
            HWREGH(USB_BASE + MUSB_TXFIFOADD_OFFSET) = c_off;
            break;
        case FIFO_RX:
            HWREGB(USB_BASE + MUSB_RXFIFOSZ_OFFSET) = c_size & 0x0f;
            HWREGH(USB_BASE + MUSB_RXFIFOADD_OFFSET) = c_off;
            break;
        case FIFO_TXRX:
            HWREGB(USB_BASE + MUSB_TXFIFOSZ_OFFSET) = c_size & 0x0f;
            HWREGH(USB_BASE + MUSB_TXFIFOADD_OFFSET) = c_off;
            HWREGB(USB_BASE + MUSB_RXFIFOSZ_OFFSET) = c_size & 0x0f;
            HWREGH(USB_BASE + MUSB_RXFIFOADD_OFFSET) = c_off;
            break;

        default:
            break;
    }

    return (offset + fifo_used);
}

extern void usb_dc_low_level_init(uint8_t busid);
extern void usb_dc_low_level_deinit(uint8_t busid);

int usb_dc_init(uint8_t busid)
{
    uint16_t offset = 0;
    uint8_t cfg_num;
    struct musb_fifo_cfg *cfg;

    usb_dc_low_level_init(busid);

#ifdef CONFIG_USB_HS
    HWREGB(USB_BASE + MUSB_POWER_OFFSET) |= USB_POWER_HSENAB;
#else
    HWREGB(USB_BASE + MUSB_POWER_OFFSET) &= ~USB_POWER_HSENAB;
#endif

    musb_set_active_ep(0);
    HWREGB(USB_BASE + MUSB_FADDR_OFFSET) = 0;

#ifndef CONFIG_USB_MUSB_JL82
    HWREGB(USB_BASE + MUSB_DEVCTL_OFFSET) |= USB_DEVCTL_SESSION;

    cfg_num = usbd_get_musb_fifo_cfg(&cfg);

    for (uint8_t i = 0; i < cfg_num; i++) {
        offset = usbd_musb_fifo_config(&cfg[i], offset);
    }

    USB_ASSERT_MSG(offset <= usb_get_musb_ram_size(), "Your fifo config is overflow, please check");
#else
    /* JL HUSB: 端点 FIFO 硬件固定（EP0=64, bulk EP=512 双缓冲），无需配置 */
#endif

    /* Enable USB interrupts */
#ifdef CONFIG_USB_MUSB_JL82
    /* 诊断数据: 总线空闲时 SUSPEND 会持续反复置位(清了立刻再挂起), 引发中断风暴。
     * CDC 场景无需 suspend/resume 处理, 只使能 RESET。 */
    HWREGB(USB_BASE + MUSB_IE_OFFSET) = USB_IE_RESET;
    {   /* 窗口读清残留中断状态 */
        uint8_t d_is;
        uint16_t d_tx, d_rx;
        jl_usb_read_intr(&d_is, &d_tx, &d_rx);
    }
#else
    HWREGB(USB_BASE + MUSB_IE_OFFSET) = USB_IE_RESET | USB_IE_SUSPND | USB_IE_RESUME;
#endif
    HWREGH(USB_BASE + MUSB_TXIE_OFFSET) = USB_TXIE_EP0;
    HWREGH(USB_BASE + MUSB_RXIE_OFFSET) = 0;

#ifdef CONFIG_USBDEV_SOF_ENABLE
    HWREGB(USB_BASE + MUSB_IE_OFFSET) |= USB_IE_SOF;
#endif

    HWREGB(USB_BASE + MUSB_POWER_OFFSET) |= USB_POWER_SOFTCONN;
    return 0;
}

int usb_dc_deinit(uint8_t busid)
{
    HWREGB(USB_BASE + MUSB_IE_OFFSET) = 0;
    HWREGH(USB_BASE + MUSB_TXIE_OFFSET) = 0;
    HWREGH(USB_BASE + MUSB_RXIE_OFFSET) = 0;

    HWREGB(USB_BASE + MUSB_POWER_OFFSET) &= ~USB_POWER_SOFTCONN;

    usb_dc_low_level_deinit(busid);
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t addr)
{
    if (addr == 0) {
        HWREGB(USB_BASE + MUSB_FADDR_OFFSET) = 0;
    }

    g_musb_udc.dev_addr = addr;
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid)
{
    HWREGB(USB_BASE + MUSB_POWER_OFFSET) |= USB_POWER_RESUME;
    usbd_musb_delay_ms(10);
    HWREGB(USB_BASE + MUSB_POWER_OFFSET) &= ~USB_POWER_RESUME;
    return 0;
}

uint8_t usbd_get_port_speed(uint8_t busid)
{
    uint8_t speed = USB_SPEED_UNKNOWN;

    if (HWREGB(USB_BASE + MUSB_POWER_OFFSET) & USB_POWER_HSMODE)
        speed = USB_SPEED_HIGH;
    else if (HWREGB(USB_BASE + MUSB_DEVCTL_OFFSET) & USB_DEVCTL_FSDEV)
        speed = USB_SPEED_FULL;
    else if (HWREGB(USB_BASE + MUSB_DEVCTL_OFFSET) & USB_DEVCTL_LSDEV)
        speed = USB_SPEED_LOW;

    return speed;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep->bEndpointAddress);
    uint8_t old_ep_idx;
    uint32_t ui32Flags = 0;
    uint16_t ui32Register = 0;

    if (ep_idx == 0) {
        g_musb_udc.out_ep[0].ep_mps = USB_CTRL_EP_MPS;
        g_musb_udc.out_ep[0].ep_type = 0x00;
        g_musb_udc.out_ep[0].ep_enable = true;
        g_musb_udc.in_ep[0].ep_mps = USB_CTRL_EP_MPS;
        g_musb_udc.in_ep[0].ep_type = 0x00;
        g_musb_udc.in_ep[0].ep_enable = true;
        return 0;
    }

    USB_ASSERT_MSG(ep_idx < CONFIG_USB_MUSB_EP_NUM, "Ep addr %02x overflow", ep->bEndpointAddress);

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    if (USB_EP_DIR_IS_OUT(ep->bEndpointAddress)) {
        g_musb_udc.out_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_musb_udc.out_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_musb_udc.out_ep[ep_idx].ep_enable = true;

#if !defined(CONFIG_USB_MUSB_SIFLI) && !defined(CONFIG_USB_MUSB_JL82)
        /* JL: 无 FIFOSZ 寄存器, EP FIFO 容量硬件固定, 跳过检查 */
        USB_ASSERT_MSG((8 << HWREGB(USB_BASE + MUSB_RXFIFOSZ_OFFSET)) >= g_musb_udc.out_ep[ep_idx].ep_mps,
                       "Ep %02x fifo is overflow", ep->bEndpointAddress);
#endif

        HWREGH(USB_RXMAP_BASE(ep_idx)) = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);

        //
        // Allow auto clearing of RxPktRdy when packet of size max packet
        // has been unloaded from the FIFO.
        //
        if (ui32Flags & USB_EP_AUTO_CLEAR) {
            ui32Register = USB_RXCSRH1_AUTOCL;
        }
        //
        // Configure the DMA mode.
        //
        if (ui32Flags & USB_EP_DMA_MODE_1) {
            ui32Register |= USB_RXCSRH1_DMAEN | USB_RXCSRH1_DMAMOD;
        } else if (ui32Flags & USB_EP_DMA_MODE_0) {
            ui32Register |= USB_RXCSRH1_DMAEN;
        }
        //
        // If requested, disable NYET responses for high-speed bulk and
        // interrupt endpoints.
        //
        if (ui32Flags & USB_EP_DIS_NYET) {
            ui32Register |= USB_RXCSRH1_DISNYET;
        }

        //
        // Enable isochronous mode if requested.
        //
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) == 0x01) {
            ui32Register |= USB_RXCSRH1_ISO;
        }

        HWREGB(USB_RXCSRH_BASE(ep_idx)) = ui32Register;

        // Reset the Data toggle to zero.
        if (HWREGB(USB_RXCSRL_BASE(ep_idx)) & USB_RXCSRL1_RXRDY)
            HWREGB(USB_RXCSRL_BASE(ep_idx)) = (USB_RXCSRL1_CLRDT | USB_RXCSRL1_FLUSH);
        else
            HWREGB(USB_RXCSRL_BASE(ep_idx)) = USB_RXCSRL1_CLRDT;

        HWREGB(USB_TXCSRH_BASE(ep_idx)) &= ~USB_TXCSRH1_MODE;
    } else {
        g_musb_udc.in_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_musb_udc.in_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_musb_udc.in_ep[ep_idx].ep_enable = true;

#if !defined(CONFIG_USB_MUSB_SIFLI) && !defined(CONFIG_USB_MUSB_JL82)
        USB_ASSERT_MSG((8 << HWREGB(USB_BASE + MUSB_TXFIFOSZ_OFFSET)) >= g_musb_udc.in_ep[ep_idx].ep_mps,
                       "Ep %02x fifo is overflow", ep->bEndpointAddress);
#endif

        HWREGH(USB_TXMAP_BASE(ep_idx)) = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);

        //
        // Allow auto setting of TxPktRdy when max packet size has been loaded
        // into the FIFO.
        //
        if (ui32Flags & USB_EP_AUTO_SET) {
            ui32Register |= USB_TXCSRH1_AUTOSET;
        }

        //
        // Configure the DMA mode.
        //
        if (ui32Flags & USB_EP_DMA_MODE_1) {
            ui32Register |= USB_TXCSRH1_DMAEN | USB_TXCSRH1_DMAMOD;
        } else if (ui32Flags & USB_EP_DMA_MODE_0) {
            ui32Register |= USB_TXCSRH1_DMAEN;
        }

        //
        // Enable isochronous mode if requested.
        //
        if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) == 0x01) {
            ui32Register |= USB_TXCSRH1_ISO;
        }

        HWREGB(USB_TXCSRH_BASE(ep_idx)) = ui32Register | USB_TXCSRH1_MODE;

        // Reset the Data toggle to zero.
        if (HWREGB(USB_TXCSRL_BASE(ep_idx)) & USB_TXCSRL1_TXRDY)
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = (USB_TXCSRL1_CLRDT | USB_TXCSRL1_FLUSH);
        else
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_TXCSRL1_CLRDT;
    }

    musb_set_active_ep(old_ep_idx);

    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t ep)
{
    return 0;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint8_t old_ep_idx;

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0x00) {
            usb_ep0_state = USB_EP0_STATE_STALL;
            HWREGB(USB_TXCSRL_BASE(ep_idx)) |= (USB_CSRL0_STALL | USB_CSRL0_RXRDYC);
        } else {
            HWREGB(USB_RXCSRL_BASE(ep_idx)) |= USB_RXCSRL1_STALL;
        }
    } else {
        if (ep_idx == 0x00) {
            usb_ep0_state = USB_EP0_STATE_STALL;
            HWREGB(USB_TXCSRL_BASE(ep_idx)) |= (USB_CSRL0_STALL | USB_CSRL0_RXRDYC);
        } else {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) |= USB_TXCSRL1_STALL;
        }
    }

    musb_set_active_ep(old_ep_idx);
    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint8_t old_ep_idx;

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0x00) {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) &= ~USB_CSRL0_STALLED;
        } else {
            // Clear the stall on an OUT endpoint.
            HWREGB(USB_RXCSRL_BASE(ep_idx)) &= ~(USB_RXCSRL1_STALL | USB_RXCSRL1_STALLED);
            // Reset the data toggle.
            HWREGB(USB_RXCSRL_BASE(ep_idx)) |= USB_RXCSRL1_CLRDT;
        }
    } else {
        if (ep_idx == 0x00) {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) &= ~USB_CSRL0_STALLED;
        } else {
            // Clear the stall on an IN endpoint.
            HWREGB(USB_TXCSRL_BASE(ep_idx)) &= ~(USB_TXCSRL1_STALL | USB_TXCSRL1_STALLED);
            // Reset the data toggle.
            HWREGB(USB_TXCSRL_BASE(ep_idx)) |= USB_TXCSRL1_CLRDT;
        }
    }

    musb_set_active_ep(old_ep_idx);
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t ep, uint8_t *stalled)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint8_t old_ep_idx;

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    if (USB_EP_DIR_IS_OUT(ep)) {
        if (HWREGB(USB_RXCSRL_BASE(ep_idx)) & USB_RXCSRL1_STALL) {
            *stalled = 1;
        } else {
            *stalled = 0;
        }
    } else {
        if (HWREGB(USB_TXCSRL_BASE(ep_idx)) & USB_TXCSRL1_STALL) {
            *stalled = 1;
        } else {
            *stalled = 0;
        }
    }
    musb_set_active_ep(old_ep_idx);
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint8_t old_ep_idx;

    if (!data && data_len) {
        return -1;
    }
    if (!g_musb_udc.in_ep[ep_idx].ep_enable) {
        return -2;
    }

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    if (HWREGB(USB_TXCSRL_BASE(ep_idx)) & USB_TXCSRL1_TXRDY) {
        musb_set_active_ep(old_ep_idx);
        return -3;
    }

    g_musb_udc.in_ep[ep_idx].xfer_buf = (uint8_t *)data;
    g_musb_udc.in_ep[ep_idx].xfer_len = data_len;
    g_musb_udc.in_ep[ep_idx].actual_xfer_len = 0;

    if (data_len == 0) {
        if (ep_idx == 0x00) {
            if (g_musb_udc.setup.wLength == 0) {
                usb_ep0_state = USB_EP0_STATE_IN_STATUS;
            } else {
                usb_ep0_state = USB_EP0_STATE_IN_ZLP;
            }
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = (USB_CSRL0_TXRDY | USB_CSRL0_DATAEND);
        } else {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_TXCSRL1_TXRDY;
            HWREGH(USB_BASE + MUSB_TXIE_OFFSET) |= (1 << ep_idx);
        }
        musb_set_active_ep(old_ep_idx);
        return 0;
    }
    data_len = MIN(data_len, g_musb_udc.in_ep[ep_idx].ep_mps);

    musb_write_packet(ep_idx, (uint8_t *)data, data_len);
    HWREGH(USB_BASE + MUSB_TXIE_OFFSET) |= (1 << ep_idx);

    if (ep_idx == 0x00) {
        usb_ep0_state = USB_EP0_STATE_IN_DATA;
        if (data_len < g_musb_udc.in_ep[ep_idx].ep_mps) {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = (USB_CSRL0_TXRDY | USB_CSRL0_DATAEND);
        } else {
            HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_CSRL0_TXRDY;
        }
    } else {
        HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_TXCSRL1_TXRDY;
    }

    musb_set_active_ep(old_ep_idx);
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint8_t old_ep_idx;

    if (!data && data_len) {
        return -1;
    }
    if (!g_musb_udc.out_ep[ep_idx].ep_enable) {
        return -2;
    }

    old_ep_idx = musb_get_active_ep();
    musb_set_active_ep(ep_idx);

    g_musb_udc.out_ep[ep_idx].xfer_buf = data;
    g_musb_udc.out_ep[ep_idx].xfer_len = data_len;
    g_musb_udc.out_ep[ep_idx].actual_xfer_len = 0;

    if (data_len == 0) {
        if (ep_idx == 0) {
            usb_ep0_state = USB_EP0_STATE_SETUP;
        }
        musb_set_active_ep(old_ep_idx);
        return 0;
    }
    if (ep_idx == 0) {
        usb_ep0_state = USB_EP0_STATE_OUT_DATA;
    } else {
        HWREGH(USB_BASE + MUSB_RXIE_OFFSET) |= (1 << ep_idx);
    }
    musb_set_active_ep(old_ep_idx);
    return 0;
}

static void handle_ep0(void)
{
    uint8_t ep_idx = 0; // EP0 index is always 0
    uint8_t ep0_status = HWREGB(USB_TXCSRL_BASE(ep_idx));
    uint16_t read_count;

    if (ep0_status & USB_CSRL0_STALLED) {
        HWREGB(USB_TXCSRL_BASE(ep_idx)) &= ~USB_CSRL0_STALLED;
        usb_ep0_state = USB_EP0_STATE_SETUP;
        return;
    }

    if (ep0_status & USB_CSRL0_SETEND) {
        HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_CSRL0_SETENDC;
        printf("[ep0] SETEND, state=%d\n", usb_ep0_state);
    }

    if (g_musb_udc.dev_addr > 0) {
        HWREGB(USB_BASE + MUSB_FADDR_OFFSET) = g_musb_udc.dev_addr;
        g_musb_udc.dev_addr = 0;
    }

    switch (usb_ep0_state) {
        case USB_EP0_STATE_SETUP:
            if (ep0_status & USB_CSRL0_RXRDY) {
                read_count = HWREGH(USB_RXCOUNT_BASE(ep_idx));

                if (read_count != 8) {
                    return;
                }

                musb_read_packet(0, (uint8_t *)&g_musb_udc.setup, 8);
                {
                    static uint8_t setup_cnt = 0;
                    if (setup_cnt++ < 10) {
                        printf("[ep0] setup#%u type=%02x req=%02x val=%04x len=%d\n",
                               setup_cnt,
                               g_musb_udc.setup.bmRequestType, g_musb_udc.setup.bRequest,
                               g_musb_udc.setup.wValue, g_musb_udc.setup.wLength);
                    }
                }
                if (g_musb_udc.setup.wLength) {
                    HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_CSRL0_RXRDYC;
                } else {
                    HWREGB(USB_TXCSRL_BASE(ep_idx)) = (USB_CSRL0_RXRDYC | USB_CSRL0_DATAEND);
                }

                usbd_event_ep0_setup_complete_handler(0, (uint8_t *)&g_musb_udc.setup);
            }
            break;

        case USB_EP0_STATE_IN_DATA:
            if (g_musb_udc.in_ep[0].xfer_len > g_musb_udc.in_ep[0].ep_mps) {
                g_musb_udc.in_ep[0].actual_xfer_len += g_musb_udc.in_ep[0].ep_mps;
                g_musb_udc.in_ep[0].xfer_len -= g_musb_udc.in_ep[0].ep_mps;
            } else {
                g_musb_udc.in_ep[0].actual_xfer_len += g_musb_udc.in_ep[0].xfer_len;
                g_musb_udc.in_ep[0].xfer_len = 0;
            }

            usbd_event_ep_in_complete_handler(0, 0x80, g_musb_udc.in_ep[0].actual_xfer_len);

            break;
        case USB_EP0_STATE_OUT_DATA:
            if (ep0_status & USB_CSRL0_RXRDY) {
                read_count = HWREGH(USB_RXCOUNT_BASE(ep_idx));

                musb_read_packet(0, g_musb_udc.out_ep[0].xfer_buf, read_count);
                g_musb_udc.out_ep[0].xfer_buf += read_count;
                g_musb_udc.out_ep[0].actual_xfer_len += read_count;

                if (read_count < g_musb_udc.out_ep[0].ep_mps) {
                    usbd_event_ep_out_complete_handler(0, 0x00, g_musb_udc.out_ep[0].actual_xfer_len);
                    HWREGB(USB_TXCSRL_BASE(ep_idx)) = (USB_CSRL0_RXRDYC | USB_CSRL0_DATAEND);
                    usb_ep0_state = USB_EP0_STATE_IN_STATUS;
                } else {
                    HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_CSRL0_RXRDYC;
                }
            }
            break;
        case USB_EP0_STATE_IN_STATUS:
        case USB_EP0_STATE_IN_ZLP:
            usb_ep0_state = USB_EP0_STATE_SETUP;
            usbd_event_ep_in_complete_handler(0, 0x80, 0);
            break;
    }
}

void USBD_IRQHandler(uint8_t busid)
{
    uint32_t is;
    uint32_t txis;
    uint32_t rxis;
    uint8_t old_ep_idx;
    uint8_t ep_idx;
    uint16_t write_count, read_count;

    if (HWREGB(USB_BASE + MUSB_DEVCTL_OFFSET) & USB_DEVCTL_HOST) {
        return;
    }

#ifdef CONFIG_USB_MUSB_JL82
    /* INT_MODE=0 窗口内读取, 读即清除(闭源 usb_read_intr 还原); 无需任何写清除 */
    {
        uint8_t  _is;
        uint16_t _tx, _rx;
        jl_usb_read_intr(&_is, &_tx, &_rx);
        is = _is;
        txis = _tx;
        rxis = _rx;
    }
#else
    is = HWREGB(USB_BASE + MUSB_IS_OFFSET);
    txis = HWREGH(USB_BASE + MUSB_TXIS_OFFSET);
    rxis = HWREGH(USB_BASE + MUSB_RXIS_OFFSET);
    HWREGB(USB_BASE + MUSB_IS_OFFSET) = is; /* 标准 MUSB: 写1清除 */
#endif

    old_ep_idx = musb_get_active_ep();

    /* Receive a reset signal from the USB bus */
    if (is & USB_IS_RESET) {
        memset(&g_musb_udc, 0, sizeof(struct musb_udc));
        usbd_event_reset_handler(0);
        HWREGH(USB_BASE + MUSB_TXIE_OFFSET) = USB_TXIE_EP0;
        HWREGH(USB_BASE + MUSB_RXIE_OFFSET) = 0;

        usb_ep0_state = USB_EP0_STATE_SETUP;
    }

#ifdef CONFIG_USBDEV_SOF_ENABLE
    if (is & USB_IS_SOF) {
        usbd_event_sof_handler(0);
    }
#endif

    if (is & USB_IS_RESUME) {
        usbd_event_resume_handler(0);
    }

    if (is & USB_IS_SUSPEND) {
        usbd_event_suspend_handler(0);
    }

    txis &= HWREGH(USB_BASE + MUSB_TXIE_OFFSET);
    /* Handle EP0 interrupt */
    if (txis & USB_TXIE_EP0) {
#ifndef CONFIG_USB_MUSB_JL82
        HWREGH(USB_BASE + MUSB_TXIS_OFFSET) = USB_TXIE_EP0; /* W1C; JL: 窗口读已清 */
#endif
        musb_set_active_ep(0);
        handle_ep0();
        txis &= ~USB_TXIE_EP0;
    }

    ep_idx = 1;
    while (txis) {
        if (txis & (1 << ep_idx)) {
            musb_set_active_ep(ep_idx);
#ifndef CONFIG_USB_MUSB_JL82
            HWREGH(USB_BASE + MUSB_TXIS_OFFSET) = (1 << ep_idx); /* W1C; JL: 窗口读已清 */
#endif
            if (HWREGB(USB_TXCSRL_BASE(ep_idx)) & USB_TXCSRL1_UNDRN) {
                HWREGB(USB_TXCSRL_BASE(ep_idx)) &= ~USB_TXCSRL1_UNDRN;
            }

            if (g_musb_udc.in_ep[ep_idx].xfer_len > g_musb_udc.in_ep[ep_idx].ep_mps) {
                g_musb_udc.in_ep[ep_idx].xfer_buf += g_musb_udc.in_ep[ep_idx].ep_mps;
                g_musb_udc.in_ep[ep_idx].actual_xfer_len += g_musb_udc.in_ep[ep_idx].ep_mps;
                g_musb_udc.in_ep[ep_idx].xfer_len -= g_musb_udc.in_ep[ep_idx].ep_mps;
            } else {
                g_musb_udc.in_ep[ep_idx].xfer_buf += g_musb_udc.in_ep[ep_idx].xfer_len;
                g_musb_udc.in_ep[ep_idx].actual_xfer_len += g_musb_udc.in_ep[ep_idx].xfer_len;
                g_musb_udc.in_ep[ep_idx].xfer_len = 0;
            }

            if (g_musb_udc.in_ep[ep_idx].xfer_len == 0) {
                HWREGH(USB_BASE + MUSB_TXIE_OFFSET) &= ~(1 << ep_idx);
                usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, g_musb_udc.in_ep[ep_idx].actual_xfer_len);
            } else {
                write_count = MIN(g_musb_udc.in_ep[ep_idx].xfer_len, g_musb_udc.in_ep[ep_idx].ep_mps);

                musb_write_packet(ep_idx, g_musb_udc.in_ep[ep_idx].xfer_buf, write_count);
                HWREGB(USB_TXCSRL_BASE(ep_idx)) = USB_TXCSRL1_TXRDY;
            }

            txis &= ~(1 << ep_idx);
        }
        ep_idx++;
    }

    rxis &= HWREGH(USB_BASE + MUSB_RXIE_OFFSET);
    ep_idx = 1;
    while (rxis) {
        if (rxis & (1 << ep_idx)) {
            musb_set_active_ep(ep_idx);
#ifndef CONFIG_USB_MUSB_JL82
            HWREGH(USB_BASE + MUSB_RXIS_OFFSET) = (1 << ep_idx); /* W1C; JL: 窗口读已清 */
#endif
            if (HWREGB(USB_RXCSRL_BASE(ep_idx)) & USB_RXCSRL1_RXRDY) {
                read_count = HWREGH(USB_RXCOUNT_BASE(ep_idx));

                musb_read_packet(ep_idx, g_musb_udc.out_ep[ep_idx].xfer_buf, read_count);
                HWREGB(USB_RXCSRL_BASE(ep_idx)) &= ~(USB_RXCSRL1_RXRDY);

                g_musb_udc.out_ep[ep_idx].xfer_buf += read_count;
                g_musb_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                g_musb_udc.out_ep[ep_idx].xfer_len -= read_count;

                if ((read_count < g_musb_udc.out_ep[ep_idx].ep_mps) || (g_musb_udc.out_ep[ep_idx].xfer_len == 0)) {
                    HWREGH(USB_BASE + MUSB_RXIE_OFFSET) &= ~(1 << ep_idx);
                    usbd_event_ep_out_complete_handler(0, ep_idx, g_musb_udc.out_ep[ep_idx].actual_xfer_len);
                } else {
                }
            }

            rxis &= ~(1 << ep_idx);
        }
        ep_idx++;
    }

    musb_set_active_ep(old_ep_idx);

}
