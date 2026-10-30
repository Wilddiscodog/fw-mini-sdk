# CherryUSB 双 CDC 问题记录（暂切回原厂栈）

> 日期: 2026-10-30  
> 状态: **暂时关闭 CherryUSB 设备栈**，改用 `driver/usb` 原厂从机协议栈（单 CDC）。  
> 开关: `Makefile` / `app_config.h` 中 `MINI_USB_STACK_CHERRYUSB=0`；改回 `1` 可恢复 CherryUSB 编译路径。  
> 相关源码保留: `third/CherryUSB/**`、`app/cdc_dual_app.c`、`app/include/usb_config.h`（不参与当前编译）。

---

## 1. 目标与现象

目标设备形态（CherryUSB）:

| 接口 | 用途 | 规划端点 (HUSB) |
|---|---|---|
| CDC1 | 数据口（曾 echo，后改为静默） | EP1 OUT/IN + EP5 INT notify |
| CDC2 | 用户专用日志 `user_cdc_puts` / `user_cdc_printf` | EP2 OUT/IN + EP6 INT notify |

系统 `printf` 仍只走 UART0(PB2)，与 CDC2 隔离。

**阻塞现象（双 CDC）:**

1. 主机打开 CDC1 / CDC2（或拉 DTR）后，Windows 对 bulk IN（及关联管道）反复 **clear-halt**。
2. 设备侧 USB 路径异常忙碌，最终触发 **看门狗复位**（WDT）。
3. 即便去掉 echo / 定时 alive、且不主动发 `SERIAL_STATE` 通知，开串口仍易复现。
4. CDC2 用户日志依赖 DTR；未打开时 `user_cdc_*` 正确丢弃，但一旦打开即卷入上述 halt 风暴。

此前单 CDC + MSC、或主机 MSC 场景已有大量 HUSB 差异记录，见 `doc/USB_HUSB_主机移植调试记录.md`（V0.4.x）。本文件只记**双 CDC 从机**未闭环项。

---

## 2. 已确认的硬件 / 原厂差异（与双 CDC 相关）

| 项 | 结论 |
|---|---|
| 控制器 | AC7912BB USB0=FUSB@0x11800，USB1=HUSB@0x16000；**共用一组 D+/D-**，勿双开 |
| 端点数量 | HUSB 支持 EP0–EP6（共 7 个）；原厂从机宏 `USB_MAX_HW_EPNUM=5` 主要覆盖 EP0–EP4 常用路径 |
| EP3/EP4 | 特殊大缓冲 / 通道模式；INT 类型原厂 `usb_g_ep_config` **必写 TXCSR bit14 (CSRH bit6)** |
| EP1/2/5/6 | 普通 DMA 通道（地址需 64B 对齐）；INT 在 EP5/6 上 CherryUSB 行为仍不稳定 |
| 通知端点 | 原厂 CDC 默认 `CDC_INTR_EP_ENABLE=0`（可无 INT EP）；CherryUSB CDC-ACM 描述符通常带 notify EP |

V0.4.2 已对 CherryUSB `ep_open` 补过 bit14，可缓解 **EP3/4 作 INT** 的 clear-halt；**EP5/6 作双 CDC notify 时问题未彻底消除**。

---

## 3. 端点演进与尝试过的缓解

1. **早期**: 双 CDC notify 落在 EP3/EP4 → 开串口 clear-halt；补 bit14 后有改善，但仍与 MSC 大缓冲争用纠缠。
2. **避开 EP3 MSC 路径**: CDC1→EP1+EP5，CDC2→EP2+EP6（`app/cdc_dual_app.c` 当前保留形态）。
3. **软件侧减负**:
   - 关闭 bulk OUT echo / 周期 uplink，避免主动 IN 加剧 halt；
   - DTR 只记掩码，**不**发 10B `SERIAL_STATE`（notify MPS=8 会截断）；
   - CDC2 仅 `user_cdc_*` 写，忙则丢弃（避免堵 LVGL）。
4. 结果: 枚举偶可成功，**主机一打开 CDC 仍 clear-halt → WDT**；双口同时打开更易炸。

未完成验证（切栈前中断）:

- 将双 CDC notify 改回 **EP3/EP4 + bit14**，数据留 EP1/EP2（更贴原厂 INT 约束）；
- 或做 **无 notify EP** 的自定义 CDC 描述符（对齐原厂 `CDC_INTR_EP_ENABLE=0`）；
- 核对 HS 下 bulk IN ZLP / `tx_busy` 与 clear-halt 的时序竞态；
- EP6 TX/RX ISR、DMA 计数与原厂 `usb_g_intr_write` 逐项对照。

---

## 4. 初步根因判断（待回切后验证）

优先级从高到低:

1. **JL HUSB 中断端点通道与 CherryUSB musb/jl82 port 不完全匹配**  
   EP3/4 有明确 bit14 门控；EP5/6 作 INT 时 host clear-halt 仍现，疑为 CSR/DMA/ISR 清 pending 路径与原厂不一致。
2. **双 IAD CDC 复合设备放大问题**  
   两套 comm+data + 两 notify，端点与接口状态机交织；单 CDC 在原厂栈上 historically 可用。
3. **IN 完成 / stall 恢复**  
   clear-halt 循环暗示主机认为端点 halt；需确认 stall 置位来源（协议 stall vs 硬件超时 vs toggle）。

---

## 5. 当前规避方案

- `MINI_USB_STACK_CHERRYUSB=0`
- 编译并启动 **原厂** `driver/usb`：`TCFG_PC_ENABLE` + `USB_DEVICE_CLASS_CONFIG=(CDC_CLASS)` + `usb_start(MINI_USB_JL_ID)`
- **不注册 OTG 设备**（`board.c` 既有注释）：避免 OTG 双口检测拆掉手动 `usb_start` 会话
- 原厂已扩展为 **双 CDC**（`CDC_DUAL_ENABLE=1`）：无 INT 通知；CDC1=EP1、CDC2=EP2；`user_cdc_*` → CDC2
- 系统日志继续只走 UART0
- **USB 在线 EQ**：已移除；CDC1 为空闲数据口（OUT 丢弃），CDC2 仍为 userlog

---

## 6. 回切 CherryUSB 前建议清单

1. 先用原厂单 CDC 确认板级 D+/D-、供电、HS 枚举基线。
2. CherryUSB 侧优先尝试 **无 INT notify** 的双 CDC，或 notify 仅用 EP3/EP4+bit14。
3. 对照原厂 `cdc.c` / `usb_g_ep_config` 补齐 EP5/6 INT 配置与完成中断清理。
4. 用 USB 分析仪或 WinUSB 事件抓 clear-halt 对应的 pipe（哪一个 EP）。
5. 通过后再恢复 `MINI_USB_STACK_CHERRYUSB=1` 与 `cdc_dual_app.c`。

---

## 7. 关键文件

| 路径 | 说明 |
|---|---|
| `app/cdc_dual_app.c` | CherryUSB 双 CDC + `user_cdc_*`（保留） |
| `third/CherryUSB/port/jl82/*` | HUSB device/host port |
| `app/include/usb_config.h` | CherryUSB 配置头 |
| `driver/usb/**` | 原厂从机栈（当前启用） |
| `doc/USB_HUSB_主机移植调试记录.md` | 主机 / EP 硬件差异总册 |
