#!/usr/bin/env bash
# Smart OTA v2: after each boot, once the device has its IP, hammer the upload
# CONTINUOUSLY (2s connect timeout, no gaps) until the next boot marker or OK.
LOG=D:/fw-mini-sdk/uart19.log
FILE=D:/fw-mini-sdk/cpu/wl82/tools/update-ota.ufw
URL=http://192.168.5.94/upload

boot_count() { grep -ac "system reset reason" "$LOG" 2>/dev/null || echo 0; }
got_ip() { tail -c 20000 "$LOG" 2>/dev/null | grep -ac "ip4 address: 192.168.5." ; }

last=$(boot_count)
echo "[smart2] baseline boot: $last"
while true; do
  cur=$(boot_count)
  if [ "$cur" -gt "$last" ]; then
    last=$cur
    echo "[smart2] boot #$cur, waiting for dhcp ip..."
    for wait in $(seq 1 15); do
      sleep 1
      if [ "$(got_ip)" -gt 0 ]; then
        echo "[smart2] ip acquired (t=${wait}s), hammering until window closes"
        try=0
        while [ "$(boot_count)" = "$cur" ]; do
          try=$((try + 1))
          out=$(curl -sS --connect-timeout 2 --max-time 150 -H "Expect:" \
                -F "fw=@${FILE};type=application/octet-stream" "$URL" 2>&1)
          echo "[smart2 t${try}] $(echo "$out" | tail -1)"
          if echo "$out" | grep -q "^OK"; then
            echo "[smart2] OTA_SUCCESS"
            exit 0
          fi
        done
        break
      fi
    done
  fi
  sleep 0.3
done
