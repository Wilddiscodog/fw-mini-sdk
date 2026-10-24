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
#define MINI_USB_MODE          MINI_USB_MODE_DEVICE

/* ================ 固件升级: MSC 虚拟U盘自动升级(双备份) =================
 * 用法: 把构建产出的 update-ota.ufw 拖入设备虚拟U盘 → 文件静默 3s 后
 *       自动校验 CRC → 写备用 bank → 烧 boot info → 2s 后复位进新固件。
 * 前提(重要):
 *   - 双备份改变 flash 布局(app 区双份), 切换后必须整体烧录一次 jl_isd.ufw,
 *     之后的版本才能走虚拟U盘升级
 *   - 升级文件由 isd_download 产出: cpu/wl82/tools/db_update_data.bin,
 *     download.bat 自动改名复制为 update-ota.ufw
 * 实现: driver/update/update.c + net_update.c(原厂 net_update.c 移植) +
 *       liba/update.a(双备份烧写) + cdc_acm_msc_app.c(检测/触发) */
#define CONFIG_DOUBLE_BANK_ENABLE  1   /* 0=单备份(不支持虚拟U盘升级) */
#define TCFG_VIR_UPDATE_ENABLE     CONFIG_DOUBLE_BANK_ENABLE

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
/* 日志镜像到 USB CDC 口(组合设备的串口, 打开串口即实时输出; UART0 仍保留)。
 * ⚠ USB 枚举异常排查期(V0.9.x)暂时关闭: 关闭后 putbyte 不再被覆盖,
 * 日志完全走原厂 lib→UART0 路径。重开需同时 MINI_USB_CDC_ENABLE=1。 */
/* #define CONFIG_USB_DEBUG_ENABLE */

/* USB 设备形态开关: 1=CDC(日志)+MSC 组合设备; 0=MSC-only(EP1, V0.8 验证配置)。
 * ⚠ 同上排查期暂时 0。CDC 代码在 cdc_acm_msc_app.c 内保留, 宏开即回。 */
#define MINI_USB_CDC_ENABLE    0

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

/* ================ xiaolu-R2 手表屏/触摸(JD9853 8bit并口 + CST816 IIC) =================
 * 触摸引脚: PH0/PH1(iic0), PH3=TP_INT, PH4=TP_RST; 屏走 EMI 8080-8bit(board.c) */
#define TCFG_LCD_JD9853_ENABLE      1
#define TCFG_TOUCH_CST816_ENABLE    1

/* ================ ADC 按键(原厂 key_driver/adkey 框架, PB01 电阻梯) ================
 * 实测: 空闲 raw=1014(3.13V), 按键1 raw=162(500mV), 按键2 raw=0(0mV)。
 * 表用中点阈值: ≤0 → 按键2; 81<raw≤588 → 按键1; >588 → 无键 */
#define TCFG_IOKEY_ENABLE           0
#define TCFG_ADKEY_ENABLE           1
#define TCFG_IRKEY_ENABLE           0
#define TCFG_TOUCH_KEY_ENABLE       0
#define TCFG_RDEC_KEY_ENABLE        0
#define TCFG_ADKEY_RTCVDD_ENABLE    0
#define TCFG_KEY_TONE_EN            0
#define TCFG_IRSENSOR_ENABLE        0
#define TCFG_SPI_LCD_ENABLE         0
#define TCFG_ADKEY_LED_IO_REUSE     0
#define TCFG_ADKEY_IR_IO_REUSE      0
#define TCFG_ADKEY_LED_SPI_IO_REUSE 0

/* ================ 蓝牙/WiFi 外设(原厂栈移植) ================ */
#define MINI_BT_SUPPORT             1   /* 0=关闭; 1=链接 btctrler/btstack 并开机自启 */
#define MINI_BT_BLE_TRANS_EN        1   /* BLE 透传服务(广播名 WL82-BLE, 透传特征 ae82 notify) */
#define CONFIG_BT_ENABLE            1   
#define MINI_WIFI_SUPPORT           1   /* WiFi STA: 扫描演示(wifi_on -> scan -> 打印 AP) */

#if MINI_BT_SUPPORT
#ifdef CONFIG_BT_ENABLE
#define CONFIG_BT_RX_BUFF_SIZE  (12 * 1024)
#define CONFIG_BT_TX_BUFF_SIZE  (6 * 1024)
#define TCFG_USER_BLE_ENABLE        1       //BLE功能使能
#define TCFG_USER_BT_CLASSIC_ENABLE 0       //经典蓝牙功能
#define BT_NET_HID_EN               0
#define TRANS_DATA_EN               1
#define BT_NET_CENTRAL_EN           0
#define TRANS_MULTI_BLE_EN          0
#define CONFIG_BLE_MESH_ENABLE      0
#define TCFG_BLE_SECURITY_EN        1
#endif
#endif

#endif
