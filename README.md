# fw-mini-sdk（AC7912BB / WL82 精简 SDK）

当前版本: **V0.2.0**（`app/include/fw_version.h`，启动横幅与 usb-irq 探针均打印）
- USB **从机 CDC**（CherryUSB，高速口）枚举与收发验证通过（Windows10+ 免驱）
- USB **主机 MSC**（CherryUSB，U盘）已移植，首版以全速主机运行（`CONFIG_USBH_JL82_FORCE_FS=1`）
- **LCD 8 位 8080 并口（EMI）+ LVGL 8.2.0** 移植完成（ ili9341 参考驱动，彩条自检/LVGL 可选）

从官方 AC79 AIoT SDK 精简而来的最小开发框架：**只保留外设驱动 + FreeRTOS + USB 从机栈**，
不含蓝牙/WiFi/媒体/服务器等大模块。应用逻辑全部在 `app/` 下自定义。

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
| GPIO 输出 | PA6 | LED 每 500ms 翻转 |
| 时钟控制 | - | 调试口每 2s 打印 sys/sdram/hsb/lsb 时钟树 |
| UART 收发 | PB3=TX, PB4=RX @115200 | 发什么回什么（回显） |
| SPI 主机 | PA1=CS PA2=DO PA3=CLK PA4=DI | 上电 5s 读 W25Qxx JEDEC ID 并打印 |
| 软件 I2C | PH0=SCL, PH1=SDA ~100kHz | 每 10s 扫描总线并打印器件地址 |
| PWM 输出 | PA5 @1kHz (TIMER2+输出通道2) | 呼吸灯（占空比 0↔100，约 5s 周期） |
| ADC 采样 | PB1 | 每 2s 打印原始值与毫伏电压 |
| 硬件定时器 | 内部 TIMER5 | 1Hz 中断，打印 1s tick |
| USB 从机(CDC) | USB D+/D- | 默认关闭；`MINI_USB_MODE_DEVICE` 时高速口枚举为串口 |
| USB 主机(MSC) | USB D+/D-（需 VBUS 5V 供电） | 默认；U盘插入枚举，打印容量并读 0 号扇区 |
| 调试打印 | PB2 @1000000 | 日志输出 |

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
`MINI_USB_MODE_DEVICE`=CDC 从机 / `MINI_USB_MODE_HOST`=MSC 主机（默认）。

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
- CDC 应用：`app/cdc_acm_app.c`（VID=0xFFFF/PID=0x8888，端点 EP1 双向 bulk 512B +
  EP2 IN 通知；Windows10+/Linux 免驱，显示为串口）。
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

USB 移植调试方法论备忘：ISR 内 PRE/POST 寄存器回读探针 + EP0 SETUP 跟踪打印 +
风暴熔断，配合版本号，每轮日志都能把问题边界收窄一层。

## LCD（EMI 8080-8bit 并口）与 LVGL

WL82 显示外设：**EMI**(8080 并口时序)/PAP/LCDC/IMD(硬件图层)。官方 8 位并口屏
（ili9341 等）走 **EMI 接口**：`dev_open("emi")`(闭源库, 已链接) + 开源屏驱动
`REGISTER_LCD_INTERFACE` 注册。移植文件在 `driver/lcd/`（框架+ili9341 参考驱动，
来自官方 apps/common/ui 与 LVGL 补丁）。

- 开关：`MINI_LCD_SUPPORT`（Makefile，默认 1）/ `MINI_LVGL_SUPPORT`（默认 0）
- 板级：`board.c` 的 `emi_data`（时序）+ `ui_cfg_data`（RS/BL/TE 引脚）——按硬件调整
- 上电现象：`lcd_test_task` 跑 8 色彩条循环（240x320 ili9341）
- LVGL 启用三步：
  1. `git clone --branch v8.2.0 --depth 1 https://github.com/lvgl/lvgl.git third\lvgl\lvgl`
  2. `toolspply_lvgl_patch.bat`（覆盖杰理适配文件 + 放置 lv_conf.h）
  3. `make clean && make MINI_LVGL_SUPPORT=1`（LCD 任务自动切到 LVGL 主循环）
- TE 撕裂同步与触摸（ft6236/gt911）暂未移植（桩实现），需要时从官方
  `apps/common/ui/lcd_driver/te_driver/`、`touch_panel/` 补齐。

## 与官方 SDK 的关系

- 闭源库、头文件、链接/烧录工具均原样复制自官方 `AC79NN_SDK_V1.1.9_2023-08-01`；
- 官方 SDK 保持在 `D:\fw-AC79_AIoT_SDK` 不动，日后升级可对比替换 `cpu/wl82/liba` + `include_lib`；
- 源码级差异：去掉了 C++ 运行时、蓝牙/WiFi/媒体全部模块、OTG 自动检测注册。
