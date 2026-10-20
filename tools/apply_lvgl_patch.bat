@echo off
rem ============================================================
rem  应用 LVGL 官方补丁到 lvgl 源码树
rem  前置: lvgl v8.2.0 源码已放到 third\lvgl\lvgl
rem    git clone --branch v8.2.0 --depth 1 https://github.com/lvgl/lvgl.git third\lvgl\lvgl
rem  本脚本把杰理适配文件覆盖/补入对应位置, 并把 lv_conf.h 放到 lvgl 目录旁
rem ============================================================
setlocal
set ROOT=%~dp0..
set LVGL=%ROOT%\third\lvgl\lvgl
set PORT=%ROOT%\third\lvgl_port

if not exist "%LVGL%\lvgl.h" (
    echo [错误] 未找到 %LVGL%\lvgl.h
    echo 请先执行: git clone --branch v8.2.0 --depth 1 https://github.com/lvgl/lvgl.git "%LVGL%"
    pause & exit /b 1
)

rem 1) 杰理补丁的 lv_conf_internal.h 覆盖 lvgl/src 同名文件
copy /y "%PORT%\overlay\lv_conf_internal.h" "%LVGL%\src\lv_conf_internal.h" >nul

rem 2) lv_conf.h 放到 lvgl 目录旁(lv_conf_internal.h 默认 ../lv_conf.h)
copy /y "%PORT%\lv_conf.h" "%ROOT%\third\lvgl\lv_conf.h" >nul

echo [ok] LVGL 补丁应用完成: %LVGL%
endlocal
