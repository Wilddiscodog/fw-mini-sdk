#ifndef WATCH_CLOCK_H
#define WATCH_CLOCK_H

#include "generic/typedef.h"
#include "lvgl.h"

lv_obj_t *watch_clock_create(lv_obj_t *parent);

/* 供状态栏: 取当前时:分(RTC 优先) */
void watch_clock_get_hm(u8 *h, u8 *m);

#endif
