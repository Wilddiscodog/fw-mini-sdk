/*
 * CherryUSB 主机栈 JL WL82 (AC791x) 粘层 —— USB1(HUSB, 高速)
 *
 * 与设备端 usb_glue_jl82.c 互斥使用（两控制器共用一组 D+/D-，
 * 由 app_config.h 的 MINI_USB_MODE 选择角色）。
 *
 * 注意：主机模式需要给 USB 座子供电 VBUS(5V)，硬件上需确认供电回路。
 */
#include "typedef.h"
#include "asm/usb.h"
#include "asm/hwi.h"
#include "asm/irq.h"
#include "os/os_api.h"
#include "fw_version.h"
#include "usbh_core.h"

#define JL82_HUSB_ID        1
#define JL82_HUSB_SIE_BASE  0x16000UL

extern void USBH_IRQHandler(uint8_t busid);

___interrupt
static void usb1_host_isr(void)
{
    uint32_t cpu;

    __asm__ volatile("%0 = cnum" : "=r"(cpu));
    if (cpu != 0) {
        return; /* 双核并发进主机 ISR 同样致命, 直接退出 */
    }

    __asm__ volatile("ssync");
    extern volatile uint32_t g_hc_irq_cnt;
    g_hc_irq_cnt++;
    USBH_IRQHandler(0);
    __asm__ volatile("csync");
}

___interrupt
static void usb1_sof_isr(void)
{
    /* SOF 帧中断: 清桥接器 SOF 挂起位(bit6 HUSB_CLR_SOF_PND) */
    *(volatile uint32_t *)0x16800UL |= 0x40;
}

void usb_hc_low_level_init(struct usbh_bus *bus)
{
    (void)bus;
    usb_h_sie_init(JL82_HUSB_ID);   /* HUSB 主机角色 PHY/桥接器上电(闭源库) */
    usb_iomode(JL82_HUSB_ID, 0);

    /* 关键: 必须显式进入主机角色(DEVCTL = SESSION|HOST = 0x03, 与闭源
     * usb_h_chirp_and_reset 同款写法)。只设 SESSION 时核心不产生 SOF,
     * U盘复位后 3ms 即挂起, 枚举控制传输超时(-USB_ERR_TIMEOUT/-14)。 */
    *(volatile uint8_t *)0x16060UL = 0x03;   /* DEVCTL: SESSION | HOST */

    /* 关键: 使能 SOF 帧时钟引擎(桥接器 bit10 HUSB_SOFIE, 与闭源
     * usb_sofie_enable(id!=0) 同款)。主机角色由自己产生 SOF;
     * 不开启则 FRAME 计数不走、设备挂起、一切传输静默无响应。 */
    *(volatile uint32_t *)0x16800UL |= 0x400;

    /* SOF 挂起清除走独立中断(IRQ_USB1_SOF_IDX=53), 不清会挂起累积 */
    unrequest_irq(IRQ_USB1_SOF_IDX, 0);
    unrequest_irq(IRQ_USB1_SOF_IDX, 1);
    request_irq(IRQ_USB1_SOF_IDX, 3, usb1_sof_isr, 0);

    /* 中断按核使能独立: 先清双核, 再只开 CPU0 */
    unrequest_irq(IRQ_USB1_CTRL_IDX, 0);
    unrequest_irq(IRQ_USB1_CTRL_IDX, 1);
    request_irq(IRQ_USB1_CTRL_IDX, 3, usb1_host_isr, 0);

    printf("[usbh] hc low level init ok, fw=%s\n", FW_VERSION);
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
    (void)bus;
    unrequest_irq(IRQ_USB1_CTRL_IDX, 0);
    usb_sie_disable(JL82_HUSB_ID);
}
