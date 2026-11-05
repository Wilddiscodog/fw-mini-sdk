#include "app_config.h"
#include "system/includes.h"
#include "sys_time.h"
#include "device/device.h"
#include "device/ioctl_cmds.h"
#include "asm/rtc.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_clock.h"
#include "watch_style.h"
#include "assets/watch_icons.h"

static void *s_rtc;
/* tick 合成起始点(RTC 不可用时回退) */
static u32 s_base_ms;
static u32 s_base_tod;
static u8 s_clock_inited;

static int rtc_read_now(struct sys_time *t)
{
    if (!s_rtc) {
        s_rtc = dev_open("rtc", NULL);
    }
    if (!s_rtc || !t) {
        return -1;
    }
    return dev_ioctl(s_rtc, IOCTL_GET_SYS_TIME, (u32)t);
}

static void clock_ensure_base(void)
{
    if (s_clock_inited) {
        return;
    }
    s_base_ms = timer_get_ms();
    s_base_tod = 10u * 3600u + 8u * 60u;
    s_clock_inited = 1;
    {
        struct sys_time probe;
        memset(&probe, 0, sizeof(probe));
        (void)rtc_read_now(&probe);
    }
}

void watch_clock_get_hm(u8 *h, u8 *m)
{
    struct sys_time t;
    u8 s;

    clock_ensure_base();
    if (rtc_read_now(&t) == 0 && t.year >= 2020) {
        if (h) {
            *h = (u8)t.hour;
        }
        if (m) {
            *m = (u8)t.min;
        }
        return;
    }

    {
        u32 ms = timer_get_ms();
        u32 tod;
        u32 elp;

        if (ms >= s_base_ms) {
            elp = ms - s_base_ms;
        } else {
            elp = (0xFFFFFFFFu - s_base_ms) + 1u + ms;
        }
        tod = s_base_tod + (elp / 1000u);
        tod %= (24u * 3600u);
        if (h) {
            *h = (u8)(tod / 3600u);
        }
        if (m) {
            *m = (u8)((tod % 3600u) / 60u);
        }
        (void)s;
    }
}

lv_obj_t *watch_clock_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_t *bg;

    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 主页仅保留小狗卡通背景, 时间改到顶部状态栏 */
    bg = lv_img_create(root);
    lv_img_set_src(bg, &watch_bg_dog);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    clock_ensure_base();
    return root;
}
