# fw-mini-sdk（AC7912BB / WL82 精简 SDK）

当前版本: **V1.9.2**（`app/include/fw_version.h`，启动横幅打印）  
SDK 基线: 官方 **AC79NN SDK V1.2.14** · 屏 **296×240** · 开发分支 `lcd_demo`

- **手表 UI**（LVGL）：主页滑页；对讲 / 网络 / 蓝牙 / 游戏 / 语音识别测试 / 步数检测 / 设置
- **IMU**：开机自动探测 **SC7U22** 或 **ICM-42607-C**（与触摸同挂软件 I2C）
- **步数检测**：片内计步（两芯片均支持）；App 内可测摔倒提醒（播 `fall_alert.wav`，退出即停）
- **本地 ASR**：Roobo 离线命令词（需音频 + `ROOBO_ALGORITHM`）
- **对讲机**：WiFi UDP；聊天气泡依赖 Flash `REC` 分区（见 `doc/当前固件说明_V1.9.md`）
- **WiFi**：AP 配网 + 网页 OTA（`update-ota.ufw`）；凭据在 VM（`VM_OPT=1`）
- **电源**：短按息屏；长按软关机；**长按电源键 4s 硬件复位**
- USB **组合设备**（CherryUSB）：CDC 日志 + MSC 虚拟 U 盘升级；主机 MSC 可选
- **蓝牙 + WiFi** 原厂协议栈；音频走 `audio_server`

从官方 AC79 AIoT SDK 精简：外设 + FreeRTOS + CherryUSB，应用在 `app/`。  
烧录注意与 Flash 布局见 **`doc/当前固件说明_V1.9.md`**。

## 目录结构

```
fw-mini-sdk/
├── app/                        ← 应用（主要改这里）
│   ├── app_main.c / include/   入口、配置、版本号
│   ├── watch_ui/               LVGL 表盘与各 App
│   ├── asr/roobo/              本地 ASR
│   ├── walkie/                 对讲
│   ├── board/wl82/             board.c + Makefile / make.bat
│   └── net/                    WiFi 配网与网页 OTA
├── driver/                     开源驱动子集（lcd / imu / iic / usb 移植等）
├── third/CherryUSB/            USB 设备/主机栈 + jl82 移植
├── third/lvgl/lvgl/            LVGL 8.2.0
├── cpu/wl82/
│   ├── liba/                   闭源库
│   ├── tools/                  烧录工具与固件输出（jl_isd.fw / .ufw）
│   │   └── audlogo_mini/       提示音资源（含摔倒提醒 wav）
│   └── sdk_ld*.c               链接脚本源（编译生成 sdk.ld，勿手改）
├── include_lib/                驱动 / OS 头文件
└── doc/                        说明文档
```

## 编译

前置：杰理工具链在 `C:\JL\pi32`（路径写在 Makefile / `make.bat`）。

```
cd app\board\wl82
make.bat                # 一键编译+打包 → cpu\wl82\tools\jl_isd.fw / jl_isd.ufw
clean.bat               # 清理
make.bat MINI_CXX_SUPPORT=1   # 可选：C++ 运行时
```

默认已开 LCD + LVGL + 音频（`MINI_AUDIO_SUPPORT`）。改 `app_config.h` 后建议 `clean` 再编。

## 烧录产物

| 文件 | 用途 |
|---|---|
| `cpu/wl82/tools/jl_isd.fw` | USB 整机烧录 |
| `cpu/wl82/tools/jl_isd.ufw` | 整包 UFW |
| `cpu/wl82/tools/update-ota.ufw` | 双备份 OTA（网页/U盘） |

改 Flash 预留 / RESET 等布局 → 建议整烧 `jl_isd.fw`；只改应用逻辑 → OTA 即可。

## 演示与常用功能

| 功能 | 说明 |
|---|---|
| 表盘 UI | 296×240 LVGL；短按息屏（开机约 5s 内防误触） |
| 步数 / 摔倒测试 | IMU 片内计步；摔倒提醒仅在 App 内轮询低 g |
| 语音识别测试 | Roobo 本地命令词（占 MIC） |
| USB 组合设备 | CDC 日志 + MSC 虚拟盘拖入 `.UFW` 升级 |
| WiFi 配网 + 网页升级 | AP / STA；上传 `update-ota.ufw` |
| 调试串口 | PB2 @1M；CDC 打开时日志可镜像到 CDC |

## 二次开发要点

1. **引脚 / 波特率** → `app/board/wl82/board.c`
2. **功能开关** → `app/include/app_config.h`（改完 `make clean`）
3. **加源文件** → `app/board/wl82/Makefile` 的 `c_SRC_FILES`
4. **中文字库** → 改文案后跑 `app/watch_ui/assets/collect_ui_chars.py` + `gen_watch_font.py`
5. **提示音** → 放入 `cpu/wl82/tools/audlogo_mini/`，路径形如 `mnt/sdfile/res/audlogo_mini/<file>`
6. **FreeRTOS** → `os_task_create` 等见 `include_lib/system/os/os_api.h`；任务函数不可 return

## USB（CherryUSB）

`MINI_USB_MODE`：`DEVICE`（默认 CDC+MSC）或 `HOST`（MSC，需 VBUS 5V）。  
同一组 D+/D-，设备/主机互斥。移植细节与 INT_MODE 窗口等见历史提交与 `third/CherryUSB/port/jl82/`。

## LCD / LVGL

- 面板：JD9853 类 296×240，EMI 8080-8bit；触摸 CST816（PH3/PH4）
- 开关：`MINI_LCD_SUPPORT` / `MINI_LVGL_SUPPORT`（默认开）
- LVGL：`third/lvgl/lvgl`（v8.2.0）；重拉可用 `tools\apply_lvgl_patch.bat`

## 与官方 SDK

闭源库/头文件对齐 **AC79NN_SDK_V1.2.14**。升级时可替换 `cpu/wl82/liba`、`include_lib`，并核对 `isd_config_rule.c` 预留区（尤其 **REC**）。

## 版本摘要

| 版本 | 变更 |
|---|---|
| V1.9.2 | 步数检测 App（SC7U22/ICM 片内计步）；App 内摔倒提醒 + `fall_alert.wav`；README 更新 |
| V1.9.1 | Roobo 本地 ASR 测试 App；CJK 字库扩充 |
| V1.9.0 | 同步官方 V1.2.14；对讲 REC；精简部分 App；复位 4s；开机防误息屏 |
| V1.8.x | 296×240 表盘；对讲/游戏/BGM；软关机 |
| ≤V1.1 | CherryUSB CDC/MSC、WiFi OTA、LCD/LVGL、音频等基线能力 |
