#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_apps.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_game_bgm.h"
#include "bt_music.h"
#include "walkie.h"
#include "roobo_asr.h"
#include <stdio.h>
#include <string.h>

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT \
    && (defined CONFIG_ASR_ALGORITHM) && (CONFIG_ASR_ALGORITHM == ROOBO_ALGORITHM)

extern void bt_music_audio_takeover(void);

static lv_obj_t *s_status;
static lv_obj_t *s_result;
static lv_obj_t *s_hist;
static char s_hist_buf[160];
static char s_hit_text[32];
static u8 s_ui_alive;

static lv_obj_t *make_app_root(lv_obj_t *parent, const char *title)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    watch_ui_create_app_chrome(root, title);
    return root;
}

static void hist_push(const char *text)
{
    char line[40];
    char tmp[sizeof(s_hist_buf)];
    int n = 0;
    char *p;

    snprintf(line, sizeof(line), "%s", text);
    if (s_hist_buf[0] == '\0') {
        snprintf(s_hist_buf, sizeof(s_hist_buf), "%s", line);
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s\n%s", line, s_hist_buf);
    p = tmp;
    while (*p && n < 4) {
        if (*p == '\n') {
            n++;
        }
        p++;
    }
    if (n >= 4) {
        *p = '\0';
    }
    snprintf(s_hist_buf, sizeof(s_hist_buf), "%s", tmp);
}

static void asr_ui_apply(void *user_data)
{
    (void)user_data;
    if (!s_ui_alive || !s_result) {
        return;
    }
    lv_label_set_text(s_result, s_hit_text);
    hist_push(s_hit_text);
    if (s_hist) {
        lv_label_set_text(s_hist, s_hist_buf);
    }
    if (s_status) {
        lv_label_set_text(s_status, "监听中...");
    }
}

static void asr_on_result(int word_id, const char *text_utf8, void *priv)
{
    (void)word_id;
    (void)priv;
    snprintf(s_hit_text, sizeof(s_hit_text), "%s",
             text_utf8 ? text_utf8 : "识别到命令");
    lv_async_call(asr_ui_apply, NULL);
}

static void asr_app_delete(lv_event_t *e)
{
    (void)e;
    s_ui_alive = 0;
    s_status = NULL;
    s_result = NULL;
    s_hist = NULL;
    roobo_asr_stop();
}

static void asr_take_mic(void)
{
    bt_music_audio_takeover();
    watch_game_bgm_stop();
    walkie_session_end();
}

lv_obj_t *app_asr_test_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "语音识别测试");
    lv_obj_t *body = lv_obj_create(root);
    int err;

    lv_obj_set_size(body, LCD_W - 16, LCD_H - 44);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(body, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 18, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(body);
    lv_obj_set_style_text_font(s_status, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_status, "正在启动...");
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 0, 0);

    s_result = lv_label_create(body);
    lv_obj_set_style_text_font(s_result, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_result, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(s_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_result, LCD_W - 40);
    lv_label_set_text(s_result, "—");
    lv_obj_align(s_result, LV_ALIGN_TOP_LEFT, 0, 28);

    s_hist = lv_label_create(body);
    lv_obj_set_style_text_font(s_hist, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_hist, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_long_mode(s_hist, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hist, LCD_W - 40);
    lv_label_set_text(s_hist,
                      "可说: 小艾小艾/播放音乐/\n暂停音乐/增大音量/\n减小音量/上一曲/下一曲");
    lv_obj_align(s_hist, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_hist_buf[0] = '\0';
    s_ui_alive = 1;
    lv_obj_add_event_cb(root, asr_app_delete, LV_EVENT_DELETE, NULL);

    asr_take_mic();
    err = roobo_asr_start(16000, asr_on_result, NULL);
    if (err) {
        lv_label_set_text(s_status, "启动失败");
        watch_ui_toast("语音识别启动失败");
    } else {
        lv_label_set_text(s_status, "监听中...");
    }
    return root;
}

void app_asr_test_exit(void)
{
    s_ui_alive = 0;
    roobo_asr_stop();
    s_status = NULL;
    s_result = NULL;
    s_hist = NULL;
}

#else /* Roobo ASR 未启用 */

lv_obj_t *app_asr_test_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    watch_ui_create_app_chrome(root, "语音识别测试");

    lv_obj_t *lab = lv_label_create(root);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(lab, "本地ASR未启用\n请打开音频与\nROOBO_ALGORITHM");
    lv_obj_align(lab, LV_ALIGN_CENTER, 0, 8);
    return root;
}

void app_asr_test_exit(void)
{
}

#endif
