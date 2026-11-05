#ifndef WATCH_GAME_BGM_H
#define WATCH_GAME_BGM_H

#include "generic/typedef.h"

/* FC 风实时合成 BGM: 无 MP3 */
enum {
    WATCH_BGM_SOFT = 0,   /* 方块 */
    WATCH_BGM_ACTIVE,     /* 平衡球/迷宫 */
    WATCH_BGM_CANDY,      /* 消消乐 */
};

#define WATCH_BGM_VOL_MIN      0
#define WATCH_BGM_VOL_MAX      100
#define WATCH_BGM_VOL_DEFAULT  45

void watch_game_bgm_vol_init(void);
u8 watch_game_bgm_vol_get(void);
void watch_game_bgm_vol_set(u8 vol);       /* 即时生效(若正在播) */
void watch_game_bgm_vol_persist(void);

void watch_game_bgm_start(u8 theme);
void watch_game_bgm_stop(void);
u8 watch_game_bgm_is_on(void);

#endif
