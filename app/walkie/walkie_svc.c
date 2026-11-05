#include "app_config.h"
#include "system/includes.h"
#include "event/key_event.h"
#include "syscfg/syscfg_id.h"
#include "bt_music.h"
#include "walkie_priv.h"
#include "walkie.h"

/* 用户自定义区 1~59; 53=对讲自动播放 */
#define WALKIE_AUTOPLAY_CFG_ID  53

static u8 s_active;
static u8 s_ptt;
static u8 s_autoplay;

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

/* 后台常驻: 只起网络/录音索引, 不关蓝牙; AV 按需拉起 */
void walkie_bg_start(void)
{
    walkie_contact_init();
    if (!s_active) {
        walkie_rec_init();
        walkie_msg_init();
        s_active = 1;
        s_ptt = 0;
    }
    /* WiFi 稍后就绪时可重试; walkie_net_start 已起则刷新 IP 并再发现 */
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
    if (!s_active) {
        walkie_rec_init();
        walkie_msg_init();
        s_active = 1;
        s_ptt = 0;
    }
    /* DHCP 后重建 UDP: 开机过早用 0.0.0.0 bind 会导致扫描收发失效 */
    if (walkie_net_restart() == 0) {
        walkie_net_discover_now();
    }
}

void walkie_on_exit(void)
{
    /* 对讲后台常驻: 离开界面不停服务 */
    walkie_ptt_stop();
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
    if (walkie_msg_busy()) {
        return;
    }
    if (walkie_av_ensure()) {
        printf("[walkie] av ensure fail, no PTT\n");
        return;
    }
    bt_music_ui_duck_begin();
    s_ptt = 1;
    walkie_av_tx_start();
}

void walkie_ptt_stop(void)
{
    if (!s_ptt) {
        return;
    }
    s_ptt = 0;
    walkie_av_tx_stop();
    /* 本地回放中由 play 任务负责停 AV / 还 BT */
    if (!walkie_av_local_play_is_on()) {
        walkie_av_stop();
        bt_music_ui_duck_end();
    }
}

u8 walkie_ptt_is_on(void)
{
    return s_ptt;
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
