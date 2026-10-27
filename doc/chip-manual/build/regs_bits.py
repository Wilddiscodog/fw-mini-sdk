# -*- coding: utf-8 -*-
"""regs_bits.py — 外设/核 SFR 位域表（仅收录 SDK 头文件或 debug.c 明确给出的位）。

自动抽取：扫描 asm/*.h 中 `#define NAME  BIT(n)` 与 `reg->CON0 & BIT(n)//注释`。
手工补全：UART putbyte【实测】、debug.c EMU/DBG 异常名、IIC CON0 等。
未公开模块返回 None，页面侧标注「位域未公开，以 API 为准」。
"""
import os
import re

ASM = r"D:\fw-AC79_AIoT_SDK\include_lib\driver\cpu\wl82\asm"


def _extract_file(path):
    """返回 [(bit_expr, name, desc)]，bit_expr 如 '0' 或 '13'。"""
    if not os.path.isfile(path):
        return []
    src = open(path, encoding="utf-8", errors="replace").read()
    out = []
    # #define FOO  BIT(3) // desc  或 /* desc */
    for m in re.finditer(
            r'#define\s+(\w+)\s+BIT\((\d+)\)\s*(?://\s*(.*)|/\*\s*(.*?)\s*\*/)?',
            src):
        name, bit, c1, c2 = m.group(1), m.group(2), m.group(3), m.group(4)
        desc = (c1 or c2 or "").strip()
        out.append((bit, name, desc))
    # iic->reg->CON0 |= BIT(14)//清空...
    for m in re.finditer(
            r'(?:->\w+|reg->\w+)\s*(?:&|\|=|&=?\s*~)\s*BIT\((\d+)\)\s*//\s*([^\n]+)',
            src):
        bit, desc = m.group(1), m.group(2).strip()
        out.append((bit, "BIT(%s)" % bit, desc))
    return out


def extract(header_name):
    return _extract_file(os.path.join(ASM, header_name))


# ---------- 手工位域表（SDK 明确或【实测】）----------
# 格式: { "模块.寄存器": [(bits, name, desc), ...] }

BIT_TABLES = {
    "UART.CON0": [
        ("0", "EN", "模块使能；未置位时写 BUF 静默丢弃【实测 putbyte】"),
        ("13", "TX_KICK", "启动发送（写 1 后写 BUF）【实测】"),
        ("15", "TX_DONE", "发送完成标志；查询等待 (CON0&(BIT15|BIT0))==BIT0 结束【实测】"),
    ],
    "IIC.CON0": [
        ("2", "CFG_DONE", "配置完成（iic_cfg_done）"),
        ("3", "DIR", "0=输出 1=输入（iic_set_output/input）"),
        ("4", "STOP", "加结束位（iic_add_end_bit）"),
        ("5", "START", "加起始位（iic_add_start_bit）"),
        ("6", "NACK", "0=ACK 1=NACK（iic_set_ack/unack）"),
        ("7", "ACK_IN", "读 ACK 状态（iic_ack_in）"),
        ("12", "CLR_EPND", "清结束位 pending"),
        ("13", "EPND", "结束位中断标志"),
        ("14", "CLR_PND", "清普通 pending"),
        ("15", "PND", "中断标志（iic_pnd）"),
    ],
    "SPI.API_FLAGS": [
        ("0", "SPI_MODE_SLAVE", "从机模式（spi.h）"),
        ("1", "SPI_UPDATE_SAMPLE_SAME", "更新与采样同沿"),
        ("2-9", "SPI_SCLK_*", "空闲电平 + 更新/采样沿组合（见 spi.h）"),
        ("10", "SPI_UNIDIR_MODE", "单向仅 DO"),
        ("11", "SPI_BIDIR_MODE", "双向 DO/DI"),
        ("12", "SPI_WR_UNUSE_SEM", "读写不等待信号量"),
        ("13", "SPI_SCLK_PORT_HD", "CLK 强驱"),
    ],
    "PWM.CH": [
        ("0-7", "PWMCH0_H..7_H", "高侧通道掩码（pwm.h）"),
        ("10-17", "PWMCH0_L..7_L", "低侧通道掩码（偏移 PWM_CHL_OFFSET=10）"),
        ("20-21", "PWM_TIMER2/3_OPCH", "TIMER2/3 PWM 输出通道"),
    ],
    "CACHE.CON": [
        ("14", "CACHE_RDY", "CACHE_WAIT：等 CACHE_CON.BIT14（cpu.h）"),
    ],
    "SFC.CON": [
        ("31", "BUSY", "SFC_WAIT：等 CON.BIT31 清零（cpu.h）"),
    ],
    # 来源: ac791n-ota-loader uboot asm/clock_hw.h（WL82 开源位定义）
    "CLOCK.CLK_CON0": [
        ("0", "RC_EN", "RC 振荡使能"),
        ("1", "RCH_EN", "0=250K / 1=16M（RCH）"),
        ("2-3", "TEST_SEL", "主时钟测试源"),
        ("4-5", "OSC_CLOCK_IN", "0=BT_OSC / 2=RTOSC_L / 3=PAT"),
        ("6-8", "MAIN_CLOCK_SEL", "0=RC / 4=BTOSC / 6=RTOSC_L / 7=PLL"),
        ("9", "SFR_MODE", "0=idle 可关 / 1=always on"),
        ("10-12", "PC0_CLOCK_OUT", "PC0 时钟输出选择"),
        ("13-15", "PA4_CLOCK_OUT", "PA4 时钟输出选择"),
    ],
    "CLOCK.CLK_CON1": [
        ("0-1", "USB_CLOCK_IN", "0=PLL48M / 1=OSC / 2=LSB / 3=关"),
        ("2-3", "DAC_CLOCK_IN", "0=PLL24M / 1=OSC / 2=LSB / 3=关"),
        ("4-6", "GPCNT_CLOCK_IN", "门控计数时钟源（SRC/SFC/HSB/AUDIO/WL/USB）"),
        ("10-11", "UART_CLOCK_IN", "0=OSC / 1=PLL48M / 2=LSB / 3=关；uboot 常置 1→48M"),
        ("14-15", "BT/WL_CLOCK_IN", "蓝牙/WiFi 时钟源选择"),
        ("16", "MEM_CLOCK_STATUS", "0=idle / 1=always on"),
        ("28-29", "SFC_CLOCK_DELAY", "SFC 时钟延时档"),
    ],
    "CLOCK.CLK_CON2": [
        ("0-3", "pll_div", "系统 PLL 档：0=192M 1=137M 2=320M 3=480M 4=107M；≥5 走 PLL2/PLL3 衍生（clk_early_init）"),
        ("4-5", "div1_sel", "后分频1：{1,3,5,7}[sel]"),
        ("6-7", "div2_sel", "后分频2：{1,2,4,8}[sel]"),
    ],
    "CLOCK.SYS_DIV": [
        ("8-10", "LSB_CLK_DIV", "LSB 分频码；实际分频 = 值+1（与 isd LSB_DIV 对应）"),
        ("16-17", "HSB_CLK_DIV", "HSB 分频码；实际分频 = 值+1（与 isd HSB_DIV 对应，见 ota-loader clock.c）"),
    ],
    "SDR.CON0": [
        ("11", "CLK_EN", "SDRAM 时钟通路使能；clk_early_init 据此判定 sdram_clk 非 0（ota-loader clock.c）"),
    ],
    "SDR.DBG": [
        ("11", "DBG_EN", "DBG_INFO.BIT11：使能调试窗口（debug.c 置位示例）"),
    ],


    "EMU.MSG": [
        ("0", "misalign_err", "非对齐访问（debug.c emu_msg）"),
        ("1", "illeg_err", "非法指令"),
        ("2", "div0_err", "除零"),
        ("3", "stackoverflow", "栈越界（配合 EMU_SSP/USP 限界）"),
        ("8", "etm_wp0_err", "ETM 写点命中"),
        ("16", "fpu_inv_err", "FPU 非法操作"),
        ("17", "fpu_inf_err", "FPU 无穷"),
        ("18", "fpu_tiny", "FPU 下溢"),
        ("19", "fpu_huge_err", "FPU 上溢"),
        ("20", "fpu_ine_err", "FPU 不精确"),
    ],
    "corex2.DBG_MSG": [
        ("0", "wdt_timout_err", "看门狗超时"),
        ("1", "prp_rd_mmu_err", "外设主读 MMU 错"),
        ("2", "prp_wr_limit_err", "外设主写限界"),
        ("3", "prp_wr_mmu_err", "外设主写 MMU 错"),
        ("4", "axi_rd_inv", "AXI 读非法"),
        ("5", "axi_wr_inv", "AXI 写非法"),
        ("6", "c1_rd_mmu_expt", "CPU1 读 MMU 异常"),
        ("7", "c0_rd_mmu_expt", "CPU0 读 MMU 异常"),
        ("8", "c1_wr_mmu_expt", "CPU1 写 MMU 异常"),
        ("9", "c0_wr_mmu_expt", "CPU0 写 MMU 异常"),
        ("10", "c1_pc_limit_err_r", "CPU1 PC 限界"),
        ("11", "c1_wr_limit_err_r", "CPU1 写限界"),
        ("12", "c0_pc_limit_err_r", "CPU0 PC 限界"),
        ("13", "c0_wr_limit_err_r", "CPU0 写限界"),
        ("14", "dmc_pnd", "DMC pending"),
        ("15", "dbg_pnd", "调试 pending"),
        ("16", "c1_if_bus_inv", "CPU1 取指总线非法"),
        ("17", "c1_rd_bus_inv", "CPU1 读总线非法"),
        ("18", "c1_wr_bus_inv", "CPU1 写总线非法"),
        ("19", "c0_if_bus_inv", "CPU0 取指总线非法"),
        ("20", "c0_rd_bus_inv", "CPU0 读总线非法"),
        ("21", "c0_wr_bus_inv", "CPU0 写总线非法"),
    ],
}


def bits_of(key):
    """key 如 'UART.CON0' / 'IIC.CON0'；无则 None。"""
    return BIT_TABLES.get(key)


def unpublished_note(module="本模块"):
    return "%s 寄存器位域未在 SDK 头文件公开，请以驱动 API / ioctl 为准。" % module
