#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include <stdio.h>
#include "watch_ui.h"
#include "watch_home.h"
#include "watch_dropdown.h"
#include "watch_apps.h"
#include "watch_style.h"
#include "watch_backlight.h"
#include "watch_volume.h"
#include "watch_power.h"
#include "watch_battery.h"
#include "assets/watch_icons.h"
#include "walkie.h"
#if LV_USE_PNG
#include "extra/libs/png/lv_png.h"
#endif

static lv_obj_t *s_home;
static lv_obj_t *s_app_layer;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_tm;
static lv_obj_t *s_notify;
static lv_timer_t *s_notify_tm;
static u8 s_app_open;
static enum watch_app_id s_app_id = WATCH_APP_COUNT;

static const struct watch_app_desc s_apps[WATCH_APP_COUNT] = {
    { "对讲",   "T", &watch_icon_walkie,   0xFF6D00, app_walkie_create },
    { "陀螺仪", "G", &watch_icon_gyro,     0x00BCD4, app_gyro_create },
#ifdef CONFIG_NET_MUSIC_MODE_ENABLE
    { "网络",   "N", &watch_icon_net,      0x7C4DFF, app_net_music_create },
#else
    { "网络",   "N", &watch_icon_net,      0x7C4DFF, NULL },
#endif
    { "蓝牙",   "B", &watch_icon_bt,       0x2979FF, app_bt_music_create },
    { "平衡球", "P", &watch_icon_ball,     0xFFD600, app_ball_create },
    { "滑雪大冒险", "K", &watch_icon_ski,  0x4FC3F7, app_ski_create },
    { "设置",   "S", &watch_icon_settings, 0x00E676, app_settings_create },
};

const struct watch_app_desc *watch_ui_app_table(void)
{
    return s_apps;
}

lv_obj_t *watch_ui_get_home(void)
{
    return s_home;
}

u8 watch_ui_app_is_open(void)
{
    return s_app_open;
}

enum watch_app_id watch_ui_app_current(void)
{
    return s_app_open ? s_app_id : WATCH_APP_COUNT;
}

u8 watch_ui_dropdown_is_open(void)
{
    return watch_dropdown_is_open();
}

void watch_ui_open_dropdown(void)
{
    if (s_app_open || !watch_power_input_enabled()) {
        return;
    }
    watch_dropdown_show();
}

void watch_ui_close_dropdown(void)
{
    watch_dropdown_hide();
}

static void toast_hide(lv_timer_t *t)
{
    (void)t;
    if (s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast_tm) {
        lv_timer_del(s_toast_tm);
        s_toast_tm = NULL;
    }
}

void watch_ui_toast(const char *msg)
{
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(WATCH_COL_CARD), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(s_toast, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(s_toast, lv_color_hex(WATCH_COL_TEXT), 0);
        lv_obj_set_style_pad_all(s_toast, 8, 0);
        lv_obj_set_style_radius(s_toast, 8, 0);
    }
    lv_label_set_text(s_toast, msg);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -WATCH_SAFE_Y - 8);
    if (s_toast_tm) {
        lv_timer_del(s_toast_tm);
    }
    s_toast_tm = lv_timer_create(toast_hide, 1500, NULL);
}

static void notify_hide(lv_timer_t *t)
{
    (void)t;
    if (s_notify) {
        lv_obj_add_flag(s_notify, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_notify_tm) {
        lv_timer_del(s_notify_tm);
        s_notify_tm = NULL;
    }
}

void watch_ui_notify(const char *msg)
{
    if (!s_notify) {
        s_notify = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_notify, LCD_W - 8, 36);
        lv_obj_set_style_bg_color(s_notify, lv_color_hex(WATCH_COL_CARD), 0);
        lv_obj_set_style_bg_opa(s_notify, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_notify, 2, 0);
        lv_obj_set_style_border_color(s_notify, lv_color_hex(WATCH_COL_ACCENT), 0);
        lv_obj_set_style_radius(s_notify, 16, 0);
        lv_obj_set_style_pad_hor(s_notify, 10, 0);
        lv_obj_set_style_pad_ver(s_notify, 6, 0);
        lv_obj_clear_flag(s_notify, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lab = lv_label_create(s_notify);
        lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(lab, LCD_W - 36);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);
    }
    {
        lv_obj_t *lab = lv_obj_get_child(s_notify, 0);
        if (lab) {
            lv_label_set_text(lab, msg ? msg : "");
        }
    }
    lv_obj_clear_flag(s_notify, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_notify, LV_ALIGN_TOP_MID, 0, WATCH_SAFE_Y + WATCH_STATUS_H);
    lv_obj_move_foreground(s_notify);
    if (s_notify_tm) {
        lv_timer_del(s_notify_tm);
    }
    s_notify_tm = lv_timer_create(notify_hide, 2000, NULL);
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

static void app_gesture_cb(lv_event_t *e)
{
    lv_dir_t dir;

    if (!s_app_open || !watch_power_input_enabled()) {
        return;
    }
    dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    /* 右滑 / 从底部上滑 → 回主页 (游戏全屏 canvas 也能冒泡) */
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        watch_ui_close_app();
    }
}

lv_obj_t *watch_ui_create_app_chrome(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LCD_W - WATCH_SAFE_X * 2, 32);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, WATCH_SAFE_Y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 12, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 56, 24);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    lv_obj_t *t = lv_label_create(bar);
    lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_RIGHT_MID, -4, 0);
    return bar;
}

void watch_ui_close_app(void)
{
    if (!s_app_open || !s_app_layer) {
        return;
    }
    if (s_app_id == WATCH_APP_WALKIE) {
        walkie_on_exit();
    }
    lv_obj_clean(s_app_layer);
    lv_obj_add_flag(s_app_layer, LV_OBJ_FLAG_HIDDEN);
    s_app_open = 0;
    s_app_id = WATCH_APP_COUNT;
}

void watch_ui_open_app(enum watch_app_id id)
{
    if (id >= WATCH_APP_COUNT || !s_apps[id].create) {
        return;
    }
    if (!watch_power_input_enabled()) {
        return;
    }
    watch_ui_close_dropdown();
    if (!s_app_layer) {
        return;
    }
    if (s_app_open) {
        watch_ui_close_app();
    }
    lv_obj_clean(s_app_layer);
    lv_obj_clear_flag(s_app_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_app_layer);
    s_app_id = id;
    if (id == WATCH_APP_WALKIE) {
        walkie_on_enter();
    }
    s_apps[id].create(s_app_layer);
    s_app_open = 1;
}

static void apply_theme(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

/* 对讲后台: 任何界面收到新消息都弹窗; 开启自动播放则播语音 */
static void walkie_bg_rx_tm(lv_timer_t *t)
{
    u32 nip = 0;
    char tip[48];
    const char *nm;
    int n;
    u8 dir = 0;
    (void)t;

    if (!walkie_is_active()) {
        return;
    }
    if (!walkie_rec_rx_notify_take(&nip)) {
        return;
    }
    nm = walkie_peer_name_by_ip(nip);
    snprintf(tip, sizeof(tip), "%s 发来一条语音",
             (nm && nm[0]) ? nm : "设备");
    watch_ui_notify(tip);

    if (!walkie_autoplay_get() || walkie_msg_busy() || walkie_ptt_is_on()) {
        return;
    }
    n = walkie_rec_count();
    if (n > 0 &&
        walkie_rec_meta(n - 1, &dir, NULL, NULL) == 0 && dir) {
        (void)walkie_rec_play(n - 1);
    }
}

void watch_ui_start(void)
{
#if LV_USE_PNG
    lv_png_init();
#endif
    watch_backlight_init();
    watch_volume_init();
    walkie_autoplay_init();
    apply_theme();

    s_home = watch_home_create(lv_scr_act());
    /* 下拉栏已移除; 保留空 create 以兼容旧符号 */

    s_app_layer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_app_layer, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(s_app_layer, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(s_app_layer, 0, 0);
    lv_obj_set_style_radius(s_app_layer, 0, 0);
    lv_obj_set_style_pad_all(s_app_layer, 0, 0);
    lv_obj_clear_flag(s_app_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_app_layer, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_app_layer, app_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(s_app_layer, LV_OBJ_FLAG_HIDDEN);

    /* 对讲作为后台 APP: 开机即收消息 */
    walkie_bg_start();
    lv_timer_create(walkie_bg_rx_tm, 200, NULL);

    watch_power_init();
    lv_timer_create(watch_power_poll, 50, NULL);
    watch_battery_init();
    watch_power_boot_anim();
}
