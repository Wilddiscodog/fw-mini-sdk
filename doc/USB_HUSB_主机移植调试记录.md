# AC7912BB (WL82) USB 主机(HUSB)移植调试记录

> 记录 fw-mini-sdk 中 CherryUSB 主机栈(usbh)在杰理 AC791x HUSB 控制器上的
> 完整调试过程。从首个 `-14` 超时、建链序列还原、速度判定修正，到轮询模式
> 与文件重复清理，共 12+ 轮迭代。
> 文档版本对应固件 **V0.3.8**(主机 HS 建链与枚举已全链路打通,
> 待验证 MSC bulk 512 数据阶段)。

---

## 1. 硬件背景(逆向确认)

### 1.1 两个 USB 控制器

| 编号 | 控制器 | 速度 | SIE 寄存器基址 | 桥接器 | PHY |
|---|---|---|---|---|---|
| USB0 (id=0) | FUSB 1.1 OTG | 全速 12Mbps | 0x11800 (JL_USB, 老式块) | - | FS PHY |
| USB1 (id=1) | **HUSB 2.0 OTG** | **高速 480Mbps** | **0x16000** (MUSB 风格) | **0x16800** (HUSB_SIE_CON) | 0x16A00 (HUSB_PHY0) |

- 两个控制器**共用同一组 D+/D- 引脚**(AC7912BB: 35/36 脚)，同一时刻只能激活一个。
- 闭源驱动 `usb.c` 的 `musb_read_usb()` 内含 `ASSERT(id == 0)`——MUSB SFR 访问路径
  只允许 USB0；USB1 的寄存器为直连 MUSB 风格布局。
- 依据：将闭源 `cpu.a` 中的 `usb.c.o`(LLVM bitcode) 用 `opt -S` 反编译为 IR 后
  逐寄存器比对；`usb_sie_enable(id)` 的 id 分支分别置 `SIEIE`(USB0) 与
  `HUSB_SIE_EN`(USB1)。

### 1.2 HUSB 关键寄存器(主机角色)

| 地址 | 寄存器 | 说明 |
|---|---|---|
| 0x16000 | SIE 基址 | 标准 MUSB 布局(FADDR=0x00, POWER=0x01, FIFO=0x20+4n, 端点窗口 0x100+0x10n) |
| 0x16001 | POWER | bit5 HSENAB, bit3 RESET(脉冲), bit4 HSMODE(状态) |
| 0x16002/4 | TXIS/RXIS | 中断状态，**清除方式见 2.2** |
| 0x16006/8 | TXIE/RXIE | 中断使能 |
| 0x1600A | INTRUSB | USB 中断状态(RESET/CONN/DISCON/SUSPEND...) |
| 0x1600B | INTRUSBE | 中断使能 |
| 0x1600C | FRAME | 帧计数(正常应 1ms ±1) |
| 0x16040 | FIFO0 | EP0 FIFO(**读须 INT_MODE 窗口**) |
| 0x16060 | DEVCTL | bit0 SESSION, bit2 HOST(硬件管理), bit6 FSDEV |
| 0x16800 | HUSB_SIE_CON(桥接器) | bit1 SIE_EN, bit5 SIE_PND, bit6 CLR_SOF_PND, bit10 SOFIE, bit12 INT_MODE, bit23 CPU_AVALID, bit25 CPU_VBUSVALID |
| 0x16A00+ | HUSB_PHY0_CON0/1 | PHY 上电/调优(闭源 `usb_h_sie_init` 管理) |

### 1.3 JL HUSB 与标准 MUSB 的差异(全部经实测/IR 逆向验证)

1. **无 FIFOSZ/FIFOADD 寄存器**：端点 FIFO 硬件固定(EP0=64, EP1=512×2 双缓冲，
   EP2+ 未验证)，跳过容量配置与容量断言。
2. **FIFO 端口只支持字节访问**：32 位宽读每个字仅低字节有效(会把 8 字节 SETUP
   包读成 `80 00 00 00 ...`)。闭源驱动全部逐字节 load/store。
3. **EP0 FIFO 读必须处于 INT_MODE=0 窗口**：桥接器 bit12 拉低后读取才有效。
4. **中断状态寄存器(IS/TXIS/RXIS)为"读清除"，但只在 INT_MODE=0 窗口内生效**；
   写 1 清除无效。闭源 `usb_read_intr` = 清 bit12 → 读三组状态 → 恢复 bit12。
5. **主机角色需要 SOF 引擎使能**(桥接器 bit10 HUSB_SOFIE)并周期清除
   SOF_PND(bit6)，否则不产生 SOF 帧。
6. **OTG 会话维持信号需 CPU 直驱**(桥接器 bit23 AVALID + bit25 VBUSVALID，
   bit22 IDDIG=0)，否则 OTG 状态机释放主机角色。
7. **DEVCTL 位语义**(Linux MUSB 定义): bit0 SESSION, bit1 HOSTREQ(写入后硬件
   消费), **bit2 HOST(硬件置位=主机角色已建立)**, bit6 FSDEV(全速设备检测)，
   bits[4:3] VBUS 电平(11=VALID)。**POWER.HSMODE 位在 JL 上常读 1，不可作为
   速度判定依据**。
8. **主机 EP0 CSR0 为 16 位，高字节 bit3 = DISPING(PING 禁用)**。高速设备的
   EP0 不应答 PING，所有 CSR0 写必须带 0x800。闭源 `usb_ctrl_transfer.c`
   每次 `usb_write_csr0` 都带 `CSR0H_DISPING`。

---

## 2. 调试时间线

每轮固定方法：**版本号 + 寄存器快照监视 + 字节级产物验证**，逐层剥洋葱。

### V0.2.x — 从"完全无响应"到"SETUP 发上总线"

| 轮次 | 现象 | 诊断 | 措施 |
|---|---|---|---|
| 首版 | 枚举超时 `-14`，无任何主机中断 | — | 建立监视体系 |
| V0.2.1 | 依旧 | DEVCTL 读回 0x5d，误判"双核抢中断" | `unrequest_irq` 清双核使能后再只开 CPU0 |
| V0.2.2~.3 | 依旧，且 `[t5]` 缺失引出新线索 | `IS=01`(SUSPEND) 4 万次不清零 → 误判"读清除"；改回写 1 清除仍不清 | 发现 SUSPEND 位在总线空闲时持续重挂起 → **只使能 RESET 中断** |
| 关键转折 | `[usb-irq] PRE(IS=05) POST(IS=00)` | **INT_MODE 窗口读清假设成立** | 实现 `jl_usb_read_intr()`(桥接器 bit12 拉低窗口内读) |
| V0.2.4 | 中断风暴根除，但 SETUP 解析错误 | `submit req=00`(应为 06) → FIFO 32 位宽读每字仅低字节有效 | **FIFO 强制逐字节访问** |
| V0.2.5 | 枚举启动，SET_CONFIG 后崩溃断言 | JL 无 FIFOSZ 寄存器，容量断言误触发 | 跳过容量检查 |
| V0.2.6~.8 | 停滞 | 发现 `#ifdef`(不判值)/陈旧 objs 等构建陷阱，逐个修复 | 构建体系修正 |

### V0.3.x — 主机建链(本轮)

| 轮次 | 现象 | 诊断 | 措施 |
|---|---|---|---|
| V0.3.0 | `submit` 后 2s 超时 `-14`,`ep0=0` | 官方 `_usb_host_mount` 有 5 步建链序列被遗漏 | roothub PORT_RESET 改为官方五步: `POWER(0x60)` → `usb_host_init(闭源完整啁啾+复位+速度协商)` → `usb_sie_enable` → 稳定等待 → 恢复中断 |
| V0.3.1 | 首轮啁啾即成功(无 retry 打印)，但 hub 报全速 | `DEVCTL=0x5f`: **bit6 FSDEV=1**(核心检测到全速设备)，但 hub 报高速(speed=3) → 速度错配: 主机按高速时序向全速设备发令牌 | **速度判定改用 DEVCTL.FSDEV/LSDEV**(弃用 JL 上不可信的 POWER.HSMODE) |
| V0.3.2 | 上报仍全速，但闭源啁啾打印 `high speed` | 速度位在啁啾后数百 ms 发生翻转(FSDEV 漂移)，读取时机不对 | **啁啾成功瞬间锁存速度**(g_port_speed_latch)，hub 使用锁存值 |
| V0.3.3 | 锁存值=2(全速)，设备从高速掉回全速重连 | `DEVCTL=5f`: HOST(bit2)被硬件置位后会丢失，且 OTG 会话信号无 CPU 驱动 → 状态机释放主机角色、总线释放、设备以全速自重连(FSDEV 重新置位) | **桥接器驱动 OTG 会话维持信号**: `|= AVALID(bit23) | VBUSVALID(bit25)`，IDDIG 保持 0；DEVCTL 改非破坏式 `|= 0x05` |
| V0.3.4 | 锁存仍=2 | `DEVCTL.FSDEV(bit6)` 在 JL 上语义不符: **高速设备也置位**，不可作为速度判据 | 锁存改用 `POWER.HSMODE(bit4)`(闭源设备驱动同款判定)，并打印判定现场。实测 `POWER=70`(HSENAB\|HSMODE\|SOFTCONN) → **高速确认** |
| V0.3.5 | 高速确认后 `CSR0=0a00`、`TXIS=0`、仍超时 | SETUP 已发上总线(CSR0L TXRDY 已消费、DISPING 已锁存)，但 **EP0 完成中断在主机角色下从不触发**(同控制器从机角色正常)。闭源 `usb_ctrl_transfer.c` 本就是**轮询 CSR0** 实现，不依赖 EP0 中断 | **传输完成改轮询模式**：任务 10ms 查询挂起集并调用处理 |
| V0.3.6 | 轮询后**完全停止枚举**，hub 事件丢失 | V0.3.6 重构残留：处理体内窗口外直接读(`is=HWREGB(IS)` 等)把参数传入的窗口读有效值**覆盖为 0**(窗口外恒 0) | 删除残留读，使用调用方传入的窗口读结果 |
| V0.3.7 | 枚举恢复 | —(回归修复确认) | — |
| V0.3.8 | 文件被多轮脚本编辑弄出**整段自我复制**(全部函数双份)+孤儿块 | 多轮 python 行编辑叠加 | 删除重复尾部(保留第一份完整正确副本)，**全部修复首次同时生效**: 枚举全链路打通(两支 U 盘: SanDisk 0781:5567 / Patriot 13fe:6000)，MSC 类加载，卡在 EP2 512B bulk(FIFOSZ 断言，已禁用待实测) |

### 关键教训(按代价排序)

1. **`#ifdef` 不判值**：`FORCE_FS` 定义为 0 时 `#ifdef` 仍成立，HSENAB 从未
   使能，连续多轮"高速"配置实际在全速跑。→ 一律 `#if defined(X) && X`。
2. **闭源驱动的行为不能当协议规范**：它"不回写中断状态"是因为走了桥接器
   INT_MODE 窗口的读清除副作用，而非寄存器本身只读清除。
3. **printf 探针可能被工具链裁剪**(原因未查明)：两处 printf 在源码/IR 中存在、
   最终 app.bin 中消失。**改用 volatile 全局计数器 + 可靠打印路径**(定时器任务
   周期打印)，不受影响。
4. ** objs 陈旧副本**：从别处拷来的 objs 树没有 .d 依赖文件，改头文件不触发
   重编。→ 构建体系自带 `-MM` 依赖；怀疑没生效时先 `clean.bat`。

---

## 3. 当前主机建链序列(V0.3.0 起框架, V0.3.8 完整生效)

```
[usb_hc_low_level_init]              ← usb_hc_init 时(无设备也可执行)
├── usb_h_sie_init(1)                ← 闭源: HUSB PHY/桥接器上电序列
├── usb_iomode(1, 0)                 ← DP/DM 切 USB 功能
├── 桥接器 |= AVALID(bit23) | VBUSVALID(bit25)   ← OTG 会话维持
├── DEVCTL |= 0x05                   ← SESSION | HOST(非破坏式)
├── 桥接器 |= SOFIE(bit10)           ← SOF 帧时钟引擎
├── unrequest_irq(IRQ_USB1_SOF/CTRL, 双核) → request_irq(只 CPU0)
└── SOF ISR: 桥接器 |= CLR_SOF_PND(bit6)

[U盘插入 → hub 检测 → PORT_RESET]
[usbh_reset_port JL82 分支]           ← 每次插入执行
├── 重试 ≤3 次(延迟 20/30/40ms):
│   ├── usb_write_power(1, 0x60)     ← HSENAB | SOFTCONN
│   └── usb_host_init(1, delay, 500) ← 闭源完整啁啾+复位+速度协商
├── 啁啾成功 → 立即锁存速度(DEVCTL.FSDEV/LSDEV, 时效窗口内)
├── usb_sie_enable(1) + 稳定等待
├── 桥接器 |= AVALID|VBUSVALID|SOFIE; DEVCTL |= 0x05
└── 恢复 INTRUSBE/TXIE/RXIE 使能
之后: hub 读锁存速度 → 枚举(EP0, DISPING 已带) → MSC
```

---

## 4. 诊断设施说明(当前固件保留)

| 设施 | 位置 | 说明 |
|---|---|---|
| 版本号 | 启动横幅 + usb-irq 探针 | `fw_version.h`，确认烧录版本 |
| `[usbh-mon]` | usb_mode_task 每 2s | irq/ep0/setup/err 计数 + DEVCTL/FRAME/CSR0/TXIE/RXIE/BR 快照 |
| `[h-ep0]` | HC 驱动 | 控制传输提交 / EP0 中断进入打印 |
| `[h-urb]` | HC 驱动 | URB 错误完成打印 |
| 风暴熔断 | ISR 入口计数 ≥10 万 | 自动断开 IRQ54 保系统(遗弃的诊断轮次遗留，可移除) |

**判读速查**：
- `ep0=0` 且 `setup` 增长 → SETUP 已发、设备不应答(查 VBUS/供电/速度匹配)
- `irq` 不随设备插入增长 → 中断链路问题
- `FRAME` 不以 1kHz 走 → SOF/帧时钟问题(检查 SOFIE 与 OTG 会话维持)
- `CSR0` 低字节 `0x10`=ERROR(无应答)、`0x02`=TXRDY 未消费(核心未启动事务)、
  `0x00`=包已上总线等待/完成(轮询模式下由 usbh_host_poll 拾取推进)

---

## 5. 已知边界与遗留

- [ ] **诊断设施清理**：固件功能稳定后移除风暴熔断/PRE-POST 探针/setup 打印(保留版本号)。
- [ ] **MSC bulk 512 数据阶段实测**：`Ep 02 fifo is overflow` 的 FIFOSZ 断言已
      禁用(寄存器不存在读垃圾值)，EP2 真实 FIFO 容量待实测；若 512 失败，
      把 MSC IN/OUT 管道都改到 EP1(已验证 512×2 双向)。
- [ ] **TE 撕裂同步 / 触摸**(LCD)未移植(桩实现)。
- [ ] **全速主机路径**帧时钟异常未深究(FORCE_FS=1 旧选项，产品无此需求)。
- [ ] **诊断设施清理**：`[h-ep0]`/`[h-urb]`/`[usbh-mon]` 打印与风暴熔断在功能
      稳定后移除或降级(当前保留便于观察)。
- [ ] **git 暂缓推送**(本地领先：V0.2.9~V0.3.8 + 本文档，共 8+ 提交)。

## 6. 版本历史

| 版本 | 变更 |
|---|---|
| V0.2.7 | 速度判定改用 DEVCTL.FSDEV/LSDEV(弃 POWER.HSMODE) |
| V0.2.8 | FORCE_FS=0, 切回高速主机(其时 #ifdef 未判值, 实际未生效) |
| V0.2.9 | 修复 #ifdef 不判值错误, HSENAB 真正使能 |
| V0.3.0 | roothub PORT_RESET 还原官方 5 步建链(POWER 0x60/usb_host_init/sie_enable/等待) |
| V0.3.1 | 建链加递增延迟重试 ×3(官方 mount 循环同款) |
| V0.3.2 | 啁啾成功瞬间锁存速度 |
| V0.3.3 | OTG 会话维持(AVALID+VBUSVALID) |
| V0.3.4 | 速度判据改 POWER.HSMODE(啁啾后时点), 高速确认(POWER=70) |
| V0.3.5 | 主机完成处理改 10ms 轮询 |
| V0.3.6 | 重构为 usbh_process_intr + 轮询入口(引入覆盖回归) |
| V0.3.7 | 修复回归(删窗口外残留读) |
| V0.3.8 | 清理重复尾部; **枚举全链路打通**, MSC 卡 EP2 512B 待实测 |
