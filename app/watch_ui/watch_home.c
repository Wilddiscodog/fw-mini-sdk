#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_home.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_power.h"
#include "watch_clock.h"
#include "assets/watch_icons.h"

#define WATCH_HOME_MAX_PAGES  (1 + WATCH_APP_COUNT)

static lv_obj_t *s_tileview;
static lv_obj_t *s_dots[WATCH_HOME_MAX_PAGES];
static int s_page_count;

static void tile_click_cb(lv_event_t *e)
{
    enum watch_app_id id = (enum watch_app_id)(uintptr_t)lv_event_get_user_data(e);

    if (!watch_power_input_enabled()) {
        return;
    }
    watch_ui_open_app(id);
}

static void make_app_page(lv_obj_t *page, const struct watch_app_desc *app,
                          enum watch_app_id id)
{
    lv_obj_t *cell;
    lv_obj_t *name;

    lv_obj_set_style_bg_color(page, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);

    cell = lv_obj_create(page);
    lv_obj_set_size(cell, WATCH_ICON_SIZE + 8, WATCH_ICON_SIZE + 22);
    lv_obj_align(cell, LV_ALIGN_CENTER, 0, WATCH_ICON_Y_OFF);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(cell, tile_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);

    if (app->icon) {
        lv_obj_t *img = lv_img_create(cell);
        lv_img_set_src(img, app->icon);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t *circle = lv_obj_create(cell);
        lv_obj_set_size(circle, WATCH_ICON_SIZE, WATCH_ICON_SIZE);
        lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(circle, lv_color_hex(app->color), 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(circle, 4, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        {
            lv_obj_t *g = lv_label_create(circle);
            lv_obj_set_style_text_font(g, &lv_font_montserrat_48, 0);
            lv_obj_set_style_text_color(g, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(g, app->glyph ? app->glyph : "?");
            lv_obj_center(g);
        }
    }

    name = lv_label_create(cell);
    lv_obj_set_style_text_font(name, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(name, app->name);
    lv_obj_set_width(name, WATCH_ICON_SIZE + 8);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    /* 紧贴图标下方, 避开底部分页点 */
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, WATCH_ICON_SIZE + 2);
}

static void refresh_dots(int page)
{
    int i;
    int total = s_page_count;
    int span;
    int start_x;

    if (total <= 0) {
        return;
    }
    span = (total - 1) * 14;
    start_x = -span / 2;
    for (i = 0; i < total; i++) {
        if (!s_dots[i]) {
            continue;
        }
        lv_obj_set_style_bg_color(s_dots[i],
                                  lv_color_hex(page == i ? WATCH_COL_ACCENT : WATCH_COL_TEXT_DIM), 0);
        lv_obj_set_width(s_dots[i], page == i ? 14 : 8);
        lv_obj_align(s_dots[i], LV_ALIGN_BOTTOM_MID, start_x + i * 14, -WATCH_SAFE_Y);
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
    refresh_dots(page);
}

lv_obj_t *watch_home_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    const struct watch_app_desc *tab = watch_ui_app_table();
    lv_obj_t *p0;
    int i;
    int col = 0;

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
    lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(s_tileview, LV_DIR_HOR);
    lv_obj_set_style_anim_time(s_tileview, 120, 0);
    lv_obj_set_scroll_snap_x(s_tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_event_cb(s_tileview, tileview_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* 页0: 卡通小狗主页 + RTC 数字时钟 */
    p0 = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_style_bg_opa(p0, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(p0, 0, 0);
    lv_obj_add_flag(p0, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_user_data(p0, (void *)(uintptr_t)0);
    watch_clock_create(p0);
    col = 1;

    for (i = 0; i < WATCH_APP_COUNT; i++) {
        lv_obj_t *tile;
        if (!tab[i].create) {
            continue;
        }
        tile = lv_tileview_add_tile(s_tileview, col, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        lv_obj_set_user_data(tile, (void *)(uintptr_t)col);
        make_app_page(tile, &tab[i], (enum watch_app_id)i);
        col++;
    }
    s_page_count = col;

    for (i = 0; i < s_page_count; i++) {
        s_dots[i] = lv_obj_create(root);
        lv_obj_set_size(s_dots[i], i == 0 ? 14 : 8, 6);
        lv_obj_set_style_radius(s_dots[i], 3, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_bg_color(s_dots[i],
                                  lv_color_hex(i == 0 ? WATCH_COL_ACCENT : WATCH_COL_TEXT_DIM), 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    refresh_dots(0);

    return root;
}

void watch_home_on_gesture(lv_event_t *e)
{
    (void)e;
    /* 亮度/音量已迁到设置, 主页不再响应下拉 */
}
