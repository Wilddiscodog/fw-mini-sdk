# -*- coding: utf-8 -*-
"""pages_sys.py — Flash加密 / 显示 / 摄像头 / 电源 / WiFi-BT / 其他"""
from gen_core import *
import regs_wl82 as R

CHIP_NOTE = callout("note",
    "依据官方 SDK 头文件与 fw-mini-sdk 移植/逆向实测整理(标注【实测】)。")


# ================================================================ SFC + 安全
def build_sfc():
    body = base_badge("SFC") + p(
        "SFC = SPI NOR Flash 控制器(0x40000 区, XIP 取指), SFCENC 为配套加密引擎"
        "(0x40400 区)。烧录镜像经 isd_download 打包, 运行时读写走 "
        "<code>sfc_norflash_api.h</code>(norflash_read/write/ioctl 擦扇区)。")
    body += sec("regs", "SFC 寄存器", sdk_regs("SFC", {
        "CON": "接口模式(2/4 线)/时钟分频",
        "BAUD": "波特率(sfc_clk = hsb/(baud+1))",
        "BUF/ADR/CNT": "数据/地址/计数",
    }))
    body += sec("vm", "Flash 布局与 VM 区",
        ul([
            "单备份: NEW_FLASH_FS=YES; 双备份: BR22_TWS_DB=YES + DB_UPDATE_DATA(OTA 载荷)",
            "VM 参数区: 默认 Flash 尾部(16M flash 例: 0xFF5000~0xFFD000), syscfg_read/write API",
            "预留区: BTIF(蓝牙配对)/EXIF(客户)/VM 均可在 isd_config_rule.c 配地址与 OPT(0 擦除/1 保留/2 保护)",
            "SFCENC + 0x7DF1.key: 代码加密(isd_download -key), EFUSE 存用户 key",
        ]))
    body += sec("sec", "加密与杂凑加速器", table(["模块", "基址", "说明"], [
        ("AES", "<code>0x41200</code>", "AES 硬件(IRQ 31), aes_hw.h"),
        ("DCP", "<code>0x41300</code>", "数据协处理(图像旋转/拷贝加速)"),
        ("SHA", "<code>0x11400 区</code>", "SHA1/256(IRQ 56), sha_hw.h【实测: hw cbc/sha demo 通过】"),
        ("CRC", "<code>0x13500</code>", "硬件 CRC16(CRC16()/calc_crc16_with_init_val 库内调用)"),
        ("RAND", "<code>0x13B00</code>", "硬件随机数"),
        ("EFUSE", "<code>0x13700</code>", "熔丝(芯片 ID/加密 key), asm/efuse.h"),
    ]))
    body += CHIP_NOTE
    page("sfc-sec.html", "Flash & Security", "Flash 与安全加速器",
         "SFC XIP、SFCENC 加密、VM 布局、AES/SHA/CRC/RAND/EFUSE", body)


# ================================================================ 显示
def build_display():
    body = p("三条显示通路 + 端口域 LCDC(0x51000 区, LCD 复用引脚控制):")
    body += table(["接口", "基址", "类型", "典型屏"], [
        ("EMI", "<code>0x41400</code>", "8080 并口 8/16bit", "ILI9341/ILI9481/JD9853 SPI 屏控制器 IC"),
        ("IMD", "<code>0x41D00 区</code>", "RGB 并口(SerDes 可扩展)", "RGB888/565 裸屏"),
        ("PAP", "<code>0x12A00</code>", "并口(打印/低速)", "并口外设"),
    ])
    body += sec("emi", "EMI 8080 接口【实测逆向】",
        p("闭源 cpu.a 的 emi.c 关键语义(反汇编参考工程 sdk.elf 还原):")
        + ul([
            "emi_write ≥5 字节 DMA 写要求缓冲 <b>4 字节对齐</b>, 否则返回 -22 且一个字节都不发"
            "(错误串 'err in emi dma data address no align 4bytes')",
            "&lt;5 字节的短写(命令字节)自动拷入内部 emi_buf(0x1C14DC8)再发, 无对齐要求",
            "DMA 寄存器在基址 0x41400: [+8]=缓冲指针 [+12]=长度(结构体偏移, 见下表); <b>可读 SDRAM</b>(与 USB DMA 不同)",
            "EMI_USE_SEND_SEM ioctl: 信号量异步模式(os_sem_pend 200tick 让出 CPU)——LCD 帧写入的推荐模式",
            "中断 IRQ_EMI_IDX=27, emi_isr 完成后 post 信号量",
        ]) + sdk_regs("EMI", {
            "CON": "位宽(8/16bit)/采样沿/使能",
            "BAUD": "HSB 分频(baudrate+1)",
            "TW": "写脉冲宽度档",
            "ADR": "DMA 缓冲地址(≥5B 写, 4 字节对齐)",
            "CNT": "DMA 传输长度",
        }) + code("""/* emi_platform_data(board.c) */
static const struct emi_platform_data emi_data = {
    .bits_mode = EMI_8BITS_MODE,      /* 8 位并口 */
    .baudrate  = EMI_BAUD_DIV8,       /* clock = HSB/(baud+1) */
    .colection = EMI_FALLING_COLT,    /* 下降沿采样 */
    .tw        = EMI_BAUD_DIV6,       /* 写脉冲宽(慢屏加大) */
    .th        = EMI_TWIDTH_NO_HALF,  .time_out = 1000,
};
dev_ioctl(hdl, EMI_USE_SEND_SEM, 0);  /* 异步信号量模式 */""")
        + callout("warn", "【实测】写屏缓冲务必 aligned(4)(u16 fb[] 静态数组可能 2 对齐); "
                  "屏驱动注册表 list_for_each_lcd_driver 取<b>第一个</b>注册项, "
                  "多面板驱动同编时按链接顺序误选——只编一个面板驱动。"))

    body += sec("lcd", "LCD 框架与应用层",
        ul([
            "lcd_drive.c: REGISTER_LCD_INTERFACE + 面板 REGISTER_LCD_DEV(.lcd 段)",
            "面板驱动原语: WriteCOM/WriteDAT_8/WriteDAT_DMA(lcd_send_map→dev_write(emi))",
            "背光在 lcd_pdata.bl_pin(GPIO 或 PWM); 复位 rst_pin; 撕裂同步 te_pin",
            "【实测】原厂 ili9481_init 第一行即开背光(背光=驱动被选中+EMI open 成功的判据)",
            "LVGL/AWTK/摄像头合成走 te_driver 框架(TE 中步同步), 未移植时用 te_stub 桩",
        ]))
    body += sec("pap", "PAP 并口寄存器", sdk_regs("PAP", {
        "CON": "并口时序/使能", "BUF": "数据",
    }) + callout("note", "IMD(RGB 屏)寄存器在 asm/WL82_imd.h 单独定义(带 IPPI/层混合), "
              "配 RX801T 类 RGB 屏使用; LCDC(0x51000 区)管 LCD 复用引脚方向。"))
    body += CHIP_NOTE
    page("display.html", "Display", "显示接口",
         "EMI 8080(实测逆向)/IMD RGB/PAP + LCD 驱动框架与背光策略", body)


# ================================================================ 摄像头
def build_camera():
    body = p("DVP 摄像头输入 → ISP → JPEG 硬件编解码 → 显示/网络, 全链硬件加速:")
    body += table(["模块", "基址/中断", "说明"], [
        ("ISC0/ISC1", "0x41A00/0x41B00 区 · IRQ 59/64", "两路 DVP 并口摄像头(xclk 由 IO 输出通道提供)"),
        ("ISP", "isp_dev.h/isp_alg.h", "3A/降噪/宽动态算法参数(闭源库)"),
        ("IMC/IMR/IMB", "imc_driver.h 等", "图像拷贝/旋转/缩放(硬件 2D)"),
        ("JPEG", "0x40900 区 · IRQ 55", "编码 jpeg_encoder.h + 解码 jpeg_decoder.h, ABR 码率自适应"),
        ("OSD", "osd.h", "字符/图层叠加"),
        ("ENC(0x13000)", "image_enc.h", "二维码识别等编码加速"),
    ])
    body += code("""/* DevKitBoard camera 平台数据示例 */
static const struct camera_platform_data camera0_data = {
    .xclk_gpio = IO_PORTH_02,   /* xclk 由 output channel 驱动 */
    .reset_gpio = ..., .pwr_gpio = ...,
    .interface_mode = CAMERA_INTERFACE_MODE_DVP,
    .clk_edge = CAMERA_CLK_EDGE_1, .fmt = CAMERA_FMT_YUV422,
};""")
    body += callout("note", "参考例程: apps/demo/demo_DevKitBoard/camera_lcd_demo.c "
              "(UVC→LCD), user_video_rec.c(录像), simple_avi_unpkg.c。")
    body += CHIP_NOTE
    page("camera.html", "Camera & Codec", "摄像头与图像编解码",
         "双 DVP ISC、ISP、JPEG 编解码、2D 加速、OSD", body)


# ================================================================ 电源
def build_power():
    body = p("电源域: CORE(DVDD 1.2V 档)/IO(VDDIO 档)/DCDC14/模拟域, "
             "档位由 isd_config 烧录(VDDIO=4→3.2V 等); P33 常供电域支持软关机与唤醒。")
    body += sec("p33", "P33 与软关机",
        code("""/* asm/power_interface.h【实测: net_update 复位路径】 */
if (get_power_keep_state() & POWER_KEEP_RTC)
    power_set_soft_poweroff(10);   /* 软关机 10ms 后上电唤醒(RTC 保时场景) */
else
    system_reset();                /* 普通复位 */"""))
    body += sec("charge", "充电与电池", ul([
        "charge.h: 充电电流/电压阈值平台数据(锂电池直充方案)",
        "LRCT(0x13600 区, IRQ 44): RTC 计数与闹钟, 掉电域保持",
        "LVD 低压检测、VCM/VDD 档位调试接口(p33.h)",
    ]))
    body += sec("wdt", "看门狗", code("""#include \"asm/wdt.h\"
wdt_close();            /* 关闭(调试) */
__wdt_clear();          /* 清狗(异常路径必须调, cpu/wl82/debug.c) */"""))
    body += CHIP_NOTE
    page("power.html", "Power", "电源管理", "电源域、P33 软关机、充电、RTC(LRCT)、看门狗", body)


# ================================================================ WiFi / BT
def build_wifibt():
    body = base_badge("WL") + p(
        "片内 2.4GHz 射频+基带: WiFi4(1T1R, 库 wl_wifi.a AP+STA / wl_wifi_sta.a 仅STA)"
        "+ 经典蓝牙/BLE(btctrler.a 控制器 + btstack.a 主机栈)。"
        "基带寄存器 JL_WL(0x14000 区, 31 个)与 RF 校准由闭源库管理。")
    body += sec("cal", "RF 校准表", code("""/* board.c(wifi_calibration_param): 每台 MP 校准后烧写 */
const struct wifi_calibration_param wifi_calibration_param = {
    .xosc_l = 0xb, .xosc_r = 0xb,          /* 晶振频偏 */
    .pa_trim_data[7], .mcs_dgain[20],      /* PA/数字增益 */
};"""))
    body += sec("coex", "协议栈接入要点【实测】",
        ul([
            "WiFi 库按名字 task_create, task_info_table 必须含: tasklet(10/1400), "
            "RtmpMlmeTask(17/700), RtmpCmdQTask(17/300), wl_rx_irq_thread(5/256)",
            "事件回调 wifi_set_event_callback 必须先于 wifi_on() 注册(空回调=首事件跳 0 崩溃)",
            "BT_STATUS_INIT_OK 只经 app_default_event_handler 强定义送达(register_sys_event_handler 收不到)",
            "WiFi MAC: 强定义 init_net_device_mac_addr(库内 weak); BLE 地址 lib_make_ble_address 由经典蓝牙地址派生",
            "BT/WiFi 共存: rf_coexistence_config.h 配置时分参数",
            "配网/OTA 应用层参考: wifi_prov.c(STA→AP 门户)+net_update.c(dual_bank OTA)",
        ]))
    body += CHIP_NOTE
    page("wifi-bt.html", "WiFi & BT", "WiFi/BT 射频集成",
         "片内 WiFi4+BT/BLE: 校准表、库任务、事件路由与 MAC(实测要点)", body)


# ================================================================ 其他
def build_misc():
    body = table(["外设", "基址/头文件", "说明"], [
        ("WDT", "wdt.h", "看门狗(见电源章)"),
        ("RDEC", "<code>0x12C00</code> · rdec.h", "旋转编码器解码(多路)"),
        ("IRFLT", "<code>0x13200</code> · irflt.h", "红外遥控接收滤波"),
        ("OSA", "<code>0x13400</code>", "LSB 时钟丢失监控(debug.c 初始化)"),
        ("CTMU", "<code>0x13C00</code> · ctmu.h", "电容触摸(按键/滑动), IRQ 36"),
        ("PWM_LED", "pwm_led.h", "呼吸灯驱动(2 LED 独立 PWM)"),
        ("PLCNT", "plcnt.h", "PL 计数(功耗计数)"),
        ("SS", "<code>0x12B00</code>", "保密接口(生产测试)"),
        ("MBIST/MBISTW", "0x41x00/0x15400 区", "存储器内建自测"),
        ("FFT", "IRQ 57", "硬件 FFT 加速器(jl_math 之外的硬核)"),
        ("ENC", "<code>0x13000</code>", "编码加速(二维码 image_enc)"),
        ("uvc_device", "uvc_device.h", "USB 摄像头设备模式"),
        ("spdif", "spdif.h", "数字音频输出"),
        ("src", "<code>0x14300</code> · src.h", "异步变采样(音频通路)"),
    ])
    body += sec("bootmode", "烧录与产线",
        ul([
            "USB 烧录: 开机短接进入(isd_config DOWNLOAD_MODEL=usb), 工具 isd_download.exe",
            "串口烧录: SERIAL 模式(UTRX 引脚), 烧录器/1拖8 需 TOOL_CONFIG 最低版本",
            "双备份升级文件: db_update_data.bin(即 update-ota.ufw), 单备份用 jl_isd.ufw【实测】",
        ]))
    body += CHIP_NOTE
    page("misc.html", "Misc", "其他外设", "WDT/RDEC/IR/CTMU/呼吸灯/FFT/烧录模式速览", body)


if __name__ == "__main__":
    build_sfc(); build_display(); build_camera(); build_power(); build_wifibt(); build_misc()
