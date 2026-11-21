# fw-mini-sdk（AC7912BB / WL82 精简 SDK）

当前版本: **V1.9.2**（`app/include/fw_version.h`，启动横幅打印）  
SDK 基线: 官方 **AC79NN SDK V1.2.14** · 屏 **296×240** · 开发分支 `lcd_demo`

- **手表 UI**（LVGL）：主页滑页；对讲 / 网络 / 蓝牙 / 游戏 / 语音识别测试 / 步数检测 / 设置 / 音乐
- **IMU**：开机自动探测 **SC7U22** 或 **ICM-42607-C**（与触摸同挂软件 I2C）
- **步数检测**：片内计步；App 内可测摔倒提醒（播 `fall_alert.wav`）
- **本地 ASR**：Roobo 离线命令词（需音频 + `ROOBO_ALGORITHM`）
- **对讲机**：WiFi UDP 实时；松键后 **A-law** 归档到 AR9331 datahub（`ch=audio`）
- **音乐**：HUB `ch=music` 曲库列表 / 点播（与对讲通道分离）
- **WiFi**：AP 配网 + 网页 OTA（`update-ota.ufw`）；网页可看 UART 镜像日志（`/log`）
- **电源**：短按息屏；长按软关机；**长按电源键 4s 硬件复位**
- USB **组合设备**（CherryUSB）：CDC 日志 + MSC 虚拟 U 盘升级

从官方 AC79 AIoT SDK 精简：外设 + FreeRTOS + CherryUSB，应用在 `app/`。  
烧录注意见 **`doc/当前固件说明_V1.9.md`**；HUB 对接见 **`doc/walkie_datahub接入说明.md`**。

## 目录结构

```
fw-mini-sdk/
├── app/                        ← 应用（主要改这里）
│   ├── app_main.c / include/   入口、配置、版本号
│   ├── watch_ui/               LVGL 表盘与各 App
│   ├── walkie/                 对讲 + hub 归档客户端
│   ├── hub_music.c             HUB 曲库
│   ├── net/                    WiFi 配网 / 网页 OTA / web_log
│   └── board/wl82/             board.c + Makefile / make.bat
├── tools/
│   ├── hub_browser/            PC：HUB 全量拉取 + 播放（HubBrowser.exe）
│   └── ar9331-*-patch/         AR9331 luci-app-datahub 补丁
├── driver/                     开源驱动子集
├── third/CherryUSB/ · third/lvgl/
├── cpu/wl82/tools/             烧录工具与固件输出
└── doc/                        说明文档
```

## 编译

前置：杰理工具链在 `C:\JL\pi32`。

```
cd app\board\wl82
make.bat                # → cpu\wl82\tools\jl_isd.fw / jl_isd.ufw / update-ota.ufw
clean.bat
```

## 烧录产物

| 文件 | 用途 |
|---|---|
| `cpu/wl82/tools/jl_isd.fw` | USB 整机烧录 |
| `cpu/wl82/tools/jl_isd.ufw` | 整包 UFW |
| `cpu/wl82/tools/update-ota.ufw` | 双备份 OTA（网页/U盘） |

改 Flash 预留 / RESET → 建议整烧 `jl_isd.fw`；只改应用 → OTA 即可。

## datahub / 对讲归档

- 手表上传：`fmt=alaw` · `rate=16000` · `ch=audio`（约 PCM 一半体积）
- 自动发现：UDP `DISCOVER` → `:3722`；失败才用兜底 `WALKIE_HUB_IP`（现为 `192.168.5.28`）
- PC 工具：双击 `tools/hub_browser/HubBrowser.exe`  
  - 全量同步元数据与附件  
  - A-law 自动转 PCM 再播  
  - 音乐按 `fmt` 保存为 `{id}-{歌名}.ape|.flac|.mp3…`（不再落成无意义的 `.bin`）

详情：`doc/walkie_datahub接入说明.md` · `tools/hub_browser/README.md`

## 演示与常用功能

| 功能 | 说明 |
|---|---|
| 表盘 UI | 296×240 LVGL；短按息屏（开机约 5s 内防误触） |
| 对讲 | UDP 实时 + hub 异步归档 / 漏收拉取 |
| 音乐 | HUB 曲库列表点播 |
| 网页 | 配网 / OTA；`/log` 看串口镜像；`/hub` 诊断 |
| 步数 / 摔倒 | IMU 片内计步；摔倒提醒仅在 App 内 |
| USB | CDC 日志 + MSC 拖入 `.UFW` |

## 二次开发要点

1. **引脚 / 波特率** → `app/board/wl82/board.c`
2. **功能开关** → `app/include/app_config.h`（改完 `make clean`）
3. **加源文件** → `app/board/wl82/Makefile` 的 `c_SRC_FILES`
4. **中文字库** → `app/watch_ui/assets/collect_ui_chars.py` + `gen_watch_font.py`
5. **提示音** → `cpu/wl82/tools/audlogo_mini/`
6. **Hub Browser 重打包** → `tools/hub_browser/build_exe.bat`

## 版本摘要

| 版本 | 变更 |
|---|---|
| V1.9.2+ | hub 归档稳定性（httpcli_put / 非阻塞发现）；Hub Browser EXE；音乐扩展名；网页 `/log` |
| V1.9.2 | 步数检测 App；摔倒提醒；README |
| V1.9.1 | Roobo 本地 ASR；CJK 字库 |
| V1.9.0 | 同步官方 V1.2.14；对讲 REC；复位 4s |
| V1.8.x | 296×240 表盘；对讲/游戏/BGM；软关机 |
