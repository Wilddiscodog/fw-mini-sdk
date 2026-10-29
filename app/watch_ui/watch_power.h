#ifndef WATCH_POWER_H
#define WATCH_POWER_H

#include "generic/typedef.h"
#include "lvgl.h"

void watch_power_init(void);
/* 开机动画(UI 建好后调用) */
void watch_power_boot_anim(void);

u8 watch_power_is_on(void);
u8 watch_power_input_enabled(void);

/* 关机动画 → P33 真关机 (不可再「软开机」) */
void watch_power_off(void);

/* 按键任务置位; LVGL 定时器 poll 后播关机动画 */
void watch_power_key_long(void);
void watch_power_poll(lv_timer_t *timer);

#endif
