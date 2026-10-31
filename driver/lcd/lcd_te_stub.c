/*
 * TE 同步(轻量实现): 面板 TEON 后 PC0 输出场消隐脉冲;
 * LVGL 路径在 lcd_lvgl_full 首片等上升沿。相机合成队列未移植。
 */
#include "typedef.h"
#include "lcd_drive.h"
#include "lcd_te_driver.h"

void init_TE(void (*cb)(u8 *buf, u32 buf_size))
{
    (void)cb;
    lcd_te_gpio_init();
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
    (void)xstart;
    (void)xend;
    (void)ystart;
    (void)yend;
}

void get_te_fps(char *fps)
{
    if (fps) {
        *fps = 0;
    }
}
