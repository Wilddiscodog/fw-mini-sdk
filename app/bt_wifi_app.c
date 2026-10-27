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
#include "event/net_event.h"
#include "lwip.h"
#include "wifi/wifi_connect.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"            /* NETWORK_SSID_INFO_CNT */

extern const u8 *bt_get_mac_addr(void);
static char save_ssid_flag, request_connect_flag;

/* 网页配网/网页升级(app/net/wifi_prov.c): 事件状态位 + 默认模式选择 + 状态机 */
void wifi_prov_notify(enum WIFI_EVENT event);
void wifi_prov_default_mode_setup(void);
void wifi_prov_run(void);


#define AP_SSID "AC79_WIFI_DEMO_"      //配置 AP模式的SSID前缀
#define AP_PWD  "12345678"                //配置 AP模式的密码
#define STA_SSID  "1705"           //配置 STA模式的SSID
#define STA_PWD  "dlk123996"      //配置 STA模式的密码

//#define STA_MODE_TEST  
#define AP_MODE_TEST   

#define FORCE_DEFAULT_MODE 1 //AP 测试强制使用当前 AP 配置,不加载历史 STA 模式

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

/* BT/WiFi 开机自启; CoreMark/按键跑分已关 */
void bt_start(void);
void wifi_start(void);

static void key_sys_event_handler(struct sys_event *event)
{
    if (event->type != SYS_KEY_EVENT) {
        return;
    }
    struct key_event *e = (struct key_event *)event->payload;
    if (e->action == KEY_EVENT_CLICK || e->action == KEY_EVENT_UP) {
        printf("[key] v=%d action=%d\n", e->value, e->action);
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

/* AP 模式 IP/DHCP 池(wifi_prov.c 的默认模式选择也调用) */
void wifi_set_lan_setting_info(void)
{
    struct lan_setting lan_setting_info = {

        .WIRELESS_IP_ADDR0  = 192,
        .WIRELESS_IP_ADDR1  = 168,
        .WIRELESS_IP_ADDR2  = 1,
        .WIRELESS_IP_ADDR3  = 1,

        .WIRELESS_NETMASK0  = 255,
        .WIRELESS_NETMASK1  = 255,
        .WIRELESS_NETMASK2  = 255,
        .WIRELESS_NETMASK3  = 0,

        .WIRELESS_GATEWAY0  = 192,
        .WIRELESS_GATEWAY1  = 168,
        .WIRELESS_GATEWAY2  = 1,
        .WIRELESS_GATEWAY3  = 1,

        .SERVER_IPADDR1  = 192,
        .SERVER_IPADDR2  = 168,
        .SERVER_IPADDR3  = 1,
        .SERVER_IPADDR4  = 1,

        .CLIENT_IPADDR1  = 192,
        .CLIENT_IPADDR2  = 168,
        .CLIENT_IPADDR3  = 1,
        .CLIENT_IPADDR4  = 2,

        .SUB_NET_MASK1   = 255,
        .SUB_NET_MASK2   = 255,
        .SUB_NET_MASK3   = 255,
        .SUB_NET_MASK4   = 0,
    };
    net_set_lan_info(&lan_setting_info);
}

static void wifi_sta_save_ssid(void)
{
    if (save_ssid_flag) {
        save_ssid_flag = 0;

        struct wifi_mode_info info;
        info.mode = STA_MODE;
        wifi_get_mode_cur_info(&info);
        wifi_store_mode_info(STA_MODE, info.ssid, info.pwd);
    }
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

/* 原厂 app 框架事件路由: sys_event 任务分发给 app_default_event_handler
 * (库内 weak 符号, 强定义接管; 实测 register_sys_event_handler 路径收不到
 *  BT_STATUS_INIT_OK, 蓝牙广播未启动即此因) */
void app_default_event_handler(struct sys_event *event)
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

/* WiFi MAC 来源: 强定义覆盖库 weak 默认(无人提供该函数时, 库打印
 * "must defie macadd method" 并用零 MAC)。复用 bt_get_mac_addr:
 * VM 缺省时 UID 派生 */
int init_net_device_mac_addr(char *macaddr, char ap_mode)
{
    (void)ap_mode;
    memcpy(macaddr, bt_get_mac_addr(), 6);
    return 0;
}

static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    wifi_prov_notify(event);            /* 配网/升级模块状态位(AP_START/DHCP/超时/扫描完) */
    struct net_event net = {0};
    net.arg = "net";
    int ret = 0;

    switch (event) {

    case WIFI_EVENT_MODULE_INIT:

#if defined(STA_MODE_TEST)
        wifi_set_sta_connect_timeout(30);   //配置STA模式连接超时后事件回调通知时间
#endif
        wifi_set_smp_cfg_timeout(30);       //配置MONITOR模式超时后事件回调通知时间

        /* 网页配网: 读存储→有则默认 STA 连存储配置, 无则默认 AP
         * (原 AP_MODE_TEST/STA_MODE_TEST 硬编码模式由其取代) */
        wifi_prov_default_mode_setup();
        break;

    case WIFI_EVENT_MODULE_START:
        puts("|network_user_callback->WIFI_EVENT_MODULE_START\n");

        struct wifi_mode_info info;
        info.mode = NONE_MODE;
        wifi_get_mode_cur_info(&info);
        if (info.mode == SMP_CFG_MODE) {
            net.arg = "net";
            net.event = NET_EVENT_SMP_CFG_FIRST;
            net_event_notify(NET_EVENT_FROM_USER, &net);
        }

        u32  tx_rate_control_tab = // 不需要哪个速率就删除掉,可以动态设定
            0
            | BIT(0) //0:CCK 1M
            | BIT(1) //1:CCK 2M
            | BIT(2) //2:CCK 5.5M
            | BIT(3) //3:OFDM 6M
            | BIT(4) //4:MCS0/7.2M
            | BIT(5) //5:OFDM 9M
            | BIT(6) //6:CCK 11M
            | BIT(7) //7:OFDM 12M
            | BIT(8) //8:MCS1/14.4M
            | BIT(9) //9:OFDM 18M
            | BIT(10) //10:MCS2/21.7M
            | BIT(11) //11:OFDM 24M
            | BIT(12) //12:MCS3/28.9M
            | BIT(13) //13:OFDM 36M
            | BIT(14) //14:MCS4/43.3M
            | BIT(15) //15:OFDM 48M
            | BIT(16) //16:OFDM 54M
            | BIT(17) //17:MCS5/57.8M
            | BIT(18) //18:MCS6/65.0M
            | BIT(19) //19:MCS7/72.2M
            ;
        wifi_set_tx_rate_control_tab(tx_rate_control_tab);
#if 0
        wifi_set_pwr(0); //把WIFI模拟功率调整到最低档位节电
#endif

        break;
    case WIFI_EVENT_MODULE_STOP:
        puts("|network_user_callback->WIFI_EVENT_MODULE_STOP\n");
        break;

    case WIFI_EVENT_AP_START:
        /* 关闭「连最优 SSID」自动重连, 但保留 store_ssid_cnt, 否则扫描列表/存配会异常 */
        wifi_set_sta_connect_best_ssid(0);
        wifi_set_store_ssid_cnt(NETWORK_SSID_INFO_CNT);
        wifi_rxfilter_cfg(1);            /* AP 默认不过滤, 扫描依赖广播 beacon */
        printf("[wifi] AP active: best-ssid reconnect off, rxfilter=AP-open\n");
        printf("|network_user_callback->WIFI_EVENT_AP_START,CH=%d\n", wifi_get_channel());
#ifdef USE_FTP_UPGRADE_DEMO
        extern void ftp_update_test(void);
        ftp_update_test();
#endif //USE_FTP_UPGRADE_DEMO
        break;
    case WIFI_EVENT_AP_STOP:
        puts("|network_user_callback->WIFI_EVENT_AP_STOP\n");
        break;

    case WIFI_EVENT_STA_START:
        puts("|network_user_callback->WIFI_EVENT_STA_START\n");
        break;
    case WIFI_EVENT_MODULE_START_ERR:
        puts("|network_user_callback->WIFI_EVENT_MODULE_START_ERR\n");
        break;
    case WIFI_EVENT_STA_STOP:
        puts("|network_user_callback->WIFI_EVENT_STA_STOP\n");
        break;
    case WIFI_EVENT_STA_DISCONNECT:
        puts("|network_user_callback->WIFI_STA_DISCONNECT\n");
        /*wifi_rxfilter_cfg(0);*/

#ifdef CONFIG_ASSIGN_MACADDR_ENABLE
        if (!mac_addr_succ_flag) {
            break;
        }
#endif

        net.event = NET_EVENT_DISCONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);

#ifndef WIFI_MODE_CYCLE_TEST
        if (!request_connect_flag) { //如果是应用程序主动请求连接导致断线就不需要发送重连事件, 否则像信号不好导致断线的原因就发送重连事件
            net.event = NET_EVENT_DISCONNECTED_AND_REQ_CONNECT;
            net_event_notify(NET_EVENT_FROM_USER, &net);
        }
#endif
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        puts("|network_user_callback->WIFI_STA_SCAN_COMPLETED\n");
#ifdef CONFIG_AIRKISS_NET_CFG
        extern void airkiss_ssid_check(void);
        airkiss_ssid_check();
#endif
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC:
        printf("|network_user_callback->WIFI_STA_CONNECT_SUCC,CH=%d\r\n", wifi_get_channel());
        /*wifi_rxfilter_cfg(3);    //过滤not_my_bssid,如果需要使用扫描空中SSID就不要过滤*/
#ifdef CONFIG_STATIC_IPADDR_ENABLE
        if (0 == compare_dhcp_ipaddr()) {
            wifi_set_sta_ip_info();
            ret = 1;
        }
#endif
        break;

    case WIFI_EVENT_MP_TEST_START:
        puts("|network_user_callback->WIFI_EVENT_MP_TEST_START\n");
        break;
    case WIFI_EVENT_MP_TEST_STOP:
        puts("|network_user_callback->WIFI_EVENT_MP_TEST_STOP\n");
        break;

    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        puts("|network_user_callback->WIFI_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID\n");
        net.event = NET_CONNECT_TIMEOUT_NOT_FOUND_SSID;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
        puts("|network_user_callback->WIFI_STA_CONNECT_ASSOCIAT_FAIL .....\n");
        net.event = NET_CONNECT_ASSOCIAT_FAIL;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        puts("|network_user_callback->WIFI_STA_CONNECT_ASSOCIAT_TIMEOUT .....\n");
        break;

    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        puts("|network_user_callback->WIFI_EVENT_STA_NETWPRK_STACK_DHCP_SUCC\n");

#ifdef USE_DevKitBoard_TEST_DEMO
        os_taskq_post("ui_main_task", 1, UI_MSG_WIFI_STA_CONNT_OK);
#endif
        wifi_sta_save_ssid(); //确认获取IP成功再真正去考虑要不要保存配置, 否则如果配置有误就保存的情况下导致下次连不上WIFI

#ifdef CONFIG_ASSIGN_MACADDR_ENABLE

        if (!is_server_assign_macaddr_ok()) { //如果使用服务器分配MAC地址的情况,需要确认MAC地址有效才发送连接成功事件到APP层,否则先访问服务器分配mac地址
            server_assign_macaddr(wifi_return_sta_mode);
            break;
        }
        mac_addr_succ_flag = 1;
#endif
#ifdef CONFIG_STATIC_IPADDR_ENABLE
        store_dhcp_ipaddr();
#endif
        request_connect_flag = 0;
        net.event = NET_EVENT_CONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        puts("|network_user_callback->WIFI_EVENT_STA_NETWPRK_STACK_DHCP_TIMEOUT\n");
        break;

    case WIFI_EVENT_P2P_START:
        puts("|network_user_callback->WIFI_EVENT_P2P_START\n");
        break;
    case WIFI_EVENT_P2P_STOP:
        puts("|network_user_callback->WIFI_EVENT_P2P_STOP\n");
        break;
    case WIFI_EVENT_P2P_GC_DISCONNECTED:
        puts("|network_user_callback->WIFI_EVENT_P2P_GC_DISCONNECTED\n");
        break;
    case WIFI_EVENT_P2P_GC_NETWORK_STACK_DHCP_SUCC:
        puts("|network_user_callback->WIFI_EVENT_P2P_GC_NETWORK_STACK_DHCP_SUCC\n");
        break;
    case WIFI_EVENT_P2P_GC_NETWORK_STACK_DHCP_TIMEOUT:
        puts("|network_user_callback->WIFI_EVENT_P2P_GC_NETWORK_STACK_DHCP_TIMEOUT\n");
        break;

    case WIFI_EVENT_SMP_CFG_START:
        puts("|network_user_callback->WIFI_EVENT_SMP_CFG_START\n");
        break;
    case WIFI_EVENT_SMP_CFG_STOP:
        puts("|network_user_callback->WIFI_EVENT_SMP_CFG_STOP\n");
        break;
    case WIFI_EVENT_SMP_CFG_TIMEOUT:
        puts("|network_user_callback->WIFI_EVENT_SMP_CFG_TIMEOUT\n");
        net.event = NET_EVENT_SMP_CFG_TIMEOUT;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_SMP_CFG_COMPLETED:
        puts("|network_user_callback->WIFI_EVENT_SMP_CFG_COMPLETED\n");
        net.event = NET_SMP_CFG_COMPLETED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    case WIFI_EVENT_PM_SUSPEND:
        puts("|network_user_callback->WIFI_EVENT_PM_SUSPEND\n");
        break;
    case WIFI_EVENT_PM_RESUME:
        puts("|network_user_callback->WIFI_EVENT_PM_RESUME\n");
        break;
    case WIFI_EVENT_AP_ON_ASSOC:
        ;
        struct eth_addr *hwaddr = (struct eth_addr *)network_ctx;
        printf("WIFI_EVENT_AP_ON_ASSOC hwaddr = %02x:%02x:%02x:%02x:%02x:%02x \r\n\r\n",
               hwaddr->addr[0], hwaddr->addr[1], hwaddr->addr[2], hwaddr->addr[3], hwaddr->addr[4], hwaddr->addr[5]);
        break;
    case WIFI_EVENT_AP_ON_DISCONNECTED:
        struct ip4_addr ipaddr;
        hwaddr = (struct eth_addr *)network_ctx;
        dhcps_get_ipaddr(hwaddr->addr, &ipaddr);
        printf("WIFI_EVENT_AP_ON_DISCONNECTED hwaddr = %02x:%02x:%02x:%02x:%02x:%02x, ipaddr = [%d.%d.%d.%d] \r\n\r\n",
               hwaddr->addr[0], hwaddr->addr[1], hwaddr->addr[2], hwaddr->addr[3], hwaddr->addr[4], hwaddr->addr[5],
               ip4_addr1(&ipaddr), ip4_addr2(&ipaddr), ip4_addr3(&ipaddr), ip4_addr4(&ipaddr));
        break;
    default:
        break;
    }

    return ret;    
}

static void wifi_status(void *p)
{
    if (wifi_is_on()) {
        stats_display(); //LWIP stats
        printf("WIFI U= %d KB/s, D= %d KB/s\r\n", wifi_get_upload_rate() / 1024, wifi_get_download_rate() / 1024);

        struct wifi_mode_info info;
        info.mode = NONE_MODE;
        wifi_get_mode_cur_info(&info);
        if (info.mode == AP_MODE) {
            for (int i = 0; i < 8; i++) {
                char *rssi;
                u8 *evm, *mac;
                if (wifi_get_sta_entry_rssi(i, &rssi, &evm, &mac)) {
                    break;
                }
                if (*rssi) { //侦测连接到AP端的设备信号质量
                    printf("MAC[%x:%x:%x:%x:%x:%x],RSSI=%d,EVM=%d \r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], *rssi, *evm);
                }
            }
        } else if (info.mode == STA_MODE) {
            printf("Router_RSSI=%d,Quality=%d \r\n", wifi_get_rssi(), wifi_get_cqi()); //侦测路由器端信号质量
        }
    }
}

/* WiFi 应用任务: 注册回调(必须先于 wifi_on: 库经 RPC 投递首事件, 空回调跳 0)
 * → wifi_on(MODULE_INIT 里配网模块已选定默认模式) → 配网状态机
 * (有存储→STA 重试 3 次→失败转 AP; 无存储→直接 AP; AP/STA 均起网页后台) */
static void wifi_app_task(void *p)
{
    (void)p;
    os_time_dly(200);
    wifi_set_event_callback(wifi_event_callback);
    if (wifi_on()) {
        printf("[wifi] wifi_on fail\n");
        while (1) { os_time_dly(600); }
    }
    u8 wmac[6];
    wifi_get_mac(wmac);
    printf("[wifi] on, mac %02x:%02x:%02x:%02x:%02x:%02x\n",
           wmac[0], wmac[1], wmac[2], wmac[3], wmac[4], wmac[5]);

    wifi_prov_run();                     /* 配网状态机, 不返回 */
}
#endif

/* 外设启动入口: 开机自启 BT + WiFi; 按键1 另触发 LVGL benchmark */
void bt_start(void)
{
#if MINI_BT_SUPPORT
    static u8 started = 0;
    if (started) {
        printf("[bt] already started\n");
        return;
    }
    started = 1;
    printf("[bt] creating bt_app task...\n");
    os_task_create(bt_module_init, NULL, 12, 1024, 0, "bt_app");
    printf("[bt] bt_app task created\n");
#endif
}

void wifi_start(void)
{
#if MINI_WIFI_SUPPORT
    static u8 started = 0;
    if (started) {
        printf("[wifi] already started\n");
        return;
    }
    started = 1;
    printf("[wifi] creating wifi_app task...\n");
    os_task_create(wifi_app_task, NULL, 12, 2048, 0, "wifi_app");
    printf("[wifi] wifi_app task created\n");
#endif
}

void bt_wifi_start(void)
{
    bt_start();
    /* BT 与 WiFi 错开, 减轻同时拉 RF/时钟 */
    os_time_dly(100);
    wifi_start();
}

/* 延后自启: app_main 里立刻 bt_wifi_start 会与 USB 枚举(~1s)撞车,
 * 日志截断、WiFi 异常; 按键晚启则正常。等 USB/系统 steadier 再拉起。 */
#ifndef BT_WIFI_BOOT_DELAY_TICKS
#define BT_WIFI_BOOT_DELAY_TICKS  500   /* 5s; os tick=10ms */
#endif

static void bt_wifi_boot_task(void *p)
{
    (void)p;
    printf("[boot] BT/WiFi deferred %d ms...\n", BT_WIFI_BOOT_DELAY_TICKS * 10);
    os_time_dly(BT_WIFI_BOOT_DELAY_TICKS);
    printf("[boot] starting BT + WiFi now\n");
    bt_wifi_start();
    while (1) {
        os_time_dly(10000);
    }
}

void bt_wifi_start_deferred(void)
{
    os_task_create(bt_wifi_boot_task, NULL, 15, 1024, 0, "bt_wifi_boot");
}
