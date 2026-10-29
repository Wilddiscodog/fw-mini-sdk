#ifndef BT_MUSIC_H
#define BT_MUSIC_H

#if (defined MINI_BT_SPEAKER_EN) && MINI_BT_SPEAKER_EN
int bt_music_ui_play_pause(void);
int bt_music_ui_vol_step(int step);
int bt_music_ui_is_playing(void);
#else
static inline int bt_music_ui_play_pause(void) { return 0; }
static inline int bt_music_ui_vol_step(int step) { (void)step; return 0; }
static inline int bt_music_ui_is_playing(void) { return 0; }
#endif

#endif
