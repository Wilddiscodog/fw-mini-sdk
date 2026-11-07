#ifndef WATCH_APPS_H
#define WATCH_APPS_H

#include "lvgl.h"

lv_obj_t *app_net_music_create(lv_obj_t *parent);
lv_obj_t *app_bt_music_create(lv_obj_t *parent);
lv_obj_t *app_walkie_create(lv_obj_t *parent);
lv_obj_t *app_settings_create(lv_obj_t *parent);
lv_obj_t *app_ball_create(lv_obj_t *parent);
void app_ball_exit(void);
lv_obj_t *app_tetris_create(lv_obj_t *parent);
void app_tetris_exit(void);
lv_obj_t *app_maze_create(lv_obj_t *parent);
void app_maze_exit(void);
lv_obj_t *app_match3_create(lv_obj_t *parent);
void app_match3_exit(void);
lv_obj_t *app_sokoban_create(lv_obj_t *parent);
void app_sokoban_exit(void);
lv_obj_t *app_2048_create(lv_obj_t *parent);
void app_2048_exit(void);

#endif
