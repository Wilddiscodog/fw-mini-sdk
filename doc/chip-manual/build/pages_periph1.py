# -*- coding: utf-8 -*-
"""pages_periph1.py — UART / SPI / IIC / TIMER / MCPWM"""
from gen_core import *
import regs_wl82 as R

CHIP_NOTE = callout("note",
    "依据官方 SDK <code>asm/uart.h·uart_dev.h·spi.h·iic.h·pwm.h</code> 与 <code>WL82.h</code> 整理。")


# ================================================================ UART
def build_uart():
    body = base_badge("UART0") + p(
        "UART0/1/2(0x12000/0x12400 区), 11 个寄存器/组。支持中断+环形 DMA 收"
        "(RXSADR/RXEADR/RXCNT), 发送可查寻或 DMA(TXADR/TXCNT)。波特率 BAUD 寄存器与主频相关。")

    body += sec("regs", "UART 寄存器(自动提取自 WL82.h)",
        sdk_regs("UART0", {
            "CON0": "控制: bit0 使能 / bit13 启动发送 / bit15 发送完成【实测】; 9bit/校验等",
            "CON1": "中断使能与标志",
            "BAUD": "波特率分频(WO)",
            "BUF": "发送/接收数据口",
            "OTCNT": "接收超时计数",
            "TXADR": "发送 DMA 起址",
            "TXCNT": "发送 DMA 计数",
            "RXSADR/RXEADR/RXCNT": "环形接收 DMA 起址/止址/计数",
            "HRXCNT": "硬件接收计数(RO)",
        }))

    body += sec("poll", "查寻收发(裸机/异常路径)",
        code("""/* cpu.a 弱 putbyte 反汇编还原【实测】, UART0=0x12000 */
JL_UART_TypeDef *u = JL_UART0;
if (c == '\\r') return;                  /* \\r 丢弃 */
if (!(u->CON0 & BIT(0))) return;         /* 未使能静默丢弃 */
u->CON0 |= BIT(13);                      /* kick 发送 */
u->BUF = c;                              /* 写数据口 */
__asm_csync();
while ((u->CON0 & (BIT(15)|BIT(0))) == BIT(0));   /* 等 bit15=1 */""")
        + callout("warn", "【实测】查询等待建议加有界超时(如 2 万拍), "
                  "串口时钟未就绪时 bit15 永不置位会死循环(fw-mini-sdk V0.9.1 教训)。"))

    body += sec("api", "驱动框架(uart_dev.h)", code("""/* 板级数据(board.c) + 设备表 {"uart0",&uart_dev_ops,...} */
UART0_PLATFORM_DATA_BEGIN(uart0_data)
    .tx_pin = IO_PORTB_02, .rx_pin = IO_PORTB_03,
    .flags = UART_DEBUG,        /* 调试口 */
    .baudrate = 1000000,
UART0_PLATFORM_DATA_END();
/* 应用层 dev_open("uart1") + dev_read/dev_write; 或 uart_open() 直拿 uart_bus_t */
ut->putbyte('x');  ut->getbyte(&c, 0);   /* 裸总线接口 */"""))
    body += CHIP_NOTE
    page("uart.html", "UART", "UART 串口 ×3",
         "UART0-2 寄存器、环形 DMA、调试口配置(含 putbyte 反汇编还原)", body)


# ================================================================ SPI
def build_spi():
    body = base_badge("SPI0") + p(
        "SPI0~3 四组(0x11C00~0x11F00 区)。SPI0 首选作 SFC NOR 引脚组(CS:PD3 CLK:PD0 "
        "D0:PD1 D1:PD2, 见 isd_config SPI=2_1_0_0); 每组 5 寄存器, 数据经 BUF FIFO "
        "或 ADR/CNT DMA。")
    body += sec("regs", "SPI 寄存器", sdk_regs("SPI0", {
        "CON": "模式/位宽/CS/时钟沿/双向半双工等(spi.h spi_set_*)",
        "BAUD": "波特率分频",
        "BUF": "数据 FIFO",
        "ADR": "DMA 地址",
        "CNT": "DMA 计数",
    }))
    body += sec("api", "驱动用法", code("""dev_open("spi2");                    /* board.c 设备表注册后 */
struct spi_platform_data { ... };     /* 板级引脚/角色 */
/* 常用 ioctl: spi_set_baud/ spi_set_mode / spi_read_write */""") )
    body += CHIP_NOTE
    page("spi.html", "SPI", "SPI 总线 ×4", "SPI0-3: 寄存器、SFC 引脚组、DMA 传输", body)


# ================================================================ IIC
def build_iic():
    body = base_badge("IIC") + p(
        "一组硬件 I²C(0x12400 区, 4 寄存器)+ SDK 软件 I²C 模拟(sw_iic)。"
        "硬件 IIC 支持中断(IRQ_IIC_IDX=23)与从机模式。")
    body += sec("regs", "IIC 寄存器", sdk_regs("IIC", {
        "CON0": "使能/中断/起始停止(iic.h 完整位定义)",
        "BUF": "数据",
        "BAUD": "时钟分频",
        "CON1": "从机地址/IO 映射",
    }))
    body += sec("api", "驱动用法", code("""/* 硬件: board.c 注册 iic0 → dev_open("iic0")
 * 软件: sw_iic0_data(plat 脚任意定) → dev_open("sw_iic0")【实测: CST816 触摸用】 */
int iic_write_regs(...);   /* iic.h: 读/写/ACK 控制等 API */"""))
    body += CHIP_NOTE
    page("iic.html", "IIC", "I²C 总线", "硬件 IIC + 软件 IIC(SDK 触摸屏即用软/硬双路径)", body)


# ================================================================ TIMER
def build_timer():
    body = base_badge("TIMER0") + p(
        "TIMER0~5 六组(0x10400~0x11200 区), 每组 4 寄存器(CON/CNT/PRD/…)。"
        "OS tick 用 IRQ_TICK_TMR; TIMER2/3 额外可作 PWM 输出(PWM_TIMER2_OPCH2/3)。")
    body += sec("regs", "TIMER 寄存器", sdk_regs("TIMER0", {
        "CON": "使能/模式(定时·计数)/中断",
        "CNT": "当前计数值",
        "PRD": "周期(自动重装)",
    }) + code("""/* sys_hi_timer 框架: sys_timer_add/sys_timeout_add_to_task(任务名, ...) */"""))
    body += sec("pcnt", "PCNT / GPCNT 计数器", base_badge("PCNT") + base_badge("GPCNT") + table(
        ["外设", "基址", "寄存器", "说明"], [
            ("PCNT", "<code>0x11000</code>", "CON/VAL", "简单脉冲计数"),
            ("GPCNT", "<code>0x11100</code>", "CON/NUM", "门控脉冲计数(GPCNT_PLATFORM_DATA: "
            "gpcnt_gpio/gss_clk_source(PLL 480M)/ch_source/cycle(15 档门控)【实测: periph_demo】)"),
        ]))
    body += CHIP_NOTE
    page("timer.html", "TIMER", "定时器与计数器", "TIMER0-5 + PCNT/GPCNT, sys_timer 框架", body)


# ================================================================ MCPWM
def build_mcpwm():
    body = base_badge("MCPWM") + p(
        "多通道 PWM 控制器(0x15000 区, 58 寄存器): 8 输出通道(PWMCH0-7_H/L 互补对)、"
        "多个 32bit 时基(timer con/cnt/prd)、死区/换向。驱动头 <code>asm/pwm.h</code>。")
    body += sec("regs", "MCPWM 寄存器(前 20 项, 全表自动提取)",
        regtable(R.regtable("MCPWM")[:20] and
                 [(o, n, m, "") for o, n, m, w in R.regtable("MCPWM")[:20]])
        + p("…共 58 项(时基×N + 通道 CON0/CON1/CMPH/CMPL×8)。位域未在 SDK 公开, "
            "全部经 pwm 驱动 API 操作。"))
    body += sec("api", "pwm 驱动(pwm.h)", code("""struct pwm_platform_data d = {
    .pwm_ch  = PWM_TIMER2_OPCH2, .freq = 1000,
    .duty    = 50.0f,            /* 支持 0.00~100.00 两位小数(point_bit) */
    .port    = IO_PORTC_05,      /* 经 IOMAP 输出通道映射到 IO */
    .deathtime = 0,              /* 死区 0-31 个系统周期 */
};
/* ioctl: PWM_RUN/STOP/PWM_SET_FREQ/PWM_SET_DUTY/PWM_SET_DEATH_TIME... */""")
        + callout("note", "【实测】xiaolu-R2 背光 PA6 网络名 LED_PWM: 可由 MCPWM/Timer-PWM "
                  "通道经 IOMAP 输出实现调光, GPIO 直推仅开关。"))
    body += CHIP_NOTE
    page("mcpwm.html", "MCPWM", "PWM 控制器", "8 通道互补输出、死区、两位小数占空比", body)


if __name__ == "__main__":
    build_uart(); build_spi(); build_iic(); build_timer(); build_mcpwm()
