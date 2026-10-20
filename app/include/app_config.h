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
//*********************************************************************************//
//                                  LCD 显示配置                                    //
//*********************************************************************************//
/* MINI_LCD_SUPPORT / MINI_LVGL_SUPPORT 开关在 Makefile(经 -D 注入此处):
 * MINI_LCD_SUPPORT=1  EMI 8080-8bit 并口 LCD(ili9341 参考驱动)
 * MINI_LVGL_SUPPORT=1 LVGL 8.2.0(需先按 README 获取 lvgl 源码到 third/lvgl/lvgl) */
#if MINI_LCD_SUPPORT
#define CONFIG_UI_ENABLE
/* 面板选择（lcd_config.h 依此决定 LCD_W/H），与官方 demo_ui 同名宏 */
#define TCFG_LCD_ILI9341_ENABLE         1
#define TCFG_LCD_ST7789S_ENABLE         0
#define TCFG_LCD_ST7789V_ENABLE         0
#define TCFG_LCD_ST7735S_ENABLE         0
#define TCFG_LCD_480x272_8BITS          0
#define HORIZONTAL_SCREEN               0   /* 0=竖屏 240x320 */
#endif

/* C++ 支持开关在 Makefile: make MINI_CXX_SUPPORT=1 (改后 make clean)
 * 开启时 -DCONFIG_CXX_SUPPORT 自动注入, app_main 会先调 cpp_run_init() */
/* USB 协议栈已替换为 CherryUSB(third/CherryUSB)，其配置见 app/include/usb_config.h。
 * 使用高速口 USB1(HUSB 2.0)；USB0(FUSB 1.1) 为全速。两控制器共用同一组 D+/D-，勿同时启动。
 * 旧杰理从机栈源码保留在 driver/usb 备查，不参与编译。 */
#define MINI_USB_MODE_DEVICE   0   /* CDC 从机(主机模式不行, 二选一) */
#define MINI_USB_MODE_HOST     1   /* MSC 主机(U盘) */
#define MINI_USB_MODE          MINI_USB_MODE_HOST

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
