#!/bin/sh
# 在 AR9331 (已刷 datahub) 上以 root 执行 — 修复「大曲约 60s 被掐断切歌」
#   scp -O hub-fix-music-stream.sh root@192.168.5.28:/tmp/
#   ssh root@192.168.5.28 'sh /tmp/hub-fix-music-stream.sh'
#
set -e
HUBCGI=/www/cgi-bin/hub
MOUNT=$(uci -q get datahub.hub.mount); MOUNT=${MOUNT:-/mnt/sda1}
ROOT=$MOUNT/hub
MARKER='# LIVEFIX_HUBFS_REDIRECT'

[ -f /etc/config/uhttpd ] || { echo "no uhttpd config"; exit 1; }
[ -f "$HUBCGI" ] || { echo "missing $HUBCGI"; exit 1; }
[ -d "$ROOT" ] || echo "WARN: $ROOT not mounted yet"

echo "== uhttpd timeouts =="
uci set uhttpd.main.script_timeout='900'
uci set uhttpd.main.network_timeout='900'
# -y alias 对目录不可靠; 清掉误配
uci -q delete uhttpd.main.prefix
uci -q delete uhttpd.main.alias
uci commit uhttpd

echo "== /www/hubfs -> $ROOT =="
ln -sfn "$ROOT" /www/hubfs

echo "== ensure cgi do_get has LIVEFIX =="
if grep -q "$MARKER" "$HUBCGI" 2>/dev/null; then
	echo "cgi already has hubfs redirect"
else
	echo "WARN: cgi missing LIVEFIX — run splice or reflash datahub cgi"
fi

echo "== restart uhttpd =="
/etc/init.d/uhttpd restart
sleep 1

echo "== smoke =="
echo "script_timeout=$(uci -q get uhttpd.main.script_timeout)"
echo "network_timeout=$(uci -q get uhttpd.main.network_timeout)"
ls -la /www/hubfs | head -3
grep -n LIVEFIX_HUBFS_REDIRECT "$HUBCGI" | head -2 || true
echo "OK"
