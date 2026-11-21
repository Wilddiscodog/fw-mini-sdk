AR9331 Pisen WMN013N 定制固件 patch
====================================

基线: OpenWrt v23.05.5 (commit 28cf53e6bd)
目标: 把品胜 WMN013N 随身路由(AR9331 + 16M SPI Flash)改造为
      "家庭设备数据中枢": U盘数据湖 + CGI API + SMB 备份 + 网页音乐播放。

patch 内容 (33 个文件, 1843 行新增)
------------------------------------
1. target/linux/ath79/dts/ar9331_pisen_wmn013n.dts
   新设备树: 16M 分区布局(u-boot/firmware/ART@0xff0000)、MAC 取自
   u-boot@0x1fc00、系统灯 GPIO27、Reset 键 GPIO11、GPIO8 hog 常供 USB VBUS、
   USB 用 generic-ehci(绕开 chipidea 无法解除 reset 的问题)。

2. target/linux/ath79/image/generic.mk
   新增 Device/pisen_wmn013n 镜像定义 (TPLINK_HWID 0x07030101, 16192k),
   打包 USB 存储/ext4/ksmbd/umdns + luci-app-datahub/usbswitch。

3. package/luci-app-datahub/    数据中枢 (核心)
   U盘 ext4 + SQLite 数据湖, CGI API /cgi-bin/hub (put/list/get/ack/reg/
   stats/export), UDP 3722 广播发现(免写 IP/key), ksmbd 网络备份,
   LuCI 状态页 + 网页音乐播放器。依赖: sqlite3-cli, socat。

4. package/luci-app-usbswitch/  USB 通道切换
   网页按钮控制 SGM7227 模拟开关 (GPIO23=OE, GPIO26=通道:
   1→AR9331 读U盘 / 0→切给电脑), 含开机默认通道服务。

5. package/luci-app-gpiotest/   GPIO 探测工具 (开发用, 默认不打进镜像)

6. files/  首启配置 + 守护
   - etc/uci-defaults/99-default-wifi:
       首启生成管理 AP "Hub-XXXX"(密码 hyundai415) + 192.168.66.1 管理网段,
       预建 wwan(DHCP STA 用), 放行家庭网段 22/80/445 + mDNS。
   - usr/bin/wifi-guard + etc/init.d/wifi-guard:
       AP+STA 同射频看门狗。STA 启用后连续 180 秒未关联(搜不到原 SSID/
       密码错)即删除该 STA 配置并 wifi reload, 管理 AP 立即恢复,
       连 Hub-XXXX 在 LuCI 重新扫描配网。开机自启。

应用方法
--------
    tar xzf ar9331-wmn013n-v23.05.5-patch.tar.gz
    cd <openwrt v23.05.5 源码或 SDK 根目录>
    git apply ../ar9331-pisen-wmn013n-v23.05.5.patch     # 或: patch -p1 < ...patch
    # 常规构建:
    ./scripts/feeds update -a && ./scripts/feeds install -a
    make menuconfig   # Target: ath79/Generic, Device: Pisen WMN013N (16M)
    make -j$(nproc)

注意事项
--------
- patch 可干净应用于 v23.05.5 (tag 对应 commit), 其他版本未验证。
- files/ 目录在上游 .gitignore 中, 用 git status 看不到属正常;
  patch 方式应用不受 ignore 影响。若手工拷贝, 需 git add -f files。
- 新增脚本均带可执行位 (patch 中 new file mode 100755)。
- 敏感信息: patch 内含管理 AP/ksmbd 密码 (hyundai415), 对外分享前请自行替换。

音乐大文件被掐断 (约 60s 切歌)
--------------------------------
根因: uhttpd CGI script_timeout 默认约 60s, `cmd=get` 用 `cat` 推 ape/flac
时 body 被掐 → 手表 EOF → 自动下一首。

本 patch 已含:
  - uci-defaults/98-datahub-uhttpd: script/network_timeout=900 + /www/hubfs 软链
  - cgi do_get: 附件 >256KB 时 302 → /hubfs/<path> (不经 CGI)

已刷旧固件、暂不重编时, 在路由上执行热修:
  sh hub-fix-music-stream.sh
(见同目录; scp 到路由器后 root 运行, 立刻生效)
