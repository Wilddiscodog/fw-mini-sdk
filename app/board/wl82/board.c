#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "device/iic.h"
#include "asm/includes.h"
#include "asm/pwm.h"

// *INDENT-OFF*

/*============================ UART0：调试打印口 (PB2, 1M) ============================ */
UART0_PLATFORM_DATA_BEGIN(uart0_data)
	.baudrate = 1000000,
	.port = PORT_REMAP,
	.output_channel = OUTPUT_CHANNEL0,
	.tx_pin = IO_PORTB_02,
	.rx_pin = -1,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
	.flags = UART_DEBUG,
UART0_PLATFORM_DATA_END();

/*============================ UART1：通用收发口 (PB3=TX, PB4=RX, 115200) ============ */
UART1_PLATFORM_DATA_BEGIN(uart1_data)
	.baudrate = 115200,
	.port = PORT_REMAP,
	.output_channel = OUTPUT_CHANNEL1,
	.input_channel  = INPUT_CHANNEL1,
	.tx_pin = IO_PORTB_03,
	.rx_pin = IO_PORTB_04,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
UART1_PLATFORM_DATA_END();

/*============================ SPI2：主机口 (C组: PA1=CS PA2=DO PA3=CLK PA4=DI) ======
 * W25Qxx 等标准 SPI Flash 接法；CS 由软件控制（见 app_main.c 的 spi 任务） */
SPI2_PLATFORM_DATA_BEGIN(spi2_data)
	.clk    = 2000000,
	.mode   = SPI_STD_MODE,
	.port   = 'C',
	.attr   = SPI_SCLK_L_UPL_SMPH | SPI_BIDIR_MODE,
SPI2_PLATFORM_DATA_END();

/*============================ IIC0：软件 I2C (PH0=SCL, PH1=SDA) ================== */
SW_IIC_PLATFORM_DATA_BEGIN(sw_iic0_data)
	.clk_pin = IO_PORTH_00,
	.dat_pin = IO_PORTH_01,
	.sw_iic_delay = 50,   /* ~100kHz, 调大变慢 */
SW_IIC_PLATFORM_DATA_END();

/*============================ PWM1：任意IO映射 (PA5, TIMER2+输出通道2) ==========
 * 呼吸灯/舵机/蜂鸣器等; 占用 output_channel2, 勿与 UART 重映射冲突(uart0/1用ch0/1) */
PWM_PLATFORM_DATA_BEGIN(pwm_data1)
	.port    = IO_PORTA_05,
	.pwm_ch  = PWM_TIMER2_OPCH2,
	.freq    = 1000,
	.duty    = 50,
	.point_bit = 0,
PWM_PLATFORM_DATA_END();

/*============================ 电源 ============================ */
static const struct low_power_param power_param = {
    .config         = TCFG_LOWPOWER_LOWPOWER_SEL,
    .btosc_disable  = TCFG_LOWPOWER_BTOSC_DISABLE,
    .vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,
    .vddiow_lev     = TCFG_LOWPOWER_VDDIOW_LEVEL,
	.vdc14_dcdc 	= TRUE,
    .vdc14_lev		= VDC14_VOL_SEL_LEVEL,
    .sysvdd_lev		= SYSVDD_VOL_SEL_LEVEL,
    .vlvd_enable	= TRUE,
    .vlvd_value		= VLVD_SEL_25V,
};

REGISTER_DEVICES(device_table) = {
	{"uart0", &uart_dev_ops, (void *)&uart0_data },
	{"uart1", &uart_dev_ops, (void *)&uart1_data },
	{"spi2",  &spi_dev_ops,  (void *)&spi2_data },
	{"iic0",  &iic_dev_ops,  (void *)&sw_iic0_data },
	{"pwm1",  &pwm_dev_ops,  (void *)&pwm_data1 },
	{"rtc",   &rtc_dev_ops,  NULL},
};
/* 注：不注册 "otg"——AC7912BB 两 USB 控制器共用 D+/D-，
 * OTG 双口检测会误拆 usb_start() 建立的从机会话（详见调试记录）。 */

#ifdef CONFIG_DEBUG_ENABLE
void debug_uart_init()
{
	uart_init(&uart0_data);
}
#endif

void board_early_init()
{
    devices_init();
}

static void board_power_init(void)
{
    power_init(&power_param);
}

void board_init()
{
	board_power_init();
}
