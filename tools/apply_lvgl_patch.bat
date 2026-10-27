@echo off
rem Apply JieLi LVGL overlays onto third\lvgl\lvgl (v8.2.0).
rem Optional fetch (then delete .git so the tree can be pushed with this repo):
rem   git clone --branch v8.2.0 --depth 1 https://github.com/lvgl/lvgl.git third\lvgl\lvgl
rem   rmdir /s /q third\lvgl\lvgl\.git
setlocal
set ROOT=%~dp0..
set LVGL=%ROOT%\third\lvgl\lvgl
set PORT=%ROOT%\third\lvgl_port

if not exist "%LVGL%\lvgl.h" (
    echo [err] missing %LVGL%\lvgl.h
    echo clone v8.2.0 into third\lvgl\lvgl first, then delete its .git folder
    exit /b 1
)

if exist "%LVGL%\.git" (
    echo [warn] removing %LVGL%\.git so lvgl sources can be committed with fw-mini-sdk
    rmdir /s /q "%LVGL%\.git"
)

copy /y "%PORT%\overlay\lv_conf_internal.h" "%LVGL%\src\lv_conf_internal.h" >nul
copy /y "%PORT%\lv_conf.h" "%ROOT%\third\lvgl\lv_conf.h" >nul

echo [ok] LVGL patch applied: %LVGL%
endlocal
