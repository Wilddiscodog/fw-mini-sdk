#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_home.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_power.h"
#include "watch_dropdown.h"

static lv_obj_t *s_tileview;
static lv_obj_t *s_dots[2];

static void tile_click_cb(lv_event_t *e)
{
    enum watch_app_id id = (enum watch_app_id)(uintptr_t)lv_event_get_user_data(e);
    if (!watch_power_input_enabled()) {
        return;
    }
    if (watch_ui_dropdown_is_open()) {
        watch_ui_close_dropdown();
        return;
    }
    watch_ui_open_app(id);
}

static lv_obj_t *make_tile(lv_obj_t *parent, int x, int y, const struct watch_app_desc *app, enum watch_app_id id)
{
    const int gap = 8;
    const int top = 36;
    const int tw = (LCD_W - gap * 4) / 3;
    const int th = (LCD_H - top - gap * 3 - 16) / 2;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, tw, th);
    lv_obj_set_pos(btn, gap + x * (tw + gap), top + y * (th + gap));
    lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_CARD_HL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, tile_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);

    lv_obj_t *icon = lv_label_create(btn);
    lv_obj_set_style_text_font(icon, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(icon, app->icon);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *name = lv_label_create(btn);
    lv_obj_set_style_text_font(name, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(name, app->name);
    lv_obj_set_width(name, tw - 8);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    return btn;
}

static void fill_page(lv_obj_t *page, int page_idx)
{
    const struct watch_app_desc *tab = watch_ui_app_table();
    int base = page_idx * 6;
    int i;
    for (i = 0; i < 6; i++) {
        int id = base + i;
        if (id >= WATCH_APP_COUNT) {
            break;
        }
        make_tile(page, i % 3, i / 3, &tab[id], (enum watch_app_id)id);
    }
}

static void tileview_changed(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *tile = lv_tileview_get_tile_act(tv);
    int page = 0;
    if (tile) {
        page = (int)(uintptr_t)lv_obj_get_user_data(tile);
    }
    if (s_dots[0]) {
        lv_obj_set_style_bg_color(s_dots[0],
                                  lv_color_hex(page == 0 ? WATCH_COL_ACCENT : WATCH_COL_TEXT_DIM), 0);
    }
    if (s_dots[1]) {
        lv_obj_set_style_bg_color(s_dots[1],
                                  lv_color_hex(page == 1 ? WATCH_COL_ACCENT : WATCH_COL_TEXT_DIM), 0);
    }
}

lv_obj_t *watch_home_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(title, "主菜单");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_tileview = lv_tileview_create(root);
    lv_obj_set_size(s_tileview, LCD_W, LCD_H - 28);
    lv_obj_align(s_tileview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(s_tileview, tileview_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *p0 = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_opa(p0, LV_OPA_TRANSP, 0);
    lv_obj_set_user_data(p0, (void *)(uintptr_t)0);
    fill_page(p0, 0);

    lv_obj_t *p1 = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_opa(p1, LV_OPA_TRANSP, 0);
    lv_obj_set_user_data(p1, (void *)(uintptr_t)1);
    /* 第2页预留空位提示 */
    {
        lv_obj_t *hint = lv_label_create(p1);
        lv_obj_set_style_text_font(hint, &lv_font_simsun_16_cjk, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(hint, "更多应用");
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);
    }

    s_dots[0] = lv_obj_create(root);
    lv_obj_set_size(s_dots[0], 8, 8);
    lv_obj_set_style_radius(s_dots[0], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dots[0], 0, 0);
    lv_obj_set_style_bg_color(s_dots[0], lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_obj_align(s_dots[0], LV_ALIGN_BOTTOM_MID, -10, -4);

    s_dots[1] = lv_obj_create(root);
    lv_obj_set_size(s_dots[1], 8, 8);
    lv_obj_set_style_radius(s_dots[1], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dots[1], 0, 0);
    lv_obj_set_style_bg_color(s_dots[1], lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_obj_align(s_dots[1], LV_ALIGN_BOTTOM_MID, 10, -4);

    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, watch_home_on_gesture, LV_EVENT_GESTURE, NULL);
    return root;
}

void watch_home_on_gesture(lv_event_t *e)
{
    if (!watch_power_input_enabled() || watch_ui_app_is_open()) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        watch_ui_open_dropdown();
    } else if (dir == LV_DIR_TOP && watch_ui_dropdown_is_open()) {
        lv_indev_wait_release(lv_indev_get_act());
        watch_ui_close_dropdown();
    }
}
