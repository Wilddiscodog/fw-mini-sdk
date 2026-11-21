/**
 * @file lv_port_indev.c
 *
 */
#include "system/includes.h"
#include "lvgl.h"
#include "watch_power.h"

static struct touch_hdl {
    u16 x;
    u16 y;
    u8 status;
};
static struct touch_hdl *hdl = NULL;
static lv_indev_t *s_touch_indev;

static bool touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

extern void *get_touch_x_y(void);

void lv_port_indev_init(void)
{
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    s_touch_indev = lv_indev_drv_register(&indev_drv);
    hdl = get_touch_x_y();
}

/* 息屏时清掉按下态, 避免亮屏瞬间误触 */
void lv_port_indev_block(u8 block)
{
    if (!s_touch_indev) {
        return;
    }
    if (block) {
        lv_indev_reset(s_touch_indev, NULL);
        lv_indev_enable(s_touch_indev, false);
    } else {
        lv_indev_enable(s_touch_indev, true);
    }
}

static bool touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    if (!hdl) {
        data->state = LV_INDEV_STATE_REL;
        return false;
    }
    data->point.x = hdl->x;
    data->point.y = hdl->y;
    /* 轻触电源息屏后屏蔽触摸: 一律上报松开, 不唤醒、不点控件 */
    if (!watch_power_input_enabled() || !hdl->status) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
    }
    return false;
}





