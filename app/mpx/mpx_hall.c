/* 上电应用大厅：列出已装 guest，点击后排队启动。 */
#include "app_config.h"

#if (defined MINI_WASM_SUPPORT) && MINI_WASM_SUPPORT && \
    (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT

#include "mpx_hall.h"

#include "system/includes.h"
#include "lcd_config.h"
#include "lvgl.h"
#include "mpx_store.h"
#include "watch_backlight.h"
#include "watch_power.h"
#include "extra/libs/png/lodepng.h"
#include "asm/wdt.h"

#include <stdlib.h>

#define HALL_MAX 16
#define HALL_ICON 88

static char s_ids[HALL_MAX][MPX_STORE_ID_MAX + 1];
static unsigned s_count;
static lv_obj_t *s_status;
static lv_img_dsc_t s_icons[HALL_MAX];
static uint8_t *s_icon_px[HALL_MAX];

void *lodepng_malloc(size_t size)
{
    return malloc(size);
}

void *lodepng_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void lodepng_free(void *ptr)
{
    free(ptr);
}

static const uint32_t s_colors[] = {
    0x3D7EFF, 0x2EAA6A, 0xE06A3A, 0x7A5CFF, 0xD4A017,
};

static const char *hall_name(const char *id)
{
    const char *name = id;
    const char *p = id;
    while (*p != '\0') {
        if (*p == '.') {
            name = p + 1;
        }
        p++;
    }
    if (strcmp(name, "demo") == 0) {
        return "Demo";
    }
    if (strcmp(name, "snake") == 0) {
        return "Snake";
    }
    if (strcmp(name, "maze-break") == 0) {
        return "Maze";
    }
    if (strcmp(name, "blocks") == 0) {
        return "Blocks";
    }
    if (strcmp(name, "tomb-explorer") == 0) {
        return "Tomb";
    }
    return name;
}

static void hall_click(lv_event_t *e)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    if (index >= s_count) {
        return;
    }
    if (mpx_store_request_launch(s_ids[index]) != 0) {
        lv_label_set_text(s_status, "busy");
        return;
    }
    lv_label_set_text(s_status, "starting");
}

static int hall_make_icon(unsigned index)
{
    const uint8_t *png = NULL;
    uint32_t png_size = 0;
    uint32_t declared_w = 0;
    uint32_t declared_h = 0;
    unsigned char *rgba = NULL;
    unsigned w = 0;
    unsigned h = 0;
    unsigned err;
    unsigned crop;
    unsigned ox;
    unsigned oy;
    unsigned x;
    unsigned y;
    uint8_t *dst;
    lv_color_t *px;

    wdt_clear();
    if (index >= HALL_MAX ||
        mpx_store_launch_png(s_ids[index], &png, &png_size, &declared_w, &declared_h) != 0) {
        return -1;
    }
    err = lodepng_decode32(&rgba, &w, &h, png, png_size);
    wdt_clear();
    if (err != 0 || rgba == NULL || w == 0 || h == 0) {
        lodepng_free(rgba);
        return -1;
    }
    dst = (uint8_t *)malloc((size_t)HALL_ICON * HALL_ICON * sizeof(lv_color_t));
    if (dst == NULL) {
        lodepng_free(rgba);
        return -1;
    }
    crop = w < h ? w : h;
    ox = (w - crop) / 2;
    oy = (h - crop) / 2;
    px = (lv_color_t *)dst;
    for (y = 0; y < HALL_ICON; y++) {
        unsigned sy = oy + (unsigned)(((uint32_t)y * crop) / HALL_ICON);
        if ((y & 7u) == 0) {
            wdt_clear();
        }
        for (x = 0; x < HALL_ICON; x++) {
            unsigned sx = ox + (unsigned)(((uint32_t)x * crop) / HALL_ICON);
            const unsigned char *sample = rgba + ((size_t)sy * w + sx) * 4u;
            unsigned a = sample[3];
            unsigned r = sample[0];
            unsigned g = sample[1];
            unsigned b = sample[2];
            if (a < 255u) {
                r = (r * a + 0x12u * (255u - a)) / 255u;
                g = (g * a + 0x18u * (255u - a)) / 255u;
                b = (b * a + 0x26u * (255u - a)) / 255u;
            }
            px[(size_t)y * HALL_ICON + x] = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
    }
    lodepng_free(rgba);
    s_icon_px[index] = dst;
    memset(&s_icons[index], 0, sizeof(s_icons[index]));
    s_icons[index].header.always_zero = 0;
    s_icons[index].header.cf = LV_IMG_CF_TRUE_COLOR;
    s_icons[index].header.w = HALL_ICON;
    s_icons[index].header.h = HALL_ICON;
    s_icons[index].data_size = (uint32_t)HALL_ICON * HALL_ICON * sizeof(lv_color_t);
    s_icons[index].data = dst;
    return 0;
}

static void hall_add_card(lv_obj_t *row, unsigned index)
{
    lv_obj_t *card;
    lv_obj_t *label;
    uint32_t color = s_colors[index % (sizeof(s_colors) / sizeof(s_colors[0]))];

    card = lv_btn_create(row);
    lv_obj_set_size(card, 100, 148);
    lv_obj_set_style_bg_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_add_event_cb(card, hall_click, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    if (hall_make_icon(index) == 0) {
        lv_obj_t *img = lv_img_create(card);
        lv_img_set_src(img, &s_icons[index]);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_set_style_radius(img, 10, 0);
        lv_obj_set_style_clip_corner(img, true, 0);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x121826), 0);
    }

    label = lv_label_create(card);
    lv_label_set_text(label, hall_name(s_ids[index]));
    lv_obj_set_width(label, 84);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void mpx_hall_start(void)
{
    lv_obj_t *scr;
    lv_obj_t *mark;
    lv_obj_t *title;
    lv_obj_t *row;
    unsigned i;

    watch_backlight_init();
    watch_power_init();
    lv_timer_create(watch_power_poll, 50, NULL);

    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1116), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    mark = lv_obj_create(scr);
    lv_obj_set_size(mark, 6, 22);
    lv_obj_set_pos(mark, 12, 12);
    lv_obj_set_style_bg_color(mark, lv_color_hex(0x3D7EFF), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mark, 3, 0);
    lv_obj_set_style_border_width(mark, 0, 0);
    lv_obj_set_style_pad_all(mark, 0, 0);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(scr);
    lv_label_set_text(title, "Hall");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_pos(title, 26, 14);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8B97AD), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -12, 14);

    row = lv_obj_create(scr);
    lv_obj_set_size(row, LCD_W, 188);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    s_count = 0;
    for (i = 0; i < HALL_MAX; i++) {
        if (mpx_store_app_at(i, s_ids[s_count], sizeof(s_ids[0])) != 0) {
            break;
        }
        hall_add_card(row, s_count);
        s_count++;
    }
    if (s_count == 0) {
        lv_label_set_text(s_status, "empty");
    }
}

#endif
