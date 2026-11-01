@echo off
rem OTA retry hammer: attempt upload until success (device may be crash-looping)
setlocal enabledelayedexpansion
set URL=http://192.168.5.94/upload
set FILE=D:\fw-mini-sdk\cpu\wl82\tools\update-ota.ufw
for /l %%i in (1,1,20) do (
    echo === attempt %%i ===
    curl -sS --connect-timeout 3 --max-time 150 -H "Expect:" -F "fw=@%FILE%;type=application/octet-stream" %URL% 2>&1
    echo.
    echo curl_exit=%%errorlevel^)
    findstr /c:"OK" %TEMP%\ota_last 2>nul >nul
)
endlocal
