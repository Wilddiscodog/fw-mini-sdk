#ifndef WATCH_POWER_H
#define WATCH_POWER_H

#include "generic/typedef.h"
#include "lvgl.h"

void watch_power_init(void);
void watch_power_boot_anim(void);

u8 watch_power_is_on(void);
u8 watch_power_input_enabled(void);

/* 真关机: LVGL 内只准备; lcd_test 轮询 take 后再 sys_power_poweroff */
void watch_power_off(void);
u8 watch_power_take_off_request(void);

/* 短按息屏/亮屏(非关机); 长按真关机 */
void watch_power_key_click(void);
void watch_power_key_long(void);
void watch_power_poll(lv_timer_t *timer);

#endif
