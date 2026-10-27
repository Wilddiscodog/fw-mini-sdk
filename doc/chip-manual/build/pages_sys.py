# -*- coding: utf-8 -*-
"""pages_sys.py — Flash加密 / 显示 / 摄像头 / 电源 / WiFi-BT / 其他（加深）"""
from gen_core import *
import regs_wl82 as R
import regs_bits as B

CHIP_NOTE = callout("note",
    "依据官方 SDK 头文件与 fw-mini-sdk 移植/逆向实测（标注【实测】）。")


# ================================================================ SFC + 安全
def build_sfc():
    body = ""
    body += sec("inst", "实例 / 基址",
        table(["模块", "基址", "说明"], [
            ("SFC", R.fmt_base("SFC"), "SPI NOR XIP"),
            ("SFCENC", R.fmt_base("SFCENC") if R.base_of("SFCENC") else "0x40400", "加密引擎"),
            ("AES", R.fmt_base("AES") if R.base_of("AES") else "0x41200", "IRQ 31"),
            ("SHA", R.fmt_base("SHA") if R.base_of("SHA") else "—", "IRQ 56"),
            ("CRC", R.fmt_base("CRC") if R.base_of("CRC") else "0x13500", "CRC16"),
            ("RAND", R.fmt_base("RAND") if R.base_of("RAND") else "0x13B00", "硬件随机"),
            ("EFUSE", R.fmt_base("EFUSE") if R.base_of("EFUSE") else "0x13700", "熔丝"),
        ])
        + p("运行时 API：<code>sfc_norflash_api.h</code>；"
            "SFC 时钟 = HSB/(BAUD+1)。"))

    body += sec("regs", "SFC 寄存器",
        sdk_regs("SFC", {
            "CON": "线宽/模式；BIT31=忙（cpu.h SFC_WAIT）",
            "BAUD": "分频", "BUF": "数据", "ADR": "地址", "CNT": "计数",
        }, bit_key="SFC.CON")
        + callout("note", B.unpublished_note("SFC 其余位 / SFCENC")))

    body += sec("init", "布局与读写",
        ul([
            "单备份 NEW_FLASH_FS；双备份 BR22_TWS_DB + DB_UPDATE_DATA",
            "VM 默认 Flash 尾（如 0xFF5000~0xFFD000），syscfg_read/write",
            "SFCENC + 0x7DF1.key：isd_download -key；EFUSE 存用户 key",
        ])
        + code("""norflash_read(buf, addr, len);
norflash_write(buf, addr, len);
/* ioctl 擦扇区；XIP 取指时勿擦正在执行的 bank */"""))

    body += sec("note", "加速器",
        p("AES/DCP/SHA/CRC/RAND/EFUSE 均以 API 为准，硬件位域未公开。")
        + callout("note", "【实测】hw cbc/sha demo 通过。"))
    body += CHIP_NOTE
    page("sfc-sec.html", "Flash & Security", "Flash 与安全加速器",
         "SFC 寄存器 + VM/加密 + AES/SHA 等", body)


# ================================================================ 显示
def build_display():
    s = Svg(920, 340)
    s.box(30, 40, 140, 70, ["UI 框架", "ui_platform /", "LVGL"], "cpu", fs=12)
    s.box(220, 30, 150, 50, ["Framebuffer", "RGB565 / MONO"], "mem", fs=11)
    s.box(220, 100, 150, 50, ["pi32v2 fill/copy", "DSP REP 加速"], "cpu", fs=11)
    s.arrow(170, 75, 220, 55); s.arrow(170, 75, 220, 125)
    s.box(420, 20, 150, 45, ["EMI 8080", R.fmt_base("EMI")], "peri", fs=11)
    s.box(420, 80, 150, 45, ["IMD RGB", "+ IMR 旋转"], "peri", fs=11)
    s.box(420, 140, 150, 45, ["PAP", R.fmt_base("PAP")], "peri", fs=11)
    s.box(420, 200, 150, 45, ["DCP / IMB", "图像协处理"], "mem", fs=11)
    s.arrow(370, 55, 420, 42); s.arrow(370, 125, 420, 100)
    s.box(620, 50, 160, 70, ["LCD 面板", "ILI/JD/ST…", "REGISTER_LCD_DEV"], "ana", fs=11)
    s.arrow(570, 42, 620, 75); s.arrow(570, 100, 620, 85)
    s.box(620, 160, 160, 55, ["TE 同步", "lcd_te_driver"], "bus", fs=11)
    s.text(30, 310, "LVGL: lv_port_disp → lcd_lvgl_full → EMI DMA；官方 sty UI: ui_core → br23_* → put_draw_context → LCD", 11)

    body = ""
    body += sec("arch", "显示架构",
        fig(s.render(), "应用 UI → FB/加速填充 → EMI/IMD/PAP → 面板")
        + p("两条应用栈并行存在于原厂 SDK："
            "<b>① 自研 sty/UI</b>（<code>ui_platform.c</code> + res）；"
            "<b>② LVGL</b>（官方补丁深度对接 <code>lcd_drive</code>）。"
            "底层共用 EMI/IMD 与面板驱动注册表。"))

    body += sec("inst", "总线与加速硬件",
        table(["模块", "基址", "IRQ", "用途"], [
            ("EMI", R.fmt_base("EMI"), "27", "8080 并口写屏（MCU 屏主流）"),
            ("IMD", R.fmt_base("IMD") if R.base_of("IMD") else "—", "65", "RGB 屏；可挂 IMR"),
            ("PAP", R.fmt_base("PAP"), "22", "低速并口"),
            ("DCP", R.fmt_base("DCP") if R.base_of("DCP") else "0x41300", "—",
             "数据协处理（旋转/拷贝类，位域未公开）"),
            ("IMR", "asm/imr.h", "—", "图像旋转（IMD 路径）"),
            ("IMB", "asm/imb.h", "—", "YUV 贴图/合成（摄像头叠加）"),
            ("IMC", "asm/imc.h", "—", "视频 OSD/裁剪/预缩放命令"),
            ("JPEG", R.fmt_base("JPG") if R.base_of("JPG") else "—", "55", "硬编解码喂显示/网络"),
        ]))

    body += sec("regs", "EMI / PAP 寄存器",
        sdk_regs("EMI", {
            "CON": "位宽/采样沿/使能",
            "BAUD": "HSB/(baud+1)",
            "TW": "写脉冲宽",
            "ADR": "DMA 缓冲（≥5B，4 字节对齐）",
            "CNT": "DMA 长度",
        })
        + callout("note", B.unpublished_note("EMI CON"))
        + sdk_regs("PAP", {"CON": "时序/使能", "BUF": "数据"}))

    body += sec("emi", "EMI 写屏语义【实测】",
        code("""static const struct emi_platform_data emi_data = {
    .bits_mode = EMI_8BITS_MODE, .baudrate = EMI_BAUD_DIV8,
    .colection = EMI_FALLING_COLT, .tw = EMI_BAUD_DIV6,
    .th = EMI_TWIDTH_NO_HALF, .time_out = 1000,
};
dev_ioctl(hdl, EMI_USE_SEND_SEM, 0);  /* 异步推荐 */""")
        + ul([
            "≥5B DMA 写须 4 字节对齐，否则 -22 且不发",
            "&lt;5B 短写进内部 emi_buf，无对齐要求",
            "DMA <b>可读 SDRAM</b>（对比 USB 仅 RAM0）",
            "面板 <code>REGISTER_LCD_DEV</code>：框架取<strong>第一个</strong>注册项",
        ]))

    body += sec("ui-plat", "官方 UI 平台（ui_platform.c）",
        p("路径：<code>apps/common/ui/ui_driver/interface/ui_platform.c</code>（"
            "<code>CONFIG_UI_ENABLE</code>）。"
            "向 <code>ui_core_init</code> 注册 <code>br23_platform_api</code>：")
        + table(["API", "作用"], [
            ("load_style / load_window", "加载 .sty 窗口与控件树"),
            ("open/get/put/close_draw_context", "申请/提交绘图上下文（FB 脏区）"),
            ("fill_rect / draw_rect / draw_image / show_text", "填充、边框、图片(含 RLE+α)、文字"),
            ("read/draw_point / invert_rect", "点读写与反色"),
            ("open_device / close_device", "打开底层 lcd_interface"),
        ])
        + ul([
            "资源：menu.res / str.res / ascii.res（及表盘 JL.sty 等）",
            "支持控件旋转 90°/270°（sty 头 rotate → ui_core_set_rotate + mirror）",
            "图片：可选 alpha 通道 + RLE 解码后 α 混合（get_mixed_pixel）",
            "输出格式：OSD16(RGB565) 或 MONO",
        ]))

    body += sec("pi32", "ui_platform 中的 CPU/DSP 加速",
        p("文件开头定义了 <code>pi32v2_fill_q15/q31</code>、"
            "<code>pi32v2_copy_q7/q31</code>，用 pi32v2 <b>硬件循环 REP</b> "
            "做成块填色/拷贝（DSP 风格内联汇编），在 "
            "<code>br23_fill_rect</code> 热路径调用。")
        + code("""#define USE_PI32V2_MATH  0      /* 默认关！改为 1 才走 DSP 路径 */
#define PI32V2_MATH_DSP
/* fill: pack 两个 RGB565 → 一次写 32bit；REP 循环 [%1++=4]=r8 */
/* copy: REP 循环 load/store 字拷贝行缓冲 */
#if USE_PI32V2_MATH
    pi32v2_fill_q31(color32, p32, count);
    pi32v2_copy_q31(_p32, __p32, width/2);  /* 后续行复制首行 */
#else
    while (count--) *p32++ = color32;       /* 纯 C */
    memcpy(...);
#endif""")
        + table(["点", "说明"], [
            ("加速本质", "不是独立 GPU，而是 <b>CPU0/1 的 pi32v2 SIMD/REP 指令</b>加速 FB 内存填充与行复制"),
            ("默认状态", "<code>USE_PI32V2_MATH=0</code> → 量产默认走 C/memcpy；打开后对齐要求更严"
             "（指针 4 字节对齐、width 偶数）"),
            ("适用操作", "大面积 fill_rect、同色行复制；draw_image/α 混合仍多为标量循环"),
            ("与 EMI DMA 关系", "CPU 加速的是<strong>合成进 FB</strong>；"
             "刷屏另走 EMI DMA（可并行：双缓冲/信号量）"),
            ("LVGL 侧", "官方 <code>lv_port_disp.c</code> 中 <code>gpu_fill</code> 被注释掉，"
             "<b>未</b>把 pi32v2_fill 接到 LVGL；LVGL 绘制仍为软件光栅"),
        ])
        + callout("note", "若要给 LVGL 加速填充，需自行实现 "
                  "<code>gpu_fill_cb</code> 调 pi32v2_*，并保证色格式/对齐与 "
                  "RGB565 FB 一致；刷屏仍建议双 buffer + EMI DMA。"))

    body += sec("lvgl", "LVGL 深度绑定（官方补丁）",
        p("原厂路径：<code>apps/common/example/third_party/lvgl/LVGL补丁/</code>；"
            "fw-mini-sdk：<code>third/lvgl</code> + <code>third/lvgl_port</code>（同款对接思路）。")
        + table(["层级", "文件/符号", "绑定点"], [
            ("任务", "lvgl_main_task / late_initcall",
             "lv_init → tick(1ms) → port_disp/indev → demo → lv_task_handler 循环"),
            ("显示", "lv_port_disp.c",
             "双缓冲各 LCD_W×10 行；flush_cb → <code>lcd_lvgl_full(xs,xe,ys,ye,buf)</code>"),
            ("屏驱", "lcd_drive / user_lcd_init",
             "与 sty UI 共用面板驱动与 EMI；只初始化屏时可单独调 user_lcd_init"),
            ("TE", "lcd_te_driver",
             "撕裂同步；摄像头合成等同框架；无 TE 时用 stub"),
            ("输入", "lv_port_indev.c",
             "触摸 FT6236/GT911/CST816 等板级实现"),
            ("FS", "lv_port_fs.c", "对接杰理 FS（可选）"),
            ("配置", "lv_conf.h + 板级 LCD_W/H",
             "与 isd/屏驱动分辨率必须一致"),
        ])
        + code("""/* 官方 lv_main.c 骨架 */
lv_init();
sys_timer_add(NULL, lv_tick_inc_1ms, 1);
lv_port_disp_init();   /* → user_lcd_init + 双缓冲 */
lv_port_indev_init();
lv_demo_widgets();     /* 或 benchmark/music */
while (1) { lv_task_handler(); }

/* flush → 硬件 */
lcd_lvgl_full(area->x1, area->x2, area->y1, area->y2, (u16 *)color_p);
lv_disp_flush_ready(disp_drv);""")
        + ul([
            "深度绑定含义：LVGL <b>不</b>另起一套总线驱动，flush 直接进原厂 "
            "<code>lcd_lvgl_full</code>→EMI；屏参/背光/复位与 sty 工程共用",
            "官方示例默认双 partial buffer（10 行×2），注释写明应用 DMA 与绘制并行——"
            "对应 EMI_USE_SEND_SEM 异步路径",
            "【实测 fw-mini-sdk】JD9853 + EMI8 + CST816；OTA 前须挂起 LVGL/EMI，"
            "否则与 Flash/WiFi 抢总线易 misalign",
            "任务优先级示例 11（低于 audio_decoder=30，高于部分网络）；"
            "重 UI 时可 #C1lvgl 与 BT#C0 错开",
        ]))

    body += sec("note", "选型与约束",
        ul([
            "MCU 屏 → EMI；RGB 屏 → IMD(+IMR)；视频 OSD → IMC；贴图 → IMB",
            "sty UI 适合表盘/资源包产品；LVGL 适合自定义控件与开源生态",
            "两者勿同时高频刷同一 EMI，除非分时/挂起一方",
            "写屏缓冲 aligned(4)；多面板驱动勿同编",
        ]))
    body += CHIP_NOTE
    page("display.html", "Display", "显示与 UI",
         "EMI/IMD、ui_platform pi32v2 加速、LVGL 官方绑定", body)


# ================================================================ 摄像头
def build_camera():
    body = ""
    body += sec("inst", "模块",
        table(["模块", "基址/IRQ", "说明"], [
            ("ISC0/1", "%s / %s · 59/64" % (
                R.fmt_base("ISC0") if R.base_of("ISC0") else "0x41A00",
                R.fmt_base("ISC1") if R.base_of("ISC1") else "0x41B00"), "双 DVP"),
            ("JPEG", R.fmt_base("JPG") if R.base_of("JPG") else "0x40900 · 55", "编解码"),
            ("ISP/IMC", "闭源 API", "3A / 2D"),
            ("ENC", R.fmt_base("ENC") if R.base_of("ENC") else "0x13000", "图像编码加速"),
        ]))

    body += sec("regs", "关键寄存器",
        (sdk_regs("ISC0") if R.regtable("ISC0") else callout("note", "ISC 结构体见 WL82.h / 驱动头"))
        + (sdk_regs("JPG") if R.regtable("JPG") else "")
        + callout("note", "位域未公开；经 camera / jpeg_* API。"))

    body += sec("init", "平台数据",
        code("""static const struct camera_platform_data camera0_data = {
    .xclk_gpio = IO_PORTH_02,
    .interface_mode = CAMERA_INTERFACE_MODE_DVP,
    .clk_edge = CAMERA_CLK_EDGE_1, .fmt = CAMERA_FMT_YUV422,
};"""))

    body += sec("note", "参考",
        p("demo：camera_lcd_demo.c、user_video_rec.c。"))
    body += CHIP_NOTE
    page("camera.html", "Camera & Codec", "摄像头与图像编解码",
         "ISC/JPEG 基址 + 平台流程", body)


# ================================================================ 电源
def build_power():
    body = ""
    body += sec("inst", "电源域",
        p("CORE DVDD / IO VDDIO / DCDC14 / 模拟域；档位 isd_config 烧录。"
            "P33 常电域：软关机与唤醒。相关：JL_POWER / JL_P33 / LRCT。")
        + base_badge("POWER")
        + (sdk_regs("POWER", {
            "CON": "电源控制（位域未公开，经 power_* API）",
        }) if R.regtable("POWER") else ""))

    body += sec("regs", "P33 / 关键",
        (sdk_regs("P33") if R.regtable("P33") else p("P33 经 SPI 窗口访问（p33.h 宏）。"))
        + callout("note", B.unpublished_note("POWER/P33")))

    body += sec("init", "软关机 / WDT / RTC",
        code("""if (get_power_keep_state() & POWER_KEEP_RTC)
    power_set_soft_poweroff(10);
else
    system_reset();
__wdt_clear();   /* 异常路径必调 */"""))

    body += sec("note", "其它",
        ul([
            "charge.h 充电流压阈值",
            "LRCT RTC 闹钟（IRQ 44）",
            "power_interface.h：SYS_SLEEP_* / POWER_KEEP_* 标志",
        ]))
    body += CHIP_NOTE
    page("power.html", "Power", "电源管理", "域、P33、WDT、充电", body)


# ================================================================ WiFi / BT
def build_wifibt():
    body = ""
    body += sec("inst", "射频集成",
        base_badge("WL")
        + p("WiFi4 1T1R（wl_wifi.a / wl_wifi_sta.a）+ 经典 BT/BLE"
            "（btctrler.a + btstack.a）。基带 JL_WL，校准闭源。"))

    body += sec("regs", "WL 寄存器",
        sdk_regs("WL") if R.regtable("WL") else callout("warn", "WL 结构体未公开完整位。")
        + callout("note", B.unpublished_note("WL")))

    body += sec("init", "接入要点",
        code("""/* 校准表 board.c */
const struct wifi_calibration_param wifi_calibration_param = {
    .xosc_l = 0xb, .xosc_r = 0xb, /* + pa_trim / mcs_dgain */
};
wifi_set_event_callback(cb);  /* 必须先于 wifi_on() */
wifi_on();""")
        + ul([
            "task_info_table 须含 tasklet / RtmpMlmeTask / RtmpCmdQTask / wl_rx_irq_thread",
            "BT_STATUS_INIT_OK 经 app_default_event_handler 强定义送达",
            "MAC：强定义 init_net_device_mac_addr；BLE 地址由经典地址派生",
        ]))

    body += sec("note", "共存【实测】",
        ul([
            "rf_coexistence_config.h 时分参数",
            "早期同时开 BT+WiFi 与 USB 可能冲日志/稳定性——可延后 wifi_on",
            "配网/OTA：wifi_prov.c + net_update.c",
        ]))
    body += CHIP_NOTE
    page("wifi-bt.html", "WiFi & BT", "WiFi/BT 射频集成",
         "WL 寄存器表 + 库任务/事件/共存", body)


# ================================================================ 其他
def build_misc():
    body = ""
    body += sec("inst", "外设速览",
        table(["外设", "基址", "说明"], [
            ("WDT", "— / wdt.h", "看门狗"),
            ("RDEC", R.fmt_base("RDEC") if R.base_of("RDEC") else "0x12C00", "旋转编码"),
            ("IRFLT", R.fmt_base("IRFLT") if R.base_of("IRFLT") else "0x13200", "红外滤波"),
            ("OSA", R.fmt_base("OSA") if R.base_of("OSA") else "0x13400", "LSB clk 丢失"),
            ("CTMU", R.fmt_base("CTMU") if R.base_of("CTMU") else "0x13C00", "电容触摸"),
            ("SS", R.fmt_base("SS") if R.base_of("SS") else "0x12B00", "保密接口"),
            ("FFT", "csfr JL_FFT / IRQ57", "FFT 加速"),
            ("SRC", R.fmt_base("SRC") if R.base_of("SRC") else "0x14300", "变采样"),
            ("ENC", R.fmt_base("ENC") if R.base_of("ENC") else "0x13000", "图像编码"),
        ]))

    body += sec("regs", "示例：OSA / FFT",
        (sdk_regs("OSA") if R.regtable("OSA") else "")
        + p("FFT 在 csfr.h：") + sdk_csfr_regs("FFT", {"CON": "控制", "CADR": "地址"})
        + callout("note", "多数 misc 外设位域未公开，以对应 *.h API 为准。"))

    body += sec("init", "常用",
        code("""JL_OSA->CON = 0x6D;  /* LSB clk 监控，见 debug.c */
request_irq(IRQ_OSA_IDX, …);
wdt_close();  __wdt_clear();"""))

    body += sec("note", "烧录",
        ul([
            "USB/串口烧录由 isd_config DOWNLOAD_MODEL 决定",
            "双备份：db_update_data.bin / update-ota.ufw；单备份 jl_isd.ufw【实测】",
        ]))
    body += CHIP_NOTE
    page("misc.html", "Misc", "其他外设", "速览表 + OSA/FFT 寄存器 + 烧录", body)


if __name__ == "__main__":
    build_sfc(); build_display(); build_camera(); build_power(); build_wifibt(); build_misc()
