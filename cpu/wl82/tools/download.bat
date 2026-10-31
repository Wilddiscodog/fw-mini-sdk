
@echo off

@echo *********************************************************************
@echo AC791N SDK
@echo *********************************************************************
@echo %date%

cd /d %~dp0

echo %*
set OBJDUMP=C:\JL\pi32\bin\llvm-objdump.exe
set OBJCOPY=C:\JL\pi32\bin\llvm-objcopy.exe
set ELFFILE=sdk.elf

REM %OBJDUMP% -D -address-mask=0x1ffffff -print-dbg %ELFFILE% > sdk.lst
%OBJCOPY% -O binary -j .text %ELFFILE% text.bin
%OBJCOPY% -O binary -j .data %ELFFILE% data.bin
%OBJCOPY% -O binary -j .ram0_data %ELFFILE% ram0_data.bin
%OBJCOPY% -O binary -j .cache_ram_data %ELFFILE% cache_ram_data.bin

%OBJDUMP% -section-headers -address-mask=0x1ffffff %ELFFILE%
%OBJDUMP% -t %ELFFILE% > symbol_tbl.txt

copy /b text.bin+data.bin+ram0_data.bin+cache_ram_data.bin app.bin






if exist ui_res\watch (
packres\packres.exe -n ui -o packres/UIPACKRES ui_res
set UI_RES=packres/UIPACKRES
) else (
set UI_RES=
)
REM slim prompts; must stay on isd -res line (empty AUDIO_RES -> FLASH ~0x207000, EQ crash)
set AUDIO_RES=audlogo_mini
if not exist audlogo_mini (
echo [ERR] missing tools\audlogo_mini , abort download
exit /b 1
)





REM Chip KEY must ride an EXISTING template line: fixbat drops newly-added
REM lines when download.bat already has accumulated multi-CR endings.
set KEY_FILE=-key 0x7DF1.key
REM set KEY_FILE=-key1 JL_791N-XXXX.key1 -mkey JL_791N-XXXX.mkey

set CFG_FILE=cfg

REM ����²���-update_files˵��
REM 1/normal������ͨ���ļ�Ŀ¼�ṹ
REM 2/embedded����ÿ���ļ�����ǰ������һ��ͷ

REM ֻ����code + res�����ļ�
REM -update_files normal
REM ���ɵ��ļ�����Ϊ��db_update_files_data.bin

REM ֻ����Ԥ������Դ�����ļ�
REM -update_files embedded_only $(files) ,����$(files)Ϊ��Ҫ��ӵ���Դ�ļ�
REM ���ɵ��ļ�����Ϊ��db_update_files_data.bin

REM ����code + res + Ԥ������Դ�����ļ�
REM -update_files embedded $(files) ,����$(files)Ϊ��Ҫ��ӵ���Դ�ļ�
REM ���ɵ��ļ�����Ϊ��db_update_files_data.bin


set UPDATE_FILES=-update_files normal
REM Keep audlogo_mini literal so fixbat/%var% loss cannot drop voice res
isd_download.exe isd_config.ini -tonorflash -dev wl82 -boot 0x1c02000 -div1 -wait 300 -uboot uboot.boot -app app.bin cfg_tool.bin -res audlogo_mini cfg -reboot 500 %KEY_FILE% %UPDATE_FILES% -expend-bin




@REM ��������˵��
@rem -format vm
@rem -format all
@rem -reboot 500

echo %errorlevel%

@REM ɾ����ʱ�ļ�
if exist *.mp3 del *.mp3
if exist *.PIX del *.PIX
if exist *.TAB del *.TAB
if exist *.res del *.res
if exist *.sty del *.sty






@REM ������ýű��İ汾��Ϣ�� FW �ļ���
fw_add.exe -noenc -fw jl_isd.fw -add script.ver -out jl_isd.fw

ufw_maker.exe -fw_to_ufw jl_isd.fw
copy jl_isd.ufw update.ufw

@REM db_update_files_data.bin (-update_files normal output, plaintext update_data head)
@REM is the dual-bank OTA/virtual-udisk upgrade file. Copy as update-ota.ufw to drag in.
@REM DO NOT use db_update_data.bin: encrypted head fails device-side head check.

if exist db_update_files_data.bin copy /Y db_update_files_data.bin update-ota.ufw



@REM ���������ļ������ļ�
rem ufw_maker.exe -chip AC630N %ADD_KEY% -output config.ufw -res bt_cfg.cfg

ping /n 2 127.1>null
IF EXIST null del null


::del app.bin
del cache_ram_data.bin
del data.bin
del ram0_data.bin
del text.bin

::�˳���ǰ������ӷ���ֵ(0),���ܶ��������Ƕ�׵���������
exit /b 0
