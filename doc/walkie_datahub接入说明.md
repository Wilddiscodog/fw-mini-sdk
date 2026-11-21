# walkie 对讲接入 AR9331 datahub

> 手表对接同一台 AR9331 随身路由上的 `luci-app-datahub`  
> （patch 见 `tools/ar9331-wmn013n-v23.05.5-patch/`）。

## 功能

手表对讲（WiFi UDP 实时）之外增加与 hub 的异步通道：

- **归档上传**: 松键后截断至最长 15s → **G.711 A-law** 压缩 → 本地队列 → 常驻 worker 上传  
  （`fmt=alaw&rate=16000`，uid=`DEVID-msgid` 幂等；失败最多重试 3 次）
- **漏收拉取**: 进聊天页 / WiFi 就绪时异步 pull；兼容旧 `pcm` 与新 `alaw`
- **状态栏**: 聊天页显示「同步中 / 待同步 / 同步失败」（仅反映本机上传，不含 pull）
- put / pull 分线程；put 热路径只用缓存/兜底 IP，不阻塞在 UDP DISCOVER

## 本仓库接入状态

| 文件 | 状态 |
|---|---|
| `app/walkie/walkie_hub.c` / `.h` | 已接入（队列 / A-law / httpcli_post） |
| `app/walkie/walkie_msg.c` | 松键后 `walkie_hub_put_voice` |
| `app/walkie/walkie_svc.c` | 进聊天页 / WiFi 就绪 `walkie_hub_pull` |
| `app/watch_ui/watch_apps.c` | 状态栏显示 hub 同步态 |
| `app/hub_music.c` | 曲库走 `ch=music`（与对讲 `ch=audio` 分离） |
| `tools/hub_browser/` | PC 端全量拉取 + 播放 GUI |

## 配置 — `app/walkie/walkie_hub.h`

```c
#define WALKIE_HUB_IP       "192.168.5.28"       /* 家庭网 STA 兜底 IP */
#define WALKIE_HUB_KEY      "495f66430e261791"   /* LuCI 数据中枢 key */
#define WALKIE_HUB_PORT     80
#define WALKIE_HUB_TIMEOUT  8000                 /* ms */
#define WALKIE_HUB_MAX_SEC  15
#define WALKIE_HUB_Q_DEPTH  2
#define WALKIE_HUB_PUT_RETRY 3
```

- **DEVID** = `walkie_self_name()`（如 `WL-FA1302`）
- **自动发现（优先）**: WiFi 就绪 / pull / put worker 启动时 UDP 广播 `DISCOVER` → `:3722`  
  期望应答 `DATAHUB <ip> <key>`（AR9331 `hub-announce`）。日志：`hub found by discovery`
- 双目标广播：`255.255.255.255` + 同网段广播；约 4×500ms 轮询（不依赖 `SO_RCVTIMEO`）
- 发现失败才用 `WALKIE_HUB_IP` 兜底；HTTP 失败会 `hub forget` 并在下次强制再发现
- 联系人名字必须填**对方对讲名**（上传 `txt=` 收件人）

## 语音归档格式

| 项 | 值 |
|----|-----|
| 通道 | `ch=audio` |
| 编码 | **G.711 A-law**（`fmt=alaw`），约 PCM 体积 1/2 |
| 采样率 | **16000 Hz**（`rate=16000`） |
| 声道 | 单声道 |
| 存储 | 无头 raw；HUB `get` 默认自动包 **WAV** 头；`&raw=1` 拿裸流 |
| 旧数据 | 可能仍是 `fmt=pcm`（s16le / 16000 / mono） |

### 本机播放（命令行）

```bash
# HUB 已包 WAV 的下载文件
ffplay voice.wav

# 裸 A-law
ffplay -f alaw -ar 16000 -ac 1 voice.bin
ffmpeg -f alaw -ar 16000 -ac 1 -i voice.bin voice.wav

# 裸 PCM s16le
ffplay -f s16le -ar 16000 -ac 1 voice.bin
```

### PC 图形工具

```
tools/hub_browser/HubBrowser.exe
```

可从 HUB **全量同步**元数据与附件，并在列表中播放（双击即可，无需 Python）。  
工具会把 A-law WAV **解码为 PCM16** 再播；音乐按 `fmt`/文件头保存为  
`{id}-{歌名}.mp3|.flac|.ape|…`（不再落成无扩展名的 `.bin`）。  
源码 / 重打包见 `tools/hub_browser/README.md`（`build_exe.bat`）。

## AR9331 hub API

| 项 | 说明 |
|----|------|
| CGI | `http://<ip>/cgi-bin/hub` |
| 发现 | UDP **3722** |
| HTTP | uhttpd **:80** |
| 鉴权 | 除 `stats` / `export` 外需 `key=` |

| cmd | 方法 | 说明 |
|-----|------|------|
| `put` | POST | 上传；`dtype=file` 时 body=裸流 |
| `list` | GET | `since`/`limit`（≤200）增量列表；可加 `ch=` / `unread` / `dev=` |
| `get` | GET | 取单条；file 默认 WAV（alaw/pcm/ulaw）；`raw=1` 裸流；**大附件(>256KB) 302→`/hubfs/...` 静态**（避开 CGI 60s 超时） |
| `ack` | GET | 标记已读 |
| `stats` | GET | 总览（免 key，响应含 key） |
| `export` | GET | `.db` 快照 + 近 N 天附件 tar.gz（免 key，`days`≤90） |

对讲上传示例 query：

```
cmd=put&key=...&dev=WL-FA1302&ch=audio&dtype=file&fmt=alaw&rate=16000&dur=2000&uid=WL-FA1302-1&txt=WL-78281C
```

## 验证

1. AR9331 固件 + U 盘就绪（LuCI / `stats` 可见 key）
2. 手表: `hub put worker up` → 发语音 → `hub put ok` / `httpcli e=0`
3. PC: 打开 `tools/hub_browser`，填 IP，同步后能列表播放
4. 对端关机时发的话，对端开机进聊天页应补气泡
