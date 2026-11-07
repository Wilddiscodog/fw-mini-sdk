#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_apps.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_fall.h"
#include "imu.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_step_lab;
static lv_obj_t *s_status_lab;
static lv_obj_t *s_act_lab;
static lv_obj_t *s_btn_lab;
static lv_obj_t *s_btn;
static lv_obj_t *s_fall_btn;
static lv_obj_t *s_fall_lab;
static lv_timer_t *s_tm;
static u8 s_ui_alive;

static const char *activity_text(u8 cls)
{
    switch (cls) {
    case 1:
        return "活动: 走";
    case 2:
        return "活动: 跑";
    default:
        return "活动: 未知";
    }
}

static void ui_apply_fall_btn(void)
{
    u8 on = watch_fall_alert_get();

    if (!s_ui_alive) {
        return;
    }
    if (s_fall_lab) {
        lv_label_set_text(s_fall_lab, on ? "关闭摔倒提醒" : "开启摔倒提醒");
    }
    if (s_fall_btn) {
        lv_obj_set_style_bg_color(s_fall_btn,
                                  lv_color_hex(on ? WATCH_COL_DANGER : 0x5C6BC0), 0);
    }
}

static void ui_apply_state(void)
{
    char buf[48];
    u8 on = imu_pedo_enabled();

    if (!s_ui_alive) {
        return;
    }
    if (s_btn_lab) {
        lv_label_set_text(s_btn_lab, on ? "关闭检测" : "开启检测");
    }
    if (s_btn) {
        lv_obj_set_style_bg_color(s_btn,
                                  lv_color_hex(on ? WATCH_COL_DANGER : WATCH_COL_OK), 0);
    }
    if (s_status_lab) {
        if (!imu_ready()) {
            lv_label_set_text(s_status_lab, "IMU未就绪");
        } else if (!imu_pedo_supported()) {
            snprintf(buf, sizeof(buf), "%s 不支持计步", imu_chip_name());
            lv_label_set_text(s_status_lab, buf);
        } else {
            lv_label_set_text(s_status_lab, on ? "检测中..." : "已关闭");
        }
    }
    ui_apply_fall_btn();
}

static void pedo_refresh(lv_timer_t *t)
{
    struct imu_pedo pedo;
    char buf[40];

    (void)t;
    if (!s_ui_alive) {
        return;
    }
    if (!imu_pedo_enabled()) {
        return;
    }
    if (imu_pedo_read(&pedo) == 0) {
        if (s_step_lab) {
            snprintf(buf, sizeof(buf), "%u", pedo.step_count);
            lv_label_set_text(s_step_lab, buf);
        }
        if (s_act_lab) {
            lv_label_set_text(s_act_lab, activity_text(pedo.activity));
        }
    }
}

static void toggle_cb(lv_event_t *e)
{
    int err;

    (void)e;
    if (!imu_ready()) {
        watch_ui_toast("IMU未就绪");
        return;
    }
    if (!imu_pedo_supported()) {
        watch_ui_toast("当前芯片无计步");
        return;
    }
    if (imu_pedo_enabled()) {
        imu_pedo_disable();
        if (s_step_lab) {
            lv_label_set_text(s_step_lab, "0");
        }
        if (s_act_lab) {
            lv_label_set_text(s_act_lab, "活动: —");
        }
        watch_ui_toast("已关闭步数检测");
    } else {
        err = imu_pedo_enable();
        if (err) {
            watch_ui_toast("开启失败");
        } else {
            if (s_step_lab) {
                lv_label_set_text(s_step_lab, "0");
            }
            watch_ui_toast("已开启, 请走动");
        }
    }
    ui_apply_state();
}

static void fall_toggle_cb(lv_event_t *e)
{
    (void)e;
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
    if (!imu_ready()) {
        watch_ui_toast("IMU未就绪");
        return;
    }
    if (watch_fall_alert_get()) {
        watch_fall_alert_set(0);
        watch_ui_toast("已关闭摔倒提醒");
    } else {
        watch_fall_alert_set(1);
        watch_ui_toast("摔倒提醒已开");
    }
    ui_apply_fall_btn();
#else
    watch_ui_toast("音频未启用");
#endif
}

static void pedo_del(lv_event_t *e)
{
    (void)e;
    s_ui_alive = 0;
    watch_fall_stop(); /* 退出即停摔倒检测, 不后台跑 */
    if (s_tm) {
        lv_timer_del(s_tm);
        s_tm = NULL;
    }
    s_step_lab = NULL;
    s_status_lab = NULL;
    s_act_lab = NULL;
    s_btn_lab = NULL;
    s_btn = NULL;
    s_fall_btn = NULL;
    s_fall_lab = NULL;
}

lv_obj_t *app_pedo_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_t *body;
    lv_obj_t *hint;
    lv_obj_t *lab;

    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    watch_ui_create_app_chrome(root, "步数检测");
    lv_obj_add_event_cb(root, pedo_del, LV_EVENT_DELETE, NULL);

    body = lv_obj_create(root);
    lv_obj_set_size(body, LCD_W - 16, LCD_H - 44);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(body, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x26C6DA), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 18, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_status_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_status_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_status_lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_status_lab, "准备中...");
    lv_obj_align(s_status_lab, LV_ALIGN_TOP_LEFT, 0, 0);

    lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(lab, "累计步数");
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 24);

    s_step_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_step_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_step_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(s_step_lab, "0");
    lv_obj_align(s_step_lab, LV_ALIGN_TOP_MID, 0, 46);

    s_act_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_act_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_act_lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_act_lab, "活动: —");
    lv_obj_align(s_act_lab, LV_ALIGN_TOP_MID, 0, 70);

    hint = lv_label_create(body);
    lv_obj_set_style_text_font(hint, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LCD_W - 40);
    lv_label_set_text(hint, "仅本页测试摔倒提醒");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -84);

    s_btn = lv_btn_create(body);
    lv_obj_set_size(s_btn, LCD_W - 48, 34);
    lv_obj_align(s_btn, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_radius(s_btn, 14, 0);
    lv_obj_set_style_shadow_width(s_btn, 0, 0);
    lv_obj_add_event_cb(s_btn, toggle_cb, LV_EVENT_CLICKED, NULL);
    s_btn_lab = lv_label_create(s_btn);
    lv_obj_set_style_text_font(s_btn_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_btn_lab, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_btn_lab, "开启检测");
    lv_obj_center(s_btn_lab);

    s_fall_btn = lv_btn_create(body);
    lv_obj_set_size(s_fall_btn, LCD_W - 48, 34);
    lv_obj_align(s_fall_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_radius(s_fall_btn, 14, 0);
    lv_obj_set_style_shadow_width(s_fall_btn, 0, 0);
    lv_obj_add_event_cb(s_fall_btn, fall_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_fall_lab = lv_label_create(s_fall_btn);
    lv_obj_set_style_text_font(s_fall_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_fall_lab, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_fall_lab, "开启摔倒提醒");
    lv_obj_center(s_fall_lab);

    s_ui_alive = 1;
    imu_init();
    watch_fall_init();
    if (imu_pedo_enabled()) {
        struct imu_pedo pedo;
        if (imu_pedo_read(&pedo) == 0 && s_step_lab) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%u", pedo.step_count);
            lv_label_set_text(s_step_lab, buf);
            if (s_act_lab) {
                lv_label_set_text(s_act_lab, activity_text(pedo.activity));
            }
        }
    }
    ui_apply_state();
    s_tm = lv_timer_create(pedo_refresh, 400, NULL);
    return root;
}

void app_pedo_exit(void)
{
    s_ui_alive = 0;
    watch_fall_stop();
    if (s_tm) {
        lv_timer_del(s_tm);
        s_tm = NULL;
    }
}
