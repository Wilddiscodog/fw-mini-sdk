@echo off
rem 一键编译: 工具链在 C:\JL\pi32, make/mkdir 用本仓库 tools\utils
rem 未指定 -j 时按 CPU 核数并行
set "ROOT=%~dp0..\..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "PATH=C:\Windows\System32;C:\JL\pi32\bin;%ROOT%\tools\utils;%PATH%"
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=6"
echo %* | findstr /R /C:"-j" >nul
if errorlevel 1 (
  "%ROOT%\tools\utils\make.exe" -j%JOBS% %*
) else (
  "%ROOT%\tools\utils\make.exe" %*
)
