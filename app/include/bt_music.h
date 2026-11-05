#ifndef BT_MUSIC_H
#define BT_MUSIC_H

#if (defined MINI_BT_SUPPORT) && MINI_BT_SUPPORT && \
    (defined MINI_BT_SPEAKER_EN) && MINI_BT_SPEAKER_EN
int bt_music_ui_play_pause(void);
int bt_music_ui_vol_step(int step);
int bt_music_ui_is_playing(void);
/* 对讲插播: 若正在播则暂停并释放 DAC; 结束后再 resume */
int bt_music_ui_duck_begin(void);
void bt_music_ui_duck_end(void);
#else
static inline int bt_music_ui_play_pause(void) { return 0; }
static inline int bt_music_ui_vol_step(int step) { (void)step; return 0; }
static inline int bt_music_ui_is_playing(void) { return 0; }
static inline int bt_music_ui_duck_begin(void) { return 0; }
static inline void bt_music_ui_duck_end(void) {}
#endif


#endif
