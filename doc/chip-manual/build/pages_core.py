# -*- coding: utf-8 -*-
"""pages_core.py — 总览 / CPU与存储 / 时钟 / GPIO / 中断"""
from gen_core import *
import regs_wl82 as R

CHIP_NOTE = callout("note",
    "本章内容依据官方 SDK <code>include_lib/driver/cpu/wl82/asm/WL82.h</code>、"
    "<code>csfr.h</code>、<code>cpu/wl82/sdk_ld*.c</code> 与 "
    "<code>cpu/wl82/tools/isd_config_rule.c</code> 整理; "
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
            ("<code>12000~12200</code>", "UART0~2", "LSB", "3 串口"),
            ("<code>12400</code>", "IIC", "LSB", "硬件 I²C"),
            ("<code>12700</code>", "USB1 (HUSB 2.0)", "LSB", "高速双角色【实测】"),
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
        ("🗄", "Cache / MMU", "I$/D$、TLB、EMU/异常对照", "cache-mmu.html"),
        ("📡", "UART ×3", "三串口, 环形 DMA", "uart.html"),
        ("🔗", "SPI ×4 / IIC", "四路 SPI + 硬件 I²C", "spi.html"),
        ("⏲", "TIMER ×6", "定时器与计数器", "timer.html"),
        ("🎵", "音频", "DAC/LADC/ALNK/PLNK/EQ/ANC/AEC", "audio.html"),
        ("🔌", "USB 双控", "FUSB1.1 + HUSB2.0 完整细节", "usb.html"),
        ("🖥", "显示", "EMI 8080 / IMD RGB / PAP", "display.html"),
    ], cols="g3"))
    body += CHIP_NOTE
    page("index.html", "Overview", "芯片总览",
         "AC7912BB(WL82/AC791N) 双核 AIoT SoC —— 总线架构、外设地址映射、启动布局", body)


# ================================================================ cpu-mem
def build_cpumem():
    body = ""
    body += sec("cores", "双核 pi32v2", p(
        "两颗 pi32v2 32bit DSP 核。"
        "每核独立 ETM/EMU；corex2 管总线/MMU 异常（见 "
        "<a href=\"cache-mmu.html\">Cache/MMU</a>）。")
        + table(["", "CPU0（习惯主核）", "CPU1（习惯从核）"], [
            ("日常", "app_core / sys_event / 多数 UI·业务；"
             "BT 常 #C0btctrler/#C0btstack",
             "算力扩展；SOFT4 软中断常挂此核；"
             "未钉核任务（含多数音频）可被调度过来"),
            ("绑核", '任务名 "#C0xxx" / irq_info 第三列=0',
             '"#C1xxx" / irq_info 第三列=1'),
        ])
        + callout("note", "音频任务/EQ/AEC 与双核协作细节 → "
                  "<a href=\"audio.html#cpu-audio\">audio.html</a>。")
        + code("""/* debug.c: 每核栈溢出检测 */
q32DSP(0)->EMU_SSP_L = (int)cpu0_sstack_begin;
q32DSP(0)->EMU_SSP_H = (int)cpu0_sstack_end;
q32DSP(0)->EMU_CON |= BIT(3);"""))

    body += sec("mem", "存储器映射",
        table(["区域", "起始地址", "说明"], [
            ("Flash XIP (SFC)", "<code>0x2000120</code>", "SPI NOR 直接取指, ≤16MB"),
            ("SDRAM", "<code>0x4000120</code>", "代码/堆/大缓冲；物理窗约 0x4000000 起"),
            ("RAM0(内部 SRAM)", "<code>0x1C00000+TLB</code>", "零等待; USB DMA 只能到本段【实测】"),
            ("boot_info", "RAM0 尾部", "双备份启动信息(sdk_ld_sdram.c)"),
            ("Cache RAM", "<code>0x1F20000+</code>", "回收 I$/D$ way 作普通 RAM"),
            ("非缓存窗口", "<code>addr+0x4000000</code>", "NO_CACHE_ADDR：≥0x4000000 → 0x8xxx_xxxx"),
        ])
        + callout("warn", "【实测】HUSB EP≥1 DMA <b>仅 RAM0</b>；EMI(LCD) DMA 可读 SDRAM。"))

    body += sec("sdram", "SDRAM 控制器（JL_SDR）",
        base_badge("SDR")
        + p("HSB 段控制器 <code>JL_SDR</code>（WL82.h <code>JL_SDR_TypeDef</code>）："
            "CON0~CON13 时序/模式 + DBG_* 调试窗口。"
            "颗粒参数与时钟由 <b>uboot + isd_config</b> 上电配置，应用一般只改频率 API，"
            "不直接改 CON 时序。可选外挂 <code>JL_PSRAM</code>（"
            + R.fmt_base("PSRAM") + "）。")
        + sdk_regs("SDR", {
            "CON0": "总控；BIT11=时钟通路使能（见位域）",
            "CON1": "时序/模式（WO，uboot 写）",
            "CON2": "刷新/行列相关",
            "CON3": "模式寄存器扩展",
            "CON4": "延时/相位相关",
            "CON5": "DQ 延时等",
            "CON6": "保留/时序",
            "CON7": "保留/时序",
            "CON8": "保留/时序",
            "CON9": "保留/时序",
            "CON10": "保留/时序",
            "CON11": "保留/时序",
            "CON12": "保留/时序",
            "CON13": "保留/时序",
            "DBG_INFO": "调试状态；异常 dmc_pnd 时打印",
            "DBG_START": "调试监视起始地址",
            "DBG_END": "调试监视结束地址",
            "DBG_ADR": "触发地址锁存",
        }, bit_key="SDR.CON0")
        + bit_table("SDR.DBG", title="DBG_INFO 已知位")
        + callout("note", "CON1~CON13 细位未在公开头文件给出；以 isd 键 "
                  "SDRAM_MODE/CL/DQ_DLY 等交 uboot 配置为准。")
        + h3_psram())

    body += sec("sdram-cfg", "SDRAM 烧录与时钟",
        table(["isd 键", "作用", "常用"], [
            ("ENABLE_SDRAM", "0=无 SDRAM（须 CONFIG_SFC_ENABLE）", "1"),
            ("SDRAM_SIZE", "容量", "2M / 8M"),
            ("SDRAM_MODE", "0=SDR / 1=DDR / 2=DDR2", "0"),
            ("SDRAM_REFRESH_TIME/CYCLES", "刷新周期", "32 / 4K|8K"),
            ("SDRAM_COLUM", "列地址位数相关", "0（A0-9）"),
            ("SDRAM_PLL3_EN", "1：用 PLL3，SYS 须 240M，可开 USB2.0", "1"),
            ("SDRAM_PLL3_NOUSB_EN", "PLL3 跟 CL，禁用 USB2.0", "0"),
            ("SDRAM_CL", "非 PLL3 时的 SDRAM 钟档 0=120M…3=240M…11=288M", "3"),
            ("SDRAM_DQ_DLY_TRM", "DQ 延时档（约对应钟速）", "5≈240M"),
            ("SDRAM_WLCNT / D_DLY / RLCNT", "启动延时 / 上电测试 / 分频", "见 isd 注释"),
        ])
        + p("时钟推算（ota-loader <code>clock.c</code>）："
            "若 <code>JL_SDR->CON0 & BIT(11)</code>，则 "
            "USB PLL3 8phase → <code>sdram_clk = pll3_mhz/2</code>；"
            "否则 4phase → <code>pll2_clk/2</code>。"
            "运行时改频：<code>sdram_clock_set</code> / "
            "<code>system_sdram_clock_set</code>（范围约 96M~244M，见 "
            "<a href=\"clock.html\">时钟章</a>）。")
        + code("""/* 读 SDRAM 钟 */
u32 sdr = clk_get("sdram");
/* 与 SYS 一起改 */
system_sdram_clock_set(SYS_CLK_240M, 240000000);
/* 仅改 SDRAM */
sdram_clock_set(192000000);
/* 恢复 uboot 初值 */
system_clock_set_default();"""))

    body += sec("sdram-use", "链接、寻址与调试用法",
        ul([
            "链接：<code>sdk_ld_sdram.c</code> 中 "
            "<code>sdram(rwx): ORIGIN=0x4000120, LENGTH=SDRAM_SIZE</code>；"
            "HEAP_END ≈ 0x4000120+SIZE-32",
            "ENTRY：SDRAM 模式 <code>0x4000120</code>；SFC/无 SDRAM 模式见 isd ENTRY",
            "CPU 视图：应用缓冲常用 <code>0x4000000+</code> 偏移宏；"
            "可缓存别名与 <code>NO_CACHE_ADDR</code> 见 Cache 章",
            "异常 <code>dmc_pnd</code>（DBG_MSG bit14）："
            "debug.c 打印 DBG_INFO / DBG_ADR / DBG_START / DBG_END",
            "使能监视示例：<code>JL_SDR->DBG_INFO |= BIT(11);</code>"
            "（配 DBG_START/END）",
            "CONFIG_NO_SDRAM_ENABLE：无外挂颗粒，须同时 CONFIG_SFC_ENABLE；"
            "WiFi 等大缓冲场景一般不可关",
        ])
        + callout("warn", "改 SDRAM 钟后须保证颗粒与 DQ 延时仍匹配；"
                  "超频/降频优先走 API，避免手改 CON 时序导致间歇性 bus fault。"))

    body += sec("link", "链接与可搬移段",
        ul([
            "链接脚本: <code>cpu/wl82/sdk_ld_sdram.c</code> / <code>sdk_ld_sfc.c</code> / <code>sdk.ld</code>",
            "FREE_DACHE_WAY / FREE_IACHE_WAY (≤7) → CACHE_RAM_SIZE；"
            "cache_ram 起点 = 0x1F20000 + (8-FREE_IACHE_WAY)×4K",
            "CONFIG_MMU_ENABLE 时 RAM0 前部留每核 0x2000 TLB（共 16KB）",
            "movable / LTO: <code>.movable.slot</code> / stub 把热代码搬到 RAM 执行",
        ])
        + callout("note", "Cache / MMU / EMU / DBG_MSG → "
                  "<a href=\"cache-mmu.html\">cache-mmu.html</a>；"
                  "时钟树 → <a href=\"clock.html\">clock.html</a>"))
    body += CHIP_NOTE
    page("cpu-mem.html", "CPU / Memory", "CPU 与存储系统",
         "双核、映射、JL_SDR 寄存器与 isd/API 用法；Cache/MMU 见专页", body)


def h3_psram():
    return (p("PSRAM 控制器（可选）：")
            + sdk_regs("PSRAM", {
                "CON": "控制", "BAUD": "分频(WO)", "QUCNT": "队列计数(WO)",
            })
            + callout("note", "PSRAM 位域未公开；板级有 PSRAM 时由 uboot/驱动初始化。"))


# ================================================================ cache-mmu
def build_cache_mmu():
    import regs_csfr as C

    s = Svg(900, 280)
    s.box(30, 40, 140, 70, ["CPU0 / CPU1", "pi32v2"], "cpu")
    s.box(220, 30, 160, 50, ["I-Cache 8way", "行 32B"], "mem")
    s.box(220, 100, 160, 50, ["D-Cache 8way", "行 32B"], "mem")
    s.box(220, 180, 160, 50, ["MMU / TLB", "每核 0x2000"], "mem")
    s.arrow(170, 75, 220, 55); s.arrow(170, 75, 220, 125); s.arrow(170, 90, 220, 205)
    s.box(440, 40, 180, 60, ["SDRAM / Flash", "可缓存视图"], "peri")
    s.box(440, 130, 180, 60, ["NO_CACHE 窗口", "+0x4000000"], "peri")
    s.box(440, 210, 180, 50, ["cache_ram", "0x1F20000+"], "mem")
    s.arrow(380, 55, 440, 70); s.arrow(380, 125, 440, 160); s.arrow(380, 205, 440, 235)
    s.box(680, 80, 180, 80, ["PRP 外设主", "USB/EMI/…", "共享 D$ way"], "rf", fs=12)
    s.arrow(600, 150, 680, 120, dashed=True)

    s2 = Svg(880, 160)
    s2.box(20, 40, 90, 70, ["way0", "I$/D$"], "mem", fs=12)
    s2.box(120, 40, 90, 70, ["way1…", "…"], "mem", fs=12)
    s2.box(220, 40, 120, 70, ["way(8-N)…7", "仍作 Cache"], "mem", fs=11)
    s2.box(380, 40, 200, 70, ["FREE_*ACHE_WAY", "→ cache_ram 4K×N"], "peri", fs=12)
    s2.box(620, 40, 220, 70, ["ORIGIN", "0x1F20000+(8-FI)×4K"], "dark", fs=11)
    s2.arrow(340, 75, 380, 75); s2.arrow(580, 75, 620, 75)
    s2.text(440, 140, "sdk_ld: CACHE_RAM_SIZE = FREE_DACHE_WAY*4K + FREE_IACHE_WAY*4K (max 7)", 11)

    body = ""
    body += sec("ov", "存储层次",
        fig(s.render(), "CPU ↔ Cache/MMU ↔ 可缓存/非缓存窗口 ↔ PRP")
        + p("核 SFR 基址 <code>csfr_base=0x1EE0000</code>（<code>asm/csfr.h</code>）。"
            "Cache 控制在 corex2；每核 EMU/ETM/中断配置在 <code>q32DSP(n)</code>。"
            "应用 API：<code>asm/cache.h</code>、<code>asm/cpu.h</code>（NO_CACHE_ADDR / 屏障）。"))

    body += sec("cache", "I-Cache / D-Cache",
        fig(s2.render(), "Cache way 回收为 cache_ram")
        + ul([
            "各 8 way，行长 <code>CACHE_LINE_COUNT=32</code>（cache.h）",
            "寄存器: <code>corex2(0)->CACHE_CON / DCACHE_WAY / ICACHE_WAY</code>",
            "<code>dcache_way_use_select(cpu_way, prp_way)</code>：D$ way 在 CPU 与 PRP 间分配",
            "<code>dcache_cwt_set(en, start, end)</code> 写直通窗口；"
            "<code>dcache_cwt_buffer_sync</code> / <code>flush_dcache</code> / <code>flushinv_dcache</code>",
            "<code>CACHE_WAIT()</code>：等 <code>CACHE_CON & BIT(14)</code>（cpu.h）",
        ])
        + bit_table("CACHE.CON")
        + code("""/* 典型一致性（DMA 前） */
flush_dcache(buf, len);          /* 或 flushinv_dcache */
__asm_csync();                   /* 核屏障 */
/* 外设用非缓存别名时可: ptr = (void *)NO_CACHE_ADDR(buf); */"""))

    body += sec("nocache", "非缓存地址窗口",
        p("<code>NO_CACHE_ADDR(addr)</code>：若 <code>addr ≥ 0x4000000</code> 则 "
          "<code>addr + 0x4000000</code>（落到 0x8000000 段非缓存视图）；"
          "<code>CPU_ADDR</code> 反向。用于 CPU 与 DMA 共享缓冲时绕过 D$。"
          "另配 <code>__asm_csync</code> / <code>__asm_ssync</code> 屏障。"
          "【实测】USB/EMI 路径对一致性敏感，勿混用脏 cache 行与 DMA。")
        + code("""#define NO_CACHE_ADDR(addr) \\
    (u32)(((u32)(addr) >= 0x4000000) ? ((u32)(addr)+0x4000000) : (u32)(addr))"""))

    body += sec("mmu", "MMU / TLB",
        ul([
            "可选：<code>CONFIG_MMU_ENABLE</code>（sdk_ld_sfc.c / sdk_ld_sdram.c）",
            "使能时 RAM0 起址放置 2 张 TLB（CPU0/CPU1 各 0x2000，共 16KB）",
            "CPU0/CPU1 与 PRP 外设主均经 MMU；越权 → corex2 DBG_MSG 中 "
            "prp_*_mmu_err / c0_*_mmu_expt / c1_*_mmu_expt",
            "出错 ID：<code>PRP_MMU_ERR_RID / PRP_MMU_ERR_WID</code>（exception_analyze 打印）",
        ]))

    body += sec("corex2", "corex2 寄存器（摘要）",
        base_badge("corex2", csfr=True)
        + sdk_csfr_regs("corex2", descs={
            "C0_CON": "CPU0 控制", "C1_CON": "CPU1 控制",
            "CACHE_CON": "Cache 控制（含 BIT14 ready）",
            "DCACHE_WAY": "D-Cache way 配置", "ICACHE_WAY": "I-Cache way 配置",
            "DBG_WR_EN": "调试寄存器写使能（debug_enter_critical 写 0xe7）",
            "DBG_MSG": "总线/MMU/限界异常标志（见下表）",
            "DBG_MSG_CLR": "清 DBG_MSG",
            "WR_LIMIT0_H/L": "写限界窗口 0（共 8 组）",
            "C0_WR_LIMIT_EN": "CPU0 写限界使能", "C1_WR_LIMIT_EN": "CPU1 写限界使能",
            "PRP_WR_LIMIT_EN": "外设主写限界使能",
            "PRP_MMU_ERR_WID/RID": "PRP MMU 写/读错误 ID",
            "AXI_RD_INV_ID/AXI_WR_INV_ID": "AXI 非法访问 ID",
            "PC_LIMIT0_H/L": "PC 限界窗口 0", "PC_LIMIT1_H/L": "PC 限界窗口 1",
        }, limit=40)
        + p("完整表含 WR_LIMIT0..7、WR_ALLOW_ID0..7 等；解析器跳过 REV_* 填充。"
            "写调试寄存器前须 <code>DBG_WR_EN=0xe7</code>（debug.c）。"))

    body += sec("dbg", "DBG_MSG 异常对照（debug.c）",
        bit_table("corex2.DBG_MSG", title="corex2.DBG_MSG 位 → 原因串")
        + callout("note", "exception_analyze 对 BIT2/11/13 会打印对应 WR_LIMITn_H/L；"
                  "BIT10 打印 PC_LIMIT0/1。"))

    body += sec("emu", "每核 EMU / ETM（q32DSP）",
        base_badge("q32DSP0", csfr=True)
        + p("每核一份：<code>q32DSP(0/1)</code> 基址 0x1EEF000 / +0x200。"
            "关键：EMU_CON/MSG、SSP/USP 限界、ETM_CON/PC0..3、WP0_*、ICFG/IPND/ILAT。")
        + bit_table("EMU.MSG", title="EMU_MSG 位（emu_msg[]，BIT(i) ↔ 名[31-i]）")
        + code("""/* 使能栈溢出检测后命中 → EMU_MSG.BIT(3) stackoverflow */
q32DSP(id)->EMU_SSP_L = lo;  q32DSP(id)->EMU_SSP_H = hi;
q32DSP(id)->EMU_CON |= BIT(3);"""))

    body += sec("api", "调试 API 片段",
        code("""/* cpu/wl82/debug.c */
pc_rang_limit(low, high);           /* PC 限界 */
dev_write_range_limit(...);         /* 写限界窗口 */
request_irq(IRQ_EXCEPTION_IDX, 7, exception_analyze_sram, 0xff);""")
        + callout("note", "总览与链接槽位见 <a href=\"cpu-mem.html\">cpu-mem.html</a>；"
                  "本页数据源：csfr.h / cache.h / cpu.h / debug.c / sdk_ld_*。"))
    body += CHIP_NOTE
    page("cache-mmu.html", "Cache / MMU", "Cache、MMU 与异常调试",
         "I$/D$ way、非缓存窗口、TLB、corex2/EMU 寄存器与 DBG_MSG 对照", body)


# ================================================================ clock
def build_clock():
    # 层次图：OSC → PLL/PLL2/PLL3 → SYS → HSB/LSB → 外设
    s = Svg(940, 420)
    s.box(20, 30, 130, 55, ["OSC0", "通常 24MHz"], "ana", fs=12)
    s.box(20, 110, 130, 50, ["RC 16M/250K", "启动/idle"], "ana", fs=12)
    s.box(20, 190, 130, 50, ["RTC OSC", "32K 可选"], "ana", fs=12)
    s.box(200, 40, 150, 70, ["系统 PLL", "192/320/480…", "CLK_CON2 选档"], "cpu", fs=12)
    s.box(200, 140, 150, 60, ["PLL2", "SDRAM 4ph 源"], "cpu", fs=12)
    s.box(200, 230, 150, 60, ["PLL3 / USB PLL", "SDRAM 8ph + HS USB"], "cpu", fs=11)
    s.arrow(150, 55, 200, 70); s.arrow(150, 135, 200, 165); s.arrow(150, 215, 200, 255)
    s.box(400, 50, 150, 55, ["SYS_CLK", "CPU/核"], "bus", fs=13)
    s.box(400, 140, 150, 50, ["HSB", "高速总线"], "bus")
    s.box(400, 220, 150, 50, ["LSB", "低速外设"], "bus")
    s.arrow(350, 75, 400, 75); s.arrow(475, 105, 475, 140)
    s.arrow(475, 190, 475, 220); s.arrow(350, 255, 400, 245, dashed=True, label="PLL3→SDR")
    s.box(600, 30, 150, 45, ["SFC", "HSB/(BAUD+1)"], "peri", fs=12)
    s.box(600, 90, 150, 45, ["UART", "常 PLL48M"], "peri", fs=12)
    s.box(600, 150, 150, 45, ["USB1 HS", "PLL3 480M"], "peri", fs=12)
    s.box(600, 210, 150, 45, ["timer/sd/spi", "= LSB"], "peri", fs=12)
    s.box(600, 270, 150, 45, ["DAC/WL/BT", "CLK_CON1 选源"], "peri", fs=11)
    s.box(600, 330, 150, 45, ["SDRAM", "PLL2/2 或 PLL3/2"], "mem", fs=11)
    for y in (52, 112, 172, 232, 292):
        s.arrow(550, 165 if y < 200 else 245, 600, y, color="#8a94a6")
    s.arrow(550, 245, 600, 350, dashed=True)
    s.text(20, 400, "烧录: isd_config → uboot 配 PLL/分频；运行时: clock.h API / 可读回 SYS_DIV·CLK_CONx", 11, "#4a5568", "start")

    body = ""
    body += sec("tree", "时钟树总览",
        fig(s.render(), "OSC → PLL/PLL2/PLL3 → SYS → HSB/LSB → 外设（依据 clock.h + ota-loader clock.c）")
        + p("两条配置路径：① <b>烧录态</b> <code>isd_config.ini</code>（由 "
            "<code>isd_config_rule.c</code> 生成）由 uboot 上电写 PLL/分频；"
            "② <b>运行态</b> <code>asm/clock.h</code> 的 "
            "<code>system_clock_set</code> / <code>clk_get</code> 等。"
            "开源位定义与推算公式参考 "
            "<code>ac791n-ota-loader/.../clock_hw.h</code>、<code>clock.c</code>（gitee-repos-Other）。"))

    body += sec("formula", "分频关系（clk_early_init 推算）",
        table(["时钟", "公式 / 来源", "典型值(默认 isd)"], [
            ("OSC", "OSC_FREQ；ANA.PLL_CON0[27:26]==2 → 40M 否则 24M", "24 MHz"),
            ("PLL 档", "CLK_CON2[3:0]：0=192 /1=137 /2=320 /3=480 /4=107 M；≥5→PLL2/3", "对应 SYS=240M 档"),
            ("SYS", "pll_base / div1 / div2；div1={1,3,5,7}[CON2[5:4]]；div2={1,2,4,8}[CON2[7:6]]", "240 MHz"),
            ("HSB", "sys / (HSB_DIV+1)；HSB_DIV = SYS_DIV[17:16]", "240/(1+1)=120 MHz"),
            ("LSB", "hsb / (LSB_DIV+1)；LSB_DIV = SYS_DIV[10:8]", "120/(2+1)=40 MHz"),
            ("SDRAM", "PLL3 8ph：usb_pll3/2；否则 PLL2/2（SDR.CON0.BIT11 使能）", "240 MHz（PLL3）"),
            ("UART", "CLK_CON1[11:10]=1 → 固定 48M（uboot 常见）", "48 MHz"),
            ("timer/sd/spi", "clk_get 返回 lsb", "=LSB"),
            ("SFC", "hsb / (JL_SFC->BAUD+1)", "视 BAUD"),
        ])
        + callout("note", "isd 注释写明分频系数 = 配置值+1。默认 "
                  "<code>SYS_CLK=240MHz; HSB_DIV=1; LSB_DIV=2;</code> → "
                  "HSB=120M、LSB=40M。勿把 HSB 当成与 SYS 同频。")
        + code("""/* ac791n-ota-loader clock.c 核心推算（摘录） */
u8 div1 = clk_div1[(JL_CLOCK->CLK_CON2 >> 4) & 3];  /* 1,3,5,7 */
u8 div2 = clk_div2[(JL_CLOCK->CLK_CON2 >> 6) & 3];  /* 1,2,4,8 */
u8 pll_div = JL_CLOCK->CLK_CON2 & 0xF;
/* pll_div→192/137/320/480/107M 后再 /div1/div2 → sys_clk */
hsb = sys / (((SYS_DIV >> 16) & 0x3) + 1);
lsb = hsb / (((SYS_DIV >> 8) & 0x7) + 1);"""))

    body += sec("isd", "烧录配置（isd_config）",
        p("改 <code>cpu/wl82/tools/isd_config_rule.c</code> 后重新打包下载，"
            "参数进入 uboot，<b>不是</b>应用启动后再写一次 ini。")
        + table(["键", "含义", "常用取值"], [
            ("OSC / OSC_FREQ", "晶振源与频率", "OSC0 / 24MHz（亦见 40M 机型）"),
            ("SYS_CLK", "系统主频目标", "240M（默认）；320M；超频 396M 需抬 DVDD"),
            ("HSB_DIV", "高速总线分频码（实际÷(值+1)）", "1 → HSB=SYS/2"),
            ("LSB_DIV", "低速总线分频码（实际÷(值+1)）", "2 → LSB=HSB/3"),
            ("SDRAM_PLL3_EN", "1：SDRAM 用 PLL3（须 SYS=240M）并可开 USB2.0", "量产常 1"),
            ("SDRAM_PLL3_NOUSB_EN", "1：PLL3 跟 SDRAM_CL，但禁用 USB2.0", "0"),
            ("SDRAM_CL", "SDRAM 时钟档（非 PLL3 时）", "0=120…3=240…11=288；上限 288M"),
            ("SDRAM_DQ_DLY_TRM", "DQ 刷新延时档", "5≈240M"),
            ("VDDIO / DVDD / DCDC14", "IO/核/DCDC 电压档", "超频须抬 DVDD（如 14→1.35V）"),
        ])
        + code("""/* 默认量产片段（isd_config_rule.c） */
OSC_FREQ=24MHz;
SYS_CLK=240MHz;
HSB_DIV=1;   /* HSB = 240/(1+1) = 120M */
LSB_DIV=2;   /* LSB = 120/(2+1) = 40M */
SDRAM_PLL3_EN=1;  /* SDRAM@240M + USB2.0 可用 */
SDRAM_CL=3;

/* 超频示例（CONFIG_OVERCLOCKING_ENABLE，量产慎用） */
SYS_CLK=396MHz; HSB_DIV=1; LSB_DIV=1;
DVDD=14; DCDC14=4;  /* 抬核压 */""")
        + callout("warn", "7913/7915 + RTC 内置电源时 SDK 会把 HSB_DIV=2、LSB_DIV=5"
                  "（P33 降速）。换板核对 isd 条件编译。"))

    body += sec("regs", "JL_CLOCK 寄存器与位域",
        base_badge("CLOCK")
        + p("与 <code>JL_POWER</code> 同基址 0x10000：PWR_CON ≡ POWER->CON。"
            "成员名以 WL82.h 为准（非旧文档的 CON0 简称）。")
        + sdk_regs("CLOCK", {
            "PWR_CON": "电源控制（同 JL_POWER->CON）",
            "RESERVED0": "保留",
            "SYS_DIV": "HSB/LSB（及部分 SFC）分频",
            "CLK_CON0": "主时钟源 / RC / 时钟输出脚",
            "CLK_CON1": "UART/USB/DAC/WL 等外设时钟源",
            "CLK_CON2": "系统 PLL 档与后分频",
            "CLK_CON3": "扩展（SDK 少直接操作）",
        })
        + bit_table("CLOCK.SYS_DIV")
        + bit_table("CLOCK.CLK_CON0")
        + bit_table("CLOCK.CLK_CON1")
        + bit_table("CLOCK.CLK_CON2")
        + callout("note", "位域摘自开源 <code>clock_hw.h</code>（ota-loader）；"
                  "官方 AIoT 闭源库可能另有封装，应用优先走 API。"))

    body += sec("api", "运行时配置方法",
        p("<code>include_lib/.../asm/clock.h</code>（fw-mini-sdk / AC79 SDK 同款应用 API）：")
        + table(["API", "作用", "范围/说明"], [
            ("clk_get(name)", "读缓存频率",
             '"sys"/"sdram"/"hsb"/"lsb"/"uart"/"timer"/"sd"/"spi"/"osc"'),
            ("clk_set(name, clk)", "按名改频", "依赖闭源实现；改前走 critical handler"),
            ("system_clock_set(clk)", "改系统时钟", "24M~396M"),
            ("sdram_clock_set(clk)", "改 SDRAM 钟", "96M~244M（SDRAM_CLK_MIN=96M）"),
            ("system_sdram_clock_set(sys, sdr)", "同时改两者", "同上"),
            ("system_clock_set_default()", "恢复上电初值", "uboot 写入的 SYS/SDR"),
            ("system_clock_set_idle()", "idle：SYS=24M，SDR 固定 120M", "低功耗"),
            ("system_clock_hsb_lsb_div(h,l)", "临时改 HSB/LSB 分频",
             "hsb=sys/h（1~4），lsb=hsb/l（1~8）；须配对 resume"),
            ("sys_clk_source_set(i)", "主时钟源", "0=BT_OSC 1=RTC 2=PLL_OSC"),
            ("sys_clk_source_rc_clk_on/off", "切 RC 16M/250K", "休眠路径"),
            ("clk_out_pc0 / clk_out_pa0", "把内部钟打到 IO", "调试测频"),
            ("system_vdd_clock_set(i)", "自定义 VDD+CLOCK 档", "与电压表联动"),
        ])
        + code("""/* 读频 */
printf("sys=%d hsb=%d lsb=%d sdram=%d\\n",
       clk_get("sys"), clk_get("hsb"), clk_get("lsb"), clk_get("sdram"));

/* 运行时降频（注意外设波特率/定时器依赖） */
system_clock_set(SYS_CLK_120M);
/* 或临时只拉大 HSB/LSB 分频 */
int tok = system_clock_hsb_lsb_div(2, 4);  /* hsb=sys/2, lsb=hsb/4 */
/* ... 临界业务 ... */
system_clock_hsb_lsb_div_resume(tok);

/* 改频临界区：驱动注册 enter/exit */
CLOCK_CRITICAL_HANDLE_REG(uart, uart_clk_enter, uart_clk_exit);""")
        + callout("warn", "改 SYS/HSB/LSB 后 UART 若绑 LSB 需重配波特率；"
                  "uboot 常把 UART 固定 48M（CLK_CON1），与 LSB 脱钩。"
                  "SFC BAUD、定时器 tick 也要复核。"))

    body += sec("peri", "外设时钟源（CLK_CON1）",
        ul([
            "UART：推荐 PLL48M（与波特率计算稳定）；亦可 OSC/LSB",
            "USB FS（FUSB）侧选源：PLL48M / OSC / LSB",
            "DAC：PLL24M / OSC / LSB",
            "BT/WL：STD_48M、OSC、HSB 等（与射频库绑定，勿随意改）",
            "GPCNT：可选 SRC/SFC/HSB/AUDIO/WL/USB 作计数源",
        ]))

    body += sec("usb240", "为何高速 USB 要求 SYS=240M？",
        p("高速 USB（HUSB / USB2.0 HS）<b>并不吃 CPU 的 240M</b>，"
            "PHY 需要的是固定 <b>480 MHz</b> UTMI 钟。"
            "官方把「SYS=240M + SDRAM_PLL3_EN=1」绑在一起，是因为 "
            "<b>USB PLL3 与 SDRAM 时钟共用同一套 PLL3</b>，只能同时满足一组比例。")
        + table(["节点", "频率", "来源"], [
            ("USB PLL3（HUSB_COM_CON0[27:18]+2）", "480 MHz", "HS PHY / UTMI 基准"),
            ("SDRAM（8phase，CON0.BIT11）", "PLL3÷2 = <b>240 MHz</b>",
             "HUSB_COM_CON2[4:3]==3 时走 PLL3（clock.c）"),
            ("SYS_CLK（isd 强制）", "<b>240 MHz</b>",
             "与 SDR@240 + DQ_DLY/电压档成套；非 PHY 直接输入"),
            ("HSB / LSB", "SYS/(HSB_DIV+1) 等", "与 USB 无硬接线比例"),
        ])
        + code("""/* ota-loader clock.c */
usb_pll3_clk = ((HUSB_COM_CON0 >> 18) & 0x3FF) + 2;   /* MHz 量纲 */
if (((HUSB_COM_CON2 >> 3) & 0x3) == 0x3) {            /* PLL3 → SDR */
    sdram_clk = (JL_SDR->CON0 & BIT(11))
                ? (usb_pll3_clk / 2 * 1000000) : 0;   /* 固定 ÷2 */
}""")
        + ul([
            "<code>SDRAM_PLL3_EN=1</code>：PLL3 锁在 USB 可用的 480M → SDR 固定 240M；"
            "<b>isd 要求 SYS_CLK=240M</b>，SDK 宣称可开 USB2.0",
            "<code>SDRAM_PLL3_NOUSB_EN=1</code>：PLL3 按 <code>SDRAM_CL</code> 改到别的钟"
            "（120~288M 档）→ <b>不再保证 480M PHY</b> → 不能用高速 USB",
            "<code>SDRAM_PLL3_EN=0</code>：SDR 改走 PLL2÷2；高速 USB 是否单独开 PLL3 "
            "取决于 uboot/闭源库，官方推荐路径仍是 PLL3 共享方案",
            "全速 FUSB（12Mbps）走 CLK_CON1 的 48M 等源，<b>不受</b> SYS=240 约束",
        ])
        + callout("warn", "改 <code>system_clock_set</code> 把 SYS 调离 240M 时，"
                  "勿假定 HS USB 仍可用：PLL3/SDR 比例由 uboot 烧录态决定，"
                  "运行时改 SYS 不会自动重锁 USB PHY 480M。"))

    body += sec("note", "约束与【实测】", ul([
        "主频/分频以 isd 烧录为准；应用 system_clock_set 会改运行值但不改 Flash 内 uboot 参数",
        "超频须同步抬 DVDD/DCDC14；量产默认关闭 CONFIG_OVERCLOCKING_ENABLE",
        "soft poweroff（P33）唤醒后时钟由 uboot/SDK 恢复路径决定",
        "LSB 丢失：JL_OSA->CON=0x6D + IRQ_OSA → 打印 lsb clk lost（debug.c）",
        "测频：clk_out_pa0(PLL_SYS_CLK_OUT) / clk_out_pc0(HSB_CLK_OUT) 用示波器核对",
    ]))
    body += CHIP_NOTE
    page("clock.html", "Clock", "时钟系统",
         "时钟树、isd 烧录、分频公式、JL_CLOCK 位域、运行时 API", body)



# ================================================================ gpio
def build_gpio():
    body = ""
    body += sec("inst", "实例与基址",
        base_badge("PORTA")
        + p("8 组端口 PORTA~H（PORT 段），共用 <code>JL_PORT_FLASH_TypeDef</code>。"
            "API：<code>asm/gpio.h</code>。时钟：端口属常开域，无独立门控宏。")
        + table(["实例", "基址(WL82.h)", "说明"], [
            ("PORTA", R.fmt_base("PORTA"), "常用 GPIO / 部分模拟脚"),
            ("PORTB", R.fmt_base("PORTB"), "按键 / UART 等"),
            ("PORTC~H", "见 WL82.h", "复用外设、唤醒脚(Wu)"),
        ]))

    body += sec("regs", "端口寄存器(每组 ×9)",
        sdk_regs("PORTA", {
            "OUT": "输出锁存",
            "IN": "输入采样(RO)",
            "DIR": "方向: 0=输出 1=输入",
            "DIE": "数字输入使能(0=模拟)",
            "PU": "内部上拉",
            "PD": "内部下拉",
            "HD0": "强驱动档位 0",
            "HD": "强驱动档位",
            "DIEH": "高耐压数字输入使能",
        })
        + callout("warn", "位域按「每 bit 对应一脚」操作，无独立命名位宏；"
                  "用 gpio_* API 或直接掩码 PORT->DIR |= BIT(n)。"))

    body += sec("init", "推荐初始化",
        code("""/* 输出 / 输入 */
gpio_direction_output(IO_PORTA_06, 1);
gpio_direction_input(IO_PORTB_01);
gpio_set_pull_up(IO_PORTB_01, 1);
gpio_set_die(IO_PORTB_01, 1);     /* ADC 复用脚采样前关 DIE */
gpio_set_hd(IO_PORTA_06, 1);      /* 需要时开强驱 */"""))

    body += sec("iomap", "IOMAP 复用",
        p("外设输出经 <code>JL_IOMAP</code> 路由；"
          "<code>gpio_output_channle()</code> / "
          "<code>gpio_uart_rx_input()</code> / <code>gpio_plnk_rx_input()</code>。")
        + code("""gpio_plnk_rx_input(IO_PORTH_02, 0, 0);
gpio_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
JL_IOMAP->CON3 |= BIT(18);"""))

    body += sec("wake", "唤醒与约束", ul([
        "WAKEUP 寄存器(PORT 段): 边沿/电平唤醒",
        "port_waked_up.h 回查唤醒源",
        "【实测】PB1 可兼长按复位(isd_config RESET=PB01_…)；带 Wu 后缀脚支持唤醒",
    ]))
    body += CHIP_NOTE
    page("gpio.html", "GPIO", "GPIO 与 IO 复用", "PORT A~H 寄存器、IOMAP、唤醒", body)


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
    body += sec("inst", "控制器",
        p("每核独立中断控制器（q32DSP ICFG/IPND/ILAT，见 "
          "<a href=\"cache-mmu.html\">cache-mmu</a>）。"
          "源编号在 <code>asm/hwi.h</code>；软中断 SOFT0-4=120~124。"
          "异常入口 IRQ_EXCEPTION_IDX=1 → exception_analyze。"))

    body += sec("tbl", "中断源总表",
        table(["编号", "名称", "说明"], rows))

    body += sec("init", "注册",
        code("""request_irq(irq_idx, priority/*0-7*/, handler, cpu);
/* cpu=0/1 绑核；0xff=当前核。irq_info_table 可强制绑核 */
request_irq(IRQ_EMI_IDX /*27*/, 3, emi_isr, 1);
request_irq(IRQ_USB1_CTRL_IDX /*54*/, …);"""))

    body += sec("note", "工程注记【实测】", ul([
        "HUSB 主机 EP0 完成中断不触发 → CSR0 轮询",
        "HUSB 设备 TXIS 读清（非 MUSB W1C）",
        "sys_event 在 CPU0；WiFi 库线程由 task_info_table 按名创建",
    ]))
    body += CHIP_NOTE
    page("irq.html", "IRQ", "中断系统", "66 个中断源 + 软中断, request_irq 绑核机制", body)


if __name__ == "__main__":
    build_index(); build_cpumem(); build_cache_mmu()
    build_clock(); build_gpio(); build_irq()
