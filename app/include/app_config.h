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
/* xiaolu-R2: 只编 lcd_jd9853.c(Makefile); 必须打开本宏, 否则 REGISTER_LCD_DEV
 * 被裁掉 → "err no lcd_dev" 后 draw_lvgl 空指针 axi_rd_inv 复位 */
#define TCFG_LCD_JD9853_ENABLE          1
#define TCFG_LCD_ILI9341_ENABLE         0
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
 *   - 升级文件由 isd_download 产出: download.bat 带 -update_files normal 时生成
 *     cpu/wl82/tools/db_update_files_data.bin(明文 JL_FILE_HEAD, name=update_data),
 *     自动改名复制为 update-ota.ufw。注意: 同目录的 db_update_data.bin 是密文头,
 *     设备端 jl_file_head_valid_check(纯 CRC16-XMODEM, 不解密)校验不过, 不能用
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
/* 日志镜像到 USB CDC: 会改写 putbyte, 与 UART0 抢路径易影响正常串口日志。
 * 暂关; 需要 CDC 日志时再打开本宏, 并建议 MINI_USB_CDC_ENABLE=1。 */
/* #define CONFIG_USB_DEBUG_ENABLE */

/* USB 设备形态: 1=CDC+MSC 组合; 0=MSC-only。暂关 CDC(与上项配套)。 */
#define MINI_USB_CDC_ENABLE    0

/* ================ 内置音频(MIC + DAC + AEC, MINI_AUDIO_SUPPORT) =================
 * Makefile: MINI_AUDIO_SUPPORT 默认 1(蓝牙音箱需要)。
 * 硬件: MIC1=PH8/PH9 拾音, MIC3=PH5/PH6 回采, PH7 高=功放使能。 */
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#define CONFIG_AUDIO_MIX_ENABLE
/* 蓝牙音箱不需变调; 开 CONFIG_AUDIO_PS_ENABLE 会拉 pitch-shift 库 */
#define CONFIG_PCM_ENC_ENABLE
#define CONFIG_SBC_DEC_ENABLE
#define CONFIG_SBC_ENC_ENABLE
#define CONFIG_MSBC_DEC_ENABLE
#define CONFIG_MSBC_ENC_ENABLE
#define CONFIG_CVSD_DEC_ENABLE
#define CONFIG_CVSD_ENC_ENABLE
#define CONFIG_AAC_DEC_ENABLE                 /* A2DP AAC 解码(较 SBC 音质更好) */
#define CONFIG_VIRTUAL_DEV_ENC_ENABLE

#define AUDIO_ENC_SAMPLE_SOURCE_MIC         0
#define AUDIO_ENC_SAMPLE_SOURCE_PLNK0       1
#define AUDIO_ENC_SAMPLE_SOURCE_PLNK1       2
#define AUDIO_ENC_SAMPLE_SOURCE_IIS0        3
#define AUDIO_ENC_SAMPLE_SOURCE_IIS1        4
#define AUDIO_ENC_SAMPLE_SOURCE_LINEIN      5

#define CONFIG_AUDIO_ENC_SAMPLE_SOURCE      AUDIO_ENC_SAMPLE_SOURCE_MIC
#define CONFIG_AUDIO_DEC_PLAY_SOURCE        "dac"
#define CONFIG_AUDIO_RECORDER_SAMPLERATE    16000
#define CONFIG_AUDIO_RECORDER_CHANNEL       1
#define CONFIG_AUDIO_RECORDER_DURATION      (30 * 1000)

#define CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE        /* 多 MIC 通道 bit_map 开关 */
#define CONFIG_AUDIO_ADC_CHANNEL_L          1     /* MIC1 拾音 */
#define CONFIG_AUDIO_ADC_CHANNEL_R          3     /* MIC3 */
#define CONFIG_AUDIO_ADC_GAIN               100
#define CONFIG_REVERB_ADC_CHANNEL           1

/* AEC: MIC3(PH5/6) 回采功放; MIC1(PH8/9) 通话 MIC */
#define CONFIG_AEC_ENC_ENABLE
#define CONFIG_DNS_ENC_ENABLE
#define CONFIG_PHONE_CALL_ADC_CHANNEL       1     /* MIC1 */
#define CONFIG_AEC_ADC_CHANNEL              3     /* MIC3 回采 */
#define CONFIG_AEC_AUDIO_ADC_GAIN           80

/* EQ(audio_config.c / EQ_CORE_V1 需要); 在线调试默认关, 先用文件/关闭 */
#define TCFG_EQ_ENABLE                      0
#define TCFG_EQ_ONLINE_ENABLE               0
#define TCFG_EQ_FILE_ENABLE                 0
#define TCFG_HW_SOFT_EQ_ENABLE              0
#define TCFG_LIMITER_ENABLE                 0
#define TCFG_DRC_ENABLE                     0
#define TCFG_AUDIO_OUT_EQ_ENABLE            0
#define TCFG_EQ_DIVIDE_ENABLE               0
#endif  /* MINI_AUDIO_SUPPORT */

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

/* ================ xiaolu-R2 触摸(CST816 IIC, PH0/1 + PH3 INT / PH4 RST) ================
 * 面板宏 TCFG_LCD_JD9853_ENABLE 已放在上方 MINI_LCD_SUPPORT 块。 */
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#define TCFG_TOUCH_CST816_ENABLE    1
#else
#define TCFG_TOUCH_CST816_ENABLE    0
#endif

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
#define MINI_BT_SPEAKER_EN          1   /* 经典蓝牙音箱: A2DP 播放 + HFP 通话 */
#define MINI_BT_BLE_TRANS_EN        1   /* BLE 透传(与音箱双模并存) */
#define CONFIG_BT_ENABLE            1
#define MINI_WIFI_SUPPORT           1

#if MINI_BT_SUPPORT
#ifdef CONFIG_BT_ENABLE
#define CONFIG_BT_RX_BUFF_SIZE  (12 * 1024)
#define CONFIG_BT_TX_BUFF_SIZE  (12 * 1024)
#define TCFG_USER_BLE_ENABLE            1
#if MINI_BT_SPEAKER_EN
/* 原厂音箱/DevKitBoard: 经典蓝牙 A2DP(+可选 AAC)+HFP; BLE 不能做手机音乐播放 */
#define TCFG_USER_BT_CLASSIC_ENABLE     1
#define CONFIG_POWER_ON_ENABLE_BT       1
#define TCFG_USER_EMITTER_ENABLE        0   /* 接收端音箱, 非发射 */
#define TCFG_BT_SNIFF_ENABLE            0
#define TCFG_BT_SUPPORT_AAC             1   /* 高音质: 对齐 demo_DevKitBoard */
#define BT_SUPPORT_MUSIC_VOL_SYNC       1
#define BT_SUPPORT_DISPLAY_BAT          0
#define SPP_TRANS_DATA_EN               0
#else
#define TCFG_USER_BT_CLASSIC_ENABLE     0
#define TCFG_BT_SUPPORT_AAC             0
#endif
#define BT_NET_HID_EN               0
#define TRANS_DATA_EN               1
#define BT_NET_CENTRAL_EN           0
#define TRANS_MULTI_BLE_EN          0
#define CONFIG_BLE_MESH_ENABLE      0
#define TCFG_BLE_SECURITY_EN        1
#endif
#endif

#ifndef TCFG_USER_EMITTER_ENABLE
#define TCFG_USER_EMITTER_ENABLE    0
#endif
#ifndef BT_SUPPORT_MUSIC_VOL_SYNC
#define BT_SUPPORT_MUSIC_VOL_SYNC   0
#endif

#endif
