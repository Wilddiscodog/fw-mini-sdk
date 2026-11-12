# walkie 对讲接入 datahub 服务器 (V1.9.3)

> 来源: `fw-mini-sdk-datahub-patch.zip`(补丁包存仓库外备份 `D:\fw-mini-sdk-backup\`)。
> 本提交只入源码与文档; **构建验证未做**——见文末"构建环境待办"。

## 功能

手表对讲(WiFi UDP 实时)之外增加与 AR9331 hub 服务器(datahub)的异步通道:

- **归档上传**: 松开 PTT 后, 本次语音整包异步上传 hub(uid=`DEVID-msgid`, 重传不重复)
- **漏收拉取**: 进聊天页 / WiFi 刚连上时, 从 hub 拉取本机未取走的语音 → 落盘成气泡 → ack;
  单轮最多 8 条, 单条上限 2MB(约 64s), 下载失败不 ack 下轮重拉
- 设计为"尽力而为": 没 IP / 线程忙 / HTTP 失败只跳过, 不影响 UDP 实时对讲

## 文件

| 文件 | 状态 | 内容 |
|---|---|---|
| app/walkie/walkie_hub.c | 新增 | datahub 客户端(上传/拉取/ack) |
| app/walkie/walkie_hub.h | 新增 | 配置宏与接口声明 |
| app/walkie/walkie_msg.c | +1 行 | 松键发送后异步归档 |
| app/walkie/walkie_svc.c | +2 行 | 进聊天页/WiFi 就绪触发拉取 |
| app/board/wl82/Makefile | +1 行 | 编入 walkie_hub.c |

## 每台手表必改 — app/walkie/walkie_hub.h 顶部三个宏

```c
#define WALKIE_HUB_IP     "192.168.1.214"      /* hub 的 IP(建议主路由绑静态租约) */
#define WALKIE_HUB_KEY    "305ae3cae41093f3"   /* hub 上: uci get datahub.hub.key */
#define WALKIE_HUB_DEVID  "watch-a1"           /* 本机唯一 ID(两台各编各的!) */
```

**重要约定**: 添加联系人时, 名字必须填"对方的 WALKIE_HUB_DEVID"
(定向消息按 hub 记录 txt 字段过滤, 名字对不上会被当成别人的消息跳过)。

## 验证方法(环境就绪后)

1. B 关机 → A PTT 说两句 → B 开机进聊天页: 气泡自动补出
2. LuCI 状态→Data Hub 页面可见归档记录(可直接播放)
3. 电脑播放任一条: `http://<hub-ip>/cgi-bin/hub?cmd=get&key=<KEY>&id=<N>`
(hub 侧 CGI 需配套新版本: list 的 unread 按设备隔离)

## 构建环境待办(重要)

远程 V1.9.0 提交将 SDK 层同步到官方 **AC79 SDK 1.2.14**(97 个头文件 + 新链接布局),
但配套文件未入库——**全新克隆当前无法编译**, 在有完整 1.2.14 环境的机器上才行。
待补齐入库的清单(来自本机构建实测):

| 缺失 | 用途 |
|---|---|
| `include_lib/net/wifi/wifi_def.h` | wifi_connect.h 新版引用 |
| `include_lib/net/lwip_2_2_0/` 整棵 + `liba/lwip_2_2_0.a` | Makefile 已指向 2.2.0 |
| `liba/lib_esco_repair.a` | AUDIO_LIB 列表引用 |
| `include_lib/system/system_lib_text.ld` | sdk_ld_sdram.c 新版引用 |
| `include_lib/common/movable/movable_text.ld` / `movable_data.ld` | 同上 |
| `include_lib/c/`(C++ 头, 仅 MINI_CXX_SUPPORT=1 时需要) | 可暂缓 |

补齐方式: 从可构建 V1.9.2 的机器把上述文件拷入并提交入库; 或将本仓 SDK 层
整体回退到 V1.8.75 布局(需同步适配 V1.9.x 应用代码)。
