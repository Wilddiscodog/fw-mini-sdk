/*
 * ble_gamepad.c —— BLE 主机连接 Xbox 等 HID 手柄 (MINI_BLE_CENTRAL_EN)
 *
 * 协议: BLE HID(HOGP)。手柄 = GATT 从机(HID 0x1812, Report 0x2A4D notify);
 *       手表 = BLE Central + GATT 客户端。
 * 骨架: 官方 common/ble/le_net_central.c 客户端框架(本仓同款在树):
 *   ble_get_client_operation_table()->init_config(client_conn_cfg_t)
 *   - match_dev_cfg: 按广播名匹配手柄
 *   - search_uuid_table: HID Report(0x2A4D, NOTIFY) —— 框架自动写 CCCD 订阅
 *   - report_data_callback: 收 notify → 解析 Xbox 报文 → 按键/摇杆回调
 *
 * Xbox One S/Series BLE 报文(0x2A4D notify, HID Report ID 0x01 前导):
 *   [0]=0x01 [1..3]=采样计数 [4]=按键字低8位(A/B/X/Y/LB/RB/View/Menu)
 *   [5]=按键字高8位(profile/Guide/Share/Dpad) [6..7]=LT [8..9]=RT (u16 LE)
 *   [10..17]=LX,LY,RX,RY (s16 LE)
 *   ★ 位定义含厂商差异, 首次连接的报文 hexdump 会打印, 按需校准 BUTTON_MAP。
 *
 * 应用层: 覆盖弱符号 ble_gamepad_on_input() 接 UI;
 *         日志: [pad] 前缀。开关: app_config MINI_BLE_CENTRAL_EN。
 */
#include "app_config.h"

#if (defined MINI_BLE_CENTRAL_EN) && MINI_BLE_CENTRAL_EN

#include "system/includes.h"
#include "os/os_api.h"
#include "btstack/btstack_task.h"
#include "btstack/le/le_user.h"
#include "btstack/le/att.h"
#include "le_common.h"
#include "le_net_central.h"

/* ============ 匹配与订阅配置 ============ */

/* 广播名匹配 —— 精确长度+精确字节(非前缀):
 * BLE 广播包 31 字节上限会截断名字, 实测 Xbox 广播为 22 字节截断版
 * "Xbox Wireless Controll"(log remoter_name 实证); 完整名留作其它型号。 */
static const char gamepad_name_trunc[] = "Xbox Wireless Controll";   /* 22B 截断名 */
static const char gamepad_name_full[]  = "Xbox Wireless Controller"; /* 24B 完整名 */

/* 本只手柄地址兜底(日志 hexdump 原序, CLI_CREAT_BY_ADDRESS 同序比对) */
static const u8 gamepad_addr_ours[6] = {0x92, 0x4F, 0xD2, 0x3B, 0xDA, 0xEC};

/* HID: service 0x1812 / characteristic Report 0x2A4D, 订阅 NOTIFY
 * (框架对 NOTIFY 型自动写 CCCD=value_handle+1 ← 0x01) */
static const target_uuid_t hid_uuids[] = {
    {
        .services_uuid16       = 0x1812,           /* HID Service */
        .characteristic_uuid16 = 0x2A4D,           /* Report */
        .opt_type              = ATT_PROPERTY_NOTIFY,
    },
    /* Protocol Mode 0x2A4C: 记下 handle, profile 完成后写 0x01 切 Report 协议 */
    {
        .services_uuid16       = 0x1812,
        .characteristic_uuid16 = 0x2A4C,
        .opt_type              = ATT_PROPERTY_WRITE_WITHOUT_RESPONSE,
    },
};

/* CLIENT_MATCH_CONN_MAX=3, 仅 3 槽: 截断名 / 完整名 / 本只地址 */
static const client_match_cfg_t match_pads[] = {
    { .create_conn_mode = BIT(CLI_CREAT_BY_NAME),
      .compare_data = (const u8 *)gamepad_name_trunc,
      .compare_data_len = sizeof(gamepad_name_trunc) - 1,
      .bonding_flag = 1 },
    { .create_conn_mode = BIT(CLI_CREAT_BY_NAME),
      .compare_data = (const u8 *)gamepad_name_full,
      .compare_data_len = sizeof(gamepad_name_full) - 1,
      .bonding_flag = 1 },
    { .create_conn_mode = BIT(CLI_CREAT_BY_ADDRESS),
      .compare_data = gamepad_addr_ours,
      .compare_data_len = 6,
      .bonding_flag = 1 },
};

static client_conn_cfg_t pad_conn_cfg = {
    .match_dev_cfg = { &match_pads[0], &match_pads[1], &match_pads[2] },
    .security_en          = 1,                   /* 手柄要求配对绑定 */
    .search_uuid_cnt      = ARRAY_SIZE(hid_uuids),
    .search_uuid_table    = hid_uuids,
    .report_data_callback = NULL,                /* 赋值见下 */
    .event_callback       = NULL,
};

/* 0x2A4C(Protocol Mode) 的 value handle, MATCH_UUID 事件时记录 */
static u16 proto_mode_handle;

/* ============ 输入抽象(供 UI 接入) ============ */
struct ble_gamepad_state {
    u16 buttons;                                 /* 见 BUTTON_* 位定义 */
    s16 lx, ly, rx, ry;
    u16 lt, rt;
};

/* 按键字位定义([4]|[5]<<8) —— 厂商差异待首次实测校准 */
#define BTN_A     BIT(0)
#define BTN_B     BIT(1)
#define BTN_X     BIT(2)
#define BTN_Y     BIT(3)
#define BTN_LB    BIT(4)
#define BTN_RB    BIT(5)
#define BTN_VIEW  BIT(6)
#define BTN_MENU  BIT(7)
#define BTN_PROFILE BIT(8)
#define BTN_GUIDE BIT(10)
#define BTN_SHARE BIT(11)

/* UI 接入点: 弱符号, 默认打日志; 上层覆盖即可接管按键 */
__attribute__((weak)) void ble_gamepad_on_input(const struct ble_gamepad_state *st,
        int pressed_mask, int released_mask)
{
    if (pressed_mask || released_mask) {
        printf("[pad] btn %04x +%-4x -%-4x lx=%4d ly=%4d rx=%4d ry=%4d lt=%3d rt=%3d",
                 st->buttons, pressed_mask, released_mask,
                 st->lx, st->ly, st->rx, st->ry, st->lt, st->rt);
    }
}

/* ============ Report 解析 ============ */
static struct ble_gamepad_state pad_last;
static u8 pad_log_left = 3;                      /* 首连前 3 包打印原文(校准布局) */

static void pad_parse_report(const u8 *data, u16 len)
{
    /* 防御: 报文最短 18 字节(含 3 字节计数), 长度不符打原文等待校准 */
    if (len < 18) {
        if (pad_log_left) {
            printf("[pad] short report(%d)", len);
            put_buf((void *)data, len);
        }
        return;
    }

    struct ble_gamepad_state st = {0};
    st.buttons = data[4] | (data[5] << 8);
    st.lt = data[6]  | (data[7]  << 8);
    st.rt = data[8]  | (data[9]  << 8);
    st.lx = (s16)(data[10] | (data[11] << 8));
    st.ly = (s16)(data[12] | (data[13] << 8));
    st.rx = (s16)(data[14] | (data[15] << 8));
    st.ry = (s16)(data[16] | (data[17] << 8));

    if (pad_log_left) {
        printf("[pad] report id=%02x seq=%02x", data[0], data[1]);
        put_buf((void *)data, len > 20 ? 20 : len);
        pad_log_left--;
    }

    u16 pressed = st.buttons & ~pad_last.buttons;
    u16 released = pad_last.buttons & ~st.buttons;
    if (pressed || released || memcmp(&st.lx, &pad_last.lx, 12)) {
        ble_gamepad_on_input(&st, pressed, released);
    }
    pad_last = st;
}

/* ============ 客户端框架回调 ============ */
static void pad_report_data_deal(att_data_report_t *report_data,
                                 const target_uuid_t *search_uuid)
{
    switch (report_data->packet_type) {
    case GATT_EVENT_NOTIFICATION:
    case GATT_EVENT_INDICATION:
        if (report_data->value_handle == hid_uuids[0].characteristic_uuid16 ||
            1) {                                 /* 只有一条 notify 源, 宽松处理 */
            pad_parse_report(report_data->blob, report_data->blob_length);
        }
        break;
    default:
        break;
    }
}

static void pad_event_cb(le_client_event_e event, u8 *packet, int size)
{
    switch (event) {
    case CLI_EVENT_MATCH_DEV:
        printf("[pad] dev matched: %s",
                 ((client_match_cfg_t *)packet)->compare_data);
        break;

    case CLI_EVENT_MATCH_UUID: {
        opt_handle_t *opt = (opt_handle_t *)packet;
        printf("[pad] uuid %04x -> handle %04x",
                 opt->search_uuid->characteristic_uuid16, opt->value_handle);
        if (opt->search_uuid->characteristic_uuid16 == 0x2A4C) {
            proto_mode_handle = opt->value_handle;
        }
        break;
    }

    case CLI_EVENT_SEARCH_PROFILE_COMPLETE:
        printf("[pad] profile complete%s", proto_mode_handle ? ", set report mode" : "");
        if (proto_mode_handle) {
            /* HID Protocol Mode = 1(Report 协议, 非 Boot) */
            u8 mode = 1;
            struct ble_client_operation_t *cli = ble_get_client_operation_table();
            cli->opt_comm_send(proto_mode_handle, &mode, 1, ATT_OP_WRITE_WITHOUT_RESPOND);
        }
        break;

    default:
        break;
    }
}

/* ============ 入口(bt_wifi_app.c INIT_OK 时调用) ============ */
void ble_gamepad_host_init(void)
{
    pad_conn_cfg.report_data_callback = pad_report_data_deal;
    pad_conn_cfg.event_callback       = pad_event_cb;

    struct ble_client_operation_t *cli = ble_get_client_operation_table();
    if (!cli) {
        printf("[pad] no client api");
        return;
    }
    cli->init_config(0, (void *)&pad_conn_cfg);
    printf("[pad] host ready, waiting for gamepad...");
}

#endif  /* MINI_BLE_CENTRAL_EN */
