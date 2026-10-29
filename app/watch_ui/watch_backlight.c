#include "app_config.h"
#include "system/includes.h"
#include "device/device.h"
#include "asm/pwm.h"
#include "lcd_drive.h"
#include "watch_backlight.h"
#include "watch_style.h"

static u8 s_percent = WATCH_BL_DEFAULT;
static u8 s_saved = WATCH_BL_DEFAULT;
static void *s_pwm;
static struct pwm_platform_data s_pwm_cfg;

void watch_backlight_init(void)
{
    s_percent = WATCH_BL_DEFAULT;
    s_saved = WATCH_BL_DEFAULT;
    s_pwm = dev_open("pwm_bl", &s_pwm_cfg);
    if (!s_pwm) {
        printf("[watch] pwm_bl open fail, GPIO backlight fallback\n");
        lcd_bl_pinstate(BL_ON);
        return;
    }
    s_pwm_cfg.pwm_ch = PWM_TIMER3_OPCH3;
    s_pwm_cfg.port = IO_PORTA_06;
    s_pwm_cfg.freq = 2000;
    s_pwm_cfg.duty = (float)s_percent;
    s_pwm_cfg.point_bit = 0;
    dev_ioctl(s_pwm, PWM_SET_FREQ, (u32)&s_pwm_cfg);
    dev_ioctl(s_pwm, PWM_RUN, (u32)&s_pwm_cfg);
    printf("[watch] backlight PWM ready %d%%\n", s_percent);
}

void watch_backlight_set(u8 percent)
{
    if (percent > WATCH_BL_MAX) {
        percent = WATCH_BL_MAX;
    }
    if (percent < WATCH_BL_MIN && percent != 0) {
        percent = WATCH_BL_MIN;
    }
    s_percent = percent;
    if (percent == 0) {
        if (s_pwm) {
            s_pwm_cfg.duty = 0;
            dev_ioctl(s_pwm, PWM_SET_DUTY, (u32)&s_pwm_cfg);
        } else {
            lcd_bl_pinstate(BL_OFF);
        }
        return;
    }
    if (s_pwm) {
        s_pwm_cfg.duty = (float)percent;
        dev_ioctl(s_pwm, PWM_SET_DUTY, (u32)&s_pwm_cfg);
        dev_ioctl(s_pwm, PWM_RUN, (u32)&s_pwm_cfg);
    } else {
        lcd_bl_pinstate(BL_ON);
    }
}

u8 watch_backlight_get(void)
{
    return s_percent < WATCH_BL_MIN ? WATCH_BL_MIN : s_percent;
}

void watch_backlight_off(void)
{
    if (s_percent >= WATCH_BL_MIN) {
        s_saved = s_percent;
    }
    watch_backlight_set(0);
}

void watch_backlight_restore(void)
{
    u8 p = s_saved < WATCH_BL_MIN ? WATCH_BL_DEFAULT : s_saved;
    watch_backlight_set(p);
}
