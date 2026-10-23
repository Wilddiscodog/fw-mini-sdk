#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "device/iic.h"
#include "asm/includes.h"
#include "asm/pwm.h"
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#include "ui_api.h"
#include "asm/emi.h"
#endif

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
	.mode   = SPI_STD_MODE,	.port   = 'C',
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

/*============================ LCD：EMI 8080-8bit 并口 (ili9341 参考驱动) ========
 * 数据线 D0~D7 为 EMI 专用引脚映射（见芯片手册 EMI 章节/参考原理图），
 * RS/BL/TE 为普通 GPIO，可按硬件调整。 */
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
static const struct emi_platform_data emi_data = {
	.bits_mode      = EMI_8BITS_MODE,       /* 8 位 8080 并口 */
	.baudrate       = EMI_BAUD_DIV8,        /* clock = HSB/(baud+1) */
	.colection      = EMI_FALLING_COLT,     /* 下降沿采样 */
	.time_out       = 1 * 1000,             /* 写超时 ms */
	.th             = EMI_TWIDTH_NO_HALF,
	.ts             = 0,
	.tw             = EMI_BAUD_DIV4,   /* (baud+1)/2 档, EMI_BAUD 是枚举类型名不可作值 */
	.data_bit_en    = 0,
};

static const struct ui_lcd_platform_data lcd_pdata = {
	.rst_pin        = -1,            /* 复位脚: -1=由硬件上电复位 */
	.cs_pin         = -1,            /* 8080 常驻选通 */
	.rs_pin         = IO_PORTC_09,   /* 命令/数据切换 */
	.bl_pin         = IO_PORTB_08,   /* 背光使能 */
	.te_pin         = IO_PORTC_00,   /* 撕裂同步(未用可 -1) */
	.touch_int_pin  = -1,
	.touch_reset_pin= -1,
	.lcd_if         = LCD_EMI,       /* 接口类型: EMI(8080) / PAP / SPI */
};

const struct ui_devices_cfg ui_cfg_data = {
	.type = TFT_LCD,
	.private_data = (void *)&lcd_pdata,
};
#endif

/*============================ WiFi RF 校准表 (MINI_WIFI_SUPPORT) =================
 * 原厂 DevKitBoard 同款默认值; 量产设备应换成本板 MP 校准数据 */
#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
#include "wifi/wifi_connect.h"
const struct wifi_calibration_param wifi_calibration_param = {
    .xosc_l     = 0xb,// 调节左晶振电容
    .xosc_r     = 0xb,// 调节右晶振电容
    .pa_trim_data  ={1, 7, 4, 7, 11, 1, 7},// 根据MP测试生成PA TRIM值
	.mcs_dgain     ={
		45,//11B_1M
	  	45,//11B_2.2M
	  	45,//11B_5.5M
		42,//11B_11M

		60,//11G_6M
		60,//11G_9M
		75,//11G_12M
		70,//11G_18M
		62,//11G_24M
		52,//11G_36M
		50,//11G_48M
		38,//11G_54M

		62,//11N_MCS0
		80,//11N_MCS1
		70,//11N_MCS2
		62,//11N_MCS3
		50,//11N_MCS4
		48,//11N_MCS5
		40,//11N_MCS6
		36,//11N_MCS7
	}
};
#endif

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

/*============================ RTC：内部时钟 (MINI_RTC_SUPPORT) ================== */
#if (defined MINI_RTC_SUPPORT) && MINI_RTC_SUPPORT
const struct rtc_init_config rtc_init_data = {
	.rtc_clk_sel   = RTC_CLK_SEL_INSIDE,
	.rtc_power_sel = RTCVDD_SUPPLY_OUTSIDE,   /* TODO: 有 RTC 电池时按硬件改 */
};
#endif

/*============================ SD0：A 口 1 线 (MINI_SD_SUPPORT) ==================
 * 引脚组/检卡方式为 DevKitBoard 默认, **必须按实际硬件核对** */
#if (defined MINI_SD_SUPPORT) && MINI_SD_SUPPORT
SD0_PLATFORM_DATA_BEGIN(sd0_data)
	.port       = 'A',              /* TODO: 按硬件改 SD 引脚组 */
	.priority   = 3,
	.data_width = 1,                /* 1 线模式 */
	.speed      = 20000000,         /* 20MHz */
	.detect_mode = SD_CMD_DECT,     /* CMD 检卡 */
	.detect_func = NULL,
SD0_PLATFORM_DATA_END();
#endif

/*============================ GPCNT：脉冲计数 (MINI_GPCNT_SUPPORT) ============== */
#if (defined MINI_GPCNT_SUPPORT) && MINI_GPCNT_SUPPORT
const struct gpcnt_platform_data gpcnt_data = {
	.gpcnt_gpio    = IO_PORTC_07,   /* TODO: 按硬件改 */
	.gss_clk_source = GPCNT_PLL_CLK,/* 480M */
	.ch_source     = GPCNT_INPUT_CHANNEL4,
	.cycle         = CYCLE_15,
};
#endif

REGISTER_DEVICES(device_table) = {
	{"uart0", &uart_dev_ops, (void *)&uart0_data },
	{"uart1", &uart_dev_ops, (void *)&uart1_data },
	{"spi2",  &spi_dev_ops,  (void *)&spi2_data },
	{"iic0",  &iic_dev_ops,  (void *)&sw_iic0_data },
#if (defined MINI_PWM_SUPPORT) && MINI_PWM_SUPPORT
	{"pwm1",  &pwm_dev_ops,  (void *)&pwm_data1 },
#endif
#if (defined MINI_RTC_SUPPORT) && MINI_RTC_SUPPORT
	{"rtc",   &rtc_dev_ops,  NULL },
#endif
#if (defined MINI_SD_SUPPORT) && MINI_SD_SUPPORT
	{"sd0",   &sd_dev_ops,   (void *)&sd0_data },
#endif
#if (defined MINI_GPCNT_SUPPORT) && MINI_GPCNT_SUPPORT
	{"gpcnt", &gpcnt_dev_ops,(void *)&gpcnt_data },
#endif
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
	{"emi",   &emi_dev_ops,  (void *)&emi_data },
#endif
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
