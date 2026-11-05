#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "fw_version.h"
#include "watch_apps.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_volume.h"
#include "watch_game_bgm.h"
#include "watch_backlight.h"

#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
#include "wifi/wifi_connect.h"
#include "lwip.h"
#endif

extern const char *bt_get_local_name(void);
extern u8 get_total_connect_dev(void);

#ifdef CONFIG_NET_MUSIC_MODE_ENABLE
extern int get_app_music_volume(void);
#endif

#include "bt_music.h"
#include "imu.h"
#include "walkie.h"

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

static lv_obj_t *make_body(lv_obj_t *root)
{
    lv_obj_t *body = lv_obj_create(root);
    lv_obj_set_size(body, LCD_W - 16, LCD_H - 44);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(body, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 18, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    return body;
}

static void stub_btn_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_toast("功能开发中");
}

/* -------- 陀螺仪 (SC7U22 / ICM-42607-C 自动探测) -------- */
static lv_obj_t *s_gyro_lab;
static lv_timer_t *s_gyro_tm;

/* JL newlib 无 %f: 转定点再 %d 打印 */
static int q_round(float v, int scale)
{
    float s = (float)scale;
    return (int)(v * s + (v >= 0.0f ? 0.5f : -0.5f));
}

static int snprint_q(char *dst, int dst_sz, int q, int scale)
{
    int neg = q < 0;
    int a = neg ? -q : q;
    int ip = a / scale;
    int fp = a % scale;
    if (scale == 100) {
        return snprintf(dst, dst_sz, "%s%d.%02d", neg ? "-" : "+", ip, fp);
    }
    return snprintf(dst, dst_sz, "%s%d.%d", neg ? "-" : "+", ip, fp);
}

static void gyro_refresh(lv_timer_t *t)
{
    char buf[192];
    char ax[12], ay[12], az[12], gx[12], gy[12], gz[12], tc[12];
    struct sc7u22_si s;
    (void)t;

    if (!s_gyro_lab) {
        return;
    }
    if (!imu_ready()) {
        lv_label_set_text(s_gyro_lab, "IMU 未就绪\n检查 PH0/PH1 总线");
        return;
    }
    if (imu_read_si(&s)) {
        lv_label_set_text(s_gyro_lab, "读数失败");
        return;
    }
    snprint_q(ax, sizeof(ax), q_round(s.ax, 100), 100);
    snprint_q(ay, sizeof(ay), q_round(s.ay, 100), 100);
    snprint_q(az, sizeof(az), q_round(s.az, 100), 100);
    snprint_q(gx, sizeof(gx), q_round(s.gx, 10), 10);
    snprint_q(gy, sizeof(gy), q_round(s.gy, 10), 10);
    snprint_q(gz, sizeof(gz), q_round(s.gz, 10), 10);
    snprint_q(tc, sizeof(tc), q_round(s.temp_c, 10), 10);

    snprintf(buf, sizeof(buf),
             "%s\n"
             "ID:0x%02X\n"
             "Acc g:\n"
             " X:%s Y:%s Z:%s\n"
             "Gyro dps:\n"
             " X:%s Y:%s Z:%s\n"
             "T:%sC",
             imu_chip_name(),
             imu_whoami(),
             ax, ay, az, gx, gy, gz, tc);
    lv_label_set_text(s_gyro_lab, buf);
}

static void gyro_del(lv_event_t *e)
{
    (void)e;
    if (s_gyro_tm) {
        lv_timer_del(s_gyro_tm);
        s_gyro_tm = NULL;
    }
    s_gyro_lab = NULL;
}

lv_obj_t *app_gyro_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "陀螺仪");
    lv_obj_t *body = make_body(root);
    lv_obj_add_event_cb(root, gyro_del, LV_EVENT_DELETE, NULL);

    if (imu_init()) {
        watch_ui_toast("陀螺仪初始化失败");
    }

    s_gyro_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_gyro_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_gyro_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(s_gyro_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_gyro_lab, LCD_W - 40);
    lv_obj_align(s_gyro_lab, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(s_gyro_lab, "读取中...");

    gyro_refresh(NULL);
    s_gyro_tm = lv_timer_create(gyro_refresh, 200, NULL);
    return root;
}

/* -------- 网络/DLNA -------- */
static void net_vol_dec(lv_event_t *e)
{
    (void)e;
    u8 v = watch_volume_get();
    if (v > WATCH_VOL_MIN + 5) {
        watch_volume_set(v - 5);
    } else {
        watch_volume_set(WATCH_VOL_MIN);
    }
    watch_volume_persist();
    watch_ui_toast("音量已调");
}

static void net_vol_inc(lv_event_t *e)
{
    (void)e;
    u8 v = watch_volume_get();
    if (v + 5 < WATCH_VOL_MAX) {
        watch_volume_set(v + 5);
    } else {
        watch_volume_set(WATCH_VOL_MAX);
    }
    watch_volume_persist();
    watch_ui_toast("音量已调");
}

lv_obj_t *app_net_music_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "网络播放");
    lv_obj_t *body = make_body(root);

    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, LCD_W - 40);

    char buf[160];
#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
    int st = wifi_get_sta_connect_state();
    snprintf(buf, sizeof(buf),
             "DLNA 网络播放器\n等待手机推送音频\nWiFi:%s\n音量:%d",
             (st == WIFI_STA_NETWORK_STACK_DHCP_SUCC) ? "已连接" : "未就绪",
             watch_volume_get());
#else
    snprintf(buf, sizeof(buf), "DLNA 网络播放器\nWiFi未启用\n音量:%d", watch_volume_get());
#endif
    lv_label_set_text(lab, buf);
    lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *dec = lv_btn_create(body);
    lv_obj_set_size(dec, 80, 32);
    lv_obj_align(dec, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(dec, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_add_event_cb(dec, net_vol_dec, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dec);
    lv_obj_set_style_text_font(dl, &lv_font_watch_16_cn, 0);
    lv_label_set_text(dl, "音量-");
    lv_obj_center(dl);

    lv_obj_t *inc = lv_btn_create(body);
    lv_obj_set_size(inc, 80, 32);
    lv_obj_align(inc, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(inc, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_add_event_cb(inc, net_vol_inc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(inc);
    lv_obj_set_style_text_font(il, &lv_font_watch_16_cn, 0);
    lv_label_set_text(il, "音量+");
    lv_obj_center(il);
    return root;
}

/* -------- 蓝牙音乐 -------- */
static void bt_play_cb(lv_event_t *e)
{
    (void)e;
#if (defined MINI_BT_SPEAKER_EN) && MINI_BT_SPEAKER_EN
    bt_music_ui_play_pause();
    watch_ui_toast("播放/暂停");
#else
    watch_ui_toast("蓝牙未启用");
#endif
}

static void bt_vdec(lv_event_t *e)
{
    (void)e;
#if (defined MINI_BT_SPEAKER_EN) && MINI_BT_SPEAKER_EN
    bt_music_ui_vol_step(-5);
#endif
    u8 v = watch_volume_get();
    watch_volume_set(v > 10 ? v - 5 : WATCH_VOL_MIN);
    watch_volume_persist();
    watch_ui_toast("音量-");
}

static void bt_vinc(lv_event_t *e)
{
    (void)e;
#if (defined MINI_BT_SPEAKER_EN) && MINI_BT_SPEAKER_EN
    bt_music_ui_vol_step(5);
#endif
    u8 v = watch_volume_get();
    watch_volume_set(v < 95 ? v + 5 : WATCH_VOL_MAX);
    watch_volume_persist();
    watch_ui_toast("音量+");
}

lv_obj_t *app_bt_music_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "蓝牙音乐");
    lv_obj_t *body = make_body(root);

    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, LCD_W - 40);

    char buf[160];
    const char *name = bt_get_local_name();
    u8 n = get_total_connect_dev();
    snprintf(buf, sizeof(buf), "本机:%s\n连接数:%d\nA2DP蓝牙音箱",
             name ? name : "-", n);
    lv_label_set_text(lab, buf);
    lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *play = lv_btn_create(body);
    lv_obj_set_size(play, 100, 32);
    lv_obj_align(play, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(play, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_obj_add_event_cb(play, bt_play_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(play);
    lv_obj_set_style_text_font(pl, &lv_font_watch_16_cn, 0);
    lv_label_set_text(pl, "播放暂停");
    lv_obj_center(pl);

    lv_obj_t *dec = lv_btn_create(body);
    lv_obj_set_size(dec, 70, 28);
    lv_obj_align(dec, LV_ALIGN_BOTTOM_LEFT, 4, -8);
    lv_obj_set_style_bg_color(dec, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_add_event_cb(dec, bt_vdec, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dec);
    lv_label_set_text(dl, "Vol-");
    lv_obj_center(dl);

    lv_obj_t *inc = lv_btn_create(body);
    lv_obj_set_size(inc, 70, 28);
    lv_obj_align(inc, LV_ALIGN_BOTTOM_RIGHT, -4, -8);
    lv_obj_set_style_bg_color(inc, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_add_event_cb(inc, bt_vinc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(inc);
    lv_label_set_text(il, "Vol+");
    lv_obj_center(il);
    return root;
}

/* -------- 对讲机: 联系人头像列表 → 聊天页; 仅按键 PTT -------- */
#define WALKIE_AVATAR_SZ  40

static const u32 s_wk_avatar_col[] = {
    0x4FC3F7, 0x7E57C2, 0x3DFFB0, 0xFF5577,
    0xFFB74D, 0x81C784, 0x64B5F6, 0xF06292,
};

static lv_obj_t *s_wk_root;
static lv_obj_t *s_wk_contacts;
static lv_obj_t *s_wk_hint;
static lv_obj_t *s_wk_chat;
static lv_obj_t *s_wk_chat_msgs;
static lv_obj_t *s_wk_chat_status;
static lv_obj_t *s_wk_chat_title;
static lv_obj_t *s_wk_mgr;
static lv_obj_t *s_wk_mgr_list;
static lv_timer_t *s_wk_tm;
static u8 s_wk_in_chat;
static u8 s_wk_in_mgr;
static int s_wk_chat_peer = -1;
static u32 s_wk_chat_peer_ip;
static int s_wk_last_rec_n = -1;

static void walkie_ui_rebuild_contacts(void);
static void walkie_chat_refresh_msgs(void);
static void walkie_close_chat(void);
static void walkie_mgr_rebuild(void);
static void walkie_mgr_close(void);

static void walkie_chat_back_cb(lv_event_t *e)
{
    (void)e;
    walkie_close_chat();
    walkie_ui_rebuild_contacts();
}

static void walkie_avatar_letter(char *out, int out_sz, const char *name)
{
    if (!out || out_sz < 2) {
        return;
    }
    if (name && name[0]) {
        out[0] = name[0];
        out[1] = '\0';
    } else {
        out[0] = '?';
        out[1] = '\0';
    }
}

static lv_obj_t *walkie_make_avatar(lv_obj_t *parent, int idx, const char *name)
{
    char letter[4];
    lv_obj_t *av = lv_obj_create(parent);
    lv_obj_t *lab;

    lv_obj_set_size(av, WALKIE_AVATAR_SZ, WALKIE_AVATAR_SZ);
    lv_obj_set_style_radius(av, 8, 0);
    lv_obj_set_style_bg_color(av,
                              lv_color_hex(s_wk_avatar_col[idx % (int)(sizeof(s_wk_avatar_col) / sizeof(s_wk_avatar_col[0]))]),
                              0);
    lv_obj_set_style_border_width(av, 0, 0);
    lv_obj_set_style_pad_all(av, 0, 0);
    lv_obj_clear_flag(av, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(av, LV_OBJ_FLAG_EVENT_BUBBLE);

    walkie_avatar_letter(letter, sizeof(letter), name);
    lab = lv_label_create(av);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(0x101018), 0);
    lv_label_set_text(lab, letter);
    lv_obj_center(lab);
    lv_obj_add_flag(lab, LV_OBJ_FLAG_EVENT_BUBBLE);
    return av;
}

static void walkie_peer_row_click(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    const struct walkie_peer *p = walkie_peer_get(idx);

    if (!p) {
        return;
    }
    if (!p->alive || !p->ip) {
        watch_ui_toast("设备离线");
        walkie_net_discover_now();
        return;
    }
    walkie_peer_select(idx);
    /* 进入聊天即配对保存 */
    if (!p->paired) {
        (void)walkie_contact_save_peer(idx);
    }
    s_wk_chat_peer = idx;
    s_wk_chat_peer_ip = p->ip;
    /* 进入聊天页 (覆盖整页) */
    {
        lv_obj_t *bar, *back, *bl, *body;
        char title[24];

        if (s_wk_chat) {
            lv_obj_del(s_wk_chat);
            s_wk_chat = NULL;
        }
        s_wk_chat = lv_obj_create(s_wk_root);
        lv_obj_set_size(s_wk_chat, LCD_W, LCD_H);
        lv_obj_align(s_wk_chat, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(s_wk_chat, lv_color_hex(WATCH_COL_BG), 0);
        lv_obj_set_style_border_width(s_wk_chat, 0, 0);
        lv_obj_set_style_pad_all(s_wk_chat, 0, 0);
        lv_obj_set_style_radius(s_wk_chat, 0, 0);
        lv_obj_clear_flag(s_wk_chat, LV_OBJ_FLAG_SCROLLABLE);

        bar = lv_obj_create(s_wk_chat);
        lv_obj_set_size(bar, LCD_W, 32);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COL_CARD), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 4, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        back = lv_btn_create(bar);
        lv_obj_set_size(back, 56, 24);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(WATCH_COL_CARD_HL), 0);
        lv_obj_set_style_radius(back, 6, 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_add_event_cb(back, walkie_chat_back_cb, LV_EVENT_CLICKED, NULL);
        bl = lv_label_create(back);
        lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(bl, lv_color_hex(WATCH_COL_ACCENT), 0);
        lv_label_set_text(bl, "返回");
        lv_obj_center(bl);

        s_wk_chat_title = lv_label_create(bar);
        lv_obj_set_style_text_font(s_wk_chat_title, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(s_wk_chat_title, lv_color_hex(WATCH_COL_TEXT), 0);
        strncpy(title, p->name, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
        lv_label_set_text(s_wk_chat_title, title);
        lv_obj_align(s_wk_chat_title, LV_ALIGN_RIGHT_MID, -8, 0);

        body = lv_obj_create(s_wk_chat);
        lv_obj_set_size(body, LCD_W - 8, LCD_H - 36);
        lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_set_style_bg_color(body, lv_color_hex(WATCH_COL_CARD), 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_radius(body, 8, 0);
        lv_obj_set_style_pad_all(body, 6, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        s_wk_chat_msgs = lv_obj_create(body);
        lv_obj_set_size(s_wk_chat_msgs, LCD_W - 24, LCD_H - 78);
        lv_obj_align(s_wk_chat_msgs, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(s_wk_chat_msgs, lv_color_hex(WATCH_COL_CARD_HL), 0);
        lv_obj_set_style_border_width(s_wk_chat_msgs, 0, 0);
        lv_obj_set_style_radius(s_wk_chat_msgs, 6, 0);
        lv_obj_set_style_pad_all(s_wk_chat_msgs, 4, 0);
        lv_obj_set_flex_flow(s_wk_chat_msgs, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_wk_chat_msgs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(s_wk_chat_msgs, LV_DIR_VER);

        s_wk_chat_status = lv_label_create(body);
        lv_obj_set_style_text_font(s_wk_chat_status, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(s_wk_chat_status, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(s_wk_chat_status, "按侧键录音, 松开发送");
        lv_obj_align(s_wk_chat_status, LV_ALIGN_BOTTOM_MID, 0, -2);

        s_wk_in_chat = 1;
        s_wk_last_rec_n = -1;
        walkie_chat_refresh_msgs();
    }
}

static void walkie_close_chat(void)
{
    walkie_ptt_stop();
    walkie_peer_select(-1);
    s_wk_in_chat = 0;
    s_wk_chat_peer = -1;
    s_wk_chat_peer_ip = 0;
    s_wk_chat_msgs = NULL;
    s_wk_chat_status = NULL;
    s_wk_chat_title = NULL;
    if (s_wk_chat) {
        lv_obj_del(s_wk_chat);
        s_wk_chat = NULL;
    }
}

static void walkie_bubble_click(lv_event_t *e)
{
    int rec_idx = (int)(uintptr_t)lv_event_get_user_data(e);

    if (walkie_rec_play(rec_idx) != 0) {
        watch_ui_toast("无法播放");
    }
}

static void walkie_chat_add_bubble(int rec_idx, u8 dir, u16 sec)
{
    lv_obj_t *row, *bubble, *lab;
    char buf[24];
    lv_coord_t bw = (LCD_W * 7) / 10; /* ~70% 屏宽 */
    lv_coord_t bh = 44;

    if (!s_wk_chat_msgs) {
        return;
    }
    row = lv_obj_create(s_wk_chat_msgs);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, bh + 8);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 3, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, walkie_bubble_click, LV_EVENT_CLICKED, (void *)(uintptr_t)rec_idx);

    bubble = lv_obj_create(row);
    lv_obj_set_size(bubble, bw, bh);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_pad_hor(bubble, 12, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(bubble, walkie_bubble_click, LV_EVENT_CLICKED, (void *)(uintptr_t)rec_idx);
    if (dir) {
        /* RX: 左侧青绿气泡 */
        lv_obj_set_style_bg_color(bubble, lv_color_hex(WATCH_COL_BUBBLE), 0);
        lv_obj_align(bubble, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        /* TX: 右侧青绿气泡 */
        lv_obj_set_style_bg_color(bubble, lv_color_hex(WATCH_COL_BUBBLE), 0);
        lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    snprintf(buf, sizeof(buf), "语音 %us", (unsigned)(sec ? sec : 1));
    lab = lv_label_create(bubble);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(0x000000), 0);
    lv_label_set_text(lab, buf);
    lv_obj_center(lab);
    lv_obj_add_flag(lab, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void walkie_chat_refresh_msgs(void)
{
    int i, n;
    u8 dir;
    u16 sec;
    u32 pip, peer_ip;

    if (!s_wk_chat_msgs || !s_wk_in_chat) {
        return;
    }
    peer_ip = s_wk_chat_peer_ip ? s_wk_chat_peer_ip : walkie_peer_selected_ip();
    /* 保持选中当前聊天对象, 防止发送中 peer 表抖动丢选中 */
    if (peer_ip) {
        walkie_peer_select_ip(peer_ip);
    }
    lv_obj_clean(s_wk_chat_msgs);
    n = walkie_rec_count();
    if (n == 0 || peer_ip == 0) {
        lv_obj_t *lab = lv_label_create(s_wk_chat_msgs);
        lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(lab, "暂无语音\n按侧键开始");
        return;
    }
    for (i = 0; i < n; i++) {
        if (walkie_rec_meta(i, &dir, &sec, &pip) != 0) {
            continue;
        }
        if (pip != peer_ip) {
            continue;
        }
        walkie_chat_add_bubble(i, dir, sec);
    }
    lv_obj_scroll_to_y(s_wk_chat_msgs, LV_COORD_MAX, LV_ANIM_OFF);
}

static void walkie_mgr_del_cb(lv_event_t *e)
{
    int cidx = (int)(uintptr_t)lv_event_get_user_data(e);

    if (walkie_contact_delete(cidx) == 0) {
        watch_ui_toast("已删除");
    }
    walkie_mgr_rebuild();
    walkie_ui_rebuild_contacts();
}

static void walkie_mgr_save_cb(lv_event_t *e)
{
    int pidx = (int)(uintptr_t)lv_event_get_user_data(e);

    if (walkie_contact_save_peer(pidx) == 0) {
        watch_ui_toast("已保存");
    } else {
        watch_ui_toast("保存失败");
    }
    walkie_mgr_rebuild();
    walkie_ui_rebuild_contacts();
}

static void walkie_mgr_back_cb(lv_event_t *e)
{
    (void)e;
    walkie_mgr_close();
    walkie_ui_rebuild_contacts();
}

static void walkie_mgr_scan_finish(lv_timer_t *t)
{
    int n = walkie_peer_count();
    int online = 0, i;
    char tip[40];
    (void)t;

    for (i = 0; i < n; i++) {
        const struct walkie_peer *p = walkie_peer_get(i);
        if (p && p->alive) {
            online++;
        }
    }
    snprintf(tip, sizeof(tip), "发现 %d 台在线", online);
    watch_ui_toast(tip);
    if (s_wk_in_mgr) {
        walkie_mgr_rebuild();
    }
}

static void walkie_mgr_scan_cb(lv_event_t *e)
{
    int r;
    lv_timer_t *tm;
    (void)e;

    walkie_on_enter();
    r = walkie_net_scan();
    if (r == -2) {
        watch_ui_toast("请先连接WiFi");
        return;
    }
    if (r != 0) {
        watch_ui_toast("网络未就绪");
        return;
    }
    watch_ui_toast("正在扫描...");
    tm = lv_timer_create(walkie_mgr_scan_finish, 2000, NULL);
    if (tm) {
        lv_timer_set_repeat_count(tm, 1);
    }
}

static void walkie_mgr_rebuild(void)
{
    int i, n, cn;
    const struct walkie_peer *p;
    lv_obj_t *lab;

    if (!s_wk_mgr_list || !s_wk_in_mgr) {
        return;
    }
    lv_obj_clean(s_wk_mgr_list);

    cn = walkie_contact_count();
    lab = lv_label_create(s_wk_mgr_list);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(lab, "已保存");

    if (cn == 0) {
        lab = lv_label_create(s_wk_mgr_list);
        lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(lab, "暂无联系人");
    }
    for (i = 0; i < cn; i++) {
        char name[WALKIE_PEER_NAME_LEN];
        char line[40];
        u8 alive = 0;
        u32 ip = 0;
        lv_obj_t *row, *btn, *bl;

        if (walkie_contact_info(i, NULL, name, sizeof(name), &alive, &ip) != 0) {
            continue;
        }
        row = lv_obj_create(s_wk_mgr_list);
        lv_obj_set_size(row, LCD_W - 28, 40);
        lv_obj_set_style_bg_color(row, lv_color_hex(WATCH_COL_CARD_HL), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lab = lv_label_create(row);
        lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
        snprintf(line, sizeof(line), "%s %s",
                 name[0] ? name : "设备", alive ? "在线" : "离线");
        lv_label_set_text(lab, line);
        lv_obj_set_width(lab, LCD_W - 28 - 56);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 2, 0);

        btn = lv_btn_create(row);
        lv_obj_set_size(btn, 48, 28);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_DANGER), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, walkie_mgr_del_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        bl = lv_label_create(btn);
        lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(bl, "删除");
        lv_obj_center(bl);
    }

    lab = lv_label_create(s_wk_mgr_list);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(lab, "附近未保存");

    n = walkie_peer_count();
    {
        int shown = 0;
        for (i = 0; i < n; i++) {
            lv_obj_t *row, *btn, *bl;

            p = walkie_peer_get(i);
            if (!p || !p->alive || p->paired) {
                continue;
            }
            shown = 1;
            row = lv_obj_create(s_wk_mgr_list);
            lv_obj_set_size(row, LCD_W - 28, 40);
            lv_obj_set_style_bg_color(row, lv_color_hex(WATCH_COL_CARD_HL), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 8, 0);
            lv_obj_set_style_pad_all(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            lab = lv_label_create(row);
            lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
            lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
            lv_label_set_text(lab, p->name[0] ? p->name : "设备");
            lv_obj_set_width(lab, LCD_W - 28 - 56);
            lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
            lv_obj_align(lab, LV_ALIGN_LEFT_MID, 2, 0);

            btn = lv_btn_create(row);
            lv_obj_set_size(btn, 48, 28);
            lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_OK), 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, walkie_mgr_save_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
            bl = lv_label_create(btn);
            lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
            lv_obj_set_style_text_color(bl, lv_color_hex(0x101018), 0);
            lv_label_set_text(bl, "保存");
            lv_obj_center(bl);
        }
        if (!shown) {
            lab = lv_label_create(s_wk_mgr_list);
            lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
            lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
            lv_label_set_text(lab, "暂无新设备");
        }
    }
}

static void walkie_mgr_close(void)
{
    s_wk_in_mgr = 0;
    s_wk_mgr_list = NULL;
    if (s_wk_mgr) {
        lv_obj_del(s_wk_mgr);
        s_wk_mgr = NULL;
    }
}

static void walkie_mgr_open(void)
{
    lv_obj_t *bar, *back, *bl, *title, *body, *scan, *sl;

    if (!s_wk_root || s_wk_in_chat) {
        return;
    }
    if (s_wk_mgr) {
        walkie_mgr_close();
    }
    walkie_on_enter();
    walkie_net_discover_now();

    s_wk_mgr = lv_obj_create(s_wk_root);
    lv_obj_set_size(s_wk_mgr, LCD_W, LCD_H);
    lv_obj_align(s_wk_mgr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_wk_mgr, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(s_wk_mgr, 0, 0);
    lv_obj_set_style_pad_all(s_wk_mgr, 0, 0);
    lv_obj_set_style_radius(s_wk_mgr, 0, 0);
    lv_obj_clear_flag(s_wk_mgr, LV_OBJ_FLAG_SCROLLABLE);

    bar = lv_obj_create(s_wk_mgr);
    lv_obj_set_size(bar, LCD_W, 32);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 56, 24);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_set_style_radius(back, 6, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, walkie_mgr_back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(title, "联系人管理");
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, -6, 0);

    /* 列表区留出底部扫描按钮高度 */
    body = lv_obj_create(s_wk_mgr);
    lv_obj_set_size(body, LCD_W - 8, LCD_H - 36 - 40);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(body, lv_color_hex(WATCH_COL_CARD), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    s_wk_mgr_list = body;
    lv_obj_set_flex_flow(s_wk_mgr_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_wk_mgr_list, 4, 0);

    scan = lv_btn_create(s_wk_mgr);
    lv_obj_set_size(scan, LCD_W - 16, 34);
    lv_obj_align(scan, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(scan, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_obj_set_style_radius(scan, 12, 0);
    lv_obj_set_style_shadow_width(scan, 0, 0);
    lv_obj_add_event_cb(scan, walkie_mgr_scan_cb, LV_EVENT_CLICKED, NULL);
    sl = lv_label_create(scan);
    lv_obj_set_style_text_font(sl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(sl, "设备扫描");
    lv_obj_center(sl);

    s_wk_in_mgr = 1;
    walkie_mgr_rebuild();
}

static void walkie_mgr_btn_cb(lv_event_t *e)
{
    (void)e;
    walkie_mgr_open();
}

static void walkie_ui_rebuild_contacts(void)
{
    int i, n;
    const struct walkie_peer *p;

    if (!s_wk_contacts || s_wk_in_chat || s_wk_in_mgr) {
        return;
    }
    lv_obj_clean(s_wk_contacts);
    n = walkie_peer_count();
    if (n == 0) {
        lv_obj_t *lab = lv_label_create(s_wk_contacts);
        lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(lab, "正在寻找设备...");
        lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 8);
        return;
    }
    for (i = 0; i < n; i++) {
        lv_obj_t *row, *name_lab;
        char sub[40];

        p = walkie_peer_get(i);
        if (!p) {
            continue;
        }
        row = lv_obj_create(s_wk_contacts);
        lv_obj_set_size(row, LCD_W - 36, WALKIE_AVATAR_SZ + 8);
        lv_obj_set_style_bg_color(row, lv_color_hex(WATCH_COL_CARD_HL), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, walkie_peer_row_click, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        walkie_make_avatar(row, i, p->name);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 0, 0);
        if (!p->alive) {
            lv_obj_set_style_opa(lv_obj_get_child(row, 0), LV_OPA_50, 0);
        }

        name_lab = lv_label_create(row);
        lv_obj_set_style_text_font(name_lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(name_lab, lv_color_hex(WATCH_COL_TEXT), 0);
        lv_label_set_text(name_lab, p->name[0] ? p->name : "设备");
        lv_obj_set_width(name_lab, LCD_W - 36 - WALKIE_AVATAR_SZ - 20);
        lv_label_set_long_mode(name_lab, LV_LABEL_LONG_CLIP);
        lv_obj_align(name_lab, LV_ALIGN_LEFT_MID, WALKIE_AVATAR_SZ + 10, -8);
        lv_obj_add_flag(name_lab, LV_OBJ_FLAG_EVENT_BUBBLE);

        if (p->alive && p->ip) {
            snprintf(sub, sizeof(sub), "%s %u.%u.%u.%u",
                     p->paired ? "在线" : "新设备",
                     (unsigned)(p->ip & 0xff),
                     (unsigned)((p->ip >> 8) & 0xff),
                     (unsigned)((p->ip >> 16) & 0xff),
                     (unsigned)((p->ip >> 24) & 0xff));
        } else {
            snprintf(sub, sizeof(sub), "%s", p->paired ? "离线" : "未在线");
        }
        name_lab = lv_label_create(row);
        lv_obj_set_style_text_font(name_lab, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(name_lab,
                                    lv_color_hex(p->alive ? WATCH_COL_OK : WATCH_COL_TEXT_DIM), 0);
        lv_label_set_text(name_lab, sub);
        lv_obj_align(name_lab, LV_ALIGN_LEFT_MID, WALKIE_AVATAR_SZ + 10, 10);
        lv_label_set_long_mode(name_lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(name_lab, LCD_W - 36 - WALKIE_AVATAR_SZ - 20);
        lv_obj_add_flag(name_lab, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void walkie_tick(lv_timer_t *t)
{
    int rec_n;
    static u8 disc_div;
    static u8 last_send_busy;
    (void)t;

    /* 仅在聊天页允许按键对讲; 联系人页不触发 */
    if (s_wk_in_chat) {
        walkie_ptt_poll();
    }

    /* 主界面定期主动发现局域网可对讲设备 (~1s, tick=50ms) */
    if (++disc_div >= 20) {
        disc_div = 0;
        walkie_net_discover_now();
    }

    if (walkie_peers_dirty_take()) {
        if (s_wk_in_chat) {
            if (s_wk_chat_peer_ip) {
                walkie_peer_select_ip(s_wk_chat_peer_ip);
            }
        } else if (s_wk_in_mgr) {
            walkie_mgr_rebuild();
        } else {
            walkie_ui_rebuild_contacts();
        }
    } else if (!s_wk_in_chat && !s_wk_in_mgr && (disc_div == 0)) {
        walkie_ui_rebuild_contacts();
    }

    if (s_wk_in_chat && s_wk_chat_status) {
        const char *st;
        u32 col = WATCH_COL_TEXT_DIM;
        if (walkie_msg_recording() || walkie_ptt_is_on()) {
            st = "录音中(松键发送)";
            col = WATCH_COL_DANGER;
        } else if (walkie_msg_sending()) {
            st = "发送中...";
            col = WATCH_COL_ACCENT;
            last_send_busy = 1;
        } else if (last_send_busy) {
            last_send_busy = 0;
            st = walkie_msg_last_send_ok() ? "已送达" : "发送失败";
            col = walkie_msg_last_send_ok() ? WATCH_COL_ACCENT : WATCH_COL_DANGER;
        } else {
            st = "按侧键录音, 松开发送";
        }
        lv_label_set_text(s_wk_chat_status, st);
        lv_obj_set_style_text_color(s_wk_chat_status, lv_color_hex(col), 0);
    } else if (s_wk_hint && !s_wk_in_mgr) {
        char buf[48];
        int online = 0, i, n = walkie_peer_count();
        for (i = 0; i < n; i++) {
            const struct walkie_peer *pp = walkie_peer_get(i);
            if (pp && pp->alive) {
                online++;
            }
        }
        snprintf(buf, sizeof(buf), "在线 %d | 点头像聊天", online);
        lv_label_set_text(s_wk_hint, buf);
    }

    /* 新消息弹窗/自动播放由全局 walkie_bg_rx_tm 处理; 此处只刷聊天气泡 */
    rec_n = walkie_rec_count();
    if (s_wk_in_chat && rec_n != s_wk_last_rec_n) {
        s_wk_last_rec_n = rec_n;
        walkie_chat_refresh_msgs();
    }
}

static void walkie_del_cb(lv_event_t *e)
{
    (void)e;
    if (s_wk_tm) {
        lv_timer_del(s_wk_tm);
        s_wk_tm = NULL;
    }
    s_wk_in_chat = 0;
    s_wk_in_mgr = 0;
    s_wk_chat_peer = -1;
    s_wk_chat_peer_ip = 0;
    s_wk_root = NULL;
    s_wk_contacts = NULL;
    s_wk_hint = NULL;
    s_wk_chat = NULL;
    s_wk_chat_msgs = NULL;
    s_wk_chat_status = NULL;
    s_wk_chat_title = NULL;
    s_wk_mgr = NULL;
    s_wk_mgr_list = NULL;
}

lv_obj_t *app_walkie_create(lv_obj_t *parent)
{
    /* 空标题: 右侧留给「联系人」按钮, 避免与「对讲机」叠字 */
    lv_obj_t *root = make_app_root(parent, "");
    lv_obj_t *body = make_body(root);
    lv_obj_t *mgr_btn, *ml;

    s_wk_root = root;
    s_wk_in_chat = 0;
    s_wk_in_mgr = 0;
    s_wk_chat = NULL;
    s_wk_mgr = NULL;
    s_wk_mgr_list = NULL;
    s_wk_chat_peer = -1;
    s_wk_chat_peer_ip = 0;
    s_wk_last_rec_n = -1;
    walkie_peer_select(-1);
    walkie_on_enter();
    walkie_net_discover_now();

    lv_obj_add_event_cb(root, walkie_del_cb, LV_EVENT_DELETE, NULL);

    mgr_btn = lv_btn_create(root);
    lv_obj_set_size(mgr_btn, 64, 24);
    lv_obj_align(mgr_btn, LV_ALIGN_TOP_RIGHT, -WATCH_SAFE_X - 2, WATCH_SAFE_Y + 4);
    lv_obj_set_style_bg_color(mgr_btn, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_obj_set_style_radius(mgr_btn, 10, 0);
    lv_obj_set_style_shadow_width(mgr_btn, 0, 0);
    lv_obj_add_event_cb(mgr_btn, walkie_mgr_btn_cb, LV_EVENT_CLICKED, NULL);
    ml = lv_label_create(mgr_btn);
    lv_obj_set_style_text_font(ml, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(ml, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(ml, "联系人");
    lv_obj_center(ml);
    lv_obj_move_foreground(mgr_btn);

    /* body: 纵向 flex, hint 固定一行, 列表占剩余高度 */
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 6, 0);

    s_wk_hint = lv_label_create(body);
    lv_obj_set_style_text_font(s_wk_hint, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_wk_hint, lv_color_hex(WATCH_COL_TEXT_DIM), 0);
    lv_label_set_text(s_wk_hint, "在线 0 | 点头像聊天");
    lv_obj_set_width(s_wk_hint, LCD_W - 40);
    lv_obj_set_style_text_align(s_wk_hint, LV_TEXT_ALIGN_LEFT, 0);

    s_wk_contacts = lv_obj_create(body);
    lv_obj_set_width(s_wk_contacts, LCD_W - 36);
    lv_obj_set_flex_grow(s_wk_contacts, 1);
    lv_obj_set_style_bg_opa(s_wk_contacts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wk_contacts, 0, 0);
    lv_obj_set_style_pad_row(s_wk_contacts, 6, 0);
    lv_obj_set_style_pad_all(s_wk_contacts, 0, 0);
    lv_obj_set_flex_flow(s_wk_contacts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_wk_contacts, LV_DIR_VER);

    walkie_ui_rebuild_contacts();
    s_wk_tm = lv_timer_create(walkie_tick, 50, NULL);
    return root;
}


/* -------- 设置 -------- */
static lv_obj_t *s_set_lab;
static lv_obj_t *s_set_sw;
static lv_obj_t *s_set_bl_lab;
static lv_obj_t *s_set_vol_lab;
static lv_obj_t *s_set_bgm_lab;
static lv_timer_t *s_set_tm;
static lv_timer_t *s_set_persist_tm;
static u8 s_set_dirty;

static void settings_persist_flush(void)
{
    if (!s_set_dirty) {
        return;
    }
    watch_volume_persist();
    watch_backlight_persist();
    watch_game_bgm_vol_persist();
    s_set_dirty = 0;
}

static void settings_persist_tm_cb(lv_timer_t *t)
{
    (void)t;
    settings_persist_flush();
    s_set_persist_tm = NULL;
}

static void settings_schedule_persist(void)
{
    s_set_dirty = 1;
    if (s_set_persist_tm) {
        lv_timer_reset(s_set_persist_tm);
        return;
    }
    s_set_persist_tm = lv_timer_create(settings_persist_tm_cb, 1500, NULL);
    if (s_set_persist_tm) {
        lv_timer_set_repeat_count(s_set_persist_tm, 1);
    }
}

static void settings_refresh(lv_timer_t *t)
{
    char buf[256];
    char ip[20] = "-";
    char ssid[33] = "-";
    const char *wifi_st = "关";
    (void)t;

    if (!s_set_lab) {
        return;
    }

#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
    if (wifi_is_on()) {
        struct wifi_mode_info info = {0};
        info.mode = STA_MODE;
        wifi_get_mode_cur_info(&info);
        if (info.ssid) {
            snprintf(ssid, sizeof(ssid), "%s", info.ssid);
        }
        if (wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
            wifi_st = "已连接";
            Get_IPAddress(WIFI_NETIF, ip);
        } else {
            wifi_st = "未连接";
        }
    }
#endif

    const char *btname = bt_get_local_name();
    u8 bt_n = get_total_connect_dev();
    snprintf(buf, sizeof(buf),
             "WiFi: %s\nSSID: %s\nIP: %s\n蓝牙: %s  连接:%d\n版本: %s",
             wifi_st, ssid, ip,
             btname ? btname : "-", bt_n,
             FW_VERSION);
    lv_label_set_text(s_set_lab, buf);
}

static void settings_autoplay_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    u8 on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;

    walkie_autoplay_set(on);
    watch_ui_toast(on ? "自动播放已开" : "自动播放已关");
}

static void settings_bl_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    char buf[24];

    if (v < WATCH_BL_MIN) {
        v = WATCH_BL_MIN;
        lv_slider_set_value(lv_event_get_target(e), v, LV_ANIM_OFF);
    }
    watch_backlight_set((u8)v);
    if (s_set_bl_lab) {
        snprintf(buf, sizeof(buf), "亮度 %d%%", v);
        lv_label_set_text(s_set_bl_lab, buf);
    }
    settings_schedule_persist();
}

static void settings_vol_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    char buf[24];

    watch_volume_set((u8)v);
    if (s_set_vol_lab) {
        snprintf(buf, sizeof(buf), "音量 %d", v);
        lv_label_set_text(s_set_vol_lab, buf);
    }
    settings_schedule_persist();
}

static void settings_bgm_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    char buf[32];

    watch_game_bgm_vol_set((u8)v);
    if (s_set_bgm_lab) {
        if (v <= 0) {
            lv_label_set_text(s_set_bgm_lab, "游戏音乐 关");
        } else {
            snprintf(buf, sizeof(buf), "游戏音乐 %d", v);
            lv_label_set_text(s_set_bgm_lab, buf);
        }
    }
    settings_schedule_persist();
}

static void settings_del(lv_event_t *e)
{
    (void)e;
    if (s_set_persist_tm) {
        lv_timer_del(s_set_persist_tm);
        s_set_persist_tm = NULL;
    }
    settings_persist_flush();
    if (s_set_tm) {
        lv_timer_del(s_set_tm);
        s_set_tm = NULL;
    }
    s_set_lab = NULL;
    s_set_sw = NULL;
    s_set_bl_lab = NULL;
    s_set_vol_lab = NULL;
    s_set_bgm_lab = NULL;
}

lv_obj_t *app_settings_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "设置");
    lv_obj_t *body = make_body(root);
    lv_obj_t *row;
    lv_obj_t *lab;
    lv_obj_t *sl;
    char buf[24];
    int y = 0;

    lv_obj_add_event_cb(root, settings_del, LV_EVENT_DELETE, NULL);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    /* 亮度 */
    s_set_bl_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_set_bl_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_set_bl_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    snprintf(buf, sizeof(buf), "亮度 %d%%", watch_backlight_get());
    lv_label_set_text(s_set_bl_lab, buf);
    lv_obj_align(s_set_bl_lab, LV_ALIGN_TOP_LEFT, 0, y);
    y += 22;

    sl = lv_slider_create(body);
    lv_obj_set_width(sl, LCD_W - 48);
    lv_obj_set_height(sl, 16);
    lv_slider_set_range(sl, WATCH_BL_MIN, WATCH_BL_MAX);
    lv_slider_set_value(sl, watch_backlight_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_CARD_HL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_ACCENT2), LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 10);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(sl, settings_bl_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 28;

    /* 音量 */
    s_set_vol_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_set_vol_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_set_vol_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    snprintf(buf, sizeof(buf), "音量 %d", watch_volume_get());
    lv_label_set_text(s_set_vol_lab, buf);
    lv_obj_align(s_set_vol_lab, LV_ALIGN_TOP_LEFT, 0, y);
    y += 22;

    sl = lv_slider_create(body);
    lv_obj_set_width(sl, LCD_W - 48);
    lv_obj_set_height(sl, 16);
    lv_slider_set_range(sl, WATCH_VOL_MIN, WATCH_VOL_MAX);
    lv_slider_set_value(sl, watch_volume_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_CARD_HL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_ACCENT2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_ACCENT), LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 10);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(sl, settings_vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 28;

    /* 游戏背景音乐 */
    s_set_bgm_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_set_bgm_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_set_bgm_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    if (watch_game_bgm_vol_get() == 0) {
        lv_label_set_text(s_set_bgm_lab, "游戏音乐 关");
    } else {
        snprintf(buf, sizeof(buf), "游戏音乐 %d", watch_game_bgm_vol_get());
        lv_label_set_text(s_set_bgm_lab, buf);
    }
    lv_obj_align(s_set_bgm_lab, LV_ALIGN_TOP_LEFT, 0, y);
    y += 22;

    sl = lv_slider_create(body);
    lv_obj_set_width(sl, LCD_W - 48);
    lv_obj_set_height(sl, 16);
    lv_slider_set_range(sl, WATCH_BGM_VOL_MIN, WATCH_BGM_VOL_MAX);
    lv_slider_set_value(sl, watch_game_bgm_vol_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_CARD_HL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xA66CFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WATCH_COL_ACCENT), LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 10);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(sl, settings_bgm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 28;

    /* 对讲自动播放 */
    row = lv_obj_create(body);
    lv_obj_set_size(row, LCD_W - 40, 34);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lab = lv_label_create(row);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(lab, "对讲自动播放");
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    s_set_sw = lv_switch_create(row);
    lv_obj_set_size(s_set_sw, 44, 24);
    lv_obj_align(s_set_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (walkie_autoplay_get()) {
        lv_obj_add_state(s_set_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_set_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_set_sw, settings_autoplay_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 42;

    s_set_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_set_lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(s_set_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(s_set_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_set_lab, LCD_W - 40);
    lv_obj_align(s_set_lab, LV_ALIGN_TOP_LEFT, 0, y);

    settings_refresh(NULL);
    s_set_tm = lv_timer_create(settings_refresh, 1000, NULL);
    return root;
}
