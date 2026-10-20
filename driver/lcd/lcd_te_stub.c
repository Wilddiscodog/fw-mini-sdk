/*
 * TE(撕裂同步)/相机合成路径桩 —— mini-sdk 未移植 te_driver 框架。
 * 仅 LVGL/直写路径（LCD_Lvgl_Full / lcd_show_frame）不需要这些函数；
 * 屏驱动注册表(.LCD_Draw/.LCD_Draw_1)引用它们以保证链接完整。
 * 若后续需要 TE 撕裂同步或摄像头合成显示，再从官方
 * apps/common/ui/lcd_driver/te_driver/ 移植。
 */
#include "typedef.h"
#include "lcd_drive.h"
#include "lcd_te_driver.h"

void init_TE(void (*cb)(u8 *buf, u32 buf_size))
{
    (void)cb;
    /* TE 撕裂同步未启用: LVGL 直写路径不依赖 */
}

u8 get_lcd_show_data_mode(void)
{
    return ui; /* ui 直写模式 */
}

void set_lcd_show_data_mode(u8 choice_data_mode)
{
    (void)choice_data_mode;
}

void ui_send_data_ready(u8 *data_buf, u32 data_size)
{
    (void)data_buf;
    (void)data_size;
}

void camera_send_data_ready(u8 *data_buf, u32 data_size)
{
    (void)data_buf;
    (void)data_size;
}

void get_lcd_ui_x_y(u16 xstart, u16 xend, u16 ystart, u16 yend)
{
    /* TE 合成路径未移植: 空实现 */
    (void)xstart; (void)xend; (void)ystart; (void)yend;
}
