# fw-mini-sdk（AC7912BB / WL82 精简 SDK）

当前版本: **V1.1.6**（`app/include/fw_version.h`，启动横幅打印）
- USB **组合设备**（CherryUSB，高速口）：CDC 日志串口 + MSC 4MB 虚拟U盘
  （VID=0xFFFF/PID=0x0003，Win10+/Linux 免驱）；拖入 `.UFW` 静默 3s 自动校验烧写复位
- USB **主机 MSC**（CherryUSB，U盘）可用，全速主机（`CONFIG_USBH_JL82_FORCE_FS=1`），插盘跑 1MB 读写基准
- **LCD 240×320 JD9853（EMI 8 位并口）+ CST816 触摸 + LVGL 8.2.0 benchmark** 默认开启
- **蓝牙 + WiFi**（原厂协议栈）：BT 开机自启；WiFi 按键2 触发，AP 配网 + 网页固件升级
- **内置音频**可选（MIC+DAC 回环自检 + AEC 回采，`MINI_AUDIO_SUPPORT` 默认关）

从官方 AC79 AIoT SDK 精简而来的开发框架：外设驱动 + FreeRTOS + CherryUSB，
蓝牙/WiFi/OTA/音频按宏开关裁剪。应用逻辑全部在 `app/` 下自定义。

## 目录结构

```
fw-mini-sdk/
├── app/                      ← 你的应用（改这里）
│   ├── app_main.c              演示：GPIO/UART/SPI/I2C/PWM/ADC/定时器/USB + 9个FreeRTOS任务
│   ├── include/app_config.h    全工程唯一的配置开关文件
│   └── board/wl82/
│       ├── board.c             板级外设注册（uart0调试 / uart1 / spi2 / rtc / 电源）
│       └── Makefile            构建入口（依赖自动跟踪，改.h也会正确重编）
├── third/CherryUSB/          ← 第三方 USB 协议栈（设备栈 + CDC 类 + jl82 移植层）
│   └── port/jl82/              usb_dc_jl82.c(DCD) + usb_glue_jl82.c(上电/中断)
├── driver/                   ← 开源驱动子集（从官方 apps/common 抽出，可按需增删）
│   ├── usb/                    旧杰理从机栈（已停用, 保留备查, 不参与编译）
│   ├── iic/                    I2C 设备框架适配层（iic.c + software_iic.c）
│   ├── system/                 启动 init.c / 版本 / 复位原因
│   ├── debug/                  日志
│   ├── config/                 日志等级配置
│   ├── update/                 升级桩
│   └── movable/                链接器 movable-region 配置（勿动）
├── cpu/wl82/
│   ├── liba/                   闭源库（cpu/event/system/cfg_tool/fs/common_lib/update/lib_usb_syn）
│   ├── setup.c 等              芯片级开源胶水层
│   ├── sdk_ld*.c               链接脚本源（编译时生成 sdk.ld，勿手改）
│   └── tools/                  烧录工具 + 固件输出（jl_isd.fw 在这里）
├── include_lib/              ← 全部驱动/OS 头文件（含 FreeRTOS）
├── lib/                      ← 版本号桩
└── tools/utils/              ← mkdir_win / fixbat 构建小工具
```

## 编译

前置：杰理工具链装在 `C:\JL\pi32`（路径已写死，改动请改 Makefile 顶部的
`TOOL_DIR`/`UTILS_DIR` 与 `make.bat`）。

```
cd app\board\wl82
make.bat                # 一键编译+打包（零环境配置），固件在 cpu\wl82\tools\jl_isd.fw
clean.bat               # 清理
make.bat MINI_CXX_SUPPORT=1   # 启用 C++ 运行时；开机自检打印 [cxx] smoke PASS
make.bat MINI_LVGL_SUPPORT=1  # 启用 LVGL（先获取 lvgl 源码，见下文）
make.bat MINI_LCD_SUPPORT=0   # 关闭 LCD
```

> 直接敲 `make` 也可以（Makefile 内已写死工具路径并 export）；`clean` 建议用 `clean.bat`。
C++ 开关说明：开启后自动加入全局构造调用（`cpp_run_init`）、new/delete、libcxx/libcxxabi（`-fno-exceptions -fno-rtti` 保持），并编译 `app/cxx_smoke.cpp` 冒烟自检；关闭时为纯 C 固件。

## 演示功能（烧录后）

| 功能 | 引脚 | 现象 |
|---|---|---|
| LCD 彩条自检 | EMI 8位并口 + 背光 PA6 | 开机 8s（与 USB 枚举解耦）后 8 色彩条循环，JD9853 240×320，2s/色 |
| USB 组合设备（默认） | USB D+/D- | CDC 日志串口 + MSC 4MB 虚拟U盘；拖入 `.UFW` 静默 3s 自动升级复位 |
| USB 主机(MSC) | USB D+/D-（需 VBUS 5V 供电） | `MINI_USB_MODE_HOST` 时 U盘枚举，跑 1MB 读写基准并打印速率 |
| UART 收发 | PB3=TX, PB4=RX @115200 | 发什么回什么（回显） |
| 硬件定时器 | 内部 TIMER5 | 1Hz 中断心跳 |
| 蓝牙 | - | 开机自启（见 `app/bt_wifi_app.c`） |
| WiFi 配网+网页升级 | 按键2 触发 | 读存储→STA 重试→失败转 AP，网页配网/上传固件（`app/net/wifi_prov.c`） |
| 调试打印 | PB2 @1000000 | 日志输出（CDC 串口打开时镜像走 CDC 日志口） |

> SPI(W25Qxx)/软件I2C扫描/PWM 呼吸灯/ADC 采样/时钟树打印等演示任务在 `app_main.c`
> 中以注释保留（且 PA5/PA6 等脚与 LCD 复用），需要时取消对应 `os_task_create` 的注释。

## 二次开发要点

1. **改外设引脚/波特率** → `app/board/wl82/board.c`（平台数据 + 设备表）。
2. **加减功能开关** → `app/include/app_config.h`（改完记得 `make clean`）。
3. **加减源文件** → `Makefile` 的 `c_SRC_FILES` 清单。
4. **FreeRTOS 用法** → 参考 `app_main.c`：`os_task_create/os_time_dly/os_sem/...`
   API 在 `include_lib/system/os/os_api.h`；**任务函数绝不允许 return**。
5. **GPIO/时钟 API** → `include_lib/driver/cpu/wl82/asm/gpio.h`、`asm/clock.h`
   （`gpio_direction_output / gpio_set_hd / clk_get("sys") / system_clock_set(...)`）。
6. **设备框架 API** → `dev_open/dev_read/dev_write/dev_ioctl/dev_close`
   （UART/SPI/SD 等都走这套，见 `include_lib/driver/device/`）。
7. **C++ 开发** → `make MINI_CXX_SUPPORT=1`；全局/静态对象的构造由
   `cpp_run_init()` 在 `app_main()` 最先执行，`app/cxx_smoke.cpp` 是可用的
   起步样例（全局对象 + 虚函数 + new/delete）。

## USB：CherryUSB 设备栈 + 主机栈（已替换杰理协议栈）

`app_config.h` 的 `MINI_USB_MODE` 选择角色（**同一时刻只能一个**，共用管脚）：
`MINI_USB_MODE_DEVICE`=CDC+MSC 组合设备（默认，含虚拟U盘升级）/ `MINI_USB_MODE_HOST`=MSC 主机。

### 主机栈（usbh）移植要点

- 新增 `port/jl82/usb_hc_jl82.c`（HC 移植：与设备端相同的 INT_MODE 窗口读清、
  字节 FIFO、免 FIFO 配置）+ `port/jl82/usb_glue_jl82_host.c`
  （`usb_h_sie_init(1)` 主机角色 PHY 上电 + IRQ54）。
- OSAL：直接使用 `osal/usb_osal_freertos.c`（JL OS 即 FreeRTOS，system.a 导出
  xTask/xQueue/xSemaphore 原生符号），两处适配：
  - 定时器：JL 未开 FreeRTOS 软定时器(`configUSE_TIMERS=0`)，改用杰理
    `sys_timer_add/del` 实现 `usb_osal_timer_*`；
  - `xPortIsInsideInterrupt()` → 杰理 `cpu_in_irq()`。
- 类注册：CherryUSB 用自定义段 `usbh_class_info` 收集类，已在
  `include_lib/driver/cpu/wl82/system.ld` 增加 `__usbh_class_info_start__/end__`。
- **首版强制全速主机**（`CONFIG_USBH_JL82_FORCE_FS=1`）：pipe 分配为 IN→EP1、
  OUT→EP2，EP1 已验证 512×2 双向 FIFO 但 EP2+ 容量未知，FS(64B) 规避；
  HS 主机留作后续实验（改 0 即可，需实测 EP2 FIFO ≥512）。
- 硬件注意：主机模式必须给 USB 座提供 **VBUS 5V** 供电。

### 从机 CDC（已验证）

- 从机使用 **USB1（HUSB 2.0，高速 480M）**；USB0（FUSB 1.1，全速）不用。
  两控制器共用一组 D+/D-，同一时刻只能起一个。
- 移植层 `third/CherryUSB/port/jl82/`（基于 CherryUSB 自带的 MUSB DCD，HUSB 即 MUSB IP）。
  **与标准 MUSB 的差异（全部经板级实测/闭源库 IR 逆向验证）**：
  1. **中断状态清除协议**：IS(0x1600A)/TXIS(0x16002)/RXIS(0x16004) 既非只读清除
     也非写1清除——必须先把桥接器(HUSB_SIE_CON, 0x16800) 的 **bit12 HUSB_INT_MODE
     拉低**，在该窗口内读取才会清除挂起位，读完恢复 bit12。见 `jl_usb_read_intr()`。
  2. **EP0 FIFO 读也需 INT_MODE 窗口**；bulk FIFO 读写、EP0 FIFO 写、CSR 寄存器
     均直接访问。
  3. **FIFO 端口只支持字节访问**：32 位宽读每个字仅低字节有效（会把 SETUP 包读成
     `80 00 00 00...`），FIFO 读写强制逐字节。
  4. **无 FIFOSZ/FIFOADD 寄存器**：端点 FIFO 硬件固定（EP0=64，EP1=512×2 双缓冲），
     跳过容量配置与容量断言。
  5. **IRQ54 使能按核独立、处理函数表全核共享**：注册前必须 `unrequest_irq` 清掉
     两个核的残留使能，再只开 CPU0，否则双核并发进 ISR。
  6. SIE 基址 0x16000、桥接器 0x16800；上电序列复用闭源库
     `usb_g_sie_init(1)` + `usb_iomode` + `usb_slave_init(1)`。
- 配置头：`app/include/usb_config.h`（经 Makefile 的 `-include usb_config.h`
  注入 CherryUSB 源文件，**文件名必须是 usb_config.h**）。
- 设备应用：`app/cdc_acm_msc_app.c`（组合设备 VID=0xFFFF/PID=0x0003；MSC 虚拟U盘 EP1，
  CDC 日志 EP2 bulk + EP3 INT 通知；Windows10+/Linux 免驱）。CDC 日志输出接原厂
  `debug_user.c` 的 `usb_log_task`；虚拟U盘升级看门（写盘静默 3s 扫根目录）见
  `doc/U盘自动升级移植记录.md`。
- board.c 不注册 otg 设备：共用管脚下 OTG 双口检测会误拆从机会话。
- 诊断设施（当前固件仍保留，稳定后可移除）：`[usb-irq]` 中断探针（含 10 万次风暴
  熔断）与 `[ep0] setup#` 枚举跟踪，均在 `port/jl82/` 内。

## 版本历史

| 版本 | 变更 |
|---|---|
| V0.0.1 | 引入版本标记；确认中断风暴现象 |
| V0.0.2 | 实现桥接器 INT_MODE 窗口读清除——中断风暴根除 |
| V0.0.4 | FIFO 强制字节访问——SETUP 包解析正确，枚举启动 |
| V0.0.5 | 跳过不存在的 FIFOSZ 寄存器断言——SET_CONFIG 后不再崩溃 |
| V0.0.5 | **USB CDC 枚举 + 回显 + 定时上行全链路验证通过** |
| V0.1.0 | USB 主机栈（usbh_core/hub/msc + musb HC 移植 + FreeRTOS OSAL）接入，U盘 MSC 演示（FS 主机） |
| V0.1.0 | `MINI_CXX_SUPPORT` 一键开关：C++ 运行时（全局构造/new-delete/STL）可切换，含冒烟自检 |
| V0.2.0 | LCD 8位 8080 并口（EMI）+ 屏驱动框架移植（ili9341 彩条自检）；LVGL 8.2.0 适配层就绪（开关+获取脚本） |
| V0.3.8 | 主机传输改轮询模式（EP0 完成中断不触发，轮询为可靠路径） |
| V0.5.5 | USB 数据面 DMA 化 + 蓝牙栈移植 + ADC 按键框架 |
| V0.6.x | BT/WiFi 移植系列：事件路由走原厂框架、WiFi 库任务补齐、全功能 wl_wifi.a |
| V0.7.0 | xiaolu-R2 手表屏 JD9853(240×320) + CST816 触摸；V0.7.2 起 LCD 默认开启 |
| V0.8.0 | MSC 虚拟U盘自动升级（双备份）：拖入 update-ota.ufw 静默 3s 自动烧写复位 |
| V0.9.x | CDC(日志)+MSC 组合设备定型（EP1/EP2/EP3 重排）；EMI 刷屏干扰 USB 枚举隔离实验，LCD 暂关 |
| V1.0.0 | WiFi 网页配网 + 网页固件升级（`app/net/wifi_prov.c`） |
| V1.1.0 | 内置音频移植（MIC+DAC 回环 + AEC 回采），`MINI_AUDIO_SUPPORT` 默认关 |
| V1.1.5 | 升级链路修复（流控/CRC 约定）；OTA 打包强制带芯片 KEY；AP 扫描配网页；BT MAC 写回 VM |
| V1.1.6 | **重新启用 LCD**；调试日志瘦身：删除 OTASTAT.TXT 诊断机制、屏显状态色通道、[usbh-mon] 寄存器转储与扇区/加密头 hex 转储；修 Makefile recipe 内 `#` 注释行被 mingw32-make 当命令执行的构建错误 |

USB 移植调试方法论备忘：ISR 内 PRE/POST 寄存器回读探针 + EP0 SETUP 跟踪打印 +
风暴熔断，配合版本号，每轮日志都能把问题边界收窄一层。

## LCD（EMI 8080-8bit 并口）与 LVGL

WL82 显示外设：**EMI**(8080 并口时序)/PAP/LCDC/IMD(硬件图层)。8 位并口屏走 **EMI 接口**：
`dev_open("emi")`(闭源库, 已链接) + 开源屏驱动 `REGISTER_LCD_INTERFACE` 注册。
移植文件在 `driver/lcd/`（框架 + jd9853 当前面板 + ili9341 参考驱动，来自官方
apps/common/ui 与 LVGL 补丁；**只编一个面板驱动**——框架取第一个注册项，多面板会按
链接顺序误选）。

- 开关：`MINI_LCD_SUPPORT`（默认 1）/ `MINI_LVGL_SUPPORT`（默认 1）
- 板级：`board.c` 的 `emi_data`（时序）+ `ui_cfg_data`（RST/RS/BL/TE 引脚）——按硬件调整
- 上电现象：`lcd_test_task` 延迟 8s 后跑 **LVGL `lv_demo_benchmark`**；`MINI_LVGL_SUPPORT=0` 时退回彩条自检
- 触摸 CST816（IIC，INT=PH3/RST=PH4）已入编译；TE 撕裂同步仍为桩（`lcd_te_stub.c`）
- LVGL 源码：`third/lvgl/lvgl`（v8.2.0，**已去掉 `.git`**，可随本仓库全量推送）。若需重拉：
  1. `git clone --branch v8.2.0 --depth 1 https://github.com/lvgl/lvgl.git third\lvgl\lvgl`
  2. 删掉 `third\lvgl\lvgl\.git`（或直接跑 `tools\apply_lvgl_patch.bat`，脚本会删 `.git` 并覆盖补丁）
  3. `make clean && make`

## 与官方 SDK 的关系

- 闭源库、头文件、链接/烧录工具均原样复制自官方 `AC79NN_SDK_V1.1.9_2023-08-01`；
- 官方 SDK 保持在 `D:\fw-AC79_AIoT_SDK` 不动，日后升级可对比替换 `cpu/wl82/liba` + `include_lib`；
- 源码级差异：C++ 运行时/蓝牙/WiFi/音频按宏开关裁剪（`MINI_CXX_SUPPORT`/
  `MINI_AUDIO_SUPPORT` 等，BT/WiFi 默认随官方栈链接）；不注册 OTG 自动检测
  （双控制器共用管脚，OTG 双口检测会误拆从机会话）。
