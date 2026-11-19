#ifndef HUB_MUSIC_H
#define HUB_MUSIC_H

#include "generic/typedef.h"

#define HUB_MUSIC_MAX_TRACKS  48
#define HUB_MUSIC_TITLE_LEN   48

struct hub_music_track {
    u32 id;
    u32 bytes;
    char fmt[8];
    char title[HUB_MUSIC_TITLE_LEN];
};

/* 同步拉列表 (勿在 LVGL/UI 线程调用, 会阻塞) */
int hub_music_refresh(void);
/* 后台线程拉列表, 完成后回调(仍在后台线程, UI 请 lv_async_call) */
typedef void (*hub_music_list_cb)(int result, void *priv);
int hub_music_refresh_async(hub_music_list_cb cb, void *priv);
u8 hub_music_list_busy(void);

int hub_music_count(void);
const struct hub_music_track *hub_music_get(int idx);

/* 点播 / 控制. fmt 决定解码器 (mp3/flac/ape/wav/wma/m4a/aac/ogg/amr) */
int hub_music_play_idx(int idx);
int hub_music_pause_resume(void);
void hub_music_stop(void);
int hub_music_next(void);
int hub_music_prev(void);

u8 hub_music_is_playing(void);
u8 hub_music_is_busy(void);      /* 缓冲/切歌中 */
int hub_music_cur_idx(void);
int hub_music_volume_get(void);
int hub_music_volume_set(int vol);

#endif
