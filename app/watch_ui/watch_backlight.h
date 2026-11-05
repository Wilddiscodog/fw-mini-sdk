#ifndef WATCH_BACKLIGHT_H
#define WATCH_BACKLIGHT_H

#include "generic/typedef.h"

void watch_backlight_init(void);
/* percent: 1..100；关机可用 0 关背光；不写 VM */
void watch_backlight_set(u8 percent);
void watch_backlight_persist(void);
u8 watch_backlight_get(void);
void watch_backlight_off(void);
void watch_backlight_restore(void);

#endif
