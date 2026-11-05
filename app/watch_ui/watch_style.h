#ifndef WATCH_STYLE_H
#define WATCH_STYLE_H

#include "assets/lv_font_watch_16_cn.h"

/* 儿童手表卡通色板：浅蓝底 + 黑字 */
#define WATCH_COL_BG        0xD6F3FF   /* APP/页面卡通浅蓝 */
#define WATCH_COL_CARD      0xFFFFFF
#define WATCH_COL_CARD_HL   0xE8F9FF
#define WATCH_COL_ACCENT    0xFF8FAB
#define WATCH_COL_ACCENT2   0x7EC8E3
#define WATCH_COL_TEXT      0x000000   /* 黑色字体 */
#define WATCH_COL_TEXT_DIM  0x4A4A4A
#define WATCH_COL_DANGER    0xFF6B6B
#define WATCH_COL_OK        0x6BCB77
#define WATCH_COL_RING      0xB8E8F5
#define WATCH_COL_RING_FG   0x4FC3F7
#define WATCH_COL_SUN       0xFFD93D
#define WATCH_COL_SKY       0xA8E6FF
#define WATCH_COL_BUBBLE    0x5ED9C8   /* 对讲气泡青绿 */

#define WATCH_BL_MIN        1
#define WATCH_BL_MAX        100
#define WATCH_BL_DEFAULT    80

#define WATCH_VOL_MIN       5
#define WATCH_VOL_MAX       100
#define WATCH_VOL_DEFAULT   50

#define WATCH_ICON_SIZE     150

/* 弧形表盘边角避让(px) */
#define WATCH_SAFE_X        14
#define WATCH_SAFE_Y        12
#define WATCH_STATUS_H      22
/* 图标略上移, 名称与底部分页点错开 */
#define WATCH_ICON_Y_OFF    (-6)

#endif

