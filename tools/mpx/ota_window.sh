#!/usr/bin/env bash
# OTA hammer: retry upload in each boot's ~7.5-20s stable window until success.
# Success criterion: server responds with a line starting with OK.
FILE=D:/fw-mini-sdk/cpu/wl82/tools/update-ota.ufw
URL=http://192.168.5.94/upload
for attempt in $(seq 1 40); do
  out=$(curl -sS --connect-timeout 1 --max-time 150 -H "Expect:" \
        -F "fw=@${FILE};type=application/octet-stream" "$URL" 2>&1)
  rc=$?
  echo "[attempt $attempt rc=$rc] $(echo "$out" | tail -2 | tr '\r\n' ' ')"
  if echo "$out" | grep -q "^OK"; then
    echo "OTA_SUCCESS"
    exit 0
  fi
  sleep 1
done
echo "OTA_GAVE_UP"
exit 1
