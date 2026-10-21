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

/* ================ USB Host Stack (usbh) ================ */
#define CONFIG_USBHOST_MAX_BUS              1
#define CONFIG_USBHOST_DEV_NAMELEN          16
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 2000  /* ms */
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN   512
#define CONFIG_USBHOST_PSC_PRIO             4
#define CONFIG_USBHOST_PSC_STACKSIZE        2048
#define CONFIG_USBHOST_MAX_SERIAL_CLASS     2
#define CONFIG_USBHOST_MAX_HID_CLASS        2
#define CONFIG_USBHOST_MAX_MSC_CLASS        2
#define CONFIG_USBHOST_MAX_AUDIO_CLASS      1
#define CONFIG_USBHOST_MAX_VIDEO_CLASS      1
#define CONFIG_USBHOST_MSC_TIMEOUT          5000
#define CONFIG_USBHOST_MAX_RHPORTS          1
#define CONFIG_USBHOST_MAX_EXTHUBS          1
#define CONFIG_USBHOST_MAX_EHPORTS          4
#define CONFIG_USBHOST_MAX_INTERFACES       8
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 2
#define CONFIG_USBHOST_MAX_ENDPOINTS        4
#define CONFIG_USB_MUSB_PIPE_NUM            5   /* WL82: EP0~EP4 作主机通道 */
#define CONFIG_USBH_JL82_FORCE_FS           0   /* 0=HS 主机(480M); 1=FS(旧选项, FS 帧时钟未验证) */

/* ================ MUSB DCD (port/jl82) ================ */
#define CONFIG_USB_MUSB_JL82            1   /* JL 变体: 读清中断/无FIFO配置寄存器/桥接器同步 */
#define CONFIG_USB_MUSB_EP_NUM          5
#define CONFIG_USB_HS                   1   /* HUSB 2.0 高速 */

#endif
