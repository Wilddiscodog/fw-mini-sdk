# -*- coding: utf-8 -*-
"""pages_periph1.py — UART / SPI / IIC / TIMER / MCPWM（加深寄存器+用法）"""
from gen_core import *
import regs_wl82 as R
import regs_bits as B

CHIP_NOTE = callout("note",
    "依据官方 SDK <code>asm/uart.h·uart_dev.h·spi.h·iic.h·pwm.h</code> 与 "
    "<code>WL82.h</code>；位域仅收录头文件/【实测】明确项。")


# ================================================================ UART
def build_uart():
    body = ""
    body += sec("inst", "实例 / 基址 / 时钟",
        table(["实例", "基址", "IRQ", "说明"], [
            ("UART0", R.fmt_base("UART0"), "19", "常用调试口"),
            ("UART1", R.fmt_base("UART1"), "20", "应用串口"),
            ("UART2", R.fmt_base("UART2"), "21", "应用串口"),
        ])
        + p("时钟走 LSB 域；板级 <code>UART0_PLATFORM_DATA_*</code> 绑定引脚与 "
            "<code>IRQ_UARTn_IDX</code>。固定口宏见 uart.h（PORTA_5_6 等）或 PORT_REMAP。"))

    body += sec("regs", "寄存器总表 + CON0 位域",
        sdk_regs("UART0", {
            "CON0": "使能 / 发送 kick / 完成标志等",
            "CON1": "中断使能与标志",
            "BAUD": "波特率分频(WO)",
            "BUF": "收发数据口",
            "OTCNT": "接收超时",
            "TXADR": "发送 DMA 起址",
            "TXCNT": "发送 DMA 计数",
            "RXSADR": "环形接收起址",
            "RXEADR": "环形接收止址",
            "RXCNT": "环形接收计数",
            "HRXCNT": "硬件接收计数(RO)",
        }, bit_key="UART.CON0")
        + callout("warn", "CON1/校验/9bit 等位未在 SDK 头文件公开，以 uart_dev 驱动为准。"))

    body += sec("init", "初始化与收发流程",
        code("""/* 1) 板级数据 + 设备表 */
UART0_PLATFORM_DATA_BEGIN(uart0_data)
    .tx_pin = IO_PORTB_02, .rx_pin = IO_PORTB_03,
    .flags = UART_DEBUG, .baudrate = 1000000,
UART0_PLATFORM_DATA_END();

/* 2) 框架: dev_open / 或 uart_open → uart_bus_t */
void *fd = dev_open("uart0", NULL);
dev_write(fd, buf, len);  /* 或 ut->putbyte / getbyte */

/* 3) 裸机查询发送(异常路径可用) */
JL_UART0->CON0 |= BIT(13);
JL_UART0->BUF = c;
__asm_csync();
while ((JL_UART0->CON0 & (BIT(15)|BIT(0))) == BIT(0));""")
        + callout("warn", "【实测】查询等待建议有界超时；时钟未就绪时 BIT15 永不置位会死等。"))

    body += sec("note", "约束", ul([
        "环形 DMA：配 RXSADR/RXEADR/RXCNT，驱动内维护写指针",
        "调试口 UART_DEBUG 与 putbyte 弱符号绑定",
        "【实测】putbyte 丢弃 '\\r'；CON0.EN=0 时写 BUF 静默丢弃",
    ]))
    body += CHIP_NOTE
    page("uart.html", "UART", "UART 串口 ×3",
         "基址/寄存器/CON0 位域、环形 DMA、查询发送【实测】", body)


# ================================================================ SPI
def build_spi():
    body = ""
    body += sec("inst", "实例 / 基址",
        table(["实例", "基址", "IRQ", "说明"], [
            ("SPI0", R.fmt_base("SPI0"), "15", "常绑 SFC NOR 脚组"),
            ("SPI1", R.fmt_base("SPI1"), "16", "通用"),
            ("SPI2", R.fmt_base("SPI2"), "37", "通用 / 屏"),
            ("SPI3", R.fmt_base("SPI3"), "25", "通用"),
        ])
        + p("SPI0 与 SFC 脚组关系见 isd_config <code>SPI=…</code>；"
            "应用 SPI 用 <code>asm/spi.h</code> 平台数据 + ioctl。"))

    body += sec("regs", "寄存器 + API 标志位",
        sdk_regs("SPI0", {
            "CON": "模式/位宽/CS/时钟沿等（位域未公开，经 spi_set_*）",
            "BAUD": "波特率分频",
            "BUF": "数据 FIFO",
            "ADR": "DMA 地址",
            "CNT": "DMA 计数",
        })
        + bit_table("SPI.API_FLAGS", title="spi.h 平台/模式标志（非 CON 硬件位）")
        + callout("note", B.unpublished_note("SPI CON 硬件")))

    body += sec("init", "初始化与传输",
        code("""dev_open("spi2", NULL);
/* ioctl: 波特率/模式/端口切换/非阻塞 */
ioctl(fd, IOCTL_SPI_SET_BIT_MODE, mode);
ioctl(fd, IOCTL_SPI_SET_PORT, 0);       /* 同组 ABC 口切换 */
ioctl(fd, IOCTL_SPI_NON_BLOCK, 1);
/* 读写: spi_read_write / dev_read/write；DMA 走 ADR/CNT */"""))

    body += sec("note", "约束", ul([
        "SPI_MAX_SIZE ≈ 2^31-1（有符号赋值限制）",
        "非阻塞发送休眠前需 IOCTL_SPI_WRITE_NON_BLOCK_FLUSH",
        "SFC 专用 ioctl：IOCTL_SFC_*（见 spi.h）",
    ]))
    body += CHIP_NOTE
    page("spi.html", "SPI", "SPI 总线 ×4", "寄存器、API 标志、DMA/ioctl 流程", body)


# ================================================================ IIC
def build_iic():
    body = ""
    body += sec("inst", "实例",
        base_badge("IIC")
        + p("硬件 I²C 一组（IRQ 23）+ 软件模拟 <code>sw_iic</code>。"
            "固定口 4 组见 iic.h（PA7/8、PC1/2、PH0/1…）。"))

    body += sec("regs", "寄存器 + CON0 位域",
        sdk_regs("IIC", {
            "CON0": "启停/ACK/方向/pending（iic.h 宏）",
            "BUF": "数据",
            "BAUD": "时钟分频",
            "CON1": "从机地址 / IO 映射相关",
        }, bit_key="IIC.CON0"))

    body += sec("init", "用法",
        code("""/* 硬件 */
dev_open("iic0", NULL);
iic_write_regs(...);  iic_read_regs(...);

/* 软件【实测 CST816】: board 注册 sw_iic0 → dev_open("sw_iic0")
 * 注意: 勿对同一总线嵌套 IIC_IOCTL_START 造成死锁 */"""))

    body += sec("note", "约束", ul([
        "硬件支持从机模式（CON1 地址）",
        "【实测】触摸驱动用软 IIC 时避免重复 START ioctl",
    ]))
    body += CHIP_NOTE
    page("iic.html", "IIC", "I²C 总线", "硬件 CON0 位域 + 软 IIC 路径", body)


# ================================================================ TIMER
def build_timer():
    body = ""
    body += sec("inst", "实例",
        table(["实例", "基址", "IRQ", "说明"], [
            ("TIMER0~3", "0x10400 起", "4~7", "通用定时"),
            ("TIMER4/5", "见 WL82.h", "62/63", "扩展"),
            ("TICK_TMR", "—", "3", "OS tick"),
        ])
        + p("TIMER2/3 可作 PWM 输出（PWM_TIMER2_OPCH2 / PWM_TIMER3_OPCH3，pwm.h）。"
            "timer 时钟 = LSB（clock 章）。"))

    body += sec("regs", "TIMER 寄存器",
        sdk_regs("TIMER0", {
            "CON": "使能/模式/中断（位域未公开）",
            "CNT": "当前计数",
            "PRD": "周期（自动重装）",
        })
        + callout("note", B.unpublished_note("TIMER CON")))

    body += sec("init", "用法",
        code("""/* 系统软定时 */
sys_timer_add(priv, cb, msec);
sys_timeout_add_to_task("app_core", priv, cb, msec);
/* 硬件: 配 CON/PRD 后 request_irq(IRQ_TIMERn_IDX, …) */"""))

    body += sec("pcnt", "PCNT / GPCNT",
        base_badge("PCNT") + base_badge("GPCNT")
        + table(["外设", "基址", "寄存器", "说明"], [
            ("PCNT", R.fmt_base("PCNT"), "CON/VAL", "脉冲计数"),
            ("GPCNT", R.fmt_base("GPCNT"), "CON/NUM",
             "门控计数；GPCNT_PLATFORM_DATA 配 gpio/源/门控档"),
        ]))
    body += CHIP_NOTE
    page("timer.html", "TIMER", "定时器与计数器", "TIMER0-5 + PCNT/GPCNT", body)


# ================================================================ MCPWM
def build_mcpwm():
    t = R.regtable("MCPWM") or []
    rows = [(o, n, m, "") for o, n, m, w in t]
    body = ""
    body += sec("inst", "实例",
        base_badge("MCPWM")
        + p("多通道 PWM（约 58 寄存器）：8 路 PWMCH_H/L 互补、多时基、死区。"
            "驱动 <code>asm/pwm.h</code>；IRQ MCTMRX=33 / CHX_PWM=34。"))

    body += sec("regs", "寄存器总表",
        regtable(rows)
        + bit_table("PWM.CH", title="pwm.h 通道掩码（API 层）")
        + callout("note", B.unpublished_note("MCPWM 硬件 CON")))

    body += sec("init", "初始化",
        code("""struct pwm_platform_data d = {
    .pwm_ch = PWM_TIMER2_OPCH2, .freq = 1000,
    .duty = 50.0f, .port = IO_PORTC_05,
    .deathtime = 0,   /* 0~31 系统周期 */
};
/* ioctl: PWM_RUN/STOP / PWM_SET_FREQ / PWM_SET_DUTY / PWM_SET_DEATH_TIME */""")
        + callout("note", "【实测】背光可用 MCPWM/Timer-PWM 经 IOMAP；GPIO 直推仅开关。"))
    body += CHIP_NOTE
    page("mcpwm.html", "MCPWM", "PWM 控制器", "全寄存器表 + 通道掩码 + ioctl", body)


if __name__ == "__main__":
    build_uart(); build_spi(); build_iic(); build_timer(); build_mcpwm()
