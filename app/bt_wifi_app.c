/*
 * JL WiFi / 蓝牙外设移植 —— 应用胶水层 (AC791x, 移植自原厂 apps/demo/demo_ble)
 *
 * 蓝牙: btctrler.a(控制器) + btstack.a(主机栈) + app/common_ble 档案层(透传服务)
 *       初始化链: btstack_init() → 起协议栈任务 → BT_STATUS_INIT_OK 事件 → bt_ble_init()
 *       (BLE 广播名 "WL82-BLE", GATT 透传特征 ae81 写 / ae82 notify, 详见 le_trans_data.c)
 * WiFi: wl_wifi_sta.a + lwip + net_server(库已入树, MINI_WIFI_SUPPORT=0 关闭,
 *       待 RF 校准数据与网络栈联调后开启)
 *
 * 事件: event.a 按 register_sys_event_handler 注册表分发(SYS_BT_EVENT=0x0010)。
 */
#include "app_config.h"
#include "typedef.h"
#include "os/os_api.h"
#include "event/event.h"
#include "event/key_event.h"
#include "event/bt_event.h"
#include "device/gpio.h"

/* ================ ADC 按键(PB01 电阻梯, 原厂 adkey 框架) ================ */
#include "asm/adc_api.h"
#include "key/key_driver.h"
#include "key/adkey.h"

/* 实测(10bit raw): 空闲 1014 / 按键1 162 / 按键2 0。
 * 中点阈值(升序), 驱动按"第一个 raw≤表项"匹配: 0→按键2, 81<raw≤588→按键1 */
const struct adkey_platform_data adkey_data = {
    .enable       = 1,
    .adkey_pin    = IO_PORTB_01,
    .extern_up_en = 1,              /* 板上外部上拉(空闲 3.13V) */
    .ad_channel   = AD_CH_PB01,     /* = 3 */
    .ad_value     = {0, 588, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023},
    .key_value    = {2, 1, NO_KEY, NO_KEY, NO_KEY, NO_KEY, NO_KEY, NO_KEY, NO_KEY, NO_KEY},
};

static volatile u8 key_bt_armed = 1;   /* 按键2 单击触发一次 BT 启动 */
void bt_wifi_start(void);               /* 定义在文件尾 */

static void key_sys_event_handler(struct sys_event *event)
{
    if (event->type != SYS_KEY_EVENT) {
        return;
    }
    struct key_event *e = (struct key_event *)event->payload;
    if (e->action == KEY_EVENT_CLICK || e->action == KEY_EVENT_UP) {
        printf("[key] v=%d action=%d\n", e->value, e->action);
        if (e->value == 2 && key_bt_armed) {
            key_bt_armed = 0;
            printf("[key] key2 -> bt start\n");
            bt_wifi_start();
        }
    }
}

static void key_framework_init(void)
{
    extern void adc_init(void);
    extern int key_driver_init(void);
    adc_init();
    key_driver_init();                  /* 内部 adkey_init(&adkey_data) + 高精度定时器扫描 */
    register_sys_event_handler(SYS_KEY_EVENT, KEY_EVENT_FROM_KEY, 2, key_sys_event_handler);
    printf("[key] adkey ready (PB01)\n");
}
/* 开机独立初始化: 与 BT 启动解耦(否则按键触发依赖按键框架, 循环依赖) */
void key_early_init(void)
{
    key_framework_init();
}
#if MINI_BT_SUPPORT
#include "btstack/btstack_task.h"
#include "btstack/avctp_user.h"
#include "bt_common.h"

#if MINI_BT_BLE_TRANS_EN
#include "le_trans_data.h"
#endif

static void bt_status_event_handler(struct bt_event *bt)
{
    switch (bt->event) {
        case BT_STATUS_INIT_OK:
            printf("[bt] controller init ok\n");
#if MINI_BT_BLE_TRANS_EN
            bt_ble_init();          /* 设置 GAP 名/广播参数, 注册透传服务并开广播 */
#endif
            break;
        default:
            break;
    }
}

static void bt_sys_event_handler(struct sys_event *event)
{
    if (event->type == SYS_BT_EVENT && event->from == BT_EVENT_FROM_CON) {
        bt_status_event_handler((struct bt_event *)event->payload);
    }
}

void bt_module_init(void *p)
{
    (void)p;
    printf("[bt] task start\n");
    os_time_dly(200);   /* 等 boot 日志与 VM 就绪 */

#if MINI_BT_BLE_TRANS_EN
    /* BLE 地址由经典蓝牙地址派生(原厂同款), MAC 从 VM/syscfg 读取 */
    u8 tmp_ble_addr[6];
    extern void lib_make_ble_address(u8 *ble_address, u8 *edr_address);
    extern int le_controller_set_mac(void *addr);
    printf("[bt] read mac...\n");
    lib_make_ble_address(tmp_ble_addr, (u8 *)bt_get_mac_addr());
    printf("[bt] set controller mac\n");
    le_controller_set_mac((void *)tmp_ble_addr);
    printf("[bt] ble addr %02x:%02x:%02x:%02x:%02x:%02x\n",
           tmp_ble_addr[0], tmp_ble_addr[1], tmp_ble_addr[2],
           tmp_ble_addr[3], tmp_ble_addr[4], tmp_ble_addr[5]);
#endif

    printf("[bt] register event + btstack_init\n");
    register_sys_event_handler(SYS_BT_EVENT, BT_EVENT_FROM_CON, 1, bt_sys_event_handler);
    extern void lmp_set_sniff_disable(void);
    lmp_set_sniff_disable();    /* demo_ble 同款: 纯 BLE 方案关 sniff */
    btstack_init();     /* 创建 btctrler/btstack 任务, 完成后抛 BT_STATUS_INIT_OK */
    printf("[bt] btstack_init done\n");

    while (1) {
        os_time_dly(600);
    }
}
#endif  /* MINI_BT_SUPPORT */

#if MINI_WIFI_SUPPORT
/* WiFi 最小闭环: 上电 → 事件回调注册 → wifi_on → 扫描 → 打印 AP 列表
 * (回调必须先于 wifi_on 注册: 库经 RPC 线程投递首事件, 空回调 = 跳 0 崩溃) */
#include "wifi/wifi_connect.h"

static int wifi_event_cb(void *priv, enum WIFI_EVENT event)
{
    printf("[wifi] event %d\n", event);
    return 0;
}


static void wifi_scan_task(void *p)
{
    (void)p;
    os_time_dly(300);

    wifi_set_event_callback(wifi_event_cb);   /* 原厂 wifi_ipc 同款: 先注册回调 */
    if (wifi_on()) {
        printf("[wifi] wifi_on fail\n");
        while (1) { os_time_dly(600); }
    }
    u8 wmac[6];
    wifi_get_mac(wmac);
    printf("[wifi] on, mac %02x:%02x:%02x:%02x:%02x:%02x\n",
           wmac[0], wmac[1], wmac[2], wmac[3], wmac[4], wmac[5]);

    while (1) {
        printf("[wifi] scan...\n");
        wifi_scan_req();
        os_time_dly(300);
        u32 cnt = 0;
        struct wifi_scan_ssid_info *r = wifi_get_scan_result(&cnt);
        printf("[wifi] ap count %u\n", cnt);
        for (u32 i = 0; i < cnt && i < 20; i++) {
            printf("  [%u] %s ch=%u rssi=%d\n", i, r[i].ssid,
                   r[i].channel_number, r[i].rssi);
        }
        wifi_clear_scan_result();
        os_time_dly(600);
    }
}
#endif

/* BT 启动入口: 由按键1单击或串口 'b' 触发(key 框架已在开机时独立初始化) */
void bt_wifi_start(void)
{
#if MINI_BT_SUPPORT
    printf("[bt] creating bt_app task...\n");
    os_task_create(bt_module_init, NULL, 12, 1024, 0, "bt_app");
    printf("[bt] bt_app task created\n");
#endif
#if MINI_WIFI_SUPPORT
    printf("[wifi] creating wifi_app task...\n");
    os_task_create(wifi_scan_task, NULL, 12, 1024, 0, "wifi_app");
    printf("[wifi] wifi_app task created\n");
#endif
}
