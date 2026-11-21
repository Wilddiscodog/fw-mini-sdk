#!/bin/sh
set -e
CGI=/www/cgi-bin/hub
NEW=/tmp/do_get_new.sh
[ -f "$CGI" ] || exit 1
[ -f "$NEW" ] || exit 1
if grep -q LIVEFIX_HUBFS_REDIRECT "$CGI"; then
	echo "already has LIVEFIX"
	exit 0
fi
cp -a "$CGI" "$CGI.bak.manual.$(date +%Y%m%d%H%M%S)"
awk '
	BEGIN { skip=0 }
	/^do_get\(\) \{/ {
		while ((getline line < "/tmp/do_get_new.sh") > 0) print line
		close("/tmp/do_get_new.sh")
		skip=1
		next
	}
	skip && /^do_ack\(\) \{/ { skip=0 }
	skip { next }
	{ print }
' "$CGI" > /tmp/hub.new
mv /tmp/hub.new "$CGI"
chmod 755 "$CGI"
grep -n LIVEFIX_HUBFS_REDIRECT "$CGI" | head -3
/etc/init.d/uhttpd restart
echo splice_ok
