/*
 * 摔倒提醒(仅计步 App 内测试): 低 g 持续 → 播 fall_alert.wav 一次
 * 退出 App 必须 watch_fall_stop(), 不做后台检测
 * 资源: mnt/sdfile/res/audlogo_mini/fall_alert.wav
 */
#include "app_config.h"
#include "system/includes.h"
#include "watch_fall.h"
#include "watch_volume.h"
#include "watch_game_bgm.h"
#include "imu.h"
#include "fs/fs.h"

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#include "server/audio_server.h"
#include "server/server_core.h"
#include "device/gpio.h"
#include "lvgl.h"
#include <string.h>

#define FALL_POLL_MS        50
#define FALL_HOLD_MS        300     /* 自由落体持续判定 */
#define FALL_COOLDOWN_MS    5000    /* 播完后冷却, 防连播 */
#define FALL_G_TH           0.40f   /* |a| < 0.4g 视为失重 */
#define FALL_PA_IO          IO_PORTH_07
#define FALL_PA_MUTE_LVL    0

#define FALL_WAV_PATH1      "mnt/sdfile/res/audlogo_mini/fall_alert.wav"
#define FALL_WAV_PATH2      "mnt/sdfile/res/tone/fall_alert.wav"

extern void bt_music_audio_takeover(void);

static u8 s_alert_on;
static u8 s_inited;
static u16 s_low_ms;            /* 连续低 g 累计 */
static u32 s_cool_left_ms;      /* 冷却剩余 */
static lv_timer_t *s_tm;

static struct server *s_dec;
static FILE *s_file;
static volatile u8 s_playing;

static void fall_pa_mute(u8 mute)
{
    gpio_direction_output(FALL_PA_IO, mute ? FALL_PA_MUTE_LVL : !FALL_PA_MUTE_LVL);
}

static void fall_play_stop(void)
{
    union audio_req req = {0};

    s_playing = 0;
    fall_pa_mute(1);
    if (s_dec) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(s_dec, AUDIO_REQ_DEC, &req);
        server_close(s_dec);
        s_dec = NULL;
    }
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
}

static void fall_dec_event(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    if (!argv) {
        return;
    }
    if (argv[0] == AUDIO_SERVER_EVENT_END || argv[0] == AUDIO_SERVER_EVENT_ERR) {
        fall_play_stop();
        s_cool_left_ms = FALL_COOLDOWN_MS;
    }
}

static int fall_play_once(void)
{
    union audio_req req = {0};
    int err;

    if (s_playing) {
        return 0;
    }

    bt_music_audio_takeover();
    watch_game_bgm_stop();

    s_file = fopen(FALL_WAV_PATH1, "r");
    if (!s_file) {
        s_file = fopen(FALL_WAV_PATH2, "r");
    }
    if (!s_file) {
        printf("[fall] open wav fail\r\n");
        return -1;
    }

    s_dec = server_open("audio_server", "dec");
    if (!s_dec) {
        fclose(s_file);
        s_file = NULL;
        return -2;
    }
    server_register_event_handler_to_task(s_dec, NULL, fall_dec_event, "app_core");

    memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = watch_volume_get();
    req.dec.output_buf_len = 6 * 1024;
    req.dec.file = s_file;
    req.dec.channel = 0;
    req.dec.sample_rate = 0;
    req.dec.priority = 1;
    req.dec.vfs_ops = NULL; /* 默认文件操作 */
    req.dec.dec_type = "wav";
    req.dec.sample_source = CONFIG_AUDIO_DEC_PLAY_SOURCE;
    err = server_request(s_dec, AUDIO_REQ_DEC, &req);
    if (err) {
        printf("[fall] dec open err=%d\r\n", err);
        fall_play_stop();
        return -3;
    }

    fall_pa_mute(0);
    s_playing = 1;
    req.dec.cmd = AUDIO_DEC_START;
    err = server_request(s_dec, AUDIO_REQ_DEC, &req);
    if (err) {
        printf("[fall] dec start err=%d\r\n", err);
        fall_play_stop();
        return -4;
    }
    printf("[fall] playing alert\r\n");
    return 0;
}

static void fall_poll(lv_timer_t *t)
{
    struct sc7u22_si si;
    float g2;

    (void)t;
    if (!s_alert_on) {
        return;
    }
    if (s_cool_left_ms > FALL_POLL_MS) {
        s_cool_left_ms -= FALL_POLL_MS;
        s_low_ms = 0;
        return;
    }
    s_cool_left_ms = 0;

    if (s_playing) {
        return;
    }
    if (!imu_ready() || imu_read_si(&si)) {
        s_low_ms = 0;
        return;
    }

    g2 = si.ax * si.ax + si.ay * si.ay + si.az * si.az;
    if (g2 < (FALL_G_TH * FALL_G_TH)) {
        s_low_ms = (u16)(s_low_ms + FALL_POLL_MS);
        if (s_low_ms >= FALL_HOLD_MS) {
            s_low_ms = 0;
            if (fall_play_once()) {
                /* 播失败也进冷却, 避免狂刷 */
                s_cool_left_ms = FALL_COOLDOWN_MS;
            }
        }
    } else {
        s_low_ms = 0;
    }
}

void watch_fall_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;
    s_alert_on = 0;
    fall_pa_mute(1);
    imu_init();
    s_tm = lv_timer_create(fall_poll, FALL_POLL_MS, NULL);
    lv_timer_pause(s_tm);
}

void watch_fall_alert_set(u8 on)
{
    watch_fall_init();
    s_alert_on = on ? 1 : 0;
    s_low_ms = 0;
    if (s_alert_on) {
        lv_timer_resume(s_tm);
        printf("[fall] alert ON\r\n");
    } else {
        lv_timer_pause(s_tm);
        fall_play_stop();
        printf("[fall] alert OFF\r\n");
    }
}

u8 watch_fall_alert_get(void)
{
    return s_alert_on;
}

void watch_fall_stop(void)
{
    if (!s_inited) {
        return;
    }
    s_alert_on = 0;
    s_low_ms = 0;
    s_cool_left_ms = 0;
    if (s_tm) {
        lv_timer_pause(s_tm);
    }
    fall_play_stop();
}

#else /* no audio */

void watch_fall_init(void)
{
}

void watch_fall_alert_set(u8 on)
{
    (void)on;
}

u8 watch_fall_alert_get(void)
{
    return 0;
}

void watch_fall_stop(void)
{
}

#endif
