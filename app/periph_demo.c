/*
 * 外设演示任务集 —— 每个外设一个宏开关(Makefile MINI_XXX_SUPPORT, 默认 0)。
 * 移植计划与启用指南见 doc/外设移植计划.md。
 * 参考原厂: apps/common/example/peripheral/(pwm/rtc/spi/wdt...)
 */
#include "app_config.h"
#include "system/includes.h"
#include "device/device.h"

#if MINI_PWM_SUPPORT
#include "asm/pwm.h"
/* PWM: 打开设备(读取 board.c 配置) → 变频/变占空比演示 */
static void pwm_demo_task(void *p)
{
    void *hdl = NULL;
    struct pwm_platform_data pwm = {0};

    hdl = dev_open("pwm1", &pwm);   /* 传参可取回 board.c 配置 */
    if (!hdl) {
        printf("[pwm] open fail\n");
        while (1) { os_time_dly(600); }
    }
    printf("[pwm] ch=0x%x freq=%d duty=%d\n", pwm.pwm_ch, pwm.freq, (int)pwm.duty);
    os_time_dly(200);

    pwm.pwm_ch = PWM_TIMER2_OPCH2;
    pwm.freq = 2000;
    pwm.duty = 20;
    dev_ioctl(hdl, PWM_SET_FREQ, (u32)&pwm);
    os_time_dly(200);
    pwm.duty = 50;
    dev_ioctl(hdl, PWM_SET_DUTY, (u32)&pwm);
    printf("[pwm] running 2kHz 50%%\n");

    while (1) { os_time_dly(600); }
}
#endif

#if MINI_RTC_SUPPORT
#include "asm/rtc.h"
/* RTC: 打开设备 → 每秒打印系统时间 */
static void rtc_demo_task(void *p)
{
    void *hdl = NULL;
    struct sys_time t = {0};

    extern void rtc_early_init(void);
    rtc_early_init();
    hdl = dev_open("rtc", NULL);
    if (!hdl) {
        printf("[rtc] open fail\n");
        while (1) { os_time_dly(600); }
    }
    dev_ioctl(hdl, IOCTL_GET_SYS_TIME, (u32)&t);
    printf("[rtc] %04d-%02d-%02d %02d:%02d:%02d\n",
           t.year, t.month, t.day, t.hour, t.min, t.sec);
    while (1) {
        os_time_dly(100);
        dev_ioctl(hdl, IOCTL_GET_SYS_TIME, (u32)&t);
        printf("[rtc] %04d-%02d-%02d %02d:%02d:%02d\n",
               t.year, t.month, t.day, t.hour, t.min, t.sec);
    }
}
#endif

#if MINI_SD_SUPPORT
/* SD 卡: dev_open 在线检测(CMD 检卡模式), 卡插入即打开成功 */
static void sd_demo_task(void *p)
{
    void *hdl = NULL;

    while (1) {
        hdl = dev_open("sd0", NULL);
        if (hdl) {
            printf("[sd] card online, hdl=%x\n", (u32)hdl);
            dev_close(hdl);
            break;
        }
        os_time_dly(100);
    }
    while (1) { os_time_dly(600); }
}
#endif

#if MINI_HW_CRYPTO_SUPPORT
#include "asm/aes_hw.h"
#include "asm/sha_hw.h"
/* 硬件加密自检: AES-128 加解密回环 + SHA256 已知向量 */
static void crypto_demo_task(void *p)
{
    unsigned char in[16] = "wl82-aes-test-01";
    unsigned char enc[16], dec[16], key[16];
    unsigned char sha[32];
    int i, ok;

    for (i = 0; i < 16; i++) { key[i] = i * 17 + 3; }

    jl_aes_encrypt_hw(in, enc, key, 16);
    jl_aes_decrypt_hw(enc, dec, key, 16);
    ok = memcmp(in, dec, 16) == 0;
    printf("[crypto] aes128 roundtrip %s enc[0..3]=%02x%02x%02x%02x\n",
           ok ? "PASS" : "FAIL", enc[0], enc[1], enc[2], enc[3]);

    jl_sha256(in, 16, sha, 0);
    printf("[crypto] sha256[0..7]=%02x%02x%02x%02x%02x%02x%02x%02x\n",
           sha[0], sha[1], sha[2], sha[3], sha[4], sha[5], sha[6], sha[7]);

    while (1) { os_time_dly(600); }
}
#endif

/* 总入口: 按 Makefile 开关拉起对应演示任务 */
void periph_demo_start(void)
{
#if MINI_PWM_SUPPORT
    os_task_create(pwm_demo_task, NULL, 10, 512, 0, "pwm_demo");
#endif
#if MINI_RTC_SUPPORT
    os_task_create(rtc_demo_task, NULL, 10, 512, 0, "rtc_demo");
#endif
#if MINI_SD_SUPPORT
    os_task_create(sd_demo_task, NULL, 10, 512, 0, "sd_demo");
#endif
#if MINI_HW_CRYPTO_SUPPORT
    os_task_create(crypto_demo_task, NULL, 10, 768, 0, "crypto_demo");
#endif
}
