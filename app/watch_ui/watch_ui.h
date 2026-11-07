#ifndef WATCH_UI_H
#define WATCH_UI_H

#include "generic/typedef.h"
#include "lvgl.h"

enum watch_app_id {
    WATCH_APP_WALKIE = 0,
    WATCH_APP_NET_MUSIC,
    WATCH_APP_BT_MUSIC,
    WATCH_APP_BALL,
    WATCH_APP_TETRIS,
    WATCH_APP_MAZE,
    WATCH_APP_MATCH3,
    WATCH_APP_SOKOBAN,
    WATCH_APP_2048,
    WATCH_APP_ASR_TEST,
    WATCH_APP_SETTINGS,
    WATCH_APP_COUNT,
};

typedef lv_obj_t *(*watch_app_create_cb)(lv_obj_t *parent);

struct watch_app_desc {
    const char *name;
    const char *glyph;          /* 圆形图标上的短字母 */
    const lv_img_dsc_t *icon;   /* C 数组图标, 可为 NULL */
    u32 color;                  /* 强调色 RGB888 */
    watch_app_create_cb create;
};

void watch_ui_start(void);
void watch_ui_open_app(enum watch_app_id id);
void watch_ui_close_app(void);
u8 watch_ui_app_is_open(void);
enum watch_app_id watch_ui_app_current(void);
void watch_ui_open_dropdown(void);
void watch_ui_close_dropdown(void);
u8 watch_ui_dropdown_is_open(void);
lv_obj_t *watch_ui_get_home(void);
const struct watch_app_desc *watch_ui_app_table(void);

/* shared chrome helpers */
lv_obj_t *watch_ui_create_app_chrome(lv_obj_t *parent, const char *title);
void watch_ui_toast(const char *msg);
/* 顶部通知条(类似安卓通知), 默认约 2s 后消失 */
void watch_ui_notify(const char *msg);

#endif
