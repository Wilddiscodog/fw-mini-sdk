# OTA 升级 "app head key verify fail" 问题记录

> 日期: 2026-10-25 · 状态: **根因已定位, 修复被打包工具链 bug 阻塞(见 §4), 待解决**
> 关联: doc/U盘自动升级移植记录.md · 提交 a61a2f8 / 59c0b17 之后的未竟工作

## 1. 现象与排查过程(时间线)

虚拟U盘拖入 update-ota.ufw 升级, OTASTAT/CDC 串口逐轮报错, 每修一层暴露下一层:

| 轮次 | 现象 | 根因 | 修复 | 提交 |
|---|---|---|---|---|
| 1 | `head_ret=-1`(头校验不过) | update-ota.ufw 由 `db_update_data.bin`(密文头)复制而来, 设备端 `jl_file_head_valid_check`(反汇编实锤: 纯 CRC16-XMODEM, 不解密)要求明文头 | 改用 `db_update_files_data.bin`(明文头), 复制逻辑移入 download.c 源头 | a61a2f8 |
| 2 | `fw_fail sector=7 ret=-14` | 首个 4K 落盘时 update.a 按名字 `task_create("dw_update")` 创建升级任务, `task_info_table` 缺该条目 → -14 | app_main.c 补 `{"dw_update", 10, 1024, 64}` | a61a2f8 |
| 3 | 一插 USB 就重启循环 | `CONFIG_USB_MUSB_EP_NUM=2`(MSC-only 时代遗留), CDC 的 EP2/EP5 注册即越界端点状态数组 | usb_config.h 改 6 (EP0~EP5) | a61a2f8 |
| 4 | `fw_fail sector=16 ret=-22` | dw_update 任务已建(前修生效), 第二个 4K 的 flash 写入被 update.a 拒绝; 网页诊断页/OTASTAT 显示 tgt=0x7fc020(目标 bank 正确)、rb=126(目标区未写入) | 加诊断(见 §3), 串口日志抓到真凶 ↓ | 59c0b17+ |
| 5 | 串口: `[UPDATE]app head key verify fail` | **打包时未传芯片 KEY**: isd_download 用默认种子 0xFFFF 编码升级文件内嵌头, 设备端用芯片 KEY(0x7DF1)校验, 必然失败 | download.c 加 `set KEY_FILE=-key 0x7DF1.key` — **被 fixbat 吞行, 未生效(§4)** | 未提交 |

## 2. 根因的完整技术细节(已实锤)

### 2.1 头加密算法(反汇编 + 已知明文对破解, 脚本见提交历史 Temp/)

update.a 的 `doe`(0x2003f54)是 16 位 LFSR 流密码, 对称 XOR:

```
状态 s = key(16bit); 首字节直接 XOR s 低 8 位; 之后每字节:
fb = s >> 15
s  = ((s << 1) & 0xEFDE) | fb | (((s>>11)^fb)&1)<<12 | (((s>>4)^fb)&1)<<5
输出 XOR s 低 8 位
```

每 32 字节块的 key = `seed ^ (块偏移>>2)`(decode_data_by_user_key 包装)。种子:

- **0x7DF1 = 芯片 KEY**(tools/0x7DF1.key, isd_download `-key` 参数传入)
- **0xFFFF = 无 KEY 时的默认种子**

验证方法: 设备日志 dump 了 flash 0x0 产品头的"编码形态"(设备自己用 0xFFFF 解码打印),
与明文产品头构成已知明文对, 重建算法 100% 匹配。

### 2.2 设备端校验链(dual_bank 流, 全部反汇编确认)

1. `dual_bank_update_write`(net_fwrite 上下文): 流前 32B 明文 CRC + `strcmp("update_data")`;
   第二个 32B(流偏移 0x20)明文 CRC + `strcmp("app_core")` —— 两层头都必须**明文**
2. dw_update 任务写 flash 前: **"app head key verify"** —— 用芯片 KEY 解码载荷首个 32B
   (应解码出 `app_area_head`), 要求升级文件的载荷是 **0x7DF1 形态**(flash 形态)
3. 包内 CRC 约定: **内层 app_core 头的 dataCrc = CRC16-XMODEM(载荷[64:文件尾])**,
   外层 update_data 头的 dataCrc 不校验载荷(实测核对)

### 2.3 isd_download 行为(实验确认)

- 无 `-key`: 产物内嵌头用 0xFFFF 种子编码 → 设备 verify 拒绝(head_ret 链: 头 CRC/名字
  都过, 死在 verify) → `fw_fail sector=16 ret=-22`
- 有 `-key 0x7DF1.key`: 产物内嵌头变 0x7DF1 形态 ✓(本机实验, 见 test_key 记录)
- 烧录镜像 jl_isd.bin 同理受 KEY 影响; 当前板上 flash 为无 KEY 时代产物(0xFFFF 形态),
  设备启动解码兼容(实测能跑), 但**升级文件必须 KEY 形态**

## 3. 已就位的诊断能力(保留)

- OTASTAT.TXT: fw_fail 行带 `ret/tgt/rb@偏移 f= s=/jedec=/sr=`(字节级回读+flash 容量+状态寄存器)
- 网页上传失败页: offset/ret/tgt/rb/boot/双层头 全量诊断
- CDC 日志镜像(CONFIG_USB_DEBUG_ENABLE): update.a 内部 `[UPDATE]` 标签日志实时可见
  (依赖 EP_NUM=6 修复; 流控三处修复保证不丢行)

## 4. ⚠️ 阻塞项: fixbat.exe 吞行 bug(待解决)

`tools/utils/fixbat.exe`(闭源)在 pre_build 里对 cpp 生成的 download.bat 做行尾处理,
**会确定性吞掉 download.c 模板中新增的行**:

- 实测两次: 加在 `REM set KEY_FILE...mkey` 之后、加在 `set CFG_FILE=cfg` 之后, 均被吞
  (含纯 ASCII 行; 早期含中文的行同样被吞)
- 直接对 cpp 原始输出手跑 fixbat 却不吞(同样输入!) —— 差异疑似与 make 管线中 bat 的
  多次累积转换有关: bat 行尾已劣化为 `\r\r\r\r\r\r\n`(每次 make 增加一个 \r),
  cpp 输入行尾为 `\n`
- 结果: `set KEY_FILE=-key 0x7DF1.key` 进不了 download.bat, 打包产物仍是 0xFFFF 形态

### 待尝试的解决路径(按优先级)

1. **绕开 download.bat**: 把 key 参数写进 isd_config.ini(isd_config_rule.c 直接生成,
   无 fixbat 环节) —— 需查 isd_download 是否支持 ini 内 KEY 字段
2. 手动复现 make 管线(cpp → bat → fixbat, 用 make 同款相对路径)逐字节对比, 找出吞行
   的确切触发条件后规避(如改变行内容/位置/行尾)
3. pre_build 后置一步: 用 powershell/python 在 fixbat 之后把 KEY 行补写进 bat
   (改 Makefile pre_build, 加一条 `python fix_key.py` 类命令)
4. 最粗暴: 不改模板, 每次构建后手工执行
   `isd_download ... -key 0x7DF1.key -update_files normal` 重打升级包(脚本化)

### 修复后的验证清单

- [ ] `update-ota.ufw` 载荷首 32B 用 0x7DF1 解码 = `app_area_head`(离线脚本已具备)
- [ ] 内层 app_core 头 dataCrc == CRC16(载荷) (设备端已改为按此约定校验)
- [ ] 拖入后串口: `Match app_core file` → 无 `app head key verify fail` → 写入推进过 sector 16
- [ ] `crc ok, burn boot info` → 2s 复位 → 新 bank 启动(注意: 板上 flash 现为无 KEY 时代
      镜像, 新 bank 为 KEY 形态, 若启动解码不兼容会回退旧 bank, 需观察)
- [ ] 全链路通后: 烧录器重刷一次带 -key 的 jl_isd.ufw, 让 flash 布局与升级包形态一致

## 5. 当前工作区未提交内容(本文档随本次提交)

- download.c: `set KEY_FILE=-key 0x7DF1.key`(已加, 待 fixbat 问题解决后生效)
- cdc_acm_msc_app.c / wifi_prov.c: CRC 约定改为内层头 dataCrc vs 载荷[64:] —— **此修复
  与 KEY 修复独立且必要**(否则 verify 过了也会死在 crc_err), 已验证与包内约定一致
- 产物 bin/ufw: 仍为 0xFFFF 形态(KEY 未生效), 拖入无效, 勿再测试
