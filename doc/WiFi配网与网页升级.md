# WiFi 网页配网 + 网页固件升级 (V1.0.0)

> 日期: 2026-10-24 · 文件: `app/net/wifi_prov.c` + `app/bt_wifi_app.c`(钩子) · 宏: `MINI_WIFI_SUPPORT`

## 1. 行为规格

```
上电 ─ wifi_on(MODULE_INIT 内读存储)
  ├─ 有 STA 存储(wifi_get_mode_stored_info) → 强制 STA 连存储配置
  │    └─ 3 次尝试 × 25s 超时 全部失败 → 切 AP
  └─ 无存储 → 直接切 AP
AP 模式: SSID=WL82-XXXXXX(MAC尾3字节) 密码 12345678 → 网页后台 http://192.168.1.1
STA 模式: 网页后台同样可用(路由器 DHCP 分配的 IP)
网页后台:
  ├─ 首页 /          状态 + WiFi 连接表单 + 固件上传表单
  ├─ GET  /scan      扫描周围 WiFi → 列表(单选, 带信道/信号) → 选一个 + 手动输密码 → 提交
  ├─ POST /connect   wifi_store_mode_info 存 VM(持久) → wifi_enter_sta_mode 切 STA
  │    └─ 60s 内没拿到 DHCP(密码错等) → 自动切回 AP 重新配网
  └─ POST /upload    multipart 流式上传 update-ota.ufw → 头校验+CRC16 → dual_bank 烧写
       └─ 校验过 → 烧 boot info → 2s 后自动重启进新固件
```

凭据存储用库自带 `wifi_store_mode_info`(VM 区持久化, 无自建存储); 模式切换用
`wifi_enter_ap_mode/wifi_enter_sta_mode`(运行时切换, 不重启 wifi 模块)。

## 2. 实现结构

| 组件 | 位置 | 说明 |
|---|---|---|
| 事件状态位 | wifi_prov.c `wifi_prov_notify` | bt_wifi_app.c 的 wifi_event_callback 首行转发(DHCP成功/失败/AP起/扫描完) |
| 默认模式选择 | wifi_prov.c `wifi_prov_default_mode_setup` | MODULE_INIT 回调里调用(库上下文, 不可阻塞): 读存储→set_default_mode(force=1) |
| 状态机 | wifi_prov.c `wifi_prov_run` | bt_wifi_app.c 的 wifi_app_task(回调注册+wifi_on 后进入, 不返回); 含网页 STA 失败回 AP 监视 |
| HTTP 服务器 | wifi_prov.c `http_server_task` | lwip socket 手写(端口80, 逐连接处理), AP/STA 通用 |
| 固件上传 | wifi_prov.c `http_handle_upload` | multipart 流式: 滑动尾窗排除结束边界; 头 32B 经 `jl_file_head_valid_check` 解密校验; CRC16(跳头32B) 比对头内 dataCrc; `net_fwrite/net_fclose` 走 dual_bank(与 MSC U盘升级同一条烧写链) |

启动: `app_main` 末尾自动 `wifi_start()`(wifi_prov.c 上电自启); 串口 'b'/按键2 再触发
有 started 防重入。

## 3. 使用

1. **配网**: 设备起 WiFi 后若没存过/连不上, 出现热点 `WL82-XXXXXX`(密码 12345678),
   手机连接 → 浏览器开 `http://192.168.1.1` → 扫描WiFi → 选择目标 WiFi → 输密码 → 连接。
   成功后设备变 STA, 凭据已存 VM, 下次上电直接连(连不上自动再转 AP)。
2. **网页升级**: 网页后台上传 `cpu/wl82/tools/update-ota.ufw` → 页面提示校验结果 →
   通过则 2s 后重启进新固件。上传中不要断开。
3. MSC U盘升级(拖入U盘)与网页升级共用 net_update 单会话, 同时只有一个能进行。

## 4. 注意/遗留

- [ ] 上板全链验证: AP 热点 → 门户 → 扫描列表 → 连接 → STA 上线 → 网页上传升级
- [ ] AP 模式扫描会短暂离开工作信道, 热点可能瞬断, 属正常
- [ ] multipart part 头 > 首包(>4KB)的场景未支持(浏览器表单不会出现)
- [ ] STA 模式 IP 无显示(未接 lwip_get_netif_info), 页面提示"见路由器 DHCP"
- [ ] 上传大文件(700KB @ WiFi) 约需数秒~数十秒, HTTP 无进度条(串口有日志)
- [ ] HTTP 明文无鉴权, 仅适合调试/内网
