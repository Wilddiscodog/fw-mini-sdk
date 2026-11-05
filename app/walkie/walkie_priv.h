#ifndef WALKIE_PRIV_H
#define WALKIE_PRIV_H

#include "walkie.h"

#define WALKIE_PCM_RATE   16000
#define WALKIE_PCM_CH     1

/* 网络包类型 (与 walkie_net.c 一致) */
enum {
    WALKIE_PKT_DISC = 1,
    WALKIE_PKT_VOICE = 2,      /* 旧流式, 保留兼容 */
    WALKIE_PKT_VOICE_END = 3,
    WALKIE_PKT_MSG_BEGIN = 4,  /* 整包消息 */
    WALKIE_PKT_MSG_DATA = 5,
    WALKIE_PKT_MSG_END = 6,
    WALKIE_PKT_MSG_ACK = 7,
};

#pragma pack(push, 1)
struct walkie_msg_begin {
    u16 msg_id;
    u32 total_bytes;
    u16 total_chunks;
    u16 sample_rate;
};
struct walkie_msg_chunk {
    u16 msg_id;
    u16 chunk_idx;
    u16 nbytes;
    /* 后接 pcm[nbytes] */
};
struct walkie_msg_end {
    u16 msg_id;
    u16 total_chunks;
    u32 total_bytes;
};
struct walkie_msg_ack {
    u16 msg_id;
    u8 ok;
};
#pragma pack(pop)

int walkie_net_start(void);
void walkie_net_stop(void);
/* DHCP 拿到 IP 后重建 UDP (避免 0.0.0.0 时绑定导致收发失效) */
int walkie_net_restart(void);
int walkie_net_send_voice(const void *pcm, u16 len);
void walkie_net_send_voice_end(void);
int walkie_net_send_msg(u32 dip, u8 type, const void *payload, u16 plen);
void walkie_net_discover_now(void);
u32 walkie_peer_selected_ip(void);

int walkie_av_start(void);
void walkie_av_stop(void);
void walkie_av_tx_start(void);
void walkie_av_tx_stop(void);
void walkie_av_rx_pcm(const void *pcm, u16 len);

void walkie_av_local_play_begin(void);
void walkie_av_local_play_end(void);
int walkie_av_local_play_write(const void *pcm, u16 len);
void walkie_av_local_play_drain(void);
u8 walkie_av_local_play_is_on(void);

int walkie_rec_init(void);
void walkie_rec_deinit(void);
void walkie_rec_tx_start(void);
void walkie_rec_tx_pcm(const void *pcm, u16 len);
void walkie_rec_tx_end(void);
void walkie_rec_rx_pcm(const void *pcm, u16 len);
void walkie_rec_rx_end(u32 peer_ip);

int walkie_msg_init(void);
void walkie_msg_deinit(void);
int walkie_msg_tx_begin(void);
void walkie_msg_tx_pcm(const void *pcm, u16 len);
void walkie_msg_tx_commit(void);
void walkie_msg_tx_cancel(void);
u8 walkie_msg_busy(void);
u8 walkie_msg_recording(void);
u8 walkie_msg_sending(void);
u8 walkie_msg_last_send_ok(void);
void walkie_msg_on_begin(u32 from_ip, const struct walkie_msg_begin *b);
void walkie_msg_on_data(u32 from_ip, const struct walkie_msg_chunk *c, u16 plen);
void walkie_msg_on_end(u32 from_ip, const struct walkie_msg_end *e);
void walkie_msg_on_ack(u16 msg_id, u8 ok);

#endif
