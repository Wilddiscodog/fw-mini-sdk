@echo off
rem 一键编译: 无需手动设置 PATH (工具链路径写死于 Makefile 与本脚本)
set PATH=C:\JL\pi32\bin;D:\fw-mini-sdk\tools\utils;%PATH%
D:\fw-mini-sdk\tools\utils\make.exe %*
