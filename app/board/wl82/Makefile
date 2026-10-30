
# fw-mini-sdk 构建脚本 (AC7912BB / WL82)
#   make          编译并打包固件
#   make clean    清理后全量重建（改过 app_config.h 后务必 clean）
#   make VERBOSE=1 显示详细命令
#
# 工具链：C:\JL\pi32 (杰理 pi32v2 clang/LTO)

# ================ LCD / LVGL 开关 ================
#   MINI_LCD_SUPPORT=0  关闭 EMI 8080 并口 LCD(JD9853 + CST816 触摸, xiaolu-R2)
#   ⚠ USB 排查期(V0.9.x)保持 0: 实测彩条真实刷 EMI 后 USB 不再枚举, 待隔离
#   MINI_LVGL_SUPPORT=1 启用 LVGL(third/lvgl/lvgl 已内置 v8.2.0; 改配置后 make clean)
MINI_LCD_SUPPORT  ?= 1
MINI_LVGL_SUPPORT ?= 1

# ================= C++ 支持开关 =================
#   make MINI_CXX_SUPPORT=1   启用 C++ 运行时(全局对象构造/new-delete/STL, -fno-exceptions -fno-rtti)
#   make clean 后再编译生效; 关闭时为纯 C 固件
MINI_CXX_SUPPORT ?= 0

# ================ 外设开关(见 doc/外设移植计划.md, 默认全关) ================
#   启用后 board.c 对应设备表项生效 + periph_demo.c 演示任务运行
#   引脚/频率在 app/board/wl82/board.c 平台数据中核对
MINI_PWM_SUPPORT       ?= 0
MINI_RTC_SUPPORT       ?= 0
MINI_SD_SUPPORT        ?= 0
MINI_GPCNT_SUPPORT     ?= 0
MINI_HW_CRYPTO_SUPPORT ?= 0
# 注意: ?= 值与 # 注释之间勿留空格, 否则 ifeq ($(VAR),1) 会因尾部空格失败
MINI_KEY_EXTRA_SUPPORT ?= 0
# 内置音频: 蓝牙音箱 MIC1/MIC3/DAC + AEC(默认开)
MINI_AUDIO_SUPPORT ?= 1
# USB 协议栈: 0=原厂 driver/usb 双 CDC(默认); 1=CherryUSB
# 改后务必 make clean
MINI_USB_STACK_CHERRYUSB ?= 0
# 音频库注入 LFLAGS 用(LFLAGS 是 := 立即展开, 此变量必须先于其定义)
# audio_server.a: 音频服务+audio_dev_ops; libaec/libdns/libjlsp: AEC 回采算法库
# (jl_math 源码在 LCD 条件块之后追加——必须在 c_SRC_FILES := 主赋值之后)
AUDIO_LIB :=
ifeq ($(strip $(MINI_AUDIO_SUPPORT)),1)
# demo_edr 通话/A2DP + demo_audio 网络/DLNA 解码集
# M4A/AAC: 用 lib_m4a_dec_seek(与 lib_m4a_dec 符号冲突, 不可双链)
AUDIO_LIB := \
	../../../cpu/wl82/liba/audio_server.a \
	../../../cpu/wl82/liba/media_app.a \
	../../../cpu/wl82/liba/libaec.a \
	../../../cpu/wl82/liba/libdns.a \
	../../../cpu/wl82/liba/libjlsp.a \
	../../../cpu/wl82/liba/lib_m4a_dec_seek.a \
	../../../cpu/wl82/liba/lib_mp3_dec.a \
	../../../cpu/wl82/liba/lib_wma_dec.a \
	../../../cpu/wl82/liba/lib_wav_dec.a \
	../../../cpu/wl82/liba/lib_flac_dec.a \
	../../../cpu/wl82/liba/lib_ape_dec.a \
	../../../cpu/wl82/liba/lib_amr_dec.a \
	../../../cpu/wl82/liba/lib_dts_dec.a \
	../../../cpu/wl82/liba/lib_ogg_vorbis_dec.a \
	../../../cpu/wl82/liba/libdrc.a \
	../../../cpu/wl82/liba/lib_crossover_coff.a \
	../../../cpu/wl82/liba/network_download.a \
	../../../cpu/wl82/liba/libmpeg.a \
	../../../cpu/wl82/liba/libdlna.a \
	../../../cpu/wl82/liba/ai_server.a
endif

ifeq ($(OS), Windows_NT)
# 工具链与构建工具路径已写死, 直接 make / make clean 即可(无需手动设置 PATH)
# 强制 cmd: 若 PATH 里有 Git Bash, 默认 SHELL=bash 会让 rm/echo 配方行为错乱
SHELL := cmd.exe
.SHELLFLAGS := /c
TOOL_DIR  := C:/JL/pi32/bin
UTILS_DIR := D:/fw-mini-sdk/tools/utils
CC    := clang.exe
CXX   := clang.exe
LD    := pi32v2-lto-wrapper.exe
AR    := llvm-ar.exe
MKDIR := mkdir_win -p

SYS_LIB_DIR := C:/JL/pi32/pi32v2-lib/r3
SYS_INC_DIR := C:/JL/pi32/pi32v2-include
EXT_CFLAGS  :=
export PATH := C:/JL/pi32/bin;D:/fw-mini-sdk/tools/utils;$(PATH)

FIXBAT          := ..\..\..\tools\utils\fixbat.exe
POST_SCRIPT     := ..\..\..\cpu\wl82\tools\download.bat
RUN_POST_SCRIPT := $(POST_SCRIPT)
else
TOOL_DIR := /opt/jieli/pi32v2/bin
CC    := clang
CXX   := clang
LD    := lto-wrapper
AR    := lto-ar
MKDIR := mkdir -p
RM    := rm -rf
export OBJDUMP := $(TOOL_DIR)/objdump
export OBJCOPY := $(TOOL_DIR)/objcopy
export OBJSIZEDUMP := $(TOOL_DIR)/objsizedump

SYS_LIB_DIR := $(TOOL_DIR)/../lib/r3
SYS_INC_DIR := $(TOOL_DIR)/../include
EXT_CFLAGS  := -D__SHELL__
export PATH:=$(TOOL_DIR):$(PATH)

FIXBAT          := touch
POST_SCRIPT     := ../../../cpu/wl82/tools/download.sh
RUN_POST_SCRIPT := bash $(POST_SCRIPT)
endif

CC  := $(TOOL_DIR)/$(CC)
CXX := $(TOOL_DIR)/$(CXX)
LD  := $(TOOL_DIR)/$(LD)
AR  := $(TOOL_DIR)/$(AR)

OUT_ELF   := ../../../cpu/wl82/tools/sdk.elf
OBJ_FILE  := $(OUT_ELF).objs.txt
BUILD_DIR := objs
ROOT_PREFIX := ../../..

# 编译参数（与官方 SDK 保持一致，勿随意增删）
CFLAGS := \
	-target pi32v2 \
	-mcpu=r3 \
	-integrated-as \
	-flto \
	-Wuninitialized \
	-Wno-invalid-noreturn \
	-fno-common \
	-Oz \
	-g \
	-fallow-pointer-null \
	-fprefer-gnu-section \
	-Wno-shift-negative-value \
	-Wframe-larger-than=2560 \
	-mllvm -pi32v2-large-program=true \
	-fms-extensions \
	-w \
	-fno-unwind-tables \
	-ffunction-sections \
	-fdata-sections \
	-fmessage-length=0 \

DEFINES := \
	-DSUPPORT_MS_EXTENSIONS \
	-DCONFIG_RELEASE_ENABLE \
	-DCONFIG_CPU_WL82 \
	-DCONFIG_JLFAT_ENABLE \
	-DCONFIG_FREE_RTOS_ENABLE \
	-DCONFIG_THREAD_ENABLE \
	-DCONFIG_AUDIO_ONCHIP \
	-D__GCC_PI32V2__ \
	-DCONFIG_NEW_ECC_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1 \
	-DCONFIG_SFC_ENABLE \
	-DEVENT_HANDLER_NUM_CONFIG=2 \
	-DEVENT_TOUCH_ENABLE_CONFIG=0 \
	-DEVENT_POOL_SIZE_CONFIG=256 \
	-DCONFIG_EVENT_KEY_MAP_ENABLE=0 \
	-DTIMER_POOL_NUM_CONFIG=15 \
	-DAPP_ASYNC_POOL_NUM_CONFIG=0 \
	-DVFS_ENABLE=1 \
	-DUSE_SDFILE_NEW=1 \
	-DSDFILE_STORAGE=1 \
	-DVFS_FILE_POOL_NUM_CONFIG=8 \
	-DVM_MAX_PAGE_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_MAX_SECTOR_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_ITEM_MAX_NUM=256 \
	-DCONFIG_BT_ENABLE=1 \
	-DCONFIG_NET_ENABLE=1 \
	-DCONFIG_UPDATA_ENABLE \
	-DCONFIG_OTA_UPDATA_ENABLE \
	-DCONFIG_ITEM_FORMAT_VM \
	-D_XOPEN_SOURCE=700 \
	-D_LIBCPP_HAS_NO_THREADS \
	-D_LIBCPP_NO_EXCEPTIONS \
	-D_GNU_SOURCE \
	-D_LIBCPP_HAS_NO_ALIGNED_ALLOCATION \
	-D__ELF__ \

DEFINES += $(EXT_CFLAGS)

ifeq ($(MINI_CXX_SUPPORT),1)
DEFINES += -DMINI_CXX_SUPPORT=1 -DCONFIG_CXX_SUPPORT
endif
DEFINES += -DMINI_LCD_SUPPORT=$(MINI_LCD_SUPPORT) -DMINI_LVGL_SUPPORT=$(MINI_LVGL_SUPPORT)
DEFINES += -DMINI_PWM_SUPPORT=$(MINI_PWM_SUPPORT) -DMINI_RTC_SUPPORT=$(MINI_RTC_SUPPORT)
DEFINES += -DMINI_SD_SUPPORT=$(MINI_SD_SUPPORT) -DMINI_GPCNT_SUPPORT=$(MINI_GPCNT_SUPPORT)
DEFINES += -DMINI_HW_CRYPTO_SUPPORT=$(MINI_HW_CRYPTO_SUPPORT)
DEFINES += -DMINI_KEY_EXTRA_SUPPORT=$(MINI_KEY_EXTRA_SUPPORT)
DEFINES += -DMINI_AUDIO_SUPPORT=$(strip $(MINI_AUDIO_SUPPORT))
DEFINES += -DMINI_USB_STACK_CHERRYUSB=$(strip $(MINI_USB_STACK_CHERRYUSB))
ifeq ($(strip $(MINI_AUDIO_SUPPORT)),1)
DEFINES += -DCONFIG_AUDIO_ENABLE
endif

# 头文件搜索路径
INCLUDES := \
	-I../../../app/include \
	-I../../../app/common_ble/include \
	-I../../../include_lib \
	-I../../../include_lib/driver \
	-I../../../include_lib/driver/device \
	-I../../../include_lib/driver/cpu/wl82 \
	-I../../../include_lib/system \
	-I../../../include_lib/system/generic \
	-I../../../include_lib/update \
	-I../../../include_lib/utils \
	-I../../../include_lib/utils/syscfg \
	-I../../../include_lib/utils/event \
	-I../../../include_lib/btctrler \
	-I../../../include_lib/btctrler/port/wl82 \
	-I../../../include_lib/btstack \
	-I../../../include_lib/btstack/third_party/common \
	-I../../../include_lib/btstack/third_party/rcsp \
	-I../../../include_lib/net \
	-I../../../include_lib/net/wifi \
	-I../../../include_lib/net/lwip_2_1_3/lwip/src/include \
	-I../../../include_lib/net/lwip_2_1_3/lwip/src/include/lwip \
	-I../../../include_lib/net/lwip_2_1_3/lwip/port \
	-I../../../include_lib/net/lwip_2_1_3/lwip/app \
	-I../../../include_lib/media \
	-I../../../include_lib/media/cpu/wl82 \
	-I../../../include_lib/server \
	-I../../../include_lib/net/network_download \
	-I../../../driver \
	-I../../../driver/usb \
	-I../../../driver/usb/include \
	-I../../../third/CherryUSB/core \
	-I../../../third/CherryUSB/common \
	-I../../../third/CherryUSB/class/cdc \
	-I../../../third/CherryUSB/class/serial \
	-I../../../third/CherryUSB/class/hid \
	-I../../../third/CherryUSB/class/msc \
	-I../../../third/CherryUSB/third_party/fatfs-0.14/source \
	-I../../../third/CherryUSB/class/hub \
	-I../../../third/CherryUSB/osal \
	-I../../../include_lib/system/os/FreeRTOS \
	-I../../../third/CherryUSB/port/musb \
	-I../../../third/CherryUSB/port/jl82 \
	-I$(SYS_INC_DIR) \

# 需要编译的 .c 文件（按目录分组，增删源码只改这里）
# USB 源码见下方 MINI_USB_STACK_CHERRYUSB 条件块
c_SRC_FILES := \
	../../../app/app_main.c \
	../../../app/bt_wifi_app.c \
	../../../app/bt_profile_config.c \
	../../../app/bt_music.c \
	../../../app/bt_decode.c \
	../../../app/net_music.c \
	../../../app/audio_config.c \
	../../../app/audio_demo.c \
	../../../app/periph_demo.c \
	../../../app/common_ble/le_hogp.c \
	../../../app/common_ble/le_trans_data.c \
	../../../app/key/key_driver.c \
	../../../app/key/adkey.c \
	../../../driver/lcd/touch_cst816.c \
	../../../app/net/wifi_conf.c \
	../../../app/net/wifi_prov.c \
	../../../app/net/wifi_log_mute.c \
	../../../driver/config/log_config/lib_btctrler_config.c \
	../../../driver/config/log_config/lib_btstack_config.c \
	../../../app/board/wl82/board.c \
	../../../driver/config/log_config/lib_driver_config.c \
	../../../driver/config/log_config/lib_system_config.c \
	../../../driver/config/log_config/lib_update_config.c \
	../../../driver/config/log_config/lib_audio_config.c \
	../../../driver/config/user_cfg.c \
	../../../driver/iic/iic.c \
	../../../driver/iic/software_iic.c \
	../../../driver/debug/debug.c \
	../../../driver/debug/debug_user.c \
	../../../driver/system/init.c \
	../../../driver/system/system_reset_reason.c \
	../../../driver/system/version.c \
	../../../driver/update/update.c \
	../../../driver/update/net_update.c \
	../../../cpu/wl82/debug.c \
	../../../cpu/wl82/iic.c \
	../../../cpu/wl82/key/adc_api.c \
	../../../cpu/wl82/port_waked_up.c \
	../../../cpu/wl82/setup.c \

# 版本号桩（INCLUDE_XXX-$hash 打印来源）
S_SRC_FILES := \
	../../../include_lib/btctrler/version.z.S \
	../../../include_lib/btstack/version.z.S \
	../../../include_lib/driver/version.z.S \
	../../../include_lib/media/version.z.S \
	../../../include_lib/net/version.z.S \
	../../../include_lib/server/version.z.S \
	../../../include_lib/system/version.z.S \
	../../../include_lib/update/version.z.S \
	../../../include_lib/utils/version.z.S \
	../../../lib/net/version.z.S \
	../../../lib/server/version.z.S \
	../../../lib/utils/version.z.S \

s_SRC_FILES :=
cpp_SRC_FILES :=
cc_SRC_FILES :=
cxx_SRC_FILES :=

# 链接参数（与官方 SDK 一致，闭源库全部在此列出）
LFLAGS := \
	--plugin-opt=-pi32v2-always-use-itblock=false \
	--plugin-opt=-enable-ipra=true \
	--plugin-opt=-pi32v2-merge-max-offset=4096 \
	--plugin-opt=-pi32v2-enable-simd=true \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-global-merge-on-const \
	--plugin-opt=-inline-threshold=5 \
	--plugin-opt=-inline-max-allocated-size=32 \
	--plugin-opt=-inline-normal-into-special-section=true \
	--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar \
	--plugin-opt=save-temps \
	--plugin-opt=-pi32v2-enable-rep-memop \
	--sort-common \
	--plugin-opt=-used-symbol-file=../../../cpu/wl82/sdk_used_list.used \
	--plugin-opt=-enable-movable-region=true \
	--plugin-opt=-movable-region-section-prefix=.movable.slot. \
	--plugin-opt=-movable-region-stub-section-prefix=.movable.stub. \
	--plugin-opt=-movable-region-prefix=.movable.region. \
	--plugin-opt=-movable-region-stub-prefix=__movable_stub_ \
	--plugin-opt=-movable-region-stub-swi-number=-2 \
	--plugin-opt=-movable-region-map-file-list=../../../driver/movable/funcname.txt \
	--plugin-opt=-movable-region-section-map-file-list=../../../driver/movable/section.txt \
	--plugin-opt=-movable-region-exclude-func-file-list=../../../driver/movable/exclude.txt \
	--plugin-opt=-pi32v2-large-program=true \
	--start-group \
	../../../cpu/wl82/liba/cpu.a \
	../../../cpu/wl82/liba/event.a \
	../../../cpu/wl82/liba/system.a \
	../../../cpu/wl82/liba/cfg_tool.a \
	../../../cpu/wl82/liba/fs.a \
	../../../cpu/wl82/liba/common_lib.a \
	../../../cpu/wl82/liba/update.a \
	$(AUDIO_LIB) \
	../../../cpu/wl82/liba/wl_rf_common.a \
	../../../cpu/wl82/liba/btctrler.a \
	../../../cpu/wl82/liba/btstack.a \
	../../../cpu/wl82/liba/crypto_toolbox_Osize.a \
	../../../cpu/wl82/liba/lib_ccm_aes.a \
	../../../cpu/wl82/liba/lib_sig_mesh.a \
	../../../cpu/wl82/liba/hsm.a \
	../../../cpu/wl82/liba/wpasupplicant.a \
	../../../cpu/wl82/liba/libmbedtls_2_2_1.a \
	../../../cpu/wl82/liba/http_cli.a \
	../../../cpu/wl82/liba/json.a \
	../../../cpu/wl82/liba/iperf.a \
	../../../cpu/wl82/liba/lwip_2_1_3.a \
	../../../cpu/wl82/liba/wl_wifi.a \
	../../../cpu/wl82/liba/net_server.a \
	--end-group \
	-T../../../cpu/wl82/sdk.ld \
	-M=../../../cpu/wl82/tools/sdk.map \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-mattr=+fprev1 \

LIBPATHS := \
	-L$(SYS_LIB_DIR) \

LIBS := \
	$(SYS_LIB_DIR)/libm.a \
	$(SYS_LIB_DIR)/libc.a \
	$(SYS_LIB_DIR)/libm.a \
	$(SYS_LIB_DIR)/libcompiler-rt.a \
	../../../cpu/wl82/liba/lib_usb_syn.a \


# ---------------- C++ 支持条件块 ----------------
ifeq ($(MINI_CXX_SUPPORT),1)
CXXFLAGS   := -std=c++11 -fno-exceptions -fno-rtti
c_SRC_FILES += ../../../driver/c++/cpp_run_init.c
cpp_SRC_FILES += ../../../driver/c++/cxx_runtime.cpp
cpp_SRC_FILES += ../../../app/cxx_smoke.cpp
INCLUDES   += -I../../../include_lib/c++ -I../../../include_lib/c++/include
LIBS       += ../../../include_lib/c++/libstdc++/libcxx.a
LIBS       += ../../../include_lib/c++/libstdc++/libcxxabi.a
endif


# ---------------- LCD / LVGL 条件块 ----------------
# LCD: EMI 8080-8bit 并口 + 屏驱动。只编一个面板驱动: 框架 list_for_each_lcd_driver
# 取第一个注册项, 多个同时编译会按链接顺序误选(lcd_ili9341.c 曾因此抢在 jd9853 前)。
ifeq ($(MINI_LCD_SUPPORT),1)
c_SRC_FILES += ../../../driver/lcd/lcd_drive.c
c_SRC_FILES += ../../../driver/lcd/lcd_jd9853.c
c_SRC_FILES += ../../../driver/lcd/lcd_te_stub.c
INCLUDES    += -I../../../driver/lcd/include
endif

# SC7U22 六轴 (PH0/PH1 iic0, 与触摸共总线)
c_SRC_FILES += ../../../driver/imu/sc7u22.c
INCLUDES    += -I../../../driver/imu/include

# ---------------- USB: CherryUSB 或原厂从机栈 ----------------
ifeq ($(strip $(MINI_USB_STACK_CHERRYUSB)),1)
c_SRC_FILES += ../../../app/cdc_dual_app.c
c_SRC_FILES += ../../../app/usbh_msc_app.c
c_SRC_FILES += ../../../app/usbh_cdc_hid_app.c
c_SRC_FILES += ../../../third/CherryUSB/core/usbd_core.c
c_SRC_FILES += ../../../third/CherryUSB/core/usbh_core.c
c_SRC_FILES += ../../../third/CherryUSB/class/cdc/usbd_cdc_acm.c
c_SRC_FILES += ../../../third/CherryUSB/class/msc/usbd_msc.c
c_SRC_FILES += ../../../third/CherryUSB/class/serial/usbh_serial.c
c_SRC_FILES += ../../../third/CherryUSB/class/serial/usbh_cdc_acm.c
c_SRC_FILES += ../../../third/CherryUSB/class/hid/usbh_hid.c
c_SRC_FILES += ../../../third/CherryUSB/class/msc/usbh_msc.c
c_SRC_FILES += ../../../third/CherryUSB/third_party/fatfs-0.14/source/ff.c
c_SRC_FILES += ../../../third/CherryUSB/class/hub/usbh_hub.c
c_SRC_FILES += ../../../third/CherryUSB/osal/usb_osal_freertos.c
c_SRC_FILES += ../../../third/CherryUSB/port/jl82/usb_hc_jl82.c
c_SRC_FILES += ../../../third/CherryUSB/port/jl82/usb_glue_jl82_host.c
c_SRC_FILES += ../../../third/CherryUSB/port/jl82/usb_dc_jl82.c
c_SRC_FILES += ../../../third/CherryUSB/port/jl82/usb_glue_jl82.c
else
# 原厂从机栈双 CDC; 主机/MSD/UAC/UVC 源码不编
c_SRC_FILES += ../../../app/user_cdc_jl.c
c_SRC_FILES += ../../../driver/usb/usb_config.c
c_SRC_FILES += ../../../driver/usb/usb_epbuf_manager.c
c_SRC_FILES += ../../../driver/usb/device/cdc.c
c_SRC_FILES += ../../../driver/usb/device/descriptor.c
c_SRC_FILES += ../../../driver/usb/device/task_pc.c
c_SRC_FILES += ../../../driver/usb/device/usb_device.c
c_SRC_FILES += ../../../driver/usb/device/user_setup.c
endif

# ---------------- jl_math 源码库(MINI_AUDIO_SUPPORT, AEC 回采用) ----------------
# 原厂 apps/common/jl_math 同目录: AEC/DNS 的 WOLA 滤波器组引用 flrfft/flrifft
# 等浮点 FFT 原语(闭源库内没有, 由应用层源码提供)
ifeq ($(strip $(MINI_AUDIO_SUPPORT)),1)
c_SRC_FILES += ../../../app/jl_math/flfft_core_pi32v2.c
c_SRC_FILES += ../../../app/jl_math/jl_fft.c
c_SRC_FILES += ../../../app/jl_math/jl_math.c
c_SRC_FILES += ../../../app/jl_math/kiss_fft.c
c_SRC_FILES += ../../../app/jl_math/nn_function_vec.c
INCLUDES    += -I../../../app/jl_math
# 本地 EQ/音效默认参数(非 USB 在线调音); TCFG_EQ_ONLINE_ENABLE=0
c_SRC_FILES += ../../../app/audio_music/audio_eff_default_parm.c
c_SRC_FILES += ../../../app/audio_music/eq_config_new.c
c_SRC_FILES += ../../../app/audio_music/effects_adj.c
INCLUDES    += -I../../../app/audio_music -I../../../include_lib/media/eq
endif

# LVGL 8.2.0 + watch_ui 智能手表 Demo
ifeq ($(MINI_LVGL_SUPPORT),1)
LVGL_DIR  := ../../../third/lvgl/lvgl
rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
LVGL_C_SRC += $(call rwildcard, $(LVGL_DIR)/src/, *.c)
c_SRC_FILES += $(LVGL_C_SRC)
c_SRC_FILES += ../../../third/lvgl_port/driver/lv_port_disp.c
c_SRC_FILES += ../../../third/lvgl_port/driver/lv_port_indev.c
c_SRC_FILES += ../../../app/watch_ui/watch_ui.c
c_SRC_FILES += ../../../app/watch_ui/watch_home.c
c_SRC_FILES += ../../../app/watch_ui/watch_clock.c
c_SRC_FILES += ../../../app/watch_ui/watch_dropdown.c
c_SRC_FILES += ../../../app/watch_ui/watch_backlight.c
c_SRC_FILES += ../../../app/watch_ui/watch_volume.c
c_SRC_FILES += ../../../app/watch_ui/watch_power.c
c_SRC_FILES += ../../../app/watch_ui/watch_apps.c
c_SRC_FILES += ../../../app/watch_ui/assets/watch_icons.c
INCLUDES    += -I../../../third/lvgl -I$(LVGL_DIR) -I$(LVGL_DIR)/src
INCLUDES    += -I../../../third/lvgl_port/driver -I../../../driver/lcd/include
INCLUDES    += -I../../../app/watch_ui -I../../../app/include
DEFINES     += -DLV_CONF_INCLUDE_SIMPLE
endif

# CoreMark: 源码仍在 third/coremark, 暂不编入(按键跑分已关)
# MINI_COREMARK_SUPPORT ?= 0


c_OBJS    := $(c_SRC_FILES:%.c=%.c.o)
S_OBJS    := $(S_SRC_FILES:%.S=%.S.o)
s_OBJS    := $(s_SRC_FILES:%.s=%.s.o)
cpp_OBJS  := $(cpp_SRC_FILES:%.cpp=%.cpp.o)
cxx_OBJS  := $(cxx_SRC_FILES:%.cxx=%.cxx.o)
cc_OBJS   := $(cc_SRC_FILES:%.cc=%.cc.o)

OBJS      := $(c_OBJS) $(S_OBJS) $(s_OBJS) $(cpp_OBJS) $(cxx_OBJS) $(cc_OBJS)
DEP_FILES := $(OBJS:%.o=%.d)

OBJS      := $(addprefix $(BUILD_DIR)/, $(OBJS:$(ROOT_PREFIX)/%=%))
DEP_FILES := $(addprefix $(BUILD_DIR)/, $(DEP_FILES:$(ROOT_PREFIX)/%=%))

VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
QUITE :=
else
QUITE := @
endif

LINK_AT ?= 1

.PHONY: all clean pre_build

.SUFFIXES:

all: pre_build $(OUT_ELF)
	$(info +POST-BUILD)
	$(QUITE) $(RUN_POST_SCRIPT) sdk

# fixbat may drop new KEY lines + accumulate multi-CR; force -key 0x7DF1.key + normalize CRLF
pre_build:
	$(info +PRE-BUILD)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/sdk_used_list.c -o ../../../cpu/wl82/sdk_used_list.used
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../driver/movable/section.c -o ../../../driver/movable/section.txt
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/sdk_ld.c -o ../../../cpu/wl82/sdk.ld
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/tools/download.c -o $(POST_SCRIPT)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)
	$(QUITE) powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\..\tools\ensure_download_key.ps1 $(POST_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/tools/isd_config_rule.c -o ../../../cpu/wl82/tools/isd_config.ini

clean:
ifeq ($(OS),Windows_NT)
	-@if exist $(subst /,\,$(OUT_ELF)) del /q /f $(subst /,\,$(OUT_ELF))
	-@if exist $(subst /,\,$(BUILD_DIR)) rd /s /q $(subst /,\,$(BUILD_DIR))
	@echo [ok] cleaned objs and sdk.elf
else
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)
endif


ifeq ($(LINK_AT), 1)
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) @$(OBJ_FILE) $(LFLAGS) $(LIBPATHS) $(LIBS)
else
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(QUITE) $(LD) -o $(OUT_ELF) $(OBJS) $(LFLAGS) $(LIBPATHS) $(LIBS)
endif


$(BUILD_DIR)/%.c.o : $(ROOT_PREFIX)/%.c
	$(info +CC $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

# CherryUSB 源文件注入配置头（限定范围, 避免与 SDK 宏冲突）
ifeq ($(strip $(MINI_USB_STACK_CHERRYUSB)),1)
$(BUILD_DIR)/third/CherryUSB/%.c.o $(BUILD_DIR)/app/cdc_acm_app.c.o : CFLAGS += -include usb_config.h
$(BUILD_DIR)/app/cdc_dual_app.c.o : CFLAGS += -include usb_config.h
$(BUILD_DIR)/app/usbh_msc_app.c.o : CFLAGS += -include usb_config.h
$(BUILD_DIR)/third/CherryUSB/third_party/fatfs-0.14/source/ff.c.o : CFLAGS += \
	-Df_open=usbh_fatfs_open -Df_close=usbh_fatfs_close -Df_read=usbh_fatfs_read -Df_write=usbh_fatfs_write \
	-Df_lseek=usbh_fatfs_lseek -Df_truncate=usbh_fatfs_truncate -Df_sync=usbh_fatfs_sync \
	-Df_opendir=usbh_fatfs_opendir -Df_closedir=usbh_fatfs_closedir -Df_readdir=usbh_fatfs_readdir \
	-Df_findfirst=usbh_fatfs_findfirst -Df_findnext=usbh_fatfs_findnext -Df_mkdir=usbh_fatfs_mkdir \
	-Df_unlink=usbh_fatfs_unlink -Df_rename=usbh_fatfs_rename -Df_stat=usbh_fatfs_stat \
	-Df_chmod=usbh_fatfs_chmod -Df_utime=usbh_fatfs_utime -Df_chdir=usbh_fatfs_chdir \
	-Df_chdrive=usbh_fatfs_chdrive -Df_getcwd=usbh_fatfs_getcwd -Df_getfree=usbh_fatfs_getfree \
	-Df_getlabel=usbh_fatfs_getlabel -Df_setlabel=usbh_fatfs_setlabel -Df_forward=usbh_fatfs_forward \
	-Df_expand=usbh_fatfs_expand -Df_mount=usbh_fatfs_mount -Df_mkfs=usbh_fatfs_mkfs \
	-Df_fdisk=usbh_fatfs_fdisk -Df_setcp=usbh_fatfs_setcp -Df_putc=usbh_fatfs_putc \
	-Df_puts=usbh_fatfs_puts -Df_printf=usbh_fatfs_printf -Df_gets=usbh_fatfs_gets
endif

$(BUILD_DIR)/%.S.o : $(ROOT_PREFIX)/%.S
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.s.o : $(ROOT_PREFIX)/%.s
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.cpp.o : $(ROOT_PREFIX)/%.cpp
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

-include $(DEP_FILES)
