/*
 * CoreMark 应用入口: 开机创建任务, 按键2 置位 g_coremark_req 后运行一次
 */
#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "asm/wdt.h"  /* wdt_close/wdt_init: 跑分期间关狗 */

volatile u8 g_coremark_req;

/* core_main.c 经 -Dmain=coremark_main 重命名 */
extern int coremark_main(void);

static void coremark_task(void *p)
{
    (void)p;
    printf("[coremark] idle, press KEY2 to run\n");
    while (!g_coremark_req) {
        os_time_dly(10);
    }
    os_time_dly(20);
    printf("[coremark] KEY2 -> start (buf in SRAM, PERFORMANCE_RUN)\n");
    os_task_set_priority("coremark", 2);
    {
        extern volatile u8 g_ui_suspend;
        g_ui_suspend = 1;
    }
    /* 勿 wifi_off(): 运行中关 WiFi 会 put_ncmsg 后总线异常复位 */
    os_time_dly(30);   /* 稍等调度 steadier 再跑 */
    wdt_close();
    coremark_main();
    wdt_init(WDT_16S);
    wdt_clear();
    printf("[coremark] finished\n");
    while (1) {
        os_time_dly(1000);
    }
}

void coremark_app_start(void)
{
    os_task_create(coremark_task, NULL, 8, 4096, 0, "coremark");
}
