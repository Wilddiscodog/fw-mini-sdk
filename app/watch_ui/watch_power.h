#ifndef WATCH_POWER_H
#define WATCH_POWER_H

#include "generic/typedef.h"
#include "lvgl.h"

void watch_power_init(void);
void watch_power_boot_anim(void);

u8 watch_power_is_on(void);
u8 watch_power_input_enabled(void);

/* LVGL 内只准备关机; lcd_test 轮询 take 后再 sys_power_poweroff */
void watch_power_off(void);
u8 watch_power_take_off_request(void);

void watch_power_key_long(void);
void watch_power_poll(lv_timer_t *timer);

#endif
