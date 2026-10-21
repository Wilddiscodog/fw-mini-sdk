@echo off
rem 一键清理: 等价于 make clean, 纯 cmd 实现(无需 rm/Git 工具)
del /q ..\..\..\cpu\wl82\tools\sdk.elf 2>nul
rmdir /s /q objs 2>nul
echo [ok] cleaned (objs + sdk.elf)
