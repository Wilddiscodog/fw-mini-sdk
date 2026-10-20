@echo off
cd /d %~dp0
isd_download.exe isd_config.ini -tonorflash -dev wl82 -boot 0x1c02000 -div1 -wait 30000 -uboot uboot.boot -app app.bin cfg_tool.bin -format all -key 0x7DF1.key -reboot 500
