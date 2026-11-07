#include "app_config.h"
#include "system/includes.h"
#include "event/key_event.h"
#include "syscfg/syscfg_id.h"
#include "bt_music.h"
#include "walkie_priv.h"
#include "walkie.h"

/* 用户自定义区 1~59; 53=对讲自动播放 */
#define WALKIE_AUTOPLAY_CFG_ID  53

#define WK_CMD_PTT_ON   1
#define WK_CMD_PTT_OFF  2
#define WK_CMD_AV_STOP  3

static u8 s_active;
static volatile u8 s_ptt;      /* UI 即时态: 按下即 1, 松开即 0 */
static volatile u8 s_tx_live;  /* 音频通路实际在采 */
static u8 s_autoplay;
static u8 s_worker_ok;

void walkie_autoplay_init(void)
{
    u8 v = 0;

    s_autoplay = 0;
    if (syscfg_read(WALKIE_AUTOPLAY_CFG_ID, &v, sizeof(v)) == sizeof(v)) {
        s_autoplay = v ? 1 : 0;
    }
}

u8 walkie_autoplay_get(void)
{
    return s_autoplay;
}

void walkie_autoplay_set(u8 on)
{
    u8 v = on ? 1 : 0;

    s_autoplay = v;
    syscfg_write(WALKIE_AUTOPLAY_CFG_ID, &v, sizeof(v));
}

/*
 * PTT 开/关必须在独立任务做 server_request:
 * 从 lcd_test(LVGL timer) 同步 ENC_OPEN/CLOSE 会与 audio_server
 * wait_send_event(lcd_test) 互锁, 表现为松键仍「录音中」+整机卡死.
 */
static void walkie_do_ptt_on(void)
{
    if (!s_active || !s_ptt) {
        return;
    }
    if (walkie_peer_selected() < 0) {
        s_ptt = 0;
        return;
    }
    if (walkie_msg_busy() && !walkie_msg_recording()) {
        s_ptt = 0;
        return;
    }
    if (walkie_av_ensure()) {
        printf("[walkie] av ensure fail, no PTT\n");
        s_ptt = 0;
        return;
    }
    if (!s_ptt) {
        /* 开麦过程中已松键 */
        if (!walkie_av_local_play_is_on()) {
            /* 保留 AV 通路, 下次按下更快; 仅确保未开 TX */
        }
        return;
    }
    bt_music_ui_duck_begin();
    walkie_av_tx_start();
    if (s_ptt) {
        s_tx_live = 1;
    } else {
        /* 松键抢跑: 立刻停采 */
        walkie_av_tx_stop();
        if (!walkie_av_local_play_is_on()) {
            bt_music_ui_duck_end();
        }
        s_tx_live = 0;
    }
}

static void walkie_do_ptt_off(void)
{
    if (s_tx_live || walkie_msg_recording()) {
        walkie_av_tx_stop();
        s_tx_live = 0;
    }
    /* 不在每次松键 teardown AV, 避免下次按下再卡 1s+;
     * 完整释放见 walkie_session_end / AV_STOP. */
    if (!walkie_av_local_play_is_on()) {
        bt_music_ui_duck_end();
    }
}

static void walkie_worker(void *p)
{
    int msg[8];

    (void)p;
    while (1) {
        if (os_taskq_pend("taskq", msg, ARRAY_SIZE(msg)) != OS_TASKQ) {
            continue;
        }
        if (msg[0] != Q_USER) {
            continue;
        }
        switch (msg[1]) {
        case WK_CMD_PTT_ON:
            walkie_do_ptt_on();
            break;
        case WK_CMD_PTT_OFF:
            walkie_do_ptt_off();
            break;
        case WK_CMD_AV_STOP:
            walkie_do_ptt_off();
            if (!walkie_av_local_play_is_on()) {
                walkie_av_stop();
            }
            break;
        default:
            break;
        }
    }
}

static void walkie_worker_ensure(void)
{
    if (s_worker_ok) {
        return;
    }
    if (thread_fork("walkie_av", 15, 2048, 64, 0, walkie_worker, NULL) == 0) {
        s_worker_ok = 1;
    } else {
        printf("[walkie] worker fork fail\n");
    }
}

static int walkie_post(int cmd)
{
    int retry;

    walkie_worker_ensure();
    if (!s_worker_ok) {
        return -1;
    }
    for (retry = 0; retry < 5; retry++) {
        if (os_taskq_post("walkie_av", 1, cmd) == 0) {
            return 0;
        }
        os_time_dly(1);
    }
    printf("[walkie] post cmd=%d fail\n", cmd);
    return -1;
}

/* 后台常驻: 只起网络/录音索引, 不关蓝牙; AV 按需拉起 */
void walkie_bg_start(void)
{
    walkie_contact_init();
    walkie_worker_ensure();
    if (!s_active) {
        walkie_rec_init();
        walkie_msg_init();
        s_active = 1;
        s_ptt = 0;
        s_tx_live = 0;
    }
    (void)walkie_net_start();
}

void walkie_on_enter(void)
{
    walkie_bg_start();
    walkie_net_refresh_ip();
    walkie_net_discover_now();
}

void walkie_on_wifi_ready(void)
{
    walkie_contact_init();
    walkie_worker_ensure();
    if (!s_active) {
        walkie_rec_init();
        walkie_msg_init();
        s_active = 1;
        s_ptt = 0;
        s_tx_live = 0;
    }
    if (walkie_net_restart() == 0) {
        walkie_net_discover_now();
    }
}

void walkie_on_exit(void)
{
    walkie_ptt_stop();
}

void walkie_session_end(void)
{
    s_ptt = 0;
    (void)walkie_post(WK_CMD_AV_STOP);
}

u8 walkie_is_active(void)
{
    return s_active;
}

int walkie_av_ensure(void)
{
    return walkie_av_start();
}

void walkie_ptt_start(void)
{
    if (!s_active || s_ptt) {
        return;
    }
    if (walkie_peer_selected() < 0) {
        return;
    }
    if (walkie_msg_busy() && !walkie_msg_recording()) {
        return;
    }
    s_ptt = 1; /* UI 立刻显示「录音中」 */
    if (walkie_post(WK_CMD_PTT_ON)) {
        s_ptt = 0;
    }
}

void walkie_ptt_stop(void)
{
    if (!s_ptt && !s_tx_live && !walkie_msg_recording()) {
        return;
    }
    s_ptt = 0; /* UI 立刻离开录音态 */
    (void)walkie_post(WK_CMD_PTT_OFF);
}

u8 walkie_ptt_is_on(void)
{
    return s_ptt || s_tx_live || walkie_msg_recording();
}

extern u8 ad_get_key_value(void);

void walkie_ptt_poll(void)
{
    u8 v;
    static u8 held;

    if (!s_active) {
        held = 0;
        return;
    }
    v = ad_get_key_value();
    if (v == KEY_PREV) {
        if (!held) {
            held = 1;
            walkie_ptt_start();
        }
    } else if (held) {
        held = 0;
        walkie_ptt_stop();
    }
}

int walkie_key_event(u8 value, u8 action)
{
    if (!s_active || value != KEY_PREV) {
        return 0;
    }
    if (action == KEY_EVENT_CLICK) {
        return 1;
    }
    if (action == KEY_EVENT_LONG || action == KEY_EVENT_HOLD) {
        walkie_ptt_start();
        return 1;
    }
    if (action == KEY_EVENT_UP) {
        walkie_ptt_stop();
        return 1;
    }
    return 0;
}
