/*
 * CherryUSB 设备栈 JL WL82 (AC791x) 粘层
 *
 * 职责：
 *  1. HUSB(USB1, 高速) 硬件上电 —— 复用闭源 cpu.a 的 usb_g_sie_init(1)：
 *     内部完成 PHY 上电序列、桥接器时钟使能、D+/D- 数字输入使能。
 *  2. DP/DM 复用切换到 USB 功能（usb_iomode）。
 *  3. 注册 USB1 控制器中断，转入 CherryUSB 的 USBD_IRQHandler。
 *
 * 注意：AC7912BB 的两个 USB 控制器共用一组 D+/D-，本 glue 只驱动 USB1(HUSB)。
 */
/* JL 头在前: JL 的 typedef bool 先落地, 避免与 stdbool 宏冲突 */
#include "typedef.h"
#include "asm/usb.h"            /* usb_dev / usb_g_sie_init / usb_iomode / usb_sie_disable */
#include "device/usb/usb_phy.h" /* usb_slave_init */
#include "asm/hwi.h"            /* request_irq / unrequest_irq / ___interrupt */
#include "asm/irq.h"
#include "os/os_api.h"          /* os_time_dly */

#include "usbd_core.h"
#include "fw_version.h"

#define JL82_HUSB_ID        1   /* usb_dev 编号: 1 = HUSB 2.0 高速口 */
#define JL82_HUSB_SIE_BASE  0x16000UL

extern void USBD_IRQHandler(uint8_t busid);

/* ---- 中断诊断 ----
 * PRE  = ISR 入口时的 IS/TXIS
 * POST = 上一次 USBD_IRQHandler(写1清除)执行完之后的 IS/TXIS 回读
 * PRE==POST => 写1清除无效(该位不是W1C); PRE!=POST 且再次挂起 => 条件持续再挂起 */
#define JL_SIE_B(v) (*(volatile uint8_t *)v)
#define JL_SIE_H(v) (*(volatile uint16_t *)v)
static volatile uint32_t jl_irq_n = 0;
static volatile uint8_t  s_post_is   = 0xEE;
static volatile uint16_t s_post_txis = 0xEEEE;

___interrupt
static void usb1_cherryusb_isr(void)
{
    uint32_t cpu;
    uint8_t  pre_is;
    uint16_t pre_txis;

    __asm__ volatile("%0 = cnum" : "=r"(cpu));
    jl_irq_n++;
    if (cpu != 0) {
        if ((jl_irq_n & 0xFFFF) == 1) {
            printf("[usb-irq] !! ON CPU%u, n=%u\n", (unsigned)cpu, jl_irq_n);
        }
        return; /* 双核并发跑 DCD 是致命的, 直接退出 */
    }

    pre_is   = JL_SIE_B(0x1600AUL);
    pre_txis = JL_SIE_H(0x16002UL);

    if (jl_irq_n <= 3 || (jl_irq_n % 20000) == 0) {
        printf("[usb-irq] fw=" FW_VERSION " n=%u PRE(IS=%02x TX=%04x) POST(IS=%02x TX=%04x) IE=%02x/%04x BR=%08x\n",
               jl_irq_n,
               pre_is, pre_txis, s_post_is, s_post_txis,
               (unsigned)JL_SIE_B(0x1600BUL), (unsigned)JL_SIE_H(0x16006UL),
               (unsigned)(*(volatile uint32_t *)0x16800UL));
    }
    __asm__ volatile("ssync");
    USBD_IRQHandler(0);
    __asm__ volatile("csync");

    s_post_is   = JL_SIE_B(0x1600AUL);
    s_post_txis = JL_SIE_H(0x16002UL);
}

void usb_dc_low_level_init(uint8_t busid)
{
    usb_g_sie_init(JL82_HUSB_ID);   /* PHY/桥接器上电(闭源库, id=1 走 HUSB 分支) */
    usb_iomode(JL82_HUSB_ID, 0);    /* DP/DM 切到 USB 功能(方向同 usb_phy_resume) */
    /* 对齐闭源 usb_device_init 序列: 清全部中断使能并使能 SIE(设备模式)。
     * 诊断数据表明跳过它之后 SIE 中断行为异常(状态位卡死引发风暴)。 */
    usb_slave_init(JL82_HUSB_ID);

    /* JL 中断: 处理函数表共享, 使能位按核独立。必须先清双核使能,
     * 否则 CPU1 若残留使能位会与 CPU0 同时进入本 ISR —— 两核并发跑
     * CherryUSB DCD 状态机会写坏端点上下文导致跳飞(实测崩在 0x1e00000)。 */
    unrequest_irq(IRQ_USB1_CTRL_IDX, 0);
    unrequest_irq(IRQ_USB1_CTRL_IDX, 1);
    request_irq(IRQ_USB1_CTRL_IDX, 3, usb1_cherryusb_isr, 0);
}

void usb_dc_low_level_deinit(uint8_t busid)
{
    unrequest_irq(IRQ_USB1_CTRL_IDX, 0);
    usb_sie_disable(JL82_HUSB_ID);
}

void usbd_musb_delay_ms(uint8_t ms)
{
    os_time_dly(ms);                /* JL tick = 10ms, 精度足够 */
}
