#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ================= fw-mini-sdk 应用配置 =================
 * 这是全工程唯一需要按项目改动的配置文件。
 * 外设的引脚/波特率等板级配置在 app/board/wl82/board.c。 */

#define __FLASH_SIZE__    (16 * 1024 * 1024)
#define __SDRAM_SIZE__    (8 * 1024 * 1024)

//*********************************************************************************//
//                                  USB配置                                         //
//*********************************************************************************//
/* USB 协议栈已替换为 CherryUSB(third/CherryUSB)，其配置见 app/include/usb_config.h。
 * 使用高速口 USB1(HUSB 2.0)；USB0(FUSB 1.1) 为全速。两控制器共用同一组 D+/D-，勿同时启动。
 * 旧杰理从机栈源码保留在 driver/usb 备查，不参与编译。 */

//*********************************************************************************//
//                                  电源配置                                        //
//*********************************************************************************//
#define TCFG_LOWPOWER_BTOSC_DISABLE             0
#define TCFG_LOWPOWER_LOWPOWER_SEL              0
#define TCFG_LOWPOWER_VDDIOM_LEVEL              VDDIOM_VOL_32V
#ifdef CONFIG_RTC_ENABLE
#define TCFG_LOWPOWER_VDDIOW_LEVEL              VDDIOW_VOL_32V
#else
#define TCFG_LOWPOWER_VDDIOW_LEVEL              VDDIOW_VOL_21V
#endif

#define VDC14_VOL_SEL_LEVEL                     VDC14_VOL_SEL_140V
#define SYSVDD_VOL_SEL_LEVEL                    SYSVDD_VOL_SEL_126V

#define CONFIG_DEBUG_ENABLE

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

#endif
