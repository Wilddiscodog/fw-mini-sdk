# -*- coding: utf-8 -*-
"""pages_core.py — 总览 / CPU与存储 / 时钟 / GPIO / 中断"""
from gen_core import *
import regs_wl82 as R

CHIP_NOTE = callout("note",
    "本章内容依据官方 SDK <code>include_lib/driver/cpu/wl82/asm/WL82.h</code>、"
    "<code>cpu/wl82/sdk_ld*.c</code> 与 <code>cpu/wl82/tools/isd_config_rule.c</code> 整理; "
    "标注【实测】的条目来自 fw-mini-sdk 移植验证。")


# ================================================================ index
def build_index():
    s = Svg(920, 470)
    # CPU 域
    s.box(30, 30, 200, 80, ["CPU0 pi32v2", "主核 240MHz", "中断/任务/UI"], "cpu", fs=13)
    s.box(30, 140, 200, 80, ["CPU1 pi32v2", "从核", "算法/协处理"], "cpu", fs=13)
    # 总线
    s.box(270, 60, 130, 46, ["HSB 高速总线"], "bus")
    s.box(270, 140, 130, 46, ["LSB 低速总线"], "bus")
    s.box(270, 220, 130, 46, ["PORT 端口段"], "bus")
    s.arrow(230, 70, 270, 83); s.arrow(230, 180, 270, 163); s.arrow(230, 200, 270, 243, dashed=True)
    # HSB 外设
    s.box(440, 20, 180, 70, ["SFC / SFCENC", "SPI NOR XIP + 加密"], "peri", fs=12)
    s.box(440, 100, 180, 60, ["SDR SDRAM 控制器", "(可挂 PSRAM)"], "mem", fs=12)
    s.box(440, 170, 180, 60, ["EMI 8080 LCD", "IMD RGB LCD"], "peri", fs=12)
    s.box(440, 235, 180, 60, ["AES / DCP / SHA", "JPEG / ISC 摄像头"], "peri", fs=12)
    s.box(440, 300, 180, 55, ["HUSB2.0 (USB1)", "480Mbps 高速双角色"], "peri", fs=12)
    for y in (55, 130, 200, 265, 327):
        s.arrow(400, 83 if y == 55 else y, 440, y, color="#8a94a6")
    # LSB 外设
    s.box(660, 20, 220, 60, ["UART0-2 / SPI0-3 / IIC"], "peri", fs=12)
    s.box(660, 90, 220, 60, ["TIMER0-5 / MCPWM / PCNT"], "peri", fs=12)
    s.box(660, 160, 220, 60, ["FUSB1.1 (USB0) / SD0 / SD1"], "peri", fs=12)
    s.box(660, 230, 220, 60, ["LADC / DAC / AUDIO / ALNK", "SRC / EQ / SBC / ANC"], "ana", fs=12)
    s.box(660, 300, 220, 70, ["WL WiFi/BT 基带", "2.4G RF (片内 PA)"], "rf", fs=12)
    for y in (50, 120, 190, 190, 260, 335):
        s.arrow(400, 163 if y == 50 else (243 if y == 120 else y), 660, y, dashed=True, color="#8a94a6")
    # 存储注记
    s.text(60, 420, "存储: SDRAM 0x4000120 起 (2~8MB) · RAM0 0x1C00000 (TLB 后) · Cache RAM 0x1F20000 · Flash XIP 0x2000120", 12, "#4a5568", "start")

    body = ""
    body += kpis([("2×pi32v2", "双核 32bit DSP"), ("240MHz", "默认主频(可 320M)"),
                  ("16MB", "最大 SPI Flash"), ("2~8MB", "SDRAM/DDR"),
                  ("2×USB", "FUSB1.1+HUSB2.0"), ("WiFi4+BT", "片内 2.4G 射频")])

    body += sec("ov", "概述", p(
        "AC7912BB 是杰理 AC791N(WL82) 系列的 AIoT SoC: 双核 pi32v2 DSP 架构, "
        "片内集成 2.4GHz WiFi4 基带+射频、经典蓝牙/BLE 控制器、SFC SPI NOR 控制器(XIP)、"
        "SDRAM/PSRAM 控制器、双 USB 控制器(全速+高速)、摄像头 DVP 接口、JPEG 编解码、"
        "8080 并口与 RGB 屏接口、完整的音频链(DAC/LADC/IIS/PLNK/ALNK/EQ/ANC)。"
        "本手册按官方 SDK 头文件逆向整理(寄存器偏移=结构体成员序×4, 基址=(64×grp+adr)×4+段基址)。")
        + callout("note", "SDK 原始路径: <code>include_lib/driver/cpu/wl82/asm/</code>; "
                  "寄存器结构体在 <code>WL82.h</code>(1330 行), 中断号在 <code>hwi.h</code>。"))

    body += sec("map", "系统总线与外设地址映射",
        p("外设分三段: <b>LSB 低速段 0x1_0000~</b>、<b>HSB 高速段 0x4_0000~</b>、"
          "<b>PORT 端口段 0x5_0000~</b>。每个外设基址 = 段基址 + (64×grp + adr)×4。")
        + table(["基址", "外设", "段", "说明"], [
            ("<code>0x10000</code>", "POWER / CLOCK / SYSTEM", "LSB", "电源、时钟、系统控制"),
            ("<code>10400~11200</code>", "TIMER0~5 / PCNT / GPCNT", "LSB", "6 个定时器 + 脉冲/占空比计数"),
            ("<code>11400/11500</code>", "SD0 / SD1", "LSB", "2 路 SDMMC"),
            ("<code>11800</code>", "USB (FUSB 1.1)", "LSB", "全速设备/主机"),
            ("<code>11900</code>", "ANA 模拟前端", "LSB", "54 个模拟寄存器"),
            ("<code>11C00~11F00</code>", "SPI0~3", "LSB", "4 路 SPI"),
            ("<code>12000~12400</code>", "UART0~2 / IIC", "LSB", "3 串口 + 硬件 I²C"),
            ("<code>12800</code>", "USB1 (HUSB 2.0)", "LSB", "高速双角色【实测】"),
            ("<code>12A00/12B00</code>", "PAP / SS", "LSB", "并口打印 / 保密接口"),
            ("<code>12E00~12F00</code>", "ALNK0/1 / AUDIO", "LSB", "音频链路"),
            ("<code>13000~13200</code>", "ENC / ADC / IRFLT", "LSB", "编码器 / LADC / 红外"),
            ("<code>13400~13F00</code>", "OSA/CRC/LRCT/EFUSE/RAND/CTMU/SBC/P33", "LSB", "监控/CRC/RTC/熔丝/随机/触摸/音频编解码/电源域"),
            ("<code>13B00</code>", "DMA", "LSB", "DMA 控制器"),
            ("<code>14000~14300</code>", "WL 射频基带 / SRC", "LSB", "WiFi/BT 基带寄存器"),
            ("<code>15000</code>", "MCPWM", "LSB", "多通道 PWM(58 寄存器)"),
            ("<code>1E000~1F000</code>", "EQ / ANC", "LSB", "均衡器/主动降噪协处理器"),
            ("<code>40000+</code>", "SFC/SFCENC/SDR/PSRAM/AES/DCP/EMI/JPG/ISC0/ISC1/IMD", "HSB", "高速外设(见对应章节)"),
            ("<code>50000~</code>", "PORTA~H / USB_IO / WAKEUP / IOMAP / LCDC", "PORT", "端口与映射"),
        ])
        + fig(s.render(), "AC7912BB 系统框图(依据 SDK 外设基址表绘制)"))

    body += sec("boot", "启动与固件布局",
        p("启动入口由 <code>isd_config.ini</code> 决定: SFC(XIP) 模式 <code>ENTRY=0x2000120</code>, "
          "SDRAM 模式 <code>ENTRY=0x4000120</code>。uboot(0x1c02000 附近)→ app 双备份/单备份布局由 "
          "<code>isd_download</code> 打包; VM 参数区默认在 Flash 尾部(如 0xff5000~0xffd000)。")
        + ul([
            "<b>单备份</b>(NEW_FLASH_FS): app 连续存放, 升级经 uboot 从 SD/U 盘重写",
            "<b>双备份</b>(BR22_TWS_DB + DB_UPDATE_DATA): app 区双份, OTA 直接写备用 bank",
            "【实测】fw-mini-sdk 使用双备份 + U 盘/网页双通道 OTA(update.a dual_bank 链路)",
        ]))

    body += sec("dirs", "手册章节", grid([
        ("📡", "UART ×3", "三串口, 环形 DMA", "uart.html"),
        ("🔗", "SPI ×4 / IIC", "四路 SPI + 硬件 I²C", "spi.html"),
        ("⏲", "TIMER ×6", "定时器与计数器", "timer.html"),
        ("🎵", "音频", "DAC/LADC/ALNK/PLNK/EQ/ANC/AEC", "audio.html"),
        ("🔌", "USB 双控", "FUSB1.1 + HUSB2.0 完整细节", "usb.html"),
        ("🖥", "显示", "EMI 8080 / IMD RGB / PAP", "display.html"),
    ]))
    body += CHIP_NOTE
    page("index.html", "Overview", "芯片总览",
         "AC7912BB(WL82/AC791N) 双核 AIoT SoC —— 总线架构、外设地址映射、启动布局", body)


# ================================================================ cpu-mem
def build_cpumem():
    body = ""
    body += sec("cores", "双核 pi32v2", p(
        "两颗 pi32v2 32bit DSP 核(CPU0 主核跑系统与任务, CPU1 可跑算法协处理)。"
        "每核有独立 ETM(异常跟踪 PC0~PC3)与 EMU(栈溢出/除零/未定义指令等仿真异常), "
        "全局 corex2 调试单元提供总线异常(WR_LIMIT/PC_LIMIT/AXI/SFR MMU)检测——"
        "SDK <code>cpu/wl82/debug.c</code> 的 exception_analyze 即逐位解析这些状态。")
        + code("""/* debug.c 异常路径(cpu/wl82/debug.c): 每核栈溢出检测 */
q32DSP(0)->EMU_SSP_L = (int)cpu0_sstack_begin;   /* 栈下限 */
q32DSP(0)->EMU_SSP_H = (int)cpu0_sstack_end;     /* 栈上限 */
q32DSP(0)->EMU_CON |= BIT(3);                   /* 使能栈溢出异常 */
/* corex2: PRP_WR_LIMIT_EN / C0_PC_LIMIT 等写保护与 PC 越界检测 */"""))

    body += sec("mem", "存储器映射",
        table(["区域", "起始地址", "说明"], [
            ("Flash XIP (SFC)", "<code>0x2000120</code>", "SPI NOR 直接取指, 长度=Flash 容量(≤16MB)"),
            ("SDRAM", "<code>0x4000120</code>", "代码/大缓冲主战场, 2MB/8MB(isd_config SDRAM_SIZE)"),
            ("RAM0(内部 SRAM)", "<code>0x1C00000+TLB</code>", "零等待内部 RAM, USB DMA 只能到本段【实测】"),
            ("boot_info", "RAM0 尾部", "双备份启动信息区(sdk_ld_sdram.c)"),
            ("Cache RAM", "<code>0x1F20000+</code>", "I-CACHE 方式回收部分 cache 作 RAM"),
        ])
        + callout("warn", "【实测】HUSB 的 EP≥1 数据面为 RAM DMA 缓冲(0x16800 桥), "
                  "<b>仅能落在 RAM0(0x1C00000~) 段</b>, SDRAM 缓冲会静默丢数据; "
                  "EMI(LCD) DMA 则可读 SDRAM——两者约束不同, 详见 USB/显示章节。")
        + p("SDRAM 控制器(SDR)支持 SDR/DDR/DDRII(isd_config: SDRAM_MODE=0/1/2), "
            "默认 240MHz 源自 PLL3(SDRAM_PLL3_EN=1 时 USB2.0 可用); "
            "PSRAM 控制器(0x41500 区域)可外挂 PSRAM。"))

    body += sec("cache", "Cache 与 MMU",
        ul([
            "<b>I-Cache / D-Cache 各 8 way</b>(corex2 系统单元 CACHE_CON/DCACHE_WAY/ICACHE_WAY, csfr.h), "
            "行长 32B(CACHE_LINE_COUNT); sdk_ld 以 FREE_DACHE_WAY/FREE_IACHE_WAY(≤7) 回收 way 作 cache_ram(0x1F20000+)",
            "<b>D-Cache way 在 CPU 与 PRP(外设总线主)间分配</b>: dcache_way_use_select(cpu_way, prp_way); "
            "另支持写直通区域 dcache_cwt_set 与 flush_dcache/flushinv_dcache(cache.h)",
            "<b>非缓存别名窗口(免 MMU)</b>: NO_CACHE_ADDR() 把 ≥0x4000000 地址 +0x4000000 得 0x8000000 段"
            "非缓存视图; DMA 与 CPU 双视图一致用 __asm_csync/ssync 屏障【实测: USB/EMI 路径】",
            "<b>MMU 可选(CONFIG_MMU_ENABLE)</b>: 使能时 RAM0 起址放 2 张 TLB 表(每核一张, "
            "SDRAM 配置 0x2000×2=16KB); debug.c 异常源含 prp/c0/c1 的 rd/wr MMU 错误——"
            "两核与 PRP 外设主都过 MMU, 越权访问触发异常(exception_analyze 解析)",
            "movable region(LTO 插件): sdk_ld 的 .movable.slot/stub 机制用于代码搬移到 RAM 执行",
            "PC 限界(pc_rang_limit)与写限界(dev_write_range_limit)调试接口(debug.c)",
        ]))
    body += CHIP_NOTE
    page("cpu-mem.html", "CPU / Memory", "CPU 与存储系统",
         "双核 pi32v2、SDRAM/PSRAM/RAM0/Cache RAM、异常与调试单元", body)


# ================================================================ clock
def build_clock():
    s = Svg(880, 300)
    s.box(30, 40, 150, 60, ["PLL(系统)", "240/320MHz"], "cpu")
    s.box(30, 150, 150, 60, ["PLL3(SDRAM)", "240MHz 固定"], "cpu")
    s.box(30, 250, 150, 44, ["OSC0 24M / RTC 32K"], "ana")
    s.box(250, 30, 150, 50, ["SYS_CLK", "÷HSB_DIV+1"], "bus")
    s.box(250, 100, 150, 50, ["HSB", "240MHz"], "bus")
    s.box(250, 170, 150, 50, ["LSB", "SYS÷(LSB_DIV+1)"], "bus")
    s.box(250, 240, 150, 44, ["timer clk", "=LSB"], "bus")
    s.box(470, 30, 170, 50, ["SFC", "HSB÷(BAUD+1)"], "peri")
    s.box(470, 100, 170, 50, ["USB2.0(PLL3)", "480M"], "peri")
    s.box(470, 170, 170, 50, ["外设时钟门控", "clk_get/clk_set API"], "peri")
    s.arrow(180, 70, 250, 55); s.arrow(180, 70, 250, 125); s.arrow(180, 180, 250, 195)
    s.arrow(400, 55, 470, 55); s.arrow(400, 125, 470, 125)
    s.text(660, 200, "isd_config_rule.c: SYS_CLK=240M / HSB_DIV=1 / LSB_DIV=2\n"
                     "超频 396M 档: HSB_DIV=1, LSB_DIV=5(量产禁用)", 11, "#4a5568", "start")

    body = ""
    body += sec("tree", "时钟树", fig(s.render(), "AC7912BB 时钟树(isd_config_rule.c + clock.h)")
        + p("SDK 预设主频(clock.h): 320M/240M/192M/160M/120M/96M/48M/40M/24M…; "
            "SDR 最低 96M(SDRAM_CLK_MIN)。时钟/分频由 <code>isd_config.ini</code> 烧录进 uboot "
            "参数(非运行时寄存器直写), 应用层用 clk_get()/clk_set() 修改。"))

    body += sec("regs", "时钟寄存器(JL_CLOCK @ 0x10000)", sdk_regs("CLOCK", {
        "CON0": "系统时钟源/分频控制(位域未公开, SDK 经 clk API 操作)",
        "CON1": "外设时钟门控",
        "CON2": "PLL 配置",
    }) + base_badge("CLOCK")
        + code("""/* clock.h API */
u32 clk_get(const char *name);   /* "sys"/"sdram"/"hsb"/"lsb"/"timer" */
int clk_set(const char *name, u32 clk);
/* 临界区保护: CLOCK_CRITICAL_HANDLE_REG + list_for_each_loop_clock_critical */"""))

    body += sec("lowp", "低功耗与时钟保持", ul([
        "soft poweroff(P33 域保持, 10ms 后唤醒)——net_update 复位路径用到【实测】",
        "LSB 时钟丢失监控: JL_OSA->CON=0x6D + IRQ_OSA, 丢失即打印 'lsb clk lost'(debug.c)",
        "低功耗配置宏 TCFG_LOWPOWER_*(app_config.h), DVDD/VDDIO 档位由 isd_config 烧录",
    ]))
    body += CHIP_NOTE
    page("clock.html", "Clock", "时钟系统", "PLL/PLL3 双锁相环、三总线下分频、SFC/USB 独立时钟", body)


# ================================================================ gpio
def build_gpio():
    body = ""
    body += base_badge("PORTA") + p(
        "8 组端口(A~H), 每组 9 个寄存器, 结构完全一致(JL_PORT_FLASH_TypeDef): "
        "OUT/IN/DIR/DIE/PU/PD/HD0/HD/DIEH。SDK API 在 <code>asm/gpio.h</code>。")
    body += sec("regs", "端口寄存器(每组 ×9)",
        sdk_regs("PORTA", {
            "OUT": "输出锁存(写 0/1 控制引脚电平)",
            "IN": "输入采样(只读)",
            "DIR": "方向: 0=输出 1=输入",
            "DIE": "数字输入使能(0=模拟)",
            "PU": "内部上拉使能",
            "PD": "内部下拉使能",
            "HD0/HD": "强驱动档位",
            "DIEH": "高耐压数字输入使能",
        })
        + code("""/* gpio.h 常用 API */
gpio_direction_output(IO_PORTA_06, 1);   /* 输出高 */
gpio_direction_input(IO_PORTB_01);
gpio_set_pull_up(IO_PORTB_01, 1);
gpio_set_die(IO_PORTB_01, 1);            /* 数字输入使能(ADC 复用时关) */"""))

    body += sec("iomap", "IOMAP 输出通道复用",
        p("外设输出(时钟/PWM/PLNK SCLK 等)经 <code>JL_IOMAP</code>(0x51007 区)路由到任意 IO:"
          "output channel(CH0_PLNK0_SCLK_OUT / CH1_UART0_TX …)由 <code>gpio_output_channle()</code> "
          "映射; 输入通道由 <code>gpio_uart_rx_input()/gpio_plnk_rx_input()</code> 反向映射。")
        + code("""/* DevKitBoard 例: PLNK0 数字麦重映射 PH2=DAT, PH3=SCLK */
gpio_plnk_rx_input(IO_PORTH_02, 0, 0);
gpio_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
JL_IOMAP->CON3 |= BIT(18);"""))

    body += sec("wake", "唤醒与端口中断", ul([
        "PORT 域 WAKEUP 寄存器(0x51003 区): 睡眠唤醒源(PORT_EDGE/LEVEL)",
        "port_waked_up.h: 唤醒源回查 API",
        "【实测】xiaolu-R2: PB1 兼长按复位(RESET=PB01_08_0, isd_config), PC1_Wu/PC0_Wu 等带 Wu 后缀引脚支持唤醒",
    ]))
    body += CHIP_NOTE
    page("gpio.html", "GPIO", "GPIO 与 IO 复用", "PORT A~H 寄存器、IOMAP 输入输出映射、唤醒源", body)


# ================================================================ irq
def build_irq():
    rows = [
        ("0", "SDTAP", "调试 tap"), ("1", "EXCEPTION", "CPU 异常(debug.c 解析)"),
        ("2", "SYSCALL", "系统调用"), ("3", "TICK_TMR", "OS tick"),
        ("4-7", "TIMER0-3", "定时器"), ("8/9", "USB_SOF/USB_CTRL", "FUSB(USB0)"),
        ("10", "RTC_WDT", "RTC 与看门狗"), ("11/12", "ALNK/ALNK1", "音频链"),
        ("13", "AUDIO", "音频 FIFO/DAC/ADC"), ("14", "PORT", "端口唤醒/中断"),
        ("15/16", "SPI0/SPI1", "SPI"), ("17/18", "SD0/SD1", "SD 卡"),
        ("19-21", "UART0/1/2", "串口"), ("22", "PAP", "并口"),
        ("23", "IIC", "硬件 I²C"), ("24", "SARADC", "LADC"),
        ("25", "SPI3", "SPI"), ("26", "GPC", "GPCNT 脉冲计数"),
        ("27", "EMI", "8080 LCD DMA(emi_isr)【实测】"), ("28", "OSA", "LSB 时钟监控"),
        ("29", "BLE_RX", "BLE 接收"), ("30", "NFC", "NFC"),
        ("31", "AES", "AES"), ("32", "RDEC", "旋转解码"),
        ("33", "MCTMRX", "MCPWM 时基"), ("34", "CHX_PWM", "MCPWM 通道"),
        ("36", "CTM", "CTMU 触摸"), ("37", "SPI2", "SPI"),
        ("38/39", "PLNK/PLNK1", "数字麦"), ("40-42", "BREDR/BT_CLKN/BT_DBG", "蓝牙控制器"),
        ("43", "WL_LOFC", "WiFi LOFC"), ("44", "LRCT", "RTC"),
        ("45", "BLE_EVENT", "BLE 事件"), ("46", "SBC", "SBC 编解码"),
        ("47", "SS", "保密接口"), ("48", "DMA_COPY", "DMA 拷贝"),
        ("49/50", "PMU_PND0/1", "电源挂起"), ("51", "LDMA", "L DMA"),
        ("52", "WF", "WiFi"), ("53/54", "USB1_SOF/USB1_CTRL", "HUSB(USB1)"),
        ("55", "JPG", "JPEG"), ("56", "SHA", "SHA"),
        ("57", "FFT", "FFT 加速器"), ("58", "SRC", "变采样"),
        ("59", "ISC0", "摄像头0"), ("60", "EQ", "均衡器"),
        ("62/63", "TIMER4/5", "定时器"), ("64", "ISC1", "摄像头1"),
        ("65", "IMD", "RGB LCD"), ("120-124", "SOFT0-4", "软中断(IPI)"),
    ]
    body = ""
    body += sec("tbl", "中断源总表(hwi.h)",
        table(["编号", "名称", "说明"], rows)
        + p("request_irq 原型: <code>request_irq(irq_idx, priority(0-7), handler, cpu)</code>"
            "(cpu=0/1 指定注册核; 0xff=当前核)。irq_info_table(app_main.c)可把指定中断"
            "强制绑核。")
        + code("""/* 实例(闭源 emi 驱动内反汇编): EMI 中断=27, 优先级 3 */
request_irq(IRQ_EMI_IDX /*27*/, 3, emi_isr, 1);
/* fw-mini-sdk: HUSB 中断经 usb_dc_jl82.c/usb_hc_jl82.c 注册(IRQ_USB1_CTRL_IDX=54) */"""))
    body += sec("note", "工程注记【实测】", ul([
        "HUSB 主机模式 EP0 完成中断实测不触发——主机控制传输走 CSR0 轮询(usb_ctrl_transfer 同款)",
        "HUSB 设备模式 TXIS 窗口读即清除, 不能像标准 MUSB 写 1 清(W1C 无效)",
        "双核各自的中断控制器, sys_event 任务在 CPU0; WiFi 库线程(tasklet 等)由 task_info_table 按名字创建",
    ]))
    body += CHIP_NOTE
    page("irq.html", "IRQ", "中断系统", "66 个中断源 + 软中断, request_irq 绑核机制", body)


if __name__ == "__main__":
    build_index(); build_cpumem(); build_clock(); build_gpio(); build_irq()
