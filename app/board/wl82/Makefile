
# fw-mini-sdk 构建脚本 (AC7912BB / WL82)
#   make          编译并打包固件
#   make clean    清理后全量重建（改过 app_config.h 后务必 clean）
#   make VERBOSE=1 显示详细命令
#
# 工具链：C:\JL\pi32 (杰理 pi32v2 clang/LTO)

# ================ LCD / LVGL 开关 ================
#   MINI_LCD_SUPPORT=0  关闭 EMI 8080 并口 LCD(ili9341 参考驱动)
#   MINI_LVGL_SUPPORT=1 启用 LVGL(需 lvgl v8.2.0 源码 + tools/apply_lvgl_patch.bat)
MINI_LCD_SUPPORT  ?= 0
MINI_LVGL_SUPPORT ?= 0

# ================= C++ 支持开关 =================
#   make MINI_CXX_SUPPORT=1   启用 C++ 运行时(全局对象构造/new-delete/STL, -fno-exceptions -fno-rtti)
#   make clean 后再编译生效; 关闭时为纯 C 固件
MINI_CXX_SUPPORT ?= 0

ifeq ($(OS), Windows_NT)
TOOL_DIR := C:/JL/pi32/bin
CC    := clang.exe
CXX   := clang.exe
LD    := pi32v2-lto-wrapper.exe
AR    := llvm-ar.exe
MKDIR := mkdir_win -p
RM    := rm -rf

SYS_LIB_DIR := C:/JL/pi32/pi32v2-lib/r3
SYS_INC_DIR := C:/JL/pi32/pi32v2-include
EXT_CFLAGS  :=
export PATH:=$(TOOL_DIR);$(PATH)

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

# 头文件搜索路径
INCLUDES := \
	-I../../../app/include \
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
	-I../../../include_lib/media \
	-I../../../include_lib/media/cpu/wl82 \
	-I../../../driver \
	-I../../../third/CherryUSB/core \
	-I../../../third/CherryUSB/common \
	-I../../../third/CherryUSB/class/cdc \
	-I../../../third/CherryUSB/class/msc \
	-I../../../third/CherryUSB/class/hub \
	-I../../../third/CherryUSB/osal \
	-I../../../include_lib/system/os/FreeRTOS \
	-I../../../third/CherryUSB/port/musb \
	-I../../../third/CherryUSB/port/jl82 \
	-I$(SYS_INC_DIR) \

# 需要编译的 .c 文件（按目录分组，增删源码只改这里）
# USB: CherryUSB 设备栈(CDC) —— 旧杰理从机栈 driver/usb 已不再参与编译, 文件保留备查
c_SRC_FILES := \
	../../../app/app_main.c \
	../../../app/cdc_acm_app.c \
	../../../app/usbh_msc_app.c \
	../../../app/board/wl82/board.c \
	../../../third/CherryUSB/core/usbd_core.c \
	../../../third/CherryUSB/core/usbh_core.c \
	../../../third/CherryUSB/class/cdc/usbd_cdc_acm.c \
	../../../third/CherryUSB/class/msc/usbh_msc.c \
	../../../third/CherryUSB/class/hub/usbh_hub.c \
	../../../third/CherryUSB/osal/usb_osal_freertos.c \
	../../../third/CherryUSB/port/jl82/usb_hc_jl82.c \
	../../../third/CherryUSB/port/jl82/usb_glue_jl82_host.c \
	../../../third/CherryUSB/port/jl82/usb_dc_jl82.c \
	../../../third/CherryUSB/port/jl82/usb_glue_jl82.c \
	../../../driver/config/log_config/lib_driver_config.c \
	../../../driver/config/log_config/lib_system_config.c \
	../../../driver/config/log_config/lib_update_config.c \
	../../../driver/config/user_cfg.c \
	../../../driver/iic/iic.c \
	../../../driver/iic/software_iic.c \
	../../../driver/debug/debug.c \
	../../../driver/debug/debug_user.c \
	../../../driver/system/init.c \
	../../../driver/system/system_reset_reason.c \
	../../../driver/system/version.c \
	../../../driver/update/update.c \
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
# LCD: EMI 8080-8bit 并口 + 屏驱动(默认 ili9341, 由 app_config.h 的 TCFG_LCD_xxx 选择)
ifeq ($(MINI_LCD_SUPPORT),1)
c_SRC_FILES += ../../../driver/lcd/lcd_drive.c
c_SRC_FILES += ../../../driver/lcd/lcd_ili9341.c
c_SRC_FILES += ../../../driver/lcd/lcd_te_stub.c
INCLUDES    += -I../../../driver/lcd/include
endif

# LVGL: 需先获取 lvgl v8.2.0 源码到 third/lvgl/lvgl 并运行 tools/apply_lvgl_patch.bat
ifeq ($(MINI_LVGL_SUPPORT),1)
LVGL_DIR  := ../../../third/lvgl/lvgl
rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
LVGL_C_SRC += $(call rwildcard, $(LVGL_DIR)/src/, *.c)
c_SRC_FILES += $(LVGL_C_SRC)
c_SRC_FILES += ../../../third/lvgl_port/driver/lv_port_disp.c
c_SRC_FILES += ../../../third/lvgl_port/lv_mian/lv_main.c
INCLUDES    += -I$(LVGL_DIR) -I$(LVGL_DIR)/src -I../../../third/lvgl_port/driver
DEFINES     += -DLV_CONF_INCLUDE_SIMPLE
endif


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

pre_build:
	$(info +PRE-BUILD)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/sdk_used_list.c -o ../../../cpu/wl82/sdk_used_list.used
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../driver/movable/section.c -o ../../../driver/movable/section.txt
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/sdk_ld.c -o ../../../cpu/wl82/sdk.ld
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/tools/download.c -o $(POST_SCRIPT)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../cpu/wl82/tools/isd_config_rule.c -o ../../../cpu/wl82/tools/isd_config.ini

clean:
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)


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
$(BUILD_DIR)/third/CherryUSB/%.c.o $(BUILD_DIR)/app/cdc_acm_app.c.o : CFLAGS += -include usb_config.h
$(BUILD_DIR)/app/usbh_msc_app.c.o : CFLAGS += -include usb_config.h
$(BUILD_DIR)/app/usbh_msc_app.c.o : CFLAGS += -include usb_config.h

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
