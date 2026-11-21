#!/bin/sh
# Make 302 Location absolute — watch httpcli cannot parse relative /hubfs/...
set -e
CGI=/www/cgi-bin/hub
cp -a "$CGI" "$CGI.bak.abs.$(date +%Y%m%d%H%M%S)"

# Replace relative Location line with absolute http://HOST/hubfs/...
awk '
	/Location: \/hubfs\/%s/ {
		print "\t\thost=${HTTP_HOST:-$SERVER_ADDR}"
		print "\t\thost=${host%%:*}"
		print "\t\t[ -n \"$host\" ] || host=192.168.5.28"
		print "\t\tprintf '\''Status: 302 Found\\r\\nLocation: http://%s/hubfs/%s\\r\\nCache-Control: public, max-age=3600\\r\\n\\r\\n'\'' \"$host\" \"$path\""
		next
	}
	{ print }
' "$CGI" > /tmp/hub.abs.new
mv /tmp/hub.abs.new "$CGI"
chmod 755 "$CGI"
grep -n -A5 LIVEFIX_HUBFS "$CGI" | head -12
/etc/init.d/uhttpd restart
echo abs_redirect_ok
