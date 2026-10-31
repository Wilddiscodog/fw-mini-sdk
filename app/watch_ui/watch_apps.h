#ifndef WATCH_APPS_H
#define WATCH_APPS_H

#include "lvgl.h"

lv_obj_t *app_gyro_create(lv_obj_t *parent);
lv_obj_t *app_net_music_create(lv_obj_t *parent);
lv_obj_t *app_bt_music_create(lv_obj_t *parent);
lv_obj_t *app_walkie_create(lv_obj_t *parent);
lv_obj_t *app_recorder_create(lv_obj_t *parent);
lv_obj_t *app_settings_create(lv_obj_t *parent);
lv_obj_t *app_ball_create(lv_obj_t *parent);
void app_ball_exit(void);

#endif
