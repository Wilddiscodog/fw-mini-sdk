#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_home.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_power.h"
#include "watch_dropdown.h"
#include "watch_clock.h"
#include "assets/watch_icons.h"

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

static lv_obj_t *make_app_icon(lv_obj_t *parent, int x, int y,
                               const struct watch_app_desc *app, enum watch_app_id id)
{
    const int gap_x = 14;
    const int gap_y = 10;
    const int top = 28;
    const int tw = (LCD_W - gap_x * 4) / 3;
    const int th = (LCD_H - top - gap_y * 3 - 18) / 2;
    const int icon_sz = 56;
    lv_obj_t *cell;
    lv_obj_t *name;

    cell = lv_obj_create(parent);
    lv_obj_set_size(cell, tw, th);
    lv_obj_set_pos(cell, gap_x + x * (tw + gap_x), top + y * (th + gap_y));
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, tile_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);

    if (app->icon) {
        lv_obj_t *img = lv_img_create(cell);
        lv_img_set_src(img, app->icon);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 6);
    } else {
        lv_obj_t *circle = lv_obj_create(cell);
        lv_obj_set_size(circle, icon_sz, icon_sz);
        lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(circle, lv_color_hex(app->color), 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(circle, 0, 0);
        lv_obj_set_style_shadow_width(circle, 12, 0);
        lv_obj_set_style_shadow_color(circle, lv_color_hex(app->color), 0);
        lv_obj_set_style_shadow_opa(circle, LV_OPA_40, 0);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        {
            lv_obj_t *g = lv_label_create(circle);
            lv_obj_set_style_text_font(g, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(g, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(g, app->glyph ? app->glyph : "?");
            lv_obj_center(g);
        }
    }

    name = lv_label_create(cell);
    lv_obj_set_style_text_font(name, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(name, app->name);
    lv_obj_set_width(name, tw);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -2);
    return cell;
}

static void fill_apps_page(lv_obj_t *page)
{
    const struct watch_app_desc *tab = watch_ui_app_table();
    int i, slot = 0;

    /* 3 列 flex 网格, 超过 6 个 app 可纵向滚动(7 号 app 起自动翻行) */
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(page, 14, 0);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_style_pad_top(page, 28, 0);
    lv_obj_set_style_pad_bottom(page, 8, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);

    for (i = 0; i < WATCH_APP_COUNT; i++) {
        if (!tab[i].create) {
            continue;
        }
        make_app_icon(page, slot % 3, slot / 3, &tab[i], (enum watch_app_id)i);
        slot++;
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
        lv_obj_set_width(s_dots[0], page == 0 ? 14 : 8);
    }
    if (s_dots[1]) {
        lv_obj_set_style_bg_color(s_dots[1],
                                  lv_color_hex(page == 1 ? WATCH_COL_ACCENT : WATCH_COL_TEXT_DIM), 0);
        lv_obj_set_width(s_dots[1], page == 1 ? 14 : 8);
    }
}

lv_obj_t *watch_home_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_t *p0;
    lv_obj_t *p1;

    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_tileview = lv_tileview_create(root);
    lv_obj_set_size(s_tileview, LCD_W, LCD_H);
    lv_obj_align(s_tileview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(s_tileview, tileview_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* 页0: 表盘 */
    p0 = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_style_bg_opa(p0, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(p0, 0, 0);
    lv_obj_set_user_data(p0, (void *)(uintptr_t)0);
    watch_clock_create(p0);

    /* 页1: 应用宫格 */
    p1 = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_LEFT);
    lv_obj_set_style_bg_color(p1, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_bg_opa(p1, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(p1, 0, 0);
    lv_obj_set_user_data(p1, (void *)(uintptr_t)1);
    {
        lv_obj_t *title = lv_label_create(p1);
        lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(title, "应用");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    }
    fill_apps_page(p1);

    s_dots[0] = lv_obj_create(root);
    lv_obj_set_size(s_dots[0], 14, 6);
    lv_obj_set_style_radius(s_dots[0], 3, 0);
    lv_obj_set_style_border_width(s_dots[0], 0, 0);
    lv_obj_set_style_bg_color(s_dots[0], lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_obj_align(s_dots[0], LV_ALIGN_BOTTOM_MID, -12, -6);

    s_dots[1] = lv_obj_create(root);
    lv_obj_set_size(s_dots[1], 8, 6);
    lv_obj_set_style_radius(s_dots[1], 3, 0);
    lv_obj_set_style_border_width(s_dots[1], 0, 0);
    lv_obj_set_style_bg_color(s_dots[1], lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_obj_align(s_dots[1], LV_ALIGN_BOTTOM_MID, 12, -6);

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
