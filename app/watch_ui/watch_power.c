#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "sys_power.h"
#include "watch_power.h"
#include "watch_backlight.h"
#include "watch_ui.h"

static u8 s_on = 1;
static u8 s_busy;
static volatile u8 s_off_req;
static volatile u8 s_do_poweroff;

void watch_power_boot_anim(void)
{
    s_busy = 0;
    s_on = 1;
}

void watch_power_off(void)
{
    if (s_busy || !s_on) {
        return;
    }
    s_busy = 1;
    s_on = 0;
    watch_ui_close_dropdown();
    watch_ui_close_app();
    watch_backlight_set(0);
    /*
     * 只置位: 真正 soft_poweroff 必须在 lcd_test 跳出 lv_task_handler 之后做。
     * 若在 LVGL 回调/另起任务里进 power_set_soft_poweroff, 另一核仍可能正跑
     * LVGL→EMI/Flash(SFC critical), 双核互相等 → 永久 "other cpu hold"。
     */
    s_do_poweroff = 1;
    printf("[watch] poweroff requested (wait lcd_test leave LVGL)\n");
}

u8 watch_power_take_off_request(void)
{
    if (!s_do_poweroff) {
        return 0;
    }
    s_do_poweroff = 0;
    return 1;
}

void watch_power_init(void)
{
    s_on = 1;
    s_busy = 0;
    s_off_req = 0;
    s_do_poweroff = 0;
}

u8 watch_power_is_on(void)
{
    return s_on && !s_busy;
}

u8 watch_power_input_enabled(void)
{
    return s_on && !s_busy;
}

void watch_power_key_long(void)
{
    s_off_req = 1;
}

void watch_power_poll(lv_timer_t *timer)
{
    (void)timer;
    if (!s_off_req) {
        return;
    }
    s_off_req = 0;
    watch_power_off();
}
