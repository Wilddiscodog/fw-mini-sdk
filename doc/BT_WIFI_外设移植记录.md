# AC791x (WL82) 蓝牙 / WiFi 外设移植记录

> 记录 fw-mini-sdk 移植杰理原厂蓝牙(btctrler/btstack)与 WiFi(wl_wifi_sta/lwip)
> 外设栈的过程。USB 部分见《USB_HUSB_主机移植调试记录.md》。
> 文档版本对应固件 **V0.6.1**(蓝牙/WiFi 已完整链接, 触发式启动, 硬件验证进行中)。

---

## 1. 硬件与栈架构

```
┌────────────────────────────────────────────────────┐
│ AC7912BB (WL82): WiFi 2.4G + BT(经典+BLE) 共用射频   │
│                                                    │
│ 蓝牙:  btctrler.a(控制器/LMP/LL)                    │
│        + btstack.a(HCI/GATT/SDP/SMP)               │
│        + wl_rf_common.a(RF 公共)                    │
│        + crypto_toolbox/lib_ccm_aes(SMP 加密)       │
│        + lib_sig_mesh(mesh, 未启用)                 │
│ WiFi:  wl_wifi_sta.a(STA 模式)                      │
│        + wpasupplicant.a(WPA)                       │
│        + lwip_2_1_3.a(TCP/IP)                       │
│        + hsm/libmbedtls/http_cli/json/iperf(服务)   │
│ 应用档案层(开源): apps/common/ble/(透传服务 le_trans_data)│
└────────────────────────────────────────────────────┘
```

- 库全部为闭源 .a,应用侧只需要:胶水初始化 + 事件路由 + 档案配置。
- 蓝牙/WiFi 射频共用,共存由库内协调;`wl_rf_common.a` 为两者公共依赖。

## 2. 移植计划(按阶段)

| 阶段 | 内容 | 判据 | 状态 |
|---|---|---|---|
| A. 系统级对照 | 电源域/VM/任务表/事件框架/时钟与原厂 demo 逐项比对 | 对照表全绿 | ✅ 完成 |
| B. 库与构建 | 六个 BT 库 + WiFi 库入树, Makefile 接线 | 零未定义符号 | ✅ 完成 |
| C. 胶水与档案 | 初始化链/事件路由/BLE 透传档案/触发门控 | 编译通过 | ✅ 完成 |
| D. BT 硬件验证 | 按键触发 → 分段日志 → 手机扫 BLE → GATT 连接 | 广播可见+连接 | ⏳ 待板测 |
| E. WiFi 硬件验证 | wifi_on → 扫描 → AP 列表打印 | ap count > 0 | ⏳ 待板测 |
| F. 联调 | BT+WiFi 并发(RF 共存), 透传收发 | 双链路稳定 | ⏳ 未开始 |

**方法论约定**(吸取 USB 移植教训):
1. 每轮改动前先做与原厂 demo 的**全量静态对照**,不允许无对照的盲改;
2. 启动链全部**分段打印**,配合 1s 心跳任务区分"系统冻结"与"单任务阻塞";
3. BT/WiFi 启动**不自动执行**,由按键1/串口 'b' 触发(启动挂死不拖累其余功能);
4. 硬件日志必须**完整截取(≥10s)**——V0.5.1/V0.5.2 的"无蓝牙日志"结论
   均基于 0.145s 截断日志,而 BT 首条打印在 ~2.2s(delay 200 tick),疑似误判。

## 3. 系统级对照表(A 阶段全量结果)

| 对照项 | 原厂 demo_ble/demo_wifi | mini-sdk | 结论 |
|---|---|---|---|
| 电源域 VDC14=1.4V/SYSVDD=1.26V | app_config + board.c power_init | **一致**,且 init.c 的 app_task_handler 在 initcall 阶段间调 board_init() | ✅ |
| VM 分区 | isd_config VM 32K | 同款布局,vm_init 实测成功(0% full) | ✅ |
| 初始化框架 | init.c: sys_timer→分级 initcall→board_init→app_main | mini driver/system/init.c 同构 | ✅ |
| 任务表 | btctrler 19/btstack 18(/wl_dev 20) | 已补,另加 bt_app/wifi_app 条目 | ✅ |
| 事件分发 | register_sys_event_handler + SYS_BT_EVENT(0x0010) | 同 API | ✅ |
| BT MAC | VM(CFG_BT_MAC_ADDR)缺省时 UID 派生+回写 | 同,但 **VM 首写暂缓**(UID 每次重派生) | ⚠️ 待 VM 联调后恢复 |
| BLE 地址 | lib_make_ble_address 派生 + le_controller_set_mac | 同 | ✅ |
| RF 电源校准 | 库内置默认(产品量产后校准) | 同库 | ✅(默认) |
| btcontroller_config.h | CONFIG_LE_ROLES=(LE_ADV) 等 | 与原厂同文件 | ✅ |
| lmp_set_sniff_disable | ble.c 在 btstack_init 前调 | V0.6.1 补上 | ✅ |
| 低功耗 CONFIG_LOW_POWER_ENABLE | 原厂使能睡眠 | mini 未使能(保守) | ⚠️ 差异可接受 |

## 4. 蓝牙初始化链(已实现)

```
按键1 单击(或串口 'b') → bt_wifi_start()
  └ os_task_create(bt_app)
bt_module_init:
  printf 分段 → lib_make_ble_address(BT MAC 派生 BLE 地址)
  → le_controller_set_mac → register_sys_event_handler(SYS_BT_EVENT)
  → lmp_set_sniff_disable → btstack_init()
      └ 内部创建 btctrler/btstack 任务(查任务表)
btctrler 初始化完成 → 系统事件 BT_STATUS_INIT_OK
  └ bt_ble_init()(le_trans_data.c: GAP 名 "xxx(BLE)"、广播参数、
      GATT 透传服务 ae81 写/ae82 notify、开广播)
手机侧: 扫描 → 连接 → ae81 写数据 → ae82 notify 回环
```

## 5. WiFi 初始化链(已实现)

```
同 bt_wifi_start → os_task_create(wifi_app)
wifi_scan_task:
  wifi_on() → wifi_get_mac 打印 → 循环{ wifi_scan_req()
  → wifi_get_scan_result 打印 AP(SSID/信道/RSSI) → wifi_clear_scan_result }
```
依赖 `-DCONFIG_NET_ENABLE=1` + lwip 头路径;STA 模式,扫描不关联。

## 6. 验证决策树(板测判读)

按键1 触发后按序出现,断在哪条即哪一层问题:

| 最后一条日志 | 卡点层 | 下一步 |
|---|---|---|
| (无 `[bt] task start`) | 任务未运行 | 查 os_task_create/调度 |
| `task start` 后无 `read mac` | VM/syscfg 读 | VM 初始化 |
| `btstack_init` 后无 `controller init ok` | 控制器/RF 初始化 | 查 RF 电源/校准 |
| `controller init ok` 但手机扫不到 | 广播层 | 查 adv 参数/天线 |
| 扫到连接后无透传 | GATT 层 | 查 ae81/ae82 与 MTU |

全程 `[alive] n` 心跳必须持续;心跳停 = 系统级冻结(优先查 IRQ/总线)。

## 7. 已知边界与遗留

- [ ] VM 首写禁用中(`user_cfg.c`),MAC 每次上电重派生;VM 池联调后恢复回写;
- [ ] WiFi 仅扫描;STA 关联/直连(AUDIO/AP 模式)未接;
- [ ] BT+WiFi 并发共存(RF)待 F 阶段实测;
- [ ] 通知端点 bit14 门控(USB 章节遗留)与 BT 无关,勿混淆;
- [ ] 触发门控在 BT/WiFi 验证通过后可改为开机自启。

## 8. 轮次记录

| 版本 | 变更 |
|---|---|
| V0.5.0 | 六个 BT 库 + BLE 透传档案层链接通过;首烧无日志(日志截断误判) |
| V0.5.1 | VM 首写暂缓(user_cfg.c);bt 模块分段打印 |
| V0.5.2 | task_info_table 补 bt_app/btstack/btctrler;bt_wifi_start 移 app_main 尾 |
| V0.5.3 | BT 启动改串口 'b' 触发 + 心跳任务 |
| V0.5.4 | 原厂 key_driver/adkey 按键框架移植(见下) |
| V0.5.5 | key 框架与 BT 触发解耦(修循环依赖);git 首推 |
| V0.6.0 | WiFi 全栈链接(wl_wifi_sta/wpasupplicant/lwip/mbedtls/hsm/http_cli/json/iperf)+ 扫描任务;MINI_WIFI_SUPPORT=1 |
| V0.6.1 | 全量系统级对照(本文档);补 lmp_set_sniff_disable;计划文档建立 |
| V0.6.2 | 外设普查与移植计划(doc/外设移植计划.md);P1 外设宏门控移植(PWM/RTC/SD/GPCNT/加密,默认全关) |
| V0.6.3 | **找到 BT"无日志"真根因: bt_wifi_app.c 漏 include app_config.h,MINI_BT/WIFI_SUPPORT 在该文件未定义,胶水被整体编译成空**(V0.5.x 起即如此,"挂死"为误判)。修复: 补 include;adkey 阈值表修正(按键1 边界 81→588,原表按键1 永不匹配);移植 wifi_conf.c + wifi_calibration_param RF 校准表(板级);触发保留按键2(用户修改) |

### 附: ADC 按键框架(V0.5.4~V0.5.5)

- 原厂 `key_driver.c`(10ms 扫描/消抖/单击长按状态机)+ `adkey.c`(ADC 电阻梯);
- PB01(AD 通道3)电阻梯实测: 空闲 1014 / 按键1 162 / 按键2 0,中点阈值表
  `{0, 81, 588, 1023…}`→`{按键2, 按键1, 无…}`;
- 事件 `key_event_notify` → SYS_KEY_EVENT → `register_sys_event_handler` 路由;
- **初始化必须独立于 BT 启动**(V0.5.4 曾埋循环依赖: 按键触发入口依赖按键框架自身)。
