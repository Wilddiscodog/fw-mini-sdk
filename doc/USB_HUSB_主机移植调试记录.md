# AC7912BB (WL82) USB 主机(HUSB)移植调试记录

> 记录 fw-mini-sdk 中 CherryUSB 主机栈(usbh)在杰理 AC791x HUSB 控制器上的
> 调试过程，包括 HS 建链、DMA 搬运、对照原厂闭源库及 MSC BOT 数据 toggle。
> 当前文档对应 **V0.3.16**：SanDisk 0781:5567 已完成枚举、TUR、INQUIRY、
> READ CAPACITY 与 sector 0 读取，MSC 注册及容量识别成功。

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
9. **主机 bulk/intr 通道数据走 RAM DMA 缓冲, 发包必写字节计数**(fw-AC79_AIoT_SDK
   `cpu.a` 的 `usb.c.o` IR 逆向, V0.3.9~V0.3.10 轮):
   - 通道 DMA 缓冲地址: TX=`0x16820+8*(n-1)`, RX=`0x16824+8*(n-1)`;
     地址**不自动递增**, 每包从同一缓冲进出(软件 memcpy)。
   - 发送字节数: `0x16804+4*n`(ep≥3 有高位 dma size 特殊格式), 置 TXRDY 前
     必写——不写则发 0 长度包, 设备判无效 CBW 后 HALT 双向管道(V0.3.9 的 -8)。
   - 屏障: 发送 memcpy 后 `csync`; 接收 memcpy 前 `ssync`。
   - 本工程 staging 放 SRAM0 的 `.usb_h_dma` 段并按 64 字节对齐；V0.3.13 日志
     证明 TADDR 低 6 位被清除，V0.3.14 起 `stage` 与 `taddr` 相等。
   - RXCSRL 消费语义: 收到包读走后写 `FLUSH(0x10)`(非读改写, 不再 REQPKT);
     出错(STALL/NAKTO/ERROR)写 `CLRDT(0x80)`。
   - 位语义(与 TI 头文件设备模式命名不同): 0x01=RXRDY, 0x40=STALL,
     0x08=NAKTO/DATAERR, 0x04=ERROR, 0x10=FLUSH, 0x20=REQPKT, 0x80=CLRDT。
   - TXCSRH.MODE(bit5)原厂主机从不置位(config 写 CSRH=0)。
   - **EP0 是例外**: `usb_write_ep0` 直接 FIFO0 字节写(无计数无窗口),
     `usb_read_ep0` 在桥接器 **bit11(0x800)** 拉低窗口内读 FIFO0(bit11 平时
     为 0, 与本移植 bit12 INT_MODE 窗口互不相干)。
   - 窗口寄存器(0x16110+0x10*(n-1)): TXMAXP@+0/TXCSR@+2/RXMAXP@+4/RXCSR@+6/
     RXCOUNT@+8/TXTYPE@+A/TXINTERVAL@+B/RXTYPE@+C/RXINTERVAL@+D; funcaddr:
     TX=`0x16080+8n`, RX=`0x16084+8n`——与 CherryUSB 移植全部一致(已比对)。
10. **DMA 地址按 64 字节对齐**：实测 `TADDR` 读回会清除低 6 位。V0.3.13
    `stage=0x01c0ba20` 而 `taddr=0x01c0ba00`; 改为 64-byte 对齐后两者均为
    `0x01c0ba40`。USB Host 缓冲及 DMA staging 放在 SRAM0。
11. **Bulk DATA toggle 跨 URB 连续**：原厂 `usb_h_ep_config` 配置端点时清一次
    toggle；`usb_h_ep_write/read` 每次传输不重复清。CherryUSB 旧代码每个 bulk/intr
    URB 都写 `CLRDT|FLUSH`，导致后续 CBW 重复使用 DATA0，被设备 ACK 但丢弃，
    后续 IN 无数据。现按 channel + 设备地址 + endpoint 记录 TX/RX toggle 生命周期；
    仅首次绑定、换绑、port reset 或传输错误时重置。
12. **RX DMA size metadata**：原厂端点配置会调用 `usb_set_dma_rsize`；CherryUSB
    初版未登记 RX staging capacity。现 Bulk-IN/interrupt-IN 初始化时注册 512 字节。

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

### V0.3.x — 主机建链与 MSC 数据通路

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
| V0.3.9 | MSC 首条命令报 `csw transfer error: -8`, 补 scsi_init 后**依旧**, 且换 U 盘(Patriot 13fe:6000)现象相同: 首条 TUR 的 CBW"发出"后 CSW 被 STALL, 之后所有 CBW 全 STALL(无 CLEAR_FEATURE 恢复, err 计数 22/盘) | 两支不同厂商 U 盘同样失败 → 系统性问题, CBW 在总线上不合法(无效 CBW → 设备 HALT IN+OUT 双管道, 与日志模式吻合)。原厂 `cpu.a` IR 逆向出关键差异: **主机通道发包前必写发送字节数寄存器**(`usb_write_ep_cnt` → `0x16804+4*ep`), 且数据搬运走 RAM DMA 缓冲(`usb_set_dma_taddr/raddr` → `0x16820/24+8*(n-1)`), **从不写 FIFO 端口**; 移植层只写 FIFO+TXRDY, 计数寄存器为 0 → 硬件发出 **0 长度包** → 设备判无效 CBW | `usb_hc_jl82.c` 主机通道(ep≥1)数据通路改原厂同款: 每管道 512B staging 缓冲 + TX memcpy+csync+写计数+TXRDY / RX 硬件落 staging+ssync+memcpy+FLUSH 消费(出错写 CLRDT); 去掉 TXCSRH.MODE(原厂 config 写 CSRH=0); EP0 保持 FIFO 端口(枚举已验证)。版本 V0.3.10 |
| V0.3.10–V0.3.11 | Bulk DMA 化后仍 `-8`；补 `TXCSR=CLRDT|FLUSH` 后首个 CSW 有进展，但连续 URB 未稳定 | 多次只看首个 URB，误把每次清 toggle 当成初始化；原厂库实际上只在 endpoint config 清一次 | 后续版本补全 DMA/RX/TADDR 诊断、SRAM0 放置、BOT reset recovery，并对照原厂 endpoint 生命周期 |
| V0.3.12–V0.14 | `stage` 与 `TADDR` 依次差 0x10、0x10、0x20 | HUSB TADDR 低 6 位被硬件清除；buffer 只有 4/32-byte 对齐 | Host USB buffers 移到 SRAM0；DMA staging 改为 64-byte 对齐；V0.3.14 实测 `stage=taddr=0x01c0ba40` |
| V0.3.15 | TUR 的 13-byte CSW 成功，但 INQUIRY 36-byte data stage 超时 | 原厂 endpoint 配置会登记 `usb_set_dma_rsize`，移植漏掉 RX DMA size metadata | Bulk-IN/interrupt-IN channel 初始化写入 staging capacity 512 |
| V0.3.16 | TUR/CSW、INQUIRY、READ CAPACITY 和 sector 0 全部成功，MSC online | 原厂只在 endpoint 初次配置时清 DATA toggle；旧移植每个 URB 都 CLRDT，后续 CBW 被设备按重复 DATA0 忽略 | 记录 TX/RX endpoint binding 与 toggle；仅首次绑定/换绑、port reset、传输错误/STALL 时复位。实测容量 30,031,872 × 512 bytes，读取 sector 0 成功 |

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
Host bulk/intr channel：首次绑定 endpoint 时清 DATA toggle；同一 endpoint 的后续
URB 保持硬件 toggle 连续递增。换 endpoint、port reset 或传输错误后重新同步。
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
- `[h-bulk] OUT` 的 `count` 是传给原厂 `usb_write_ep_cnt(USB1, channel, len)` 的长度，
  不是对只写寄存器的读回值；`stage` 与 `taddr` 应相等且 64-byte 对齐。
- `[h-bulk] TXIS` 行: CSRL bit0(TXRDY)仍=1 → 包未被硬件发出(无握手/未启动);
  bit2=ERROR、bit5=STALLED
- `[h-bulk] RXIS` 行: CSRL bit6=STALL(设备 HALT)、bit3=NAKTO/DATAERR、bit2=ERROR、
  bit0=RXRDY；`RXCSRL=0x03` 表示 RXRDY + FIFO FULL，本轮是正常收包。
- 连续 CBW/CSW 的 TX/RX toggle 必须由硬件跨 URB 保持；正常新 URB 不应再次 CLRDT。

---

## 5. 已知边界与遗留

- [x] **MSC BOT 数据阶段**：V0.3.16 实测 TUR/CSW、INQUIRY、READ CAPACITY、
  sector 0 read 全通过；设备注册 `/dev/sda`，报告 30,031,872 blocks × 512 bytes。
- [x] **MSC STALL recovery**：Bulk-Only Reset 后依次 CLEAR_FEATURE(BULK-IN/OUT HALT)；
  传输异常会使 toggle binding 失效，下一次提交重新同步 DATA0。
- [ ] **TE 撕裂同步 / 触摸**(LCD)未移植(桩实现)。
- [ ] **全速主机路径**帧时钟异常未深究(FORCE_FS=1 旧选项，产品无此需求)。
- [ ] **诊断设施清理**：MSC 已验证通过；稳定后可移除 `[h-ep0]`/`[h-urb]`/
  `[usbh-mon]`/`[h-bulk]`、风暴熔断及 PRE-POST/setup 探针(保留版本号)。
- [ ] **git 暂缓推送**(本地领先至 V0.3.16 + 本文档)。

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
| V0.3.9 | 修复 MSC 首命令 `-8`(STALL): app 补 `usbh_msc_scsi_init`; CBW 长度/相位修正 |
| V0.3.10 | Bulk/intr 改原厂 RAM DMA staging；使用 endpoint count、csync/ssync、RX FLUSH/CLRDT；去 TXCSRH.MODE |
| V0.3.11 | 暂加每 URB `CLRDT|FLUSH`；该处理导致后续 URB toggle 被重置，后来确认不是正确的常规提交行为 |
| V0.3.12 | USB_NOCACHE 缓冲放入 SRAM0；MSC 增加 BOT reset recovery；用完整 CBW/寄存器诊断追查仍存问题 |
| V0.3.13 | 对齐 DMA stage 到 32 bytes；日志证明 HUSB TADDR 低位仍截断 |
| V0.3.14 | DMA stage 改 64-byte 对齐；实测 stage 与 TADDR 相等 |
| V0.3.15 | 对照原厂补 RX DMA size metadata (`usb_set_dma_rsize`) |
| V0.3.16 | 对照原厂修复 DATA toggle 生命周期；MSC 全流程及 sector 0 读取实测通过 |
| V0.4.0 | 组合设备端点探针: 2×CDC + MSC(1MB RAM 盘)，EP_NUM 5→7，EP1/3/4/5/6 布局；实测 CDC 回显全 0、MSC CBW 解码 0x00——**设备模式 EP≥1 数据损坏** |
| V0.4.1 | **设备模式数据面切 DMA 缓冲(与主机侧同构)**: `usb_g_ep_read/write` IR 证实 EP≥1 = memcpy 进出 taddr/raddr(0x16820/24+8(n-1)) + ep_cnt(0x16804+4n)；FIFO 端口仅 FIFO0(EP0)真实存在，FIFO1+ 读恒 0。设备侧 staging `.usb_h_dma` 64B 对齐；ep_open 绑定通道地址；RX 消费写 FLUSH |
| V0.4.2 | 组合设备两通知端点(ep:83/84)开串口即 clear-halt 循环 → **原厂 `usb_g_ep_config` 对 INT 类型端点必写 TXCSR bit14(CSRH bit6)**(EP3/4 特殊通道模式门控)，CherryUSB 从不设；ep_open IN 分支补 bit14 + 小缓冲显式清 0x16850/54 模式位。另: RAM 盘挂载前手工格式化 FAT32(`msc_ram_mkfs32`, 卷标 WL82RAMDISK) |
| V0.4.3 | **MSC 迁 EP3 双向大缓冲**(0x83 IN/0x03 OUT, 各 4096B, bit14 + 0x16850 bit0/bit1 + 打包计数)；EP 重分配: CDC1 通知→EP2(64B 组 INT 探针)、CDC2 通知留 EP4(8B 打包计数)、EP5 空闲；驱动改造: per-EP bigsz、EP3 独立 4160B staging(+64 越界余量)、TXIS 按 launch_len 推进(大缓冲一次交付整段)、RXCOUNT 钳制；`MSC_MAX_BUFSIZE` 512→4096(SCSI 一次 4KB) |
| V0.5.0 | **蓝牙外设移植(原厂栈)**: btctrler/btstack/wl_rf_common/crypto_toolbox/lib_ccm_aes/lib_sig_mesh 六库入树，apps/common/ble 档案层(透传服务)+bt_profile_config+双 log_config；`bt_wifi_app.c` 胶水(btstack_init → BT_STATUS_INIT_OK → bt_ble_init，事件走 register_sys_event_handler)；任务表加 btctrler/btstack。RAM0 198KB/511KB。**WiFi 库已入树未链接**(MINI_WIFI_SUPPORT=0，待 RF 校准/lwip 接线) |

## 7. HUSB FIFO 架构与原厂 MRX 模式(数据面专题)

### 7.1 FIFO 架构结论: 混合式——EP0 真硬件 FIFO, EP1~6 为"内建 DMA + SRAM"

| 证据 | 指向 |
|---|---|
| FIFO0 端口(0x16020)读写正常(枚举全链路) | EP0 有真硬件 FIFO(64B) |
| FIFO1~6 端口(0x16024+)读出恒 0 | EP1+ 无 FIFO 端口 |
| 数据通路 = taddr/raddr 指向 RAM + 计数寄存器, 硬件自动搬运 | "FIFO"由系统内存替代 |
| DMA 只可达内部 SRAM0(0x1C00000), 够不到 SDRAM(V0.3.13/14) | 缓冲必须落 RAM0 |
| csync/ssync 屏障、大缓冲打包计数 | 总线同步式 DMA, 非包 FIFO |

### 7.2 FIFO 容量计算

| 通道 | FIFO 形态 | 单方向最大 | 备注 |
|---|---|---|---|
| EP0 | 真硬件 FIFO | **64 B** | 固定 |
| EP3/4 | RAM+DMA 大缓冲模式 | **8188 B/方向** = 4×2047 | 仅 INT 类, >1024 才使能; 8B 对齐 |
| EP1/2/5/6 | RAM+DMA 普通模式 | 登记多大算多大(受 RAM0 预算), 每包仍限 MPS | 64B 对齐 |

RAM0 预算: 总量 0x7FE00 ≈ 511.5KB, 当前固件已用 ~56KB → **可分配 ≈ 455KB**。
吞吐与"FIFO 深度"无关(SIE↔RAM DMA 直通), 瓶颈在软件节奏(轮询泵/中断频率)。

### 7.3 原厂 MRX 模式(Multi-RX/TX 多包聚合)

`CONFIG_USB_SUPPORT_MRX_TX` 仅在 `usb_bulk_transfer.c` 被引用, SDK 默认不定义——
板级产品配置项, 用于 U 盘/usbnet 等大流量管道。

**本质**: 把端点从"一包一中断一拷贝"升级为"大缓冲流水线"。关键前提是
**计数寄存器语义 = 总字节数而非包数**: 写入 len 后硬件自动按 MPS 切包并处理
ACK/NAK 流控, 全部完成后才报一次完成(铁证: `usb_bulk_tx_isr` MRX 下一次
发射 8KB 并原样记为已接受, 量产代码不可能丢数据)。

**两种模式对比(8KB 传输、512B 包):**

```
逐包(MRX 关, rxmap=txmap=512):
  TX: [memcpy 512B]→[计数 512]→[TXRDY]→[等TXIS] ×16 轮 = 16 次中断
  RX: [RXRDY]→[拷 512B]→[清 RXRDY] ×16 轮             = 16 次中断
MRX(rxmap=1KB, txmap=8KB):
  TX: [memcpy 8KB]→[计数 8192]→[TXRDY] → 硬件自主流 16 包 → 1 次中断
  RX: 硬件自动把连续包累计进缓冲(RXCOUNT 逐包累加) → 软件每 1KB 排水一次
```

**分层实现(易混淆点):**

| | EP3/EP4(大缓冲模式) | EP1/2/5/6 |
|---|---|---|
| 使能 | TXCSR bit14 + 0x16850/54 模式位 | 无需模式位 |
| 计数格式 | 打包 `(dma_size<<16)\|len\|0x80000000` | 纯 32 位字节数 |
| 硬件缓冲分块 | 有: 4 块×2047B 页式管理, 硬件自动翻页 | 无, 连续 RAM 整块 |
| MRX 软件切片 | 1KB(RX)/8KB(TX) | 同左(纯软件放大) |

即: **硬件分块是 EP3/4 独有**(这也是 8B 对齐 + bit14 门控的原因);
bulk 在 EP1/2 上的 MRX 只是"大缓冲 + 普通计数"的软件版。

**`usb_get_dma_size` 的角色**: 读软件记账成员(config 时 `usb_set_dma_tsize/rsize`
登记), 不是硬件寄存器; EP3/4 把它塞进计数高位, 因为硬件翻页需要知道缓冲边界。

**实战细节(原厂代码痕迹):**
1. audio.c DMA 缓冲 `+64` 并注释"防止硬件越界"——RX 硬件可能按突发粒度超前写,
   逻辑数据外要垫 64B;
2. RXCOUNT 只在 RXRDY 时有效, 排水前必查 RXRDY;
3. 双缓冲(`usb_set_dma_dual_raddr`)只写软件记账、不碰硬件——是软件交替提交;
4. MRX 收益前提是数据流连续(连续读/大吞吐), 小命令管道无意义;
5. 原厂只在 U 盘/usbnet 开 MRX, HID/CDC 小管道不开。

**对本工程 MSC 的启示**: 当前 MSC(EP5, 逐包模式)。冲吞吐的正确顺序:
① BOT 泵 10ms 轮询 → OUT 完成事件驱动(最大头);
② TX 大块一次发射(减少中断);
③ 前两步做完仍不够才考虑动 EP3/4 大缓冲(需搬通道, 目前无必要)。
