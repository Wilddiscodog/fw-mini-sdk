/*
 * CherryUSB 配置 —— JL WL82 (AC791x) HUSB 高速从机
 * 经编译参数 -include 注入所有 CherryUSB 源文件。
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common ================ */
#define CONFIG_USB_PRINTF(...)          printf(__VA_ARGS__)
#define CONFIG_USB_PRINTF_COLOR_ENABLE  1
#define CONFIG_USB_DBG_LEVEL            2  /* 0=ERR 1=WARN 2=INFO 3=DBG */
#define CONFIG_USB_ALIGN_SIZE           4
/* JL sdk.ld 无 .noncacheable 段, 该属性置空 */
#define USB_NOCACHE_RAM_SECTION

/* ================ USB Device Stack ================ */
#define CONFIG_USBDEV_MAX_BUS           1
#define CONFIG_USBDEV_EP_NUM            5   /* WL82: EP0~EP4 */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#define CONFIG_USBDEV_ADVANCE_DESC      1   /* 使用描述符回调接口 */
/* 无 MSC/MTP/EP0_THREAD: 不引用任何 osal 符号, 免移植 OSAL */

/* ================ MUSB DCD (port/jl82) ================ */
#define CONFIG_USB_MUSB_JL82            1   /* JL 变体: 读清中断/无FIFO配置寄存器/桥接器同步 */
#define CONFIG_USB_MUSB_EP_NUM          5
#define CONFIG_USB_HS                   1   /* HUSB 2.0 高速 */

#endif
