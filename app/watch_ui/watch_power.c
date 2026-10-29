#include "app_config.h"
#include "system/includes.h"
#include "event/key_event.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "fw_version.h"
#include "sys_power.h"
#include "watch_power.h"
#include "watch_backlight.h"
#include "watch_ui.h"
#include "watch_style.h"

static u8 s_on = 1;
static u8 s_busy;
static volatile u8 s_off_req;
static lv_obj_t *s_layer;
static lv_obj_t *s_title;
static lv_obj_t *s_sub;
static u8 s_bl_anim;

static void anim_set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void anim_set_bl(void *obj, int32_t v)
{
    (void)obj;
    if (v < WATCH_BL_MIN) {
        v = 0;
    }
    watch_backlight_set((u8)v);
}

static void hw_poweroff_cb(lv_anim_t *a)
{
    (void)a;
    printf("[watch] P33 soft poweroff\n");
    sys_power_poweroff();
    while (1) {
        os_time_dly(1000);
    }
}

static void ensure_layer(void)
{
    if (s_layer) {
        return;
    }
    s_layer = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(s_layer, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(s_layer, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_bg_opa(s_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_layer, 0, 0);
    lv_obj_set_style_radius(s_layer, 0, 0);
    lv_obj_set_style_pad_all(s_layer, 0, 0);
    lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_layer, LV_OBJ_FLAG_CLICKABLE);

    s_title = lv_label_create(s_layer);
    lv_obj_set_style_text_font(s_title, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(s_title, "");
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -12);

    s_sub = lv_label_create(s_layer);
    lv_obj_set_style_text_font(s_sub, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_sub, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_sub, "");
    lv_obj_align(s_sub, LV_ALIGN_CENTER, 0, 16);
}

static void fade_opa(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t ms,
                     lv_anim_ready_cb_t ready)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    if (ready) {
        lv_anim_set_ready_cb(&a, ready);
    }
    lv_anim_start(&a);
}

static void boot_fade_out_ready(lv_anim_t *a)
{
    (void)a;
    if (s_layer) {
        lv_obj_add_flag(s_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_layer, LV_OPA_COVER, 0);
    }
    s_busy = 0;
    s_on = 1;
    printf("[watch] boot anim done\n");
}

static void boot_hold_ready(lv_anim_t *a)
{
    (void)a;
    fade_opa(s_layer, LV_OPA_COVER, LV_OPA_TRANSP, 450, boot_fade_out_ready);
}

static void boot_fade_in_ready(lv_anim_t *a)
{
    (void)a;
    lv_anim_t hold;
    lv_anim_init(&hold);
    lv_anim_set_var(&hold, s_layer);
    lv_anim_set_time(&hold, 700);
    lv_anim_set_values(&hold, 0, 1);
    lv_anim_set_exec_cb(&hold, NULL);
    lv_anim_set_ready_cb(&hold, boot_hold_ready);
    lv_anim_start(&hold);
}

void watch_power_boot_anim(void)
{
    char ver[32];

    ensure_layer();
    s_busy = 1;
    lv_label_set_text(s_title, "智能手表");
    snprintf(ver, sizeof(ver), "%s", FW_VERSION);
    lv_label_set_text(s_sub, ver);
    lv_obj_set_style_bg_color(s_layer, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_opa(s_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_layer);
    fade_opa(s_layer, LV_OPA_TRANSP, LV_OPA_COVER, 500, boot_fade_in_ready);
}

static void shutdown_bl_ready(lv_anim_t *a)
{
    (void)a;
    watch_backlight_set(0);
    lv_anim_t delay;
    lv_anim_init(&delay);
    lv_anim_set_var(&delay, s_layer);
    lv_anim_set_time(&delay, 200);
    lv_anim_set_values(&delay, 0, 1);
    lv_anim_set_exec_cb(&delay, NULL);
    lv_anim_set_ready_cb(&delay, hw_poweroff_cb);
    lv_anim_start(&delay);
}

static void shutdown_fade_ready(lv_anim_t *a)
{
    (void)a;
    s_bl_anim = watch_backlight_get();
    if (s_bl_anim < WATCH_BL_MIN) {
        s_bl_anim = WATCH_BL_DEFAULT;
    }
    {
        lv_anim_t bla;
        lv_anim_init(&bla);
        lv_anim_set_var(&bla, s_layer);
        lv_anim_set_values(&bla, s_bl_anim, 0);
        lv_anim_set_time(&bla, 400);
        lv_anim_set_exec_cb(&bla, anim_set_bl);
        lv_anim_set_ready_cb(&bla, shutdown_bl_ready);
        lv_anim_start(&bla);
    }
}

void watch_power_off(void)
{
    if (s_busy || !s_on) {
        return;
    }
    s_busy = 1;
    s_on = 0;
    watch_ui_close_dropdown();
    watch_ui_close_app();

    ensure_layer();
    lv_label_set_text(s_title, "正在关机");
    lv_label_set_text(s_sub, "See you");
    lv_obj_set_style_bg_color(s_layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_opa(s_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_layer);
    printf("[watch] shutdown anim\n");
    fade_opa(s_layer, LV_OPA_TRANSP, LV_OPA_COVER, 550, shutdown_fade_ready);
}

void watch_power_init(void)
{
    s_on = 1;
    s_busy = 0;
    s_off_req = 0;
    ensure_layer();
    lv_obj_add_flag(s_layer, LV_OBJ_FLAG_HIDDEN);
}

u8 watch_power_is_on(void)
{
    return s_on && !s_busy;
}

u8 watch_power_input_enabled(void)
{
    return s_on && !s_busy;
}

void watch_power_key_long(void)
{
    s_off_req = 1;
}

void watch_power_poll(lv_timer_t *timer)
{
    (void)timer;
    if (!s_off_req) {
        return;
    }
    s_off_req = 0;
    watch_power_off();
}
