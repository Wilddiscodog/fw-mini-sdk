#include "app_config.h"
#include "system/includes.h"
#include "asm/gpio.h"
#include "asm/adc_api.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_battery.h"
#include "watch_style.h"

/* 4.2V 锂电线性估算: 3.50V→0%, 4.20V→100% */
#define BAT_MV_EMPTY    3500
#define BAT_MV_FULL     4200

#define PIN_CHG_FULL    IO_PORTA_00
#define PIN_USB_DET     IO_PORTA_08

static lv_obj_t *s_lbl;
static lv_timer_t *s_tm;
static u16 s_mv;
static u8 s_pct;
static u8 s_usb;
static u8 s_full;

static void gpio_in_ext(u32 pin)
{
	gpio_latch_en(pin, 0);
	gpio_set_die(pin, 1);
	gpio_set_direction(pin, 1);
	gpio_set_pull_up(pin, 0);
	gpio_set_pull_down(pin, 0);
}

static void battery_sample(void)
{
	u32 v;

	/* 片内 VBAT 通道已是 1/4 分压采样, *4 得电池 mV */
	v = adc_get_voltage(AD_CH_VBAT) * 4;
	if (v < 2000 || v > 5000) {
		/* 异常读数时保持上次 */
	} else {
		s_mv = (u16)v;
	}

	if (s_mv <= BAT_MV_EMPTY) {
		s_pct = 0;
	} else if (s_mv >= BAT_MV_FULL) {
		s_pct = 100;
	} else {
		s_pct = (u8)(((u32)s_mv - BAT_MV_EMPTY) * 100 /
			     (BAT_MV_FULL - BAT_MV_EMPTY));
	}

	s_usb = gpio_read(PIN_USB_DET) ? 1 : 0;
	/* CHRG 开漏: 0=充电中, 1=充满或未充 */
	s_full = (s_usb && gpio_read(PIN_CHG_FULL)) ? 1 : 0;
}

static void battery_ui_refresh(lv_timer_t *t)
{
	char buf[40];
	u16 mv;
	(void)t;

	battery_sample();
	mv = s_mv;

	if (s_lbl) {
		if (s_usb && s_full) {
			lv_snprintf(buf, sizeof(buf), "%d.%02dV 满",
				    mv / 1000, (mv % 1000) / 10);
		} else if (s_usb) {
			lv_snprintf(buf, sizeof(buf), "%d.%02dV 充",
				    mv / 1000, (mv % 1000) / 10);
		} else {
			lv_snprintf(buf, sizeof(buf), "%d.%02dV",
				    mv / 1000, (mv % 1000) / 10);
		}
		lv_label_set_text(s_lbl, buf);
	}
}

void watch_battery_init(void)
{
	gpio_in_ext(PIN_CHG_FULL);
	gpio_in_ext(PIN_USB_DET);

	battery_sample();

	if (!s_lbl) {
		s_lbl = lv_label_create(lv_layer_top());
		lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_14, 0);
		lv_obj_set_style_text_color(s_lbl, lv_color_hex(WATCH_COL_TEXT), 0);
		lv_obj_set_style_bg_opa(s_lbl, LV_OPA_TRANSP, 0);
		lv_obj_align(s_lbl, LV_ALIGN_TOP_MID, 0, 4);
		lv_obj_add_flag(s_lbl, LV_OBJ_FLAG_FLOATING);
	}

	if (s_tm) {
		lv_timer_del(s_tm);
	}
	s_tm = lv_timer_create(battery_ui_refresh, 500, NULL);
	battery_ui_refresh(NULL);
}

u16 watch_battery_voltage_mv(void)
{
	return s_mv;
}

u8 watch_battery_percent(void)
{
	return s_pct;
}

u8 watch_battery_usb_in(void)
{
	return s_usb;
}

u8 watch_battery_charge_full(void)
{
	return s_full;
}

u8 watch_battery_charging(void)
{
	return (s_usb && !gpio_read(PIN_CHG_FULL)) ? 1 : 0;
}
