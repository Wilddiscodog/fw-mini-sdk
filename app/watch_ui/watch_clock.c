#include "app_config.h"
#include "system/includes.h"
#include "sys_time.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_clock.h"
#include "watch_style.h"

static lv_obj_t *s_lbl_hm;
static lv_obj_t *s_lbl_sec;
static lv_obj_t *s_lbl_date;
static lv_obj_t *s_arc;
static lv_timer_t *s_tm;
/* tick 合成起始点(无 RTC 时用) */
static u32 s_base_ms;
static u32 s_base_tod; /* seconds since midnight at start */

static void clock_get_hms(u8 *h, u8 *m, u8 *s, u16 *ms_out)
{
    u32 ms = timer_get_ms();
    u32 tod;
    u32 elp;

    if (ms >= s_base_ms) {
        elp = ms - s_base_ms;
    } else {
        elp = (0xFFFFFFFFu - s_base_ms) + 1u + ms;
    }
    tod = s_base_tod + (elp / 1000u);
    if (ms_out) {
        *ms_out = (u16)(elp % 1000u);
    }
    tod %= (24u * 3600u);
    *h = (u8)(tod / 3600u);
    *m = (u8)((tod % 3600u) / 60u);
    *s = (u8)(tod % 60u);
}

static void clock_tick(lv_timer_t *t)
{
    char buf[32];
    u8 h, m, s;
    u16 ms;
    static const char *wdays[] = {
        "日", "一", "二", "三", "四", "五", "六"
    };
    u32 day_i;
    (void)t;

    clock_get_hms(&h, &m, &s, &ms);
    if (s_lbl_hm) {
        lv_snprintf(buf, sizeof(buf), "%02u:%02u", h, m);
        lv_label_set_text(s_lbl_hm, buf);
    }
    if (s_lbl_sec) {
        lv_snprintf(buf, sizeof(buf), "%02u", s);
        lv_label_set_text(s_lbl_sec, buf);
    }
    if (s_arc) {
        /* 秒针弧: 0..60 → 0..3600 (0.1 deg units of arc range we set 0..360) */
        lv_arc_set_value(s_arc, (s * 6) + (ms * 6 / 1000));
    }
    if (s_lbl_date) {
        /* 简化日期: 用累计天推星期(演示) */
        day_i = (s_base_tod / 86400u + (timer_get_ms() - s_base_ms) / 86400000u) % 7u;
        lv_snprintf(buf, sizeof(buf), "周%s  ·  右滑打开应用", wdays[day_i]);
        lv_label_set_text(s_lbl_date, buf);
    }
}

lv_obj_t *watch_clock_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* 外圈装饰弧 */
    s_arc = lv_arc_create(root);
    lv_obj_set_size(s_arc, 200, 200);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, -36, -4);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 360);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(WATCH_COL_RING), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(WATCH_COL_RING_FG), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    s_lbl_hm = lv_label_create(root);
    lv_obj_set_style_text_font(s_lbl_hm, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_hm, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(s_lbl_hm, "00:00");
    lv_obj_align(s_lbl_hm, LV_ALIGN_CENTER, -36, -8);

    s_lbl_sec = lv_label_create(root);
    lv_obj_set_style_text_font(s_lbl_sec, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_sec, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(s_lbl_sec, "00");
    lv_obj_align_to(s_lbl_sec, s_lbl_hm, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);

    s_lbl_date = lv_label_create(root);
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_lbl_date, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_lbl_date, "");
    lv_obj_align(s_lbl_date, LV_ALIGN_BOTTOM_MID, 0, -28);

    /* 侧边小装饰条 */
    {
        lv_obj_t *bar = lv_obj_create(root);
        lv_obj_set_size(bar, 4, 72);
        lv_obj_align(bar, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COL_ACCENT2), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    s_base_ms = timer_get_ms();
    /* 默认从 10:08 起, 贴近 SiFli demo 观感 */
    s_base_tod = 10u * 3600u + 8u * 60u;

    if (s_tm) {
        lv_timer_del(s_tm);
    }
    s_tm = lv_timer_create(clock_tick, 200, NULL);
    clock_tick(NULL);
    return root;
}
