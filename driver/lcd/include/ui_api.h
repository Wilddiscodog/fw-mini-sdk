/*
 * 精简版 ui_api.h —— 仅保留 lcd_drive.c 需要的板级配置类型。
 * 官方完整版在 apps/common/ui/include/ui_api.h（依赖整个 UI 框架），
 * mini-sdk 的 LCD/LVGL 链路不需要 UI 框架。
 */
#ifndef _UI_API_MINI_H_
#define _UI_API_MINI_H_

#include "lcd_drive.h"

enum ui_devices_type {
    LED_7,
    LCD_SEG3X9,
    TFT_LCD,   /* 彩屏 */
    DOT_LCD,   /* 点阵屏 */
};

/* 板级配置数据结构（board.c 里以固定名字 ui_cfg_data 提供） */
struct ui_devices_cfg {
    enum ui_devices_type type;
    void *private_data;
};

#endif
