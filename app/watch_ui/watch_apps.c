#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "fw_version.h"
#include "watch_apps.h"
#include "watch_ui.h"
#include "watch_style.h"
#include "watch_volume.h"

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
#include "sc7u22.h"
#include "user_cdc_log.h"


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
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 10, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    return body;
}

static void stub_btn_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_toast("功能开发中");
}

/* -------- 陀螺仪 SC7U22 -------- */
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
    if (!sc7u22_ready()) {
        lv_label_set_text(s_gyro_lab, "SC7U22 未就绪\n检查 PH0/PH1 总线");
        user_cdc_puts("[gyro] SC7U22 not ready\r\n");
        return;
    }
    if (sc7u22_read_si(&s)) {
        lv_label_set_text(s_gyro_lab, "读数失败");
        user_cdc_puts("[gyro] read fail\r\n");
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
             "ID:0x%02X\n"
             "Acc g:\n"
             " X:%s Y:%s Z:%s\n"
             "Gyro dps:\n"
             " X:%s Y:%s Z:%s\n"
             "T:%sC",
             sc7u22_whoami(),
             ax, ay, az, gx, gy, gz, tc);
    lv_label_set_text(s_gyro_lab, buf);
    /* CDC2: 不用 %f (库未链浮点 printf) */
    user_cdc_printf("[gyro] id=%02X acc=%s,%s,%s gyr=%s,%s,%s T=%s\r\n",
                    sc7u22_whoami(), ax, ay, az, gx, gy, gz, tc);
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

    printf("[gyro] app create\n");
    if (sc7u22_init()) {
        printf("[gyro] sc7u22_init fail\n");
        watch_ui_toast("陀螺仪初始化失败");
    } else {
        printf("[gyro] sc7u22 ready whoami=0x%02x\n", sc7u22_whoami());
    }
    user_cdc_puts("[gyro] app open\r\n");

    s_gyro_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_gyro_lab, &lv_font_simsun_16_cjk, 0);
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
    watch_ui_toast("音量已调");
}

lv_obj_t *app_net_music_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "网络播放");
    lv_obj_t *body = make_body(root);

    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_simsun_16_cjk, 0);
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
    lv_obj_set_style_text_font(dl, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(dl, "音量-");
    lv_obj_center(dl);

    lv_obj_t *inc = lv_btn_create(body);
    lv_obj_set_size(inc, 80, 32);
    lv_obj_align(inc, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(inc, lv_color_hex(WATCH_COL_CARD_HL), 0);
    lv_obj_add_event_cb(inc, net_vol_inc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(inc);
    lv_obj_set_style_text_font(il, &lv_font_simsun_16_cjk, 0);
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
    watch_ui_toast("音量+");
}

lv_obj_t *app_bt_music_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "蓝牙音乐");
    lv_obj_t *body = make_body(root);

    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_simsun_16_cjk, 0);
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
    lv_obj_set_style_text_font(pl, &lv_font_simsun_16_cjk, 0);
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

/* -------- 对讲 / 录音 -------- */
lv_obj_t *app_walkie_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "对讲机");
    lv_obj_t *body = make_body(root);
    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(lab, "对讲机\n(后续接入)");
    lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn = lv_btn_create(body);
    lv_obj_set_size(btn, 140, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_DANGER), 0);
    lv_obj_add_event_cb(btn, stub_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(bl, "按住说话");
    lv_obj_center(bl);
    return root;
}

lv_obj_t *app_recorder_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "录音机");
    lv_obj_t *body = make_body(root);
    lv_obj_t *lab = lv_label_create(body);
    lv_obj_set_style_text_font(lab, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(lab, "录音机\n(后续接入)");
    lv_obj_align(lab, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn = lv_btn_create(body);
    lv_obj_set_size(btn, 120, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(btn, lv_color_hex(WATCH_COL_DANGER), 0);
    lv_obj_add_event_cb(btn, stub_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(bl, "开始录音");
    lv_obj_center(bl);
    return root;
}

/* -------- 设置 -------- */
static lv_obj_t *s_set_lab;
static lv_timer_t *s_set_tm;

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
             "WiFi: %s\nSSID: %s\nIP: %s\n\n蓝牙: %s\n连接: %d\n\n版本: %s",
             wifi_st, ssid, ip,
             btname ? btname : "-", bt_n,
             FW_VERSION);
    lv_label_set_text(s_set_lab, buf);
}

static void settings_del(lv_event_t *e)
{
    (void)e;
    if (s_set_tm) {
        lv_timer_del(s_set_tm);
        s_set_tm = NULL;
    }
    s_set_lab = NULL;
}

lv_obj_t *app_settings_create(lv_obj_t *parent)
{
    lv_obj_t *root = make_app_root(parent, "设置");
    lv_obj_t *body = make_body(root);
    lv_obj_add_event_cb(root, settings_del, LV_EVENT_DELETE, NULL);

    s_set_lab = lv_label_create(body);
    lv_obj_set_style_text_font(s_set_lab, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_set_lab, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_long_mode(s_set_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_set_lab, LCD_W - 40);
    lv_obj_align(s_set_lab, LV_ALIGN_TOP_LEFT, 0, 0);

    settings_refresh(NULL);
    s_set_tm = lv_timer_create(settings_refresh, 1000, NULL);
    return root;
}
