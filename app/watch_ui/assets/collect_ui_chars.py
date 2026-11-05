#!/usr/bin/env python3
# Collect unique chars needed by watch_ui Chinese labels.
import re
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
chars = set()
pat = re.compile(r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]+")

for p in root.rglob("*.c"):
    text = p.read_text(encoding="utf-8", errors="ignore")
    for m in pat.findall(text):
        chars.update(m)

# ASCII used in mixed labels / numbers / punctuation
ascii_extra = (
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    " .,:;!?%/+-_()[]{}|~@#*&<>\"'\n\r\t="
)
chars.update(ascii_extra)

# Safety net: common UI words (cover dynamic strings / future typos)
extra = (
    "周一二三四五六日右滑打开应用返回亮度音量陀螺仪网络蓝牙对讲录音设置游戏"
    "功能开发中未就绪检查总线读数失败初始化失败播放暂停已调未启用本机连接数音箱"
    "按侧键录音松开发送无法播放暂无语音开始搜索同固件设备在线点头像进入聊天"
    "已送达发送失败录音机后续接入关已连接未连接版本语音发来一条平衡球本局分"
    "新纪录排行榜点按任意处再来一局倾斜设备移动小球内存不足无法控制等待手机推送音频"
    "WiFiSSIDIP读中PH失败VolA2DPDLNA"
    "联系人管理删除配对保存发现在线离线添加取消确定附近新设备已保存未保存"
    "设备扫描正在请先连接网络未就绪台"
)
chars.update(extra)

cjk = sorted(c for c in chars if "\u4e00" <= c <= "\u9fff")
symbols = "".join(sorted(chars))
out = pathlib.Path(__file__).with_name("watch_ui_symbols.txt")
out.write_text(symbols, encoding="utf-8")
print("cjk_count", len(cjk))
print("total_unique", len(chars))
print("wrote", out)
print("cjk:", "".join(cjk))
