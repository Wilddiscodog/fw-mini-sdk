#ifndef NET_MUSIC_H
#define NET_MUSIC_H

#include "event/net_event.h"

#ifdef CONFIG_NET_MUSIC_MODE_ENABLE

/* STA DHCP 成功后启动 DLNA MediaRenderer + 网络解码 */
int net_music_start(void);
void net_music_stop(void);

/* 转发 SYS_NET_EVENT(CONNECTED 时补开 ai_server) */
int net_music_on_net_event(struct net_event *event);

/* BT A2DP 抢占 DAC 时停掉网络播放 */
void net_music_audio_takeover(void);

int get_app_music_volume(void);
int watch_net_music_set_volume(int vol);

#else
static inline int net_music_start(void) { return 0; }
static inline void net_music_stop(void) {}
static inline int net_music_on_net_event(struct net_event *e) { (void)e; return 0; }
static inline void net_music_audio_takeover(void) {}
static inline int get_app_music_volume(void) { return 50; }
static inline int watch_net_music_set_volume(int vol) { (void)vol; return 0; }
#endif

#endif
