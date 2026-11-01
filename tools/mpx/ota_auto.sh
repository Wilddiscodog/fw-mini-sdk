#!/usr/bin/env bash
# Auto-OTA catcher: watch uart19.log for a fresh boot marker, then hammer the
# OTA upload inside the stable window (wifi ~7s, mpx test deferred while an
# upgrade runs, so a connected upload runs to completion even past 20s).
LOG=D:/fw-mini-sdk/uart19.log
FILE=D:/fw-mini-sdk/cpu/wl82/tools/update-ota.ufw
URL=http://192.168.5.94/upload

boot_count() {
  grep -ac "system reset reason" "$LOG" 2>/dev/null || echo 0
}

last=$(boot_count)
echo "[auto] baseline boot count: $last"
while true; do
  cur=$(boot_count)
  if [ "$cur" -gt "$last" ]; then
    last=$cur
    echo "[auto] new boot detected (#$cur), waiting 7s for wifi+portal"
    sleep 7
    for try in 1 2 3 4 5 6 7 8 9 10 11 12; do
      out=$(curl -sS --connect-timeout 1 --max-time 150 -H "Expect:" \
            -F "fw=@${FILE};type=application/octet-stream" "$URL" 2>&1)
      echo "[auto boot#$cur try$try] $(echo "$out" | tail -2 | tr '\r\n' ' ')"
      if echo "$out" | grep -q "^OK"; then
        echo "[auto] OTA_SUCCESS on boot#$cur"
        exit 0
      fi
      # 本周期内若出现新的 boot 标记则放弃本轮, 等下一个窗口
      if [ "$(boot_count)" -gt "$cur" ]; then
        echo "[auto] boot#$$ ended early, next window"
        break
      fi
      sleep 1
    done
  fi
  sleep 1
done
