#ifndef WATCH_HOME_H
#define WATCH_HOME_H

#include "lvgl.h"

lv_obj_t *watch_home_create(lv_obj_t *parent);
void watch_home_on_gesture(lv_event_t *e);

#endif
