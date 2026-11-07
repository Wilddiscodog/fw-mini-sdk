#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "sys_power.h"
#include "watch_power.h"
#include "watch_backlight.h"
#include "watch_ui.h"
#include "lcd_drive.h"

extern void disp_enable_update(void);
extern void disp_disable_update(void);

static u8 s_on = 1;                 /* 亮屏可用 */
static u8 s_asleep;                 /* 息屏低功耗(非关机) */
static u8 s_busy;
static volatile u8 s_off_req;       /* 长按 → 真关机 */
static volatile u8 s_sleep_tog;     /* 短按 → 息屏/亮屏切换 */
static volatile u8 s_do_poweroff;
static u32 s_boot_ms;               /* 开机时刻: 5s 内禁止息屏 */

#define WATCH_LOCK_GUARD_MS  5000u

static u8 watch_power_lock_allowed(void)
{
    return (timer_get_ms() - s_boot_ms) >= WATCH_LOCK_GUARD_MS;
}

void watch_power_boot_anim(void)
{
    s_busy = 0;
    s_on = 1;
    s_asleep = 0;
    s_boot_ms = timer_get_ms();
    s_sleep_tog = 0; /* 丢掉开机松手可能积压的短按 */
}

static void watch_power_sleep(void)
{
    if (s_busy || s_asleep) {
        return;
    }
    if (!watch_power_lock_allowed()) {
        return;
    }
    s_asleep = 1;
    s_on = 0;
    watch_ui_close_dropdown();
    disp_disable_update();
    watch_backlight_off();
    user_lcd_enter_sleep();
}

static void watch_power_wake(void)
{
    if (s_busy || !s_asleep) {
        return;
    }
    user_lcd_exit_sleep();
    watch_backlight_restore();
    disp_enable_update();
    s_asleep = 0;
    s_on = 1;
    /* 强制整屏刷新一次 */
    if (lv_scr_act()) {
        lv_obj_invalidate(lv_scr_act());
    }
}

void watch_power_off(void)
{
    if (s_busy) {
        return;
    }
    s_busy = 1;
    s_on = 0;
    s_asleep = 0;
    watch_ui_close_dropdown();
    watch_ui_close_app();
    disp_disable_update();
    watch_backlight_set(0);
    /* 只置位: soft_poweroff 在 lcd_test 离开 lv_task_handler 后执行 */
    s_do_poweroff = 1;
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
    s_asleep = 0;
    s_busy = 0;
    s_off_req = 0;
    s_sleep_tog = 0;
    s_do_poweroff = 0;
    s_boot_ms = timer_get_ms();
}

u8 watch_power_is_on(void)
{
    return s_on && !s_busy;
}

u8 watch_power_input_enabled(void)
{
    return s_on && !s_busy && !s_asleep;
}

void watch_power_key_click(void)
{
    s_sleep_tog = 1;
}

void watch_power_key_long(void)
{
    s_off_req = 1;
}

void watch_power_poll(lv_timer_t *timer)
{
    (void)timer;
    if (s_sleep_tog) {
        s_sleep_tog = 0;
        if (s_asleep) {
            watch_power_wake();
        } else if (!s_busy && watch_power_lock_allowed()) {
            watch_power_sleep();
        }
        /* 开机 5s 内的短按(含长按开机松手)直接丢弃, 避免误息屏 */
    }
    if (!s_off_req) {
        return;
    }
    s_off_req = 0;
    watch_power_off();
}
