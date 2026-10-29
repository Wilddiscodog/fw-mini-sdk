#ifndef WATCH_DROPDOWN_H
#define WATCH_DROPDOWN_H

#include "lvgl.h"

lv_obj_t *watch_dropdown_create(lv_obj_t *parent);
void watch_dropdown_show(void);
void watch_dropdown_hide(void);
u8 watch_dropdown_is_open(void);

#endif
