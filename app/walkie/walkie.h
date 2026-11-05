#ifndef WALKIE_H
#define WALKIE_H

#include "generic/typedef.h"

#define WALKIE_PORT           3721
#define WALKIE_MAX_PEERS      8
#define WALKIE_PEER_NAME_LEN  16
#define WALKIE_VER_LEN        16
/* 协议版本: 发现匹配用它, 与固件号解耦, 避免升版后互相看不见 */
#define WALKIE_PROTO_VER      "1"

struct walkie_peer {
    u8 used;
    u8 alive;     /* 1=局域网在线 */
    u8 paired;    /* 1=已保存联系人 */
    u8 mac[6];
    u32 ip;
    char name[WALKIE_PEER_NAME_LEN];
    char ver[WALKIE_VER_LEN];
    u32 last_ms;
};

void walkie_bg_start(void);
void walkie_on_enter(void);
void walkie_on_exit(void);
u8 walkie_is_active(void);
/* WiFi 拿到 IP 后调用: 刷新本机地址并强制发现 */
void walkie_on_wifi_ready(void);
/* 播放/PTT 前按需打开音频通路 */
int walkie_av_ensure(void);

void walkie_ptt_start(void);
void walkie_ptt_stop(void);
u8 walkie_ptt_is_on(void);

int walkie_peer_count(void);
const struct walkie_peer *walkie_peer_get(int idx);
int walkie_peer_selected(void);
void walkie_peer_select(int idx);
void walkie_peer_select_ip(u32 ip);
u32 walkie_peer_selected_ip(void);
u8 walkie_peer_ip_alive(u32 ip);
const char *walkie_peer_name_by_ip(u32 ip);

/* UI 定时刷新用: 返回 1 表示 peer 表有变化 */
u8 walkie_peers_dirty_take(void);
/* 主界面主动触发一次局域网发现广播 */
void walkie_net_discover_now(void);
void walkie_net_refresh_ip(void);
/* 主动扫描: 确保网络已起并连发发现包. 返回 0=已开始 -1=网络未起 -2=无有效IP */
int walkie_net_scan(void);
u8 walkie_net_has_ip(void);

/* 永久联系人(VM): 配对保存 / 删除 / 开机自动找回 */
void walkie_contact_init(void);
int walkie_contact_count(void);
/* 返回 0 成功; alive=当前是否在线 */
int walkie_contact_info(int idx, u8 mac[6], char *name, int name_len,
                        u8 *alive, u32 *ip);
int walkie_contact_save_peer(int peer_idx);
int walkie_contact_delete(int contact_idx);
u8 walkie_contact_has_mac(const u8 mac[6]);

/* 整包消息状态(松键发送 / ACK 重传) */
u8 walkie_msg_busy(void);
u8 walkie_msg_recording(void);
u8 walkie_msg_sending(void);
u8 walkie_msg_last_send_ok(void);

int walkie_rec_count(void);
int walkie_rec_info(int idx, char *line, int line_len);
/* dir: 0=TX 1=RX; 返回 0 成功 */
int walkie_rec_meta(int idx, u8 *dir, u16 *sec, u32 *peer_ip);
int walkie_rec_play(int idx);

/* 网页导出: 确保 REC 可用; 导出第 idx 段为 16kHz/mono/s16le WAV */
int walkie_rec_ensure(void);
u32 walkie_rec_pcm_bytes(int idx);
/* write_cb 返回 <0 中止; 成功返回 0 */
int walkie_rec_export_wav(int idx,
                          int (*write_cb)(void *ctx, const void *data, u32 len),
                          void *ctx);
/* 调试: 清空全部对讲录音(nor_fs 索引) */
int walkie_rec_clear_all(void);
/* 新收到一条 RX 语音时置位; take 后清零. 返回 1=有通知 */
u8 walkie_rec_rx_notify_take(u32 *peer_ip);

/* 设置: 对讲界面收到新消息自动播放 */
void walkie_autoplay_init(void);
u8 walkie_autoplay_get(void);
void walkie_autoplay_set(u8 on);

/* KEY_PREV: 返回 1 表示已消费; 真按下态靠 walkie_ptt_poll 读 ADC */
int walkie_key_event(u8 value, u8 action);
void walkie_ptt_poll(void);

#endif
