# Hub Browser — datahub 全量拉取与播放

从 AR9331 `luci-app-datahub` 拉取记录并播放对讲 / 附件。

## 依赖

- 与 HUB 同一局域网
- **推荐**：直接双击已打包的 `HubBrowser.exe`（无需安装 Python）
- 或：Windows / Python 3.8+（标准库即可）

## 启动

双击：

```
tools\hub_browser\HubBrowser.exe
```

缓存目录自动建在 exe 同级：`cache\`。

源码运行：

```bat
cd /d D:\fw-mini-sdk\tools\hub_browser
python hub_browser.py
```

重新打包单文件 EXE：

```bat
build_exe.bat
```

## 用法

1. **HUB IP**: 默认 `192.168.5.11`（与手表 `WALKIE_HUB_IP` 一致）
2. **Key**: 可留空，点「读 stats」自动填入
3. **刷新列表**: `list` 拉最新最多 200 条（可按通道过滤）
4. **全量同步**:  
   - `export` 下载库快照 + 近 N 天附件  
   - 解析 SQLite 全部记录  
   - 对缺失的 `dtype=file` 再 `get` 下载（默认拉 WAV，可直接播）
5. 选中行 → **播放** / **打开目录**

本地缓存目录：`tools/hub_browser/cache/`（`meta.db` + `files/`）。

## 音频说明

| fmt | HUB 返回 | 本工具保存 |
|-----|----------|-------------|
| `alaw` / `ulaw` / `pcm` | 对讲裸流或 WAV | **PCM16 `.wav`**（winsound 可播） |
| `mp3` `flac` `ape` `m4a` `ogg`… | 原文件流 | **`{id}-{歌名}.{fmt}`**（按库内 fmt / 文件头纠正，不再落成 `.bin`） |

HUB 上物理路径可能仍是 `*.bin`，但 `records.fmt` 记的是真实扩展名（如 `ape`/`flac`）。  
本工具下载时按 fmt（必要时读文件头）补全扩展名；点播放/再同步会把旧的 `.bin` 改名。
