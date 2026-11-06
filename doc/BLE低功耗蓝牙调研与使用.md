# 低功耗蓝牙 (BLE) 调研与使用 —— 官方 SDK 对照

> 日期: 2026-11-06 · 分支: lcd_demo (V1.8.75)
> 结论先行: **BLE 透传功能本仓库已完整集成并开机自启**, 无需新增;
> 本文档记录官方 SDK 的实现链路(对照依据)与本仓现状、手机端验证步骤、应用层收发 API。

## 1. 官方 SDK 的 BLE 实现 (demo_ble 为基准)

### 1.1 启动链(四步, 与经典蓝牙共用协议栈底座)

```
bt_ble_module_init()                        ← 应用入口(demo_ble/ble.c)
  ├─ lmp_set_sniff_disable()                纯 BLE 方案关 sniff
  ├─ lib_make_ble_address(ble_addr, edr)    BLE 地址 ← 经典蓝牙地址派生
  ├─ le_controller_set_mac(ble_addr)        写控制器
  └─ btstack_init()                         创建 btctrler/btstack 任务(任务表查名)
        ↓ 协议栈起好后抛事件
BT_STATUS_INIT_OK                           ← sys_event 分发(app_default_event_handler 强定义)
  └─ bt_ble_init()                          ← common 层(common_ble/le_trans_data.c)
       ├─ gap 名 = bt_get_local_name() + "(BLE)" 后缀
       ├─ att_server_init(profile_data, read_cb, write_cb)   注册 GATT 表
       ├─ att_server_register_packet_handler(cbk_packet_handler)
       └─ ble_module_enable(1) → bt_ble_adv_enable(1)        自动开始广播
```

要点:
- **双核**: btctrler/btstack 任务名加 `#C0` 前缀钉在 CPU0(BT 控制器中断在核 0,
  任务漂核会丢包——本仓 V1.4.0 实测过 A2DP 丢包 30% 的教训, BLE 同理)。
- **事件路由**: `BT_STATUS_INIT_OK` 只经 `app_default_event_handler`(库内 weak,
  应用强定义)送达, `register_sys_event_handler` 路径收不到(实测)。
- **地址**: BLE 地址不能自定, 由经典蓝牙地址经 `lib_make_ble_address` 派生(原厂约定)。

### 1.2 GATT 透传 profile (le_trans_data.h, 官方 gatt_inc_generator 生成)

| UUID | 属性 | 方向 | 说明 |
|---|---|---|---|
| 0x1800 / 0x2A00 | GAP 服务 | 读 | 设备名(广播名一致) |
| **0xAE80** | 主服务 | — | 杰理透传服务 |
| **0xAE01** | Write Without Response | **手机 → 设备** | 写入即触发应用回调 |
| **0xAE02** | Notify | **设备 → 手机** | 订阅后接收设备数据 |
| 0xAE03 | Write Without Response | 手机 → 设备 | 第二写通道(部分固件作 ACK/大文件) |

### 1.3 应用层数据面 API (le_trans_data.c → `ble_server_operation_t`)

```c
static const struct ble_server_operation_t mi_ble_operation = {
    .adv_enable         = set_adv_enable,        /* 开/关广播 */
    .disconnect         = ble_disconnect,
    .get_buffer_vaild   = get_buffer_vaild_len,  /* 可写余量(流控) */
    .send_data          = app_send_user_data_do, /* → ae02 notify 发给手机 */
    .regist_wakeup_send = regiest_wakeup_send,   /* 发送唤醒回调(继续发包) */
    .regist_recieve_cbk = regiest_recieve_cbk,   /* 注册 ae01 收数回调 */
    .regist_state_cbk   = regiest_state_cbk,     /* 连接/断开状态回调 */
};
/* 手机写 ae01 → att_write_callback → app_recieve_callback(priv, data, len)
 * 设备发手机: mi_ble_operation.send_data(priv, buf, len) → ae02 notify */
```

### 1.4 官方各形态开关(demo_ble/include/app_config.h)

| 宏 | 含义 | 本仓取值 |
|---|---|---|
| TCFG_USER_BLE_ENABLE | BLE 总使能 | 1 |
| TRANS_DATA_EN | 从机透传 | 1 |
| BT_NET_HID_EN | 从机 HID(键盘) | 0 |
| BT_NET_CENTRAL_EN | 主机(扫连别的 BLE) | 0 |
| TRANS_MULTI_BLE_EN | 一对多 | 0 |
| CONFIG_BLE_MESH_ENABLE | Mesh | 0 |
| TCFG_BLE_SECURITY_EN | 配对加密 | 1 |

## 2. 本仓现状 (lcd_demo V1.8.75): 已集成, 开机自启

| 环节 | 状态 | 位置 |
|---|---|---|
| 配置宏 | ✅ `MINI_PWR_BISECT=5 → MINI_BT_SUPPORT=1, MINI_BT_BLE_TRANS_EN=1` | app_config.h |
| 透传服务源码 | ✅ 编译中(le_trans_data.c / le_hogp.c) | Makefile L300 |
| 协议栈库 | ✅ btctrler.a / btstack.a | Makefile 链接 |
| 协议栈任务 | ✅ `#C0btctrler/#C0btstack/#C0btencry`(钉 CPU0) + `bt_app` | app_main.c 任务表 |
| 启动调用 | ✅ app_main → `bt_wifi_start_deferred()` → `bt_start()` → `bt_module_init()` | bt_wifi_app.c L605 |
| BLE 地址派生 | ✅ `lib_make_ble_address` + `le_controller_set_mac` | bt_wifi_app.c L196 |
| INIT_OK → 广播 | ✅ `bt_ble_init()`(le_trans_data.c) | bt_wifi_app.c L197 |
| 固件字符串验证 | ✅ app.bin 含 `ble_init` / `(BLE)`(10/31 构建) | cpu/wl82/tools |

**无需任何代码改动**。广播名 = 经典蓝牙名 + `(BLE)`(名字本体由 btstack.a 从 VM 读取,
`bt_get_local_name` 也是库内符号)。

## 3. 手机端验证步骤

1. 手机装 **nRF Connect**(Android/iOS 均可);
2. 设备上电后等射频起(约数秒, bt_wifi_boot 错峰启动);
3. SCAN → 找到名为 **`<蓝牙名>(BLE)`** 的设备 → CONNECT;
4. 展开服务可见 **0xAE80** 透传服务;
5. 往 **0xAE01** 写任意字节(如 `01 02 03`) → 设备串口日志出现透传接收;
6. 订阅 **0xAE02** notify → 应用层调 `send_data` 即可在手机收到上行数据。

## 4. 后续可选增强(都已有官方参考代码)

- [ ] BLE 名称定制: 现在=经典蓝牙名+`(BLE)`; 要独立名可在 bt_ble_init 前改 gap_device_name 逻辑
- [ ] BLE 配网/传大文件: 参考 `common_ble/le_net_cfg.c`(杰理配网协议, 已在树未编译)
- [ ] BLE HID: 参考 demo_ble 的 BT_NET_HID_EN 路径(ble_hid_set_config)
- [ ] BLE OTA: 参考 wifi_story_machine 的 rcsp 通道(RCSP_BTMATE_EN 分支已在 le_trans_data.c 内置)
