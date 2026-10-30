/*
 * CherryUSB 配置 —— JL WL82 (AC791x) HUSB 高速从机
 * 经编译参数 -include 注入所有 CherryUSB 源文件。
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common ================ */
#define USBHOST_DEBUG_LOGS_ENABLE       0   /* 1=Enable USB host debug logs, 0=benchmark mode */
#define CONFIG_USB_PRINTF(...)          printf(__VA_ARGS__)
#define CONFIG_USB_PRINTF_COLOR_ENABLE  1
#if USBHOST_DEBUG_LOGS_ENABLE
#define CONFIG_USB_DBG_LEVEL            2  /* 0=ERR 1=WARN 2=INFO 3=DBG */
#else
#define CONFIG_USB_DBG_LEVEL            0
#endif
#define CONFIG_USB_ALIGN_SIZE           4
/* USB 栈与控制器共用缓冲放入 SRAM0, 避免落入 SDRAM */
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".usb_h_bss")))

/* ================ USB Device Stack ================ */
#define CONFIG_USBDEV_MAX_BUS           1
#define CONFIG_USBDEV_EP_NUM            7   /* EP0 + CDC1/2 data/notify 最大到 EP6 */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#define CONFIG_USBDEV_ADVANCE_DESC      1   /* 使用描述符回调接口 */
/* MSC 设备类: 3MB SDRAM 盘, IRQ-safe queue 唤醒专用 worker */
#define CONFIG_USBDEV_MSC_MAX_LUN       1
#define CONFIG_USBDEV_MSC_MAX_BUFSIZE   4096   /* 单次处理最多 4 KiB */
#define CONFIG_USBDEV_MSC_THREAD        1
#define CONFIG_USBDEV_MSC_PRIO          4
#define CONFIG_USBDEV_MSC_STACKSIZE     2048
#define CONFIG_USBDEV_MSC_MANUFACTURER_STRING   "fw-mini-sdk"
#define CONFIG_USBDEV_MSC_PRODUCT_STRING        "WL82 3MB RAM Disk"
#define CONFIG_USBDEV_MSC_VERSION_STRING        "1.00"
/* EP0 仍在中断上下文处理; MSC SCSI/data work 由专用线程执行 */

/* ================ USB Host Stack (usbh) ================ */
#define CONFIG_USBHOST_MAX_BUS              1
#define CONFIG_USBHOST_DEV_NAMELEN          16
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 2000  /* ms */
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN   512
#define CONFIG_USBHOST_PSC_PRIO             4
#define CONFIG_USBHOST_PSC_STACKSIZE        2048
#define CONFIG_USBHOST_MAX_SERIAL_CLASS     2
#define CONFIG_USBHOST_SERIAL_BULKIN_SIZE   512
#define CONFIG_USBHOST_SERIAL_RX_SIZE       2048
#define CONFIG_USBHOST_MAX_HID_CLASS        4
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
/* 复合设备端点上限(数组维度, 必须 > 实际最大 EP 号)。
 * 双 CDC: data EP1/EP2, notify EP5/EP6 —— 不可用 EP3 bulk(驱动里绑 MSC 4KB 大缓冲)。 */
#define CONFIG_USB_MUSB_EP_NUM          7   /* EP0~EP6 */
#define CONFIG_USB_HS                   1   /* HUSB 2.0 高速 */

#endif
