# U盘自动升级移植记录 (MSC 虚拟U盘 · 双备份)

> 版本: V0.8.0 · 日期: 2026-10-24
> 需求: 打开 USB MSC(4MB RAM 盘), 固件文件拖入 U 盘后自动检测, 文件 3s 大小
> 不变即认为写入完毕, 自动升级。

## 1. 原厂机制调研(对比结论)

| 原厂参考 | 路径(D:\fw-AC79_AIoT_SDK) | 作用 |
|---|---|---|
| ram_disk_update.c | apps/common/usb/device/ | 虚拟U盘扇区写钩子: 逐扇区找 JL_FILE_HEAD("update_data")流式识别升级文件 |
| ram_disk_circle.c | apps/common/usb/device/ | 2GB 假 FAT32 虚拟U盘设备(读合成/写丢弃+钩子), 我们已有真实 4MB RAM 盘不采用 |
| net_update.c | apps/common/update/ | net_fopen/net_fwrite/net_fclose: 4K 缓冲 → dual_bank 写备用 bank → 烧 boot info → 2s 后复位 |
| 生成升级固件.bat | cpu/wl82/tools/ | **方式2(OTA/FTP/虚拟U盘, 仅双备份): db_update_files_data.bin → update-ota.ufw** |
| update.a | liba/ | 闭源: dual_bank_* 烧写、jl_file_head_valid_check(头解密+校验)、get_target_udate_addr、calc_crc16_with_init_val、app_active_update_task_init |
| sd_udisk_upgrade 示例 | apps/common/example/update/ | 方式1(真 U盘/SD, 单备份可用): 重启进 uboot 从介质重读——RAM 盘数据不跨复位, 不适用 |

关键判断:
- 虚拟U盘升级**只能走双备份**(flash 内 app 区双份, 应用态直接写备用 bank);
  单备份需复位进 uboot 从真实介质重读, RAM 盘不满足。
- 升级文件 = `update-ota.ufw` = isd_download 产出的 `db_update_data.bin`
  (本版工具链文件名, 原厂文档叫 db_update_files_data.bin), 文件头为加密
  JL_FILE_HEAD(32B), 设备端 jl_file_head_valid_check 解密校验后 szFileName
  == "update_data"。
- jl_isd.ufw(整烧镜像, ~1MB)与升级文件(仅 OTA 载荷, ~703KB)是**两个不同的文件**,
  拖错文件不会误触发(头校验不过)。

## 2. 本工程实现

```
PC 拖入 update-ota.ufw
   │ SCSI WRITE(10) × N
   ▼
usbd_msc_sector_write()  ── memcpy 进 4MB RAM 盘
   └─ msc_update_on_write(): 逐 512B 找 JL_FILE_HEAD(解密+名字匹配)
        → 记录 first_sector / file_size / data_crc, write_seq++
msc_ota_watch_task(500ms 轮询, msc_only_init 时启动):
   ① write_seq 3s 无变化(静默判据, 用户需求)
   ② FAT16 根目录出现 *.UFW 目录项, 首簇/大小与检测流一致(拷贝真正收尾)
   ③ 触发 msc_ota_do_upgrade():
        net_fopen("update-ota.ufw")  → RAM 盘按 512B 流式 net_fwrite
        CRC16(跳过 32B 头, 覆盖 [32, file_size)) 与头内 dataCrc 比对
        ├─ CRC 错 → net_fclose(fd,1): 不烧写不复位, 允许重新拖入
        └─ CRC 对 → net_fclose(fd,0): 烧 boot_info → 2s 后 system_reset
                     → 重启进新 bank
```

与原厂 ram_disk_update.c 的差异(有意为之): 原厂"边拷边写 flash"(最后一个扇区
落地即触发); 本工程按用户需求改为"**拷贝完整收尾后**再升级"(3s 静默 + FAT 目录
项就绪), Windows 侧拷贝对话框能正常结束, 触发更稳。flash 写入链路(net_update.c
→ update.a 双备份)与原厂完全一致。

### 文件清单

| 文件 | 修改 |
|---|---|
| app/include/app_config.h | `CONFIG_DOUBLE_BANK_ENABLE 1` + `TCFG_VIR_UPDATE_ENABLE`(升级总开关) |
| driver/update/net_update.c | 新增: 原厂 net_update.c 移植(差异: storage_device_ready 恒 0 桩 + 编译门) |
| app/cdc_acm_msc_app.c | 升级模块: msc_update_on_write 钩子 + msc_ota_watch_task + FAT16 几何常量提取 |
| app/app_main.c | 启用 usb_mode_task(此前被注释, USB 设备模式从未启动——LTO 把升级模块一并裁掉, app.bin 无 [msc-ota] 串即此因) |
| app/board/wl82/Makefile | 编入 net_update.c |
| cpu/wl82/tools/download.bat | `db_update_data.bin → update-ota.ufw` 自动复制 |

## 3. flash 布局变化(重要)

切换双备份后 isd_config.ini 生成 `BR22_TWS_DB=YES; DB_UPDATE_DATA=YES;`(原单备份
`NEW_FLASH_FS=YES` 消失)。本次构建 FLASH INFO:
```
FLASH_NEED_SIZE : 0xfff000        (16M flash)
CODE_BOUNDARY_LINE : 0x7fc000     (8M 处双 bank 分界)
VM_START_ADDR : 0xff5000
```
**当前设备上烧的是单备份布局固件, 必须先用烧录器整体烧一次本次产出的
jl_isd.ufw(双备份全量镜像), 之后才能走虚拟U盘升级。** 升级文件用
tools/update-ota.ufw, 不要用 jl_isd.ufw。

## 4. 使用方法

1. (仅首次)烧录 jl_isd.ufw 全量镜像。
2. USB 连电脑 → 出现 4MB U 盘(WL82RAMDISK)。
3. 把 `cpu/wl82/tools/update-ota.ufw` 拖入 U 盘, 等拷贝完成。
4. 3s 后日志: `[msc-ota] head @sec xx ...` → `file settled, flashing...` →
   `crc ok, burn boot info...` → `net_update successfuly , system will reset
   after 2s` → 自动重启进新固件。
5. 升级成功后首启日志: `The upgrade is successful, no need to upgrade again!`
   (update.c 的 update_success_boot_check)。

## 5. 上板验证要点 / 遗留

- [ ] 首次全量烧录后, 拖入 update-ota.ufw 实测完整链路(检测→CRC→烧写→复位→新版本启动)
- [ ] CRC 错误路径实测(拖半截文件): 应打印 crc err 且不烧写
- [ ] 拖入 jl_isd.ufw(整烧镜像): 应静默拒绝(头校验不过, 无升级动作)
- [ ] 升级期间 USB 主机读盘行为(复位瞬间 Windows 可能弹"设备拔出", 属预期)
- [ ] 版本号递增: isd_config_rule.c 的 BR22_TWS_VERSION 按原厂 note.txt 规则
      升级固件版本时应 +1(当前 0, 未启用版本防回滚, 暂不影响)
- [ ] RAM 盘内容掉电即失: 每次插拔 U 盘都是空盘, 属设计内
