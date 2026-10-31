#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ================= fw-mini-sdk 应用配置 =================
 * 这是全工程唯一需要按项目改动的配置文件。
 * 外设的引脚/波特率等板级配置在 app/board/wl82/board.c。 */

#define __FLASH_SIZE__    (16 * 1024 * 1024)
#define __SDRAM_SIZE__    (8 * 1024 * 1024)

/*
 * 软关机二分 (与 Makefile MINI_PWR_BISECT 一致, 改后 make clean):
 *   0 仅 ADKEY+原厂 P33    1 +LCD/LVGL    2 +USB
 *   3 +音频                4 +BT          5 +WiFi
 *   99 手动(Makefile 各开关)
 */
#ifndef MINI_PWR_BISECT
#define MINI_PWR_BISECT                 5
#endif

//*********************************************************************************//
//                                  LCD 显示配置                                    //
//*********************************************************************************//
#if MINI_LCD_SUPPORT
#define CONFIG_UI_ENABLE
#define TCFG_LCD_JD9853_ENABLE          1
#define TCFG_LCD_ILI9341_ENABLE         0
#define TCFG_LCD_ST7789S_ENABLE         0
#define TCFG_LCD_ST7789V_ENABLE         0
#define TCFG_LCD_ST7735S_ENABLE         0
#define TCFG_LCD_480x272_8BITS          0
#define HORIZONTAL_SCREEN               1
#endif

/* ================ USB ================ */
#ifndef MINI_USB_STACK_CHERRYUSB
#define MINI_USB_STACK_CHERRYUSB    0
#endif
#ifndef MINI_USB_ENABLE
#define MINI_USB_ENABLE             1
#endif

#if MINI_USB_ENABLE
#if MINI_USB_STACK_CHERRYUSB
#define MINI_USB_MODE_DEVICE   0
#define MINI_USB_MODE_HOST     1
#define MINI_USB_MODE          MINI_USB_MODE_DEVICE
#else
#define CONFIG_USB_ENABLE
#define TCFG_PC_ENABLE                      1
#define USB_PC_NO_APP_MODE                  2
#define USB_MALLOC_ENABLE                   1
#define USB_DEVICE_CLASS_CONFIG             (CDC_CLASS)
#define CDC_DUAL_ENABLE                     1
#define CDC_INTR_EP_ENABLE                  0
#define TCFG_HOST_AUDIO_ENABLE              0
#define TCFG_HOST_UVC_ENABLE                0
#define TCFG_HID_HOST_ENABLE                0
#define TCFG_UDISK_ENABLE                   0
#ifndef MINI_USB_JL_ID
#define MINI_USB_JL_ID                      1
#endif
#ifndef MINI_USER_CDC_PORT
#define MINI_USER_CDC_PORT                  1
#endif
#include "usb_std_class_def.h"
#include "usb_common_def.h"
#endif
#endif /* MINI_USB_ENABLE */

/* ================ 固件升级: 双备份烧写后端(网页 OTA 用) =================
 * 升级途径: WiFi 网页 OTA(wifi_prov.c, 上传整包→SDRAM→net_update 流式烧写)
 * → 备用 bank → 烧 boot info → 2s 后复位进新固件。
 * 历史注: 旧"虚拟U盘拖拽升级"随 MSC 接口删除一并移除(2026-10, USB 改双 CDC),
 * TCFG_VIR_UPDATE_ENABLE 现仅作为 net_update.c 双备份烧写后端的编译门。
 * 前提: 双备份改变 flash 布局(app 区双份), 切换后必须整体烧录一次 jl_isd.ufw */
#define CONFIG_DOUBLE_BANK_ENABLE  1   /* 0=单备份(不支持在线升级) */
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

/* ADKEY(PB01) 与开关机键复用: 长按进 P33 软关机, 再按唤醒冷启动 */
#define CONFIG_PRESS_LONG_KEY_POWERON

#define CONFIG_DEBUG_ENABLE
/* 系统日志只走 UART0(PB2); WiFi 库日志由 wifi_log_mute.c 静音。
 * 原厂双 CDC: user_cdc_* → CDC2(EP2); CherryUSB: 同为 CDC2 日志口。 */



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
/* 解码格式对齐 demo_audio(本地+网络/DLNA); A2DP 仍用 AAC + m4a_dec_seek */
#define CONFIG_AAC_DEC_ENABLE
#define CONFIG_MP3_DEC_ENABLE
#define CONFIG_M4A_DEC_ENABLE
#define CONFIG_WMA_DEC_ENABLE
#define CONFIG_WAV_DEC_ENABLE
#define CONFIG_FLAC_DEC_ENABLE
#define CONFIG_APE_DEC_ENABLE
#define CONFIG_AMR_DEC_ENABLE
#define CONFIG_DTS_DEC_ENABLE
#define CONFIG_OGG_VORBIS_DEC_ENABLE
#define CONFIG_PCM_DEC_ENABLE
#define CONFIG_ADPCM_DEC_ENABLE
#define CONFIG_VIRTUAL_DEV_ENC_ENABLE

/* 网络播放 + DLNA MediaRenderer(demo_audio 同款, STA 拿到 IP 后启动)
 * 暂时屏蔽: 不启 ai_server/libdlna, 表盘隐藏「网络」入口 */
/* #define CONFIG_NET_MUSIC_MODE_ENABLE */
/* #define CONFIG_DLNA_SDK_ENABLE */

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
/* 暂关 DNS 深度降噪, 便于确认串扰/回声是否与 DNS 有关; libdns.a / dns_encoder 任务仍保留 */
/* #define CONFIG_DNS_ENC_ENABLE */
#define CONFIG_PHONE_CALL_ADC_CHANNEL       1     /* MIC1 */
#define CONFIG_AEC_ADC_CHANNEL              3     /* MIC3 回采 */
#define CONFIG_AEC_AUDIO_ADC_GAIN           80

/* EQ: 本地播放用; 已移除 USB CDC1 在线调音(cfg_tool)。
 * CDC1 闲置数据口; CDC2=user_cdc_* 日志。 */
#define TCFG_EQ_ENABLE                      1
#define TCFG_EQ_ONLINE_ENABLE               0
#define TCFG_EQ_FILE_ENABLE                 1
#define TCFG_HW_SOFT_EQ_ENABLE              1
#define TCFG_LIMITER_ENABLE                 1
#define TCFG_DRC_ENABLE                     TCFG_LIMITER_ENABLE
#define TCFG_AUDIO_OUT_EQ_ENABLE            0
#define TCFG_EQ_DIVIDE_ENABLE               0
#define TCFG_PHONE_EQ_ENABLE                0
#define TCFG_AEC_UL_EQ_ENABLE               0
#define TCFG_DYNAMIC_EQ_ENABLE              0
#define TCFG_MIC_EFFECT_ENABLE              0
#define TCFG_PHONE_DRC_ENABLE               0
#define TCFG_AUDIO_OUT_DRC_ENABLE           0
#define TCFG_DRC_SPILT_ENABLE               0
#define TCFG_AUDIO_MDRC_ENABLE              0
#define TCFG_LAST_WHOLE_DRC_ENABLE          0
#define CONFIG_EQ_FILE_NAME                 "mnt/sdfile/res/cfg/eq_cfg_hw.bin"
#define TCFG_NULL_COMM                      0
#define TCFG_USB_COMM                       1
#define TCFG_UART_COMM                      2
#define TCFG_SPP_COMM                       3
#define TCFG_COMM_TYPE                      TCFG_NULL_COMM
#define TCFG_ONLINE_ENABLE                  0
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

/* SC7U22 六轴: 与 CST816 同挂 PH0/PH1(iic0); 二分档<1 时关 */
#ifndef TCFG_SC7U22_ENABLE
#if MINI_PWR_BISECT >= 1
#define TCFG_SC7U22_ENABLE          1
#else
#define TCFG_SC7U22_ENABLE          0
#endif
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

/* ================ 蓝牙/WiFi (受 MINI_PWR_BISECT 约束) ================ */
#if MINI_PWR_BISECT >= 4
#define MINI_BT_SUPPORT             1
#else
#define MINI_BT_SUPPORT             0
#endif
#if MINI_PWR_BISECT >= 5
#define MINI_WIFI_SUPPORT           1
#else
#define MINI_WIFI_SUPPORT           0
#endif
#if MINI_BT_SUPPORT
#define MINI_BT_SPEAKER_EN          1
#define MINI_BT_BLE_TRANS_EN        1
#define CONFIG_BT_ENABLE            1
#else
#define MINI_BT_SPEAKER_EN          0
#define MINI_BT_BLE_TRANS_EN        0
#endif

#if MINI_BT_SUPPORT
#ifdef CONFIG_BT_ENABLE
/* ACL 收包池(进 ram0_bss, sdk_ld.c 重生成 sdk.ld)。音箱场景对齐官方 demo_edr/
 * story_machine(SDRAM 在位): RX=17K+400 —— 12K(DevKitBoard 值)在 A2DP 连续流
 * 下余量不足, RX 池打满即丢包(日志 K/F 刷屏) */
#define CONFIG_BT_RX_BUFF_SIZE  (17 * 1024 + 400)
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
