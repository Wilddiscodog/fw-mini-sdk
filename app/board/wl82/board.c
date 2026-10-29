#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "device/iic.h"
#include "asm/includes.h"
#include "asm/pwm.h"
#include "asm/power_interface.h"
#include "asm/system_reset_reason.h"
#include "key/adkey.h"
#include "sys_power.h"
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#include "ui_api.h"
#include "asm/emi.h"
#endif
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#include "asm/dac.h"
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

/* LCD 背光 PWM: PA6 + TIMER3 OPCH3 (与 pwm1 通道分离) */
PWM_PLATFORM_DATA_BEGIN(pwm_data_bl)
	.port    = IO_PORTA_06,
	.pwm_ch  = PWM_TIMER3_OPCH3,
	.freq    = 2000,
	.duty    = 80,
	.point_bit = 0,
PWM_PLATFORM_DATA_END();

/*============================ LCD：EMI 8080-8bit 并口 (JD9853, xiaolu-R2) =======
 * 引脚表来自设备原理图 zc-p25136_c0682-v2-3-1210.pdf 第2页(原始路径
 * J:\xiaolu-r2\新建文件夹\, 详见 doc/屏幕触摸移植记录.md):
 *   PC1~PC8  = LCD_D0~D7   芯片 LCD 复用脚, dev_open("emi") 时由 EMI 控制器配置
 *   PC10     = LCD_WR      同上(EMI 硬件)
 *   PC9      = LCD_CS      软件 GPIO(EMI 硬件不输出 CS; cs_pin 必须配置,
 *                          悬空恒高则面板忽略全部写 → 背光亮但黑屏)
 *   PB5      = LCD_RS      GPIO, 命令/数据切换(lcd_rs_pinstate)
 *   PB6      = LCD_RD      GPIO 拉高(芯片无 LCD_RD 复用, EMI 只写; 见 lcd_jd9853.c)
 *   PB7      = LCD_RESET   GPIO
 *   PC0      = LCD_TE      GPIO 输入(撕裂同步)
 *   PA6      = LED_PWM     背光使能(原理图网络名 PA6_LED_PWM, 可改 PWM 调光) */
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
static const struct emi_platform_data emi_data = {
	.bits_mode      = EMI_8BITS_MODE,       /* 8 位 8080 并口 */
	.baudrate       = EMI_BAUD_DIV8,        /* clock = HSB/(baud+1) */
	.colection      = EMI_FALLING_COLT,     /* 下降沿采样 */
	.time_out       = 1 * 1000,             /* 写超时 ms */
	.th             = EMI_TWIDTH_NO_HALF,
	.ts             = 0,
	.tw             = EMI_BAUD_DIV8,   /* WR 脉宽(HSB 周期数): 对齐官方 DevKitBoard
	                                       (tw==baudrate 同为 DIV8)。曾用 DIV4(3 周期
	                                       ≈25ns@120M)过快 → 面板采样错位花屏 */
	.data_bit_en    = 0,
};

static const struct ui_lcd_platform_data lcd_pdata = {
	.rst_pin        = IO_PORTB_07,   /* LCD_RESET: PB7 */
	/* LCD_CS: PC9。原注释称"EMI 硬件选通"不成立: 反汇编 emi_open 仅配置
	 * PC1~PC8+PC10(WR), 无 PC9; 官方 DevKitBoard EMI 参考里 PC9 是普通 GPIO。
	 * CS 悬空恒高 → 面板忽略全部写(背光亮但黑屏), 必须软件 GPIO 驱动 */
	.cs_pin         = IO_PORTC_09,
	.rs_pin         = IO_PORTB_05,   /* LCD_RS: PB5 */
	.bl_pin         = IO_PORTA_06,   /* 背光 LED_PWM: PA6 */
	.te_pin         = IO_PORTC_00,   /* LCD_TE: PC0 */
	.touch_int_pin  = -1,            /* 触摸在 PH3, 由 touch_cst816.c 自管 */
	.touch_reset_pin= -1,            /* 触摸 RST 在 PH4, 同上 */
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

/* 软关机/休眠唤醒: ADKEY 同脚 PB01 (按下拉低 → 下降沿唤醒) */
#define PORT_WAKEUP_IO			IO_PORTB_01
#define PORT_WAKEUP_NUM			(PORT_WAKEUP_IO / IO_GROUP_NUM)

static const struct port_wakeup port0 = {
	.edge       = FALLING_EDGE,
	.attribute  = BLUETOOTH_RESUME,
	.iomap      = PORT_WAKEUP_IO,
	.low_power  = POWER_SLEEP_WAKEUP | POWER_OFF_WAKEUP,
};

static const struct long_press lpres_port = {
	.enable     = FALSE,
	.use_sec4   = TRUE,
	.edge       = FALLING_EDGE,
	.iomap      = PORT_WAKEUP_IO,
};

static const struct sub_wakeup sub_wkup = {
	.attribute  = BLUETOOTH_RESUME,
};

static const struct charge_wakeup charge_wkup = {
	.attribute  = BLUETOOTH_RESUME,
};

static const struct wakeup_param wk_param = {
	.port[PORT_WAKEUP_NUM] = &port0,
	.sub = &sub_wkup,
	.charge = &charge_wkup,
	.lpres = &lpres_port,
};

extern const struct adkey_platform_data adkey_data;

#ifdef CONFIG_PRESS_LONG_KEY_POWERON
/* ADKEY 电源键按下时 ADC 很低(实测≈0); 阈值 50 对齐官方 */
static unsigned int read_power_key(int dly)
{
	gpio_latch_en(adkey_data.adkey_pin, 0);
	gpio_direction_input(adkey_data.adkey_pin);
	gpio_set_die(adkey_data.adkey_pin, 0);
	gpio_set_pull_up(adkey_data.adkey_pin, 0);
	gpio_set_pull_down(adkey_data.adkey_pin, 0);

	if (dly) {
		delay_us(3000);
	}

	JL_ADC->CON = (0xf << 12) | (adkey_data.ad_channel << 8) | (1 << 6) | (1 << 4) | (1 << 3) | (6 << 0);
	while (!(JL_ADC->CON & BIT(7)));
	return (JL_ADC->RES < 50);
}

void sys_power_poweroff_wait_powerkey_up(void)
{
	JL_TIMER1->CON = 0;
	JL_TIMER1->CON = 0;
	JL_TIMER1->CON = 0;
	JL_ADC->CON = 0;
	JL_ADC->CON = 0;
	JL_ADC->CON = 0;
	while (read_power_key(1));
}
#else
void sys_power_poweroff_wait_powerkey_up(void)
{
}
#endif

void sys_power_poweroff(void)
{
	printf("[power] soft poweroff enter\n");
	sys_power_poweroff_wait_powerkey_up();
	power_set_soft_poweroff(0);
}

/* 进软关机前 IO 高阻(除 SDRAM 相关 PD/PE/PF/PG) */
static void board_set_soft_poweroff(void)
{
	u32 IO_PORT;
	JL_PORT_FLASH_TypeDef *gpio[] = {JL_PORTA, JL_PORTB, JL_PORTC, JL_PORTD, JL_PORTE, JL_PORTF, JL_PORTG, JL_PORTH};
	u8 p, i;

	for (p = 0; p < 8; ++p) {
		if (gpio[p] == JL_PORTD || gpio[p] == JL_PORTE || gpio[p] == JL_PORTF || gpio[p] == JL_PORTG) {
			continue;
		}
		for (i = 0; i < 16; ++i) {
			IO_PORT = IO_PORTA_00 + p * 16 + i;
			gpio_set_pull_up(IO_PORT, 0);
			gpio_set_pull_down(IO_PORT, 0);
			gpio_set_direction(IO_PORT, 1);
			gpio_set_die(IO_PORT, 0);
			gpio_set_dieh(IO_PORT, 0);
			gpio_set_hd(IO_PORT, 0);
			gpio_set_hd1(IO_PORT, 0);
			gpio_latch_en(IO_PORT, 1);
		}
	}
}

static void sleep_exit_callback(u32 usec)
{
	(void)usec;
}

static void sleep_enter_callback(u8 step)
{
	if (step == 1) {
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
		dac_power_off();
#endif
	}
}

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

/*============================ 内置音频: MIC + DAC (MINI_AUDIO_SUPPORT) =========
 * 原理图接线(与芯片 pad 名对照):
 *   PH8=MIC1N, PH9=MIC1P  → 拾音(通话 MIC1 差分; 相对 ladc.h「PH8=P/PH9=N」板级对调)
 *   PH6=MIC3P, PH5=MIC3N  → 功放回采(与 ladc.h 一致)
 *   PH7 高电平 = 功放使能(pa_mute_value=0 → MUTE 拉低, 开声拉高)
 * 差分仍开 LADC_CH_MIC1_P_N|MIC3_P_N(两脚都进 ADC); MIC1 板级对调仅极性反相。 */
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#include "asm/audio.h"
#include "server/audio_dev.h"           /* audio_platform_data + audio_dev_ops */
static const struct dac_platform_data dac_data = {
    .sw_differ          = 0,
    .pa_auto_mute       = 1,
    .pa_mute_port       = IO_PORTH_07,
    .pa_mute_value      = 0,            /* MUTE=低; 播放/通话 unmute → PH7 高=功放开 */
    /* DAC 三参数对齐官方 demo_edr(音箱参考, 上板实测正常):
     * V1.4.0 误抄 DevKitBoard(综合视频演示板)的 differ=0/0x05/ch_num=4,
     * 4 数字通道消耗立体声数据速率失配 → A2DP 持续 ~27% 冲刷丢包(K/F 刷屏) */
    .differ_output      = 1,            /* 硬件差分输出模式 */
    .hw_channel         = 0x03,
    .ch_num             = 1,            /* 差分只需开一个通道 */
    .vcm_init_delay_ms  = 1000,
    .mute_delay_ms      = 200,
};
static const struct adc_platform_data adc_data = {
    /* MIC1 差分(PH8/PH9) + MIC3 差分(PH5/PH6); 通道号见 app_config */
    .mic_channel        = LADC_CH_MIC1_P_N | LADC_CH_MIC3_P_N,
    .mic_ch_num         = 2,
    .all_channel_open   = 1,
    .linein_channel     = 0,
    .linein_ch_num      = 0,
    .isel               = 2,
    .dump_num           = 480,
};
static const struct audio_pf_data audio_pf_d = {
    .adc_pf_data = &adc_data,
    .dac_pf_data = &dac_data,
    /* IIS/PLNK/SPDIF 未用: NULL, 库仅在选择对应 source 时才解引用 */
};
static const struct audio_platform_data audio_data = {
    .private_data = (void *)&audio_pf_d,
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
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
	{"audio", &audio_dev_ops, (void *)&audio_data },
#endif
	{"pwm1",  &pwm_dev_ops,  (void *)&pwm_data1 },
	{"pwm_bl",&pwm_dev_ops,  (void *)&pwm_data_bl },
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
    /* 官方 demo_edr 同款: DAC 模拟通道早期配置 + VCM 稳定等待
     * (差分模式按 ch_num 选 0x3/0xf, 非差分用 hw_channel) */
    dac_early_init(0, dac_data.differ_output ? (dac_data.ch_num > 1 ? 0xf : 0x3) : dac_data.hw_channel, 1000);
    devices_init();
}

static void board_power_init(void)
{
	power_init(&power_param);
	power_set_callback(TCFG_LOWPOWER_LOWPOWER_SEL, sleep_enter_callback,
			   sleep_exit_callback, board_set_soft_poweroff);
	power_keep_state(POWER_KEEP_RESET | POWER_KEEP_DACVDD);
	power_wakeup_init(&wk_param);

#ifdef CONFIG_PRESS_LONG_KEY_POWERON
	/* 端口唤醒后若电源键已松开 → 视为误触, 立刻再关机 */
	if (system_reset_reason_get() == SYS_RST_PORT_WKUP) {
		for (int i = 0; i < 500; i++) {
			if (0 == read_power_key(1)) {
				sys_power_poweroff();
				break;
			}
		}
	}
#endif
	extern void clean_wakeup_source_port(void);
	clean_wakeup_source_port();
}

__attribute__((weak)) void clean_wakeup_source_port(void)
{
}

void board_init()
{
	board_power_init();
}
