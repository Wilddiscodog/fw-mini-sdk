/* fw-mini-sdk 演示应用：GPIO / UART / SPI / USB / 时钟 + FreeRTOS 任务
 *
 * 现象（烧录后）：
 *  - PA6 LED 每 500ms 翻转          —— GPIO 输出
 *  - 调试口(PB2)每 2s 打印时钟树    —— 时钟控制 clk_get()
 *  - UART1(PB3=TX,PB4=RX)回显数据   —— UART 设备框架 dev_open/dev_read
 *  - 上电 5s 后读一次 W25Qxx ID(PA1~PA4) —— SPI 主机 dev_ioctl
 *  - USB1(高速)枚举为 CDC 串口      —— CherryUSB 设备栈(回显 + 定时上行)
 */
#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "asm/gpio.h"
#include "asm/clock.h"
#include "device/device.h"
#include "device/uart.h"
#include "asm/spi.h"
#include "device/iic.h"
#include "asm/pwm.h"
#include "asm/adc_api.h"
#include "fw_version.h"
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#include "lcd_drive.h"
#endif

/* CherryUSB 组合设备 2×CDC + MSC(RAM 盘) (高速口 USB1) */
/* 实现在 cdc_acm_msc_app.c, 声明见下方 usb_mode_task 前 */

#define CHERRYUSB_BUSID     0
#define JL82_HUSB_SIE_BASE  0x16000UL

/*============================ 中断列表 ============================ */
const struct irq_info irq_info_table[] = {
#ifdef CONFIG_IPMASK_ENABLE
    { IRQ_SOFT5_IDX,      6,   0    },
    { IRQ_SOFT4_IDX,      6,   1    },
#endif
#if CPU_CORE_NUM == 1
    { IRQ_SOFT5_IDX,      7,   0    },
    { IRQ_SOFT4_IDX,      7,   1    },
    { -2,     			-2,   -2   },
#endif
    { -1,     -1,   -1    },
};

static void heartbeat_task(void *p)
{
    static u32 n = 0;
    while (1) {
        os_time_dly(100);
    }
}

/*============================ 任务列表 ============================
 * 表项供 task_create()（OS 原生接口）查表用；
 * os_task_create() 显式传优先级/栈大小，不依赖此表。 */
const struct task_info task_info_table[] = {
    {"app_core",            15,     2048,	  1024 },
    {"sys_event",           29,     512,	   0 },
    {"systimer",            14,     256, 	   0 },
    {"sys_timer",            9,     512,	  128 },
    {"usb_server",          20,     1024,	  64  },
    {"usb_msd0",             1,     512,	  128 },
    {"usb_msd1",             1,     512,	  128 },
    /* BT 协议栈任务(官方 demo_DevKitBoard/demo_edr 同款; btctrler.a 按名 task_create)
     * 双核必须 "#C0" 前缀把 BT 任务钉在 CPU0 —— BT 控制器中断在 CPU0, 任务漂到
     * CPU1 会因跨核调度延迟丢 ACL 包(V1.4.0 漏抄此前缀, A2DP 丢包~30% 即此因);
     * 双核版 btencry 优先级 16、btstack 栈 1024, 与单核版(14/768)不同 */
#if CPU_CORE_NUM > 1
    {"#C0btencry",          16,     512,   128 },
    {"#C0btctrler",         19,     512,   384 },
    {"#C0btstack",          18,     1024,  384 },
#else
    {"btencry",             14,     512,	  128 },
    {"btctrler",            19,     512,	  384 },
    {"btstack",             18,     768,	  384 },
#endif
    {"bt_app",              12,     1024,   0   },
    {"wifi_app",            12,     2048,   0   },
    /* WiFi 协议栈任务(demo_wifi 同款, 库按名字 task_create, 缺一不可):
     * tasklet=收发主任务, RtmpMlmeTask=MLME 状态机, RtmpCmdQTask=命令队列,
     * wl_rx_irq_thread=RX 中断线程 */
    {"tasklet",             10,     1400,   0   },
    {"RtmpMlmeTask",        17,     700,    0   },
    {"RtmpCmdQTask",        17,     300,    0   },
    {"wl_rx_irq_thread",     5,     256,    0   },
    /* 双备份升级任务(update.a 按名 task_create; 缺失→返回-14→U盘/WiFi 升级首包 fw_fail) */
    {"update",              21,     512,    32  },
    {"dw_update",           10,     1024,   64  },
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
    /* 音频库任务(demo_edr + DevKitBoard/DNS; 库按名字 task_create, 缺表项会创建失败) */
    {"audio_server",        16,     512,    64  },
    {"audio_mix",           28,     512,    0   },
    {"audio_decoder",       30,     1024,   64  },
    {"audio_encoder",       12,     384,    64  },
    {"echo_deal",           11,     1024,   32  },
    {"aec_encoder",         13,     1024,   0   },
    {"dns_encoder",         13,     512,    0   },
    {"msbc_encoder",        13,     256,    0   },
    {"sbc_encoder",         13,     512,    0   },
    {"cvsd_encoder",        13,     512,    0   },
    {"vir_dev_task",        13,     256,    0   },
#endif
#ifdef CONFIG_NET_MUSIC_MODE_ENABLE
    /* DLNA/ai_server(demo_DevKitBoard 同款) */
    {"ai_server",           15,     1024,   64  },
#endif
    {0, 0, 0, 0, 0},
};

/*============================ GPIO：LED 闪烁 ============================
 * 注意: PA6 在 xiaolu-R2 硬件上是 LCD 背光(LED_PWM), 若启用 LCD 请勿再
 * 启用本任务; 需要闪烁指示灯时换到空闲 IO。 */
static void led_task(void *p)
{
    gpio_direction_output(IO_PORTA_06, 1);
    while (1) {
        gpio_direction_output(IO_PORTA_06, 0);
        os_time_dly(10);
        gpio_direction_output(IO_PORTA_06, 1);
        os_time_dly(10);
    }
}

/*============================ 时钟控制：周期打印时钟树 ============================ */
static void clock_task(void *p)
{
    while (1) {
        printf("[clk] sys=%dMHz sdram=%dMHz hsb=%dMHz lsb(timer)=%dMHz\n",
               clk_get("sys") / 1000000, clk_get("sdram") / 1000000,
               clk_get("hsb") / 1000000, clk_get("timer") / 1000000);
        os_time_dly(200);
    }
}

/*============================ UART1：回显 ============================ */
static u8 uart1_rx_buf[256] __attribute__((aligned(32))); /* 循环接收buf */
static void uart1_echo_task(void *p)
{
    u8 tmp[64];
    void *hdl = dev_open("uart1", NULL);
    if (!hdl) {
        printf("[uart1] open fail\n");
        while (1) { os_time_dly(1000); }
    }
    dev_ioctl(hdl, UART_SET_CIRCULAR_BUFF_ADDR, (int)uart1_rx_buf);
    dev_ioctl(hdl, UART_SET_CIRCULAR_BUFF_LENTH, sizeof(uart1_rx_buf));
    dev_ioctl(hdl, UART_SET_RECV_BLOCK, 1);
    dev_ioctl(hdl, UART_SET_RECV_TIMEOUT, 1000);
    dev_ioctl(hdl, UART_START, 0);
    printf("[uart1] echo ready @PB3(TX)/PB4(RX) 115200\n");

    while (1) {
        int len = dev_read(hdl, tmp, sizeof(tmp));
        if (len > 0) {
            dev_write(hdl, tmp, len);   /* 回显 */
        } else if (len == UART_CIRCULAR_BUFFER_WRITE_OVERLAY) {
            dev_ioctl(hdl, UART_FLUSH, 0);
        }
    }
}

/*============================ SPI2：读 W25Qxx JEDEC ID ============================ */
#define W25X_JEDEC_ID   0x9F
static void spi_cs(u8 sta)
{
    gpio_direction_output(IO_PORTA_01, sta);
}
static void spi_task(void *p)
{
    u8 id[3] = {0, 0, 0};

    spi_cs(1);
    gpio_set_hd(IO_PORTA_03, 1);  /* CLK 开强驱（杜邦线测试需要） */
    os_time_dly(100);

    void *hdl = dev_open("spi2", NULL);
    if (!hdl) {
        printf("[spi2] open fail\n");
        while (1) { os_time_dly(1000); }
    }

    spi_cs(0);
    dev_ioctl(hdl, IOCTL_SPI_SEND_BYTE, W25X_JEDEC_ID);
    for (u8 i = 0; i < 3; i++) {
        dev_ioctl(hdl, IOCTL_SPI_READ_BYTE, (u32)&id[i]);
    }
    spi_cs(1);

    printf("[spi2] W25Qxx JEDEC ID = %02X %02X %02X%s\n",
           id[0], id[1], id[2],
           (id[0] == 0xFF || id[0] == 0x00) ? " (未检测到Flash, 检查接线)" : "");
    dev_close(hdl);

    while (1) { os_time_dly(10000); }  /* 只读一次；FreeRTOS 任务不允许返回 */
}

/*============================ IIC0：总线扫描 (PH0=SCL, PH1=SDA) ============== */
static void iic_scan_task(void *p)
{
    void *hdl = dev_open("iic0", NULL);
    if (!hdl) {
        printf("[iic0] open fail\n");
        while (1) { os_time_dly(10000); }
    }
    printf("[iic0] scan ready @PH0(SCL)/PH1(SDA)\n");
    while (1) {
        int found = 0;
        printf("[iic0] dev:");
        for (u8 addr = 0x08; addr < 0x78; addr++) {
            dev_ioctl(hdl, IIC_IOCTL_START, 0);
            if (dev_ioctl(hdl, IIC_IOCTL_TX_WITH_START_BIT, addr << 1) == 0) {
                printf(" 0x%02x", addr);
                found++;
            }
            dev_ioctl(hdl, IIC_IOCTL_STOP, 0);
        }
        printf("%s\n", found ? "" : " (none)");
        os_time_dly(1000);   /* 10s 扫一轮 */
    }
}

/*============================ PWM1：PA5 呼吸灯 (1kHz) ======================== */
static void pwm_breath_task(void *p)
{
    struct pwm_platform_data pwm = {0};
    void *hdl = dev_open("pwm1", &pwm);
    int duty = 0, dir = 1;

    if (!hdl) {
        printf("[pwm1] open fail\n");
        while (1) { os_time_dly(10000); }
    }
    printf("[pwm1] breath ready @PA5 %dHz\n", pwm.freq);
    while (1) {
        duty += dir * 4;
        if (duty >= 100) { duty = 100; dir = -1; }
        if (duty <= 0)   { duty = 0;   dir = 1; }
        pwm.pwm_ch = PWM_TIMER2_OPCH2;
        pwm.duty = duty;
        dev_write(hdl, &pwm, 0);
        os_time_dly(10);     /* 100ms 一步, ~5s 一个呼吸周期 */
    }
}

/*============================ ADC：PB01 电压采样 ============================= */
static void adc_task(void *p)
{
    adc_init();
    gpio_set_die(IO_PORTA_08, 0);       /* 模拟功能 */
    gpio_set_direction(IO_PORTA_08, 1);
    gpio_set_pull_up(IO_PORTA_08, 0);
    gpio_set_pull_down(IO_PORTA_08, 0);
    adc_add_sample_ch(AD_CH_PA08);

    while (1) {
        printf("[adc] PA08 raw=%d %dmv\n",
               (int)adc_get_value(AD_CH_PA08), (int)adc_get_voltage(AD_CH_PA08));
        os_time_dly(200);   /* 2s */
    }
}

/*============================ 硬件定时器：TIMER5 1Hz 中断 =====================
 * PWM_TIMER2_OPCH2 占用 TIMER2, 故选 TIMER5; CON[7:4] 分频字段为错位编码 */
static JL_TIMER_TypeDef *const TMR5 = JL_TIMER5;
static volatile u32 t5_ticks = 0;
___interrupt
static void timer5_isr(void)
{
    if (TMR5->CON & BIT(15)) {
        TMR5->CON |= BIT(14);           /* 清 pnd */
        t5_ticks++;
        // printf("[t5] 1s tick %u\n", t5_ticks);
    }
}
static void hw_timer_task(void *p)
{
    static const u32 psc_tab[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
                                    2048, 4096, 8192, 16384, 32768};
    static const u8 psc_reg[16] = {0, 4, 1, 5, 2, 6, 3, 7, 8, 12, 9, 13, 10, 14, 11, 15};
    u32 clk = clk_get("timer");         /* lsb 40MHz */
    u8 i = 0;

    while (clk / psc_tab[i] > 65535) {
        i++;
    }
    request_irq(IRQ_TIMER5_IDX, 3, timer5_isr, 0);
    TMR5->CON = 0;
    TMR5->CNT = 0;
    TMR5->PRD = clk / psc_tab[i];       /* 40MHz/1024/39062 ≈ 1Hz */
    TMR5->CON |= (psc_reg[i] << 4);
    TMR5->CON |= BIT(0);                /* 启动 */

    while (1) { os_time_dly(10000); }   /* FreeRTOS 任务不允许返回 */
}

/*============================ LCD：EMI 8080 并口 ============================ */
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
#include "lvgl.h"
extern void lv_init(void);
extern void lv_port_disp_init(void);
extern void lv_port_indev_init(void);
extern void lv_demo_benchmark(void);

static void lv_tick_10ms_cb(void *p)
{
    extern void lv_tick_inc(u32 tick);
    lv_tick_inc(10);            /* sys_timer 粒度=OS tick(10ms), 心跳须与实
                                 * 际周期一致, 否则动画/跑分计时失真 */
}
#endif

static void lcd_test_task(void *p)
{
    /* 延迟点屏, 避免开机 EMI 刷屏干扰 USB 枚举 */
    os_time_dly(800);
    user_lcd_init();

    struct lcd_interface *lcd = lcd_get_hdl();
    u16 lcd_w = 0, lcd_h = 0;
    if (lcd == NULL || lcd->draw_lvgl == NULL) {
        printf("[lcd] no panel driver matched, park lcd_test\n");
        while (1) { os_time_dly(10000); }
    }
    {
        struct lcd_info li;
        memset(&li, 0, sizeof(li));
        if (lcd->get_screen_info) {
            lcd->get_screen_info(&li);
        }
        lcd_w = li.width;
        lcd_h = li.height;
        if (lcd_w == 0 || lcd_h == 0) {
            printf("[lcd] panel not ready (lcd_dev missing), park lcd_test\n");
            while (1) { os_time_dly(10000); }
        }
    }

#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
    /* LVGL benchmark(third/lvgl_port 移植层): disp/indev 在本任务内初始化
     * (user_lcd_init 幂等, disp_init 里重复调用是 no-op); tick 走 sys_timer */
    printf("[lcd] panel ready %dx%d, start LVGL benchmark\n", lcd_w, lcd_h);
    lv_init();
    sys_timer_add(NULL, lv_tick_10ms_cb, 10);
    lv_port_disp_init();
    lv_port_indev_init();
    lv_demo_benchmark();
    while (1) {
        lv_task_handler();
        os_time_dly(1);         /* 10ms: LVGL 建议 5-10ms 轮询一次 */
    }
#else
    printf("[lcd] panel ready, park (MINI_LVGL_SUPPORT=0)\n");
    while (1) { os_time_dly(10000); }
#endif
}
#endif

/*============================ USB：按 MINI_USB_MODE 选择角色 ================= */
#if (MINI_USB_MODE == MINI_USB_MODE_HOST)
extern void usbh_host_demo_start(void);
extern void usbh_host_poll(void);
static void usb_mode_task(void *p)
{
    os_time_dly(100);
    usbh_host_demo_start();     /* 单 host bus 上统一启动，自动分流 CDC/HID/MSC */

    /* 主机传输完成改为轮询模式(与闭源 usb_ctrl_transfer 轮询 CSR0 同款):
     * 主机角色下 EP0 完成中断不触发(实测), 轮询是可靠路径。 */
    while (1) {
        os_time_dly(1);         /* 10ms 轮询周期 */
        usbh_host_poll();       /* 窗口读挂起集, 有挂起才处理(读即清除) */
    }
}
#else
extern void msc_only_init(uint8_t busid, uintptr_t reg_base);
static void usb_mode_task(void *p)
{
    os_time_dly(100);    /* 等 boot 日志打完 */
#if MINI_USB_CDC_ENABLE
    printf("[usb] CherryUSB CDC(log) + MSC 3MB RAM disk composite\n");
#else
    printf("[usb] CherryUSB MSC-only 3MB RAM disk on EP1\n");
#endif
    msc_only_init(CHERRYUSB_BUSID, JL82_HUSB_SIE_BASE);

    while (1) {
        os_time_dly(10000);
    }
}
#endif

/*============================ 应用入口 ============================ */
void app_main()
{
#ifdef CONFIG_CXX_SUPPORT
    extern void cpp_run_init(void);   /* C++ 全局对象构造(必须最先调用) */
    extern int cxx_smoke_test(void);
    cpp_run_init();
#endif
    printf("\r\n\r\n ===== fw-mini-sdk %s build %s =====\r\n", FW_VERSION, __TIME__);
#ifdef CONFIG_CXX_SUPPORT
    cxx_smoke_test();
#endif
    printf("[clk] sys=%dMHz sdram=%dMHz hsb=%dMHz\r\n",
           clk_get("sys") / 1000000, clk_get("sdram") / 1000000, clk_get("hsb") / 1000000);

    // os_task_create(led_task,         NULL, 10, 512,  0, "led_task");
    // os_task_create(clock_task,       NULL,  9, 512,  0, "clock_task");
    os_task_create(uart1_echo_task,  NULL,  8, 1024, 0, "uart1_echo");
    /* WiFi/蓝牙外设(原厂栈移植): 蓝牙开机自启, 与 USB 角色无关。
     * 放在任务创建末尾: 即使 BT 启动路径挂死也不影响前面任务。 */
    // os_task_create(spi_task,         NULL,  8, 1024, 0, "spi_test");
    // os_task_create(iic_scan_task,    NULL,  8, 1024, 0, "iic_scan");
    // os_task_create(pwm_breath_task,  NULL,  8, 512,  0, "pwm_breath");
    // os_task_create(adc_task,         NULL,  8, 512,  0, "adc_task");
    os_task_create(hw_timer_task,    NULL,  8, 512,  0, "hw_timer");
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
    os_task_create(lcd_test_task,    NULL, 22, 2048, 0, "lcd_test");
#endif
    /* USB 设备模式: MSC 4MB RAM 盘(含虚拟U盘自动升级看门, 见 cdc_acm_msc_app.c) */
    // os_task_create(usb_mode_task,    NULL,  8, 2048, 0, "usb_mode");
    os_task_create(heartbeat_task,   NULL, 30,  512, 0, "heartbeat");
    /* 按键框架先就绪; BT/WiFi 延后自启(避开 USB 枚举窗, 见 bt_wifi_start_deferred) */
    extern void key_early_init(void);
    extern void bt_wifi_start_deferred(void);
    key_early_init();
    bt_wifi_start_deferred();
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT && !MINI_BT_SPEAKER_EN
    /* 音箱模式走 A2DP/HFP, 不再跑 mic→DAC 回环自检(抢 audio 设备) */
    extern void audio_demo_start(void);
    // audio_demo_start();
#endif
}
