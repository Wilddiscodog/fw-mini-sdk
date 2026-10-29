#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_dropdown.h"
#include "watch_style.h"
#include "watch_backlight.h"
#include "watch_volume.h"

static lv_obj_t *s_mask;
static lv_obj_t *s_panel;
static lv_obj_t *s_bl_slider;
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_bl_lab;
static lv_obj_t *s_vol_lab;
static u8 s_open;

static void mask_click(lv_event_t *e)
{
    (void)e;
    watch_dropdown_hide();
}

static void bl_changed(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    if (v < WATCH_BL_MIN) {
        v = WATCH_BL_MIN;
        lv_slider_set_value(lv_event_get_target(e), v, LV_ANIM_OFF);
    }
    watch_backlight_set((u8)v);
    if (s_bl_lab) {
        char buf[24];
        snprintf(buf, sizeof(buf), "亮度 %d%%", v);
        lv_label_set_text(s_bl_lab, buf);
    }
}

static void vol_changed(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    watch_volume_set((u8)v);
    if (s_vol_lab) {
        char buf[24];
        snprintf(buf, sizeof(buf), "音量 %d", v);
        lv_label_set_text(s_vol_lab, buf);
    }
}

lv_obj_t *watch_dropdown_create(lv_obj_t *parent)
{
    s_mask = lv_obj_create(parent);
    lv_obj_set_size(s_mask, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(s_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_mask, 0, 0);
    lv_obj_set_style_radius(s_mask, 0, 0);
    lv_obj_add_flag(s_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_mask, mask_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_mask, LV_OBJ_FLAG_HIDDEN);

    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, LCD_W, 120);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 10, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, -130);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_bl_lab = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_bl_lab, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_bl_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(s_bl_lab, "亮度");
    lv_obj_align(s_bl_lab, LV_ALIGN_TOP_LEFT, 0, 0);

    s_bl_slider = lv_slider_create(s_panel);
    lv_obj_set_width(s_bl_slider, LCD_W - 40);
    lv_slider_set_range(s_bl_slider, WATCH_BL_MIN, WATCH_BL_MAX);
    lv_slider_set_value(s_bl_slider, watch_backlight_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bl_slider, lv_color_hex(WATCH_COL_CARD_HL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bl_slider, lv_color_hex(WATCH_COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bl_slider, lv_color_hex(WATCH_COL_ACCENT2), LV_PART_KNOB);
    lv_obj_align(s_bl_slider, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_add_event_cb(s_bl_slider, bl_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_vol_lab = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_vol_lab, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_vol_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(s_vol_lab, "音量");
    lv_obj_align(s_vol_lab, LV_ALIGN_TOP_LEFT, 0, 56);

    s_vol_slider = lv_slider_create(s_panel);
    lv_obj_set_width(s_vol_slider, LCD_W - 40);
    lv_slider_set_range(s_vol_slider, WATCH_VOL_MIN, WATCH_VOL_MAX);
    lv_slider_set_value(s_vol_slider, watch_volume_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(WATCH_COL_CARD_HL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(WATCH_COL_ACCENT2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(WATCH_COL_ACCENT), LV_PART_KNOB);
    lv_obj_align(s_vol_slider, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_add_event_cb(s_vol_slider, vol_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_open = 0;
    return s_panel;
}

void watch_dropdown_show(void)
{
    char buf[24];
    if (!s_panel) {
        return;
    }
    lv_slider_set_value(s_bl_slider, watch_backlight_get(), LV_ANIM_OFF);
    lv_slider_set_value(s_vol_slider, watch_volume_get(), LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "亮度 %d%%", watch_backlight_get());
    lv_label_set_text(s_bl_lab, buf);
    snprintf(buf, sizeof(buf), "音量 %d", watch_volume_get());
    lv_label_set_text(s_vol_lab, buf);

    lv_obj_clear_flag(s_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_mask);
    lv_obj_move_foreground(s_panel);
    lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, 0);
    s_open = 1;
}

void watch_dropdown_hide(void)
{
    if (!s_panel) {
        return;
    }
    lv_obj_add_flag(s_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, -130);
    s_open = 0;
}

u8 watch_dropdown_is_open(void)
{
    return s_open;
}
