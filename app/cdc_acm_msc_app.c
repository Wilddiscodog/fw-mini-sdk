/*
 * CherryUSB 设备 —— MSC(4MB RAM 盘, 虚拟U盘自动升级) [+ CDC 日志口, 宏控]
 *
 * 端点规划:
 *   EP1(0x81 IN / 0x01 OUT) bulk 512 —— MSC(普通 DMA 模式, V0.8 验证配置)
 *   [MINI_USB_CDC_ENABLE=1 时] EP2(0x82/0x02) CDC 数据 + EP5(0x85) INT 通知(CDC 为日志输出口)
 *
 * USB 枚举异常排查期(V0.9.x): CDC 与 CDC 日志暂时关闭(app_config.h),
 * 只保留 MSC 升级路径(msc_ota, V0.9.3 起钩子中断内仅计数, 检测全在任务)。
 */
#include "app_config.h"

#if TCFG_VIR_UPDATE_ENABLE
/* JL 系统头须先于 CherryUSB 头(宏/声明冲突), 升级模块用 */
#include "os/os_api.h"
#include "asm/wdt.h"
#include "net_update.h"                  /* CONFIG_UPGRADE_OTA_FILE_NAME */
#include "update/update_loader_download.h"
#endif

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#if TCFG_VIR_UPDATE_ENABLE
/* 虚拟U盘升级: 定义与任务在本文件尾部(见 msc_only_init 后的升级模块段) */
static void msc_update_on_write(uint32_t sector, const uint8_t *buffer, uint32_t length);
static void msc_ota_watch_task(void *arg);
#endif

/*!< 端点地址: MSC 数据 EP1(V0.8 验证); CDC 组合时数据 EP2 + 通知 EP3 */
#define MSC_IN_EP    0x81
#define MSC_OUT_EP   0x01

#if MINI_USB_CDC_ENABLE
#define CDC1_IN_EP   0x82
#define CDC1_OUT_EP  0x02
#define CDC1_INT_EP  0x85
#endif

#define USBD_VID           0xFFFF
#define USBD_PID           0x0003
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#if MINI_USB_CDC_ENABLE
/*!< 3 个接口: CDC(0 comm + 1 data) + MSC(2); IAD 复合设备类 */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN + MSC_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
    MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
    MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
    MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC1_INT_EP, CDC1_OUT_EP, CDC1_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
    MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "fw-mini-sdk",                /* Manufacturer */
    "WL82 CDC-LOG + MSC 4MB",     /* Product */
    "WL82-0001",                  /* Serial Number */
};
#else
/*!< MSC-only(单接口, V0.8 验证配置) */
#define USB_CONFIG_SIZE (9 + MSC_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "fw-mini-sdk",                /* Manufacturer */
    "WL82 MSC-only 4MB",          /* Product */
    "WL82-0001",                  /* Serial Number */
};
#endif

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return config_descriptor_fs;
    }
    return NULL;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return other_speed_config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return other_speed_config_descriptor_fs;
    }
    return NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_descriptor_msc = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

#if MINI_USB_CDC_ENABLE
/* ---- CDC 数据缓冲(RAM0 .usb_h_bss; 设备栈为 CPU 搬运, 位置不敏感, 跟随工程约定) ---- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_write_buffer[2048];

volatile bool cdc1_tx_busy = false;
static volatile bool cdc_notify_busy = false;   /* EP5 SERIAL_STATE 通知在途 */
volatile uint8_t dtr_intf_mask = 0;   /* bit0: CDC DTR(主机打开串口) */
volatile uint8_t cdc_configured = 0;  /* SET_CONFIGURATION 完成 */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        cdc_configured = 1;
        cdc1_tx_busy = false;
        cdc_notify_busy = false;
        usbd_ep_start_read(busid, CDC1_OUT_EP, cdc1_read_buffer, 2048);
        break;
    case USBD_EVENT_DISCONNECTED:
        cdc_configured = 0;
        break;
    default:
        break;
    }
}

/* ---- CDC: 日志输出口。主机发来的数据只接收丢弃(日志口无命令处理) ---- */
void usbd_cdc1_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    (void)nbytes;
    usbd_ep_start_read(busid, CDC1_OUT_EP, cdc1_read_buffer, 2048);
}

void usbd_cdc1_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if ((nbytes % 512) == 0 && nbytes) {
        usbd_ep_start_write(busid, CDC1_IN_EP, NULL, 0);   /* ZLP 收尾 */
    } else {
        cdc1_tx_busy = false;
    }
}

/*!< 端点回调表 */
struct usbd_endpoint cdc1_out_ep = { .ep_addr = CDC1_OUT_EP, .ep_cb = usbd_cdc1_bulk_out };
struct usbd_endpoint cdc1_in_ep  = { .ep_addr = CDC1_IN_EP,  .ep_cb = usbd_cdc1_bulk_in };

/* ---- CDC 通知端点(EP5 INT): 注册以完成 ep_open 硬件配置(bit14+mps 模式);
 *     DTR 变化时发 PSTN SERIAL_STATE 通知(标准 CDC 行为, 主机串口状态指示) ---- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_notify_buf[16];

static void usbd_cdc_notify_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    cdc_notify_busy = false;
}

static void usbd_cdc_send_serial_state(uint8_t busid, uint8_t intf, uint16_t state)
{
    if (cdc_notify_busy) {
        return;                    /* 上一通知未完成: 丢弃本次(状态类信息, 可丢) */
    }
    cdc_notify_buf[0] = 0xA1;      /* bmRequestType: Device-to-Host | Class | Interface */
    cdc_notify_buf[1] = 0x20;      /* SERIAL_STATE */
    cdc_notify_buf[2] = 0;         /* wValue */
    cdc_notify_buf[3] = 0;
    cdc_notify_buf[4] = intf;      /* wIndex = comm 接口号 */
    cdc_notify_buf[5] = 0;
    cdc_notify_buf[6] = 2;         /* wLength */
    cdc_notify_buf[7] = 0;
    cdc_notify_buf[8] = (uint8_t)state;   /* UART 状态位图: bit0 DCD, bit1 DSR */
    cdc_notify_buf[9] = (uint8_t)(state >> 8);
    cdc_notify_busy = true;
    usbd_ep_start_write(busid, CDC1_INT_EP, cdc_notify_buf, 10);
}

struct usbd_endpoint cdc_notify_ep = { .ep_addr = CDC1_INT_EP, .ep_cb = usbd_cdc_notify_in };

struct usbd_interface intf0; /* CDC comm */
struct usbd_interface intf1; /* CDC data */
#else
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    (void)event;                      /* MSC-only: MSC 类自行管理数据端点 */
}
#endif

struct usbd_interface intf_msc;

/* ---- 4MB RAM 盘(8192 扇区 × 512B)。放 SDRAM .bss: RAM0(.usb_h_bss) 容量不足,
 * 且设备栈为 CPU FIFO 搬运, 对缓冲无 DMA 属性要求 ---- */
#define MSC_RAM_SECTORS  8192u
#define MSC_RAM_SIZE     (MSC_RAM_SECTORS * 512u)
static uint8_t msc_ram_disk[MSC_RAM_SIZE];

/* ---- FAT16 几何(与 msc_ram_mkfs16() 保持一致, 升级检测要用) ----
 * 扇区0=BPB; FAT×2 各 32 扇区(扇区1~64); 固定根目录 512 项×32B(扇区65~96);
 * 数据区从扇区 97 起, 簇号 N ↔ 扇区 97+(N-2), 1 扇区/簇 */
#define MSC_FAT_START_SECTOR   1u
#define MSC_FAT_SECTORS        32u
#define MSC_ROOT_START_SECTOR  (MSC_FAT_START_SECTOR + 2u * MSC_FAT_SECTORS)
#define MSC_ROOT_ENTRIES       512u
#define MSC_DATA_START_SECTOR  (MSC_ROOT_START_SECTOR + MSC_ROOT_ENTRIES * 32u / 512u)

/* ---- FAT16 格式化(4MB 盘, 1 扇区/簇, 固定根目录) ---- */
static void msc_ram_put16(uint32_t off, uint16_t v)
{
    msc_ram_disk[off] = (uint8_t)v;
    msc_ram_disk[off + 1] = (uint8_t)(v >> 8);
}

static void msc_ram_mkfs16(void)
{
    const uint32_t fat_start_sector = MSC_FAT_START_SECTOR;
    const uint32_t fat_sectors = MSC_FAT_SECTORS;
    const uint32_t root_start_sector = MSC_ROOT_START_SECTOR;
    const uint32_t second_fat_sector = fat_start_sector + fat_sectors;

    memset(msc_ram_disk, 0, MSC_RAM_SIZE);

    /* --- BPB(扇区0) --- */
    msc_ram_disk[0] = 0xEB; msc_ram_disk[1] = 0x3C; msc_ram_disk[2] = 0x90;
    memcpy(&msc_ram_disk[3], "JLWL82 ", 8);
    msc_ram_put16(11, 512);          /* 每扇区字节 */
    msc_ram_disk[13] = 1;            /* 每簇扇区 */
    msc_ram_put16(14, 1);            /* 保留扇区 */
    msc_ram_disk[16] = 2;            /* FAT 数 */
    msc_ram_put16(17, 512);          /* 根目录项数 */
    msc_ram_put16(19, MSC_RAM_SECTORS);
    msc_ram_disk[21] = 0xF8;         /* 介质描述 */
    msc_ram_put16(22, fat_sectors);  /* 每 FAT 扇区数 */
    msc_ram_put16(24, 32);           /* 每道扇区 */
    msc_ram_put16(26, 8);            /* 磁头数 */
    msc_ram_disk[36] = 0x80;         /* 物理驱动器号 */
    msc_ram_disk[38] = 0x29;         /* 扩展引导标志 */
    msc_ram_disk[39] = 0x32;         /* 卷 ID */
    msc_ram_disk[40] = 0x38;
    msc_ram_disk[41] = 0x4C;
    msc_ram_disk[42] = 0x57;
    memcpy(&msc_ram_disk[43], "WL82RAMDISK", 11);
    memcpy(&msc_ram_disk[54], "FAT16   ", 8);
    msc_ram_disk[510] = 0x55; msc_ram_disk[511] = 0xAA;

    /* FAT16 entries 0 and 1: media descriptor and reserved cluster. */
    msc_ram_put16(fat_start_sector * 512U, 0xFFF8U);
    msc_ram_put16(fat_start_sector * 512U + 2U, 0xFFFFU);
    memcpy(&msc_ram_disk[second_fat_sector * 512U],
           &msc_ram_disk[fat_start_sector * 512U], fat_sectors * 512U);

    /* FAT16 uses a fixed root directory immediately after both FAT copies. */
    memcpy(&msc_ram_disk[root_start_sector * 512U], "WL82RAMDISK", 11);
    msc_ram_disk[root_start_sector * 512U + 11U] = 0x08;
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
    (void)busid;
    (void)lun;
    *block_num = MSC_RAM_SECTORS;
    *block_size = 512;
}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    uint32_t offset;

    if (busid != 0 || lun != 0 || !buffer || sector >= MSC_RAM_SECTORS ||
        length > CONFIG_USBDEV_MSC_MAX_BUFSIZE) {
        return -1;
    }
    offset = sector * 512U;
    if (length > MSC_RAM_SIZE - offset) {
        return -1;
    }
    memcpy(buffer, &msc_ram_disk[offset], length);
    return 0;
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    uint32_t offset;

    if (busid != 0 || lun != 0 || !buffer || sector >= MSC_RAM_SECTORS ||
        length > CONFIG_USBDEV_MSC_MAX_BUFSIZE) {
        return -1;
    }
    offset = sector * 512U;
    if (length > MSC_RAM_SIZE - offset) {
        return -1;
    }
    memcpy(&msc_ram_disk[offset], buffer, length);
#if TCFG_VIR_UPDATE_ENABLE
    msc_update_on_write(sector, buffer, length);
#endif
    return 0;
}

void msc_only_init(uint8_t busid, uintptr_t reg_base)
{
    /* Prepare a valid FAT16 superfloppy image before exposing the LUN. */
    msc_ram_mkfs16();

    usbd_desc_register(busid, &usb_descriptor_msc);

#if MINI_USB_CDC_ENABLE
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc1_out_ep);
    usbd_add_endpoint(busid, &cdc1_in_ep);
    usbd_add_endpoint(busid, &cdc_notify_ep);   /* EP3 INT: ep_open 硬件配置 + SERIAL_STATE */
#endif
    usbd_add_interface(busid, usbd_msc_init_intf(busid, &intf_msc, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);

#if TCFG_VIR_UPDATE_ENABLE
    /* 虚拟U盘升级看门: 500ms 轮询, 盘被写过且静默 3s 后扫 FAT 根目录触发升级 */
    os_task_create(msc_ota_watch_task, NULL, 9, 2048, 0, "msc_ota");
#endif
}

#if MINI_USB_CDC_ENABLE
/* ---- CDC 日志输出口: 原厂 debug_user.c usb_log_task 调用(usb_id 忽略, 单口)
 *     512B 分块 + 完成等待; 串口未打开(DTR=0)/未配置/写超时 → 丢弃返回 len */
/* CDC 日志输出口: 原厂 debug_user.c usb_log_task 调用(usb_id 忽略, 单口)
 * 512B 分块 + 完成等待; 约定"返回实发字节数, 未发出的由调用方续传":
 *   - 主机未连(DTR=0)/未配置: 返回 0(不假装成功, 调用方据此保留数据)
 *   - 上一包未完成(主机暂停读取): 先等; 5s 仍未完则带着 busy 返回——
 *     端点武装状态不动, 主机恢复读取产生 IN 完成后自然续传(绝不在
 *     旧包未落时重新武装覆盖, 那是此前"日志断断续续"的丢失根源) */
uint32_t cdc_write_data(int usb_id, uint8_t *buf, uint32_t len)
{
    (void)usb_id;
    if (!cdc_configured || !(dtr_intf_mask & 0x01)) {
        return 0;
    }
    uint32_t sent = 0;
    while (sent < len) {
        int wait = 0;
        while (cdc1_tx_busy && ++wait < 500) {
            os_time_dly(1);
        }
        if (cdc1_tx_busy) {
            return sent;              /* 主机暂停读取: 保留余量, 不动端点 */
        }
        uint32_t n = (len - sent > 512U) ? 512U : (len - sent);
        memcpy(cdc1_write_buffer, buf + sent, n);
        cdc1_tx_busy = true;
        usbd_ep_start_write(0, CDC1_IN_EP, cdc1_write_buffer, n);
        wait = 0;
        while (cdc1_tx_busy && ++wait < 500) {  /* 5s 超时(拔线保护) */
            os_time_dly(1);
        }
        if (cdc1_tx_busy) {
            return sent;              /* 不清 busy: 包仍挂着, IN 完成后自续 */
        }
        sent += n;
    }
    return sent;
}
#endif  /* MINI_USB_CDC_ENABLE */

/*============================ 虚拟U盘自动升级(双备份) ============================
 * 原厂对应: apps/common/usb/device/ram_disk_update.c(扇区流拦截) —— 本工程把
 * "边拷边写 flash"改为"拷贝完整落盘后再升级"(用户需求: 文件 3s 大小不变即认为
 * 写完), 触发时机更稳(Windows 目录项已收敛), flash 写入链路仍走原厂
 * net_fopen/net_fwrite/net_fclose(dual_bank 双备份烧写 + 2s 后自动复位)。
 *
 * 检测(V0.9.3 起): MSC 扇区写钩子在 USB 中断上下文, 只做 write_seq++(纯计数)——
 *   jl_file_head_valid_check 所在模块依赖 norflash_read/norflash_mutex(update.a
 *   flash_fs_api.c), 在中断里调用会并发抢 flash → 异常复位(V0.9.2 开 USB 即
 *   重启循环的根因)。全部检测/校验移到看门任务(任务上下文):
 *   盘被写过 + 3s 写静默 → 扫 FAT16 根目录 *.UFW 目录项 → 读该文件首扇区跑
 *   JL_FILE_HEAD 校验(szFileName=="update_data", db_update_data.bin 即该格式)
 *   → 512B 流式 CRC16 校验 → net_fwrite 写入 → 烧 boot info → 复位。 */
#if TCFG_VIR_UPDATE_ENABLE
/* JL 升级文件头(update.a 内部同款布局, 共 32B) */
typedef struct stJL_FILE_HEAD {
    uint16_t u16Crc;
    uint16_t u16DataCrc;
    uint32_t u32Address;
    uint32_t u32Length;                  /* 含本 32B 头 */
    uint8_t  u8Attribute;
    uint8_t  u8Res;
    uint16_t u16Index;
    char     szFileName[16];
} JL_FILE_HEAD;

extern int jl_file_head_valid_check(void *head);             /* update.a(会读 flash, 仅任务上下文调用) */
extern uint16_t calc_crc16_with_init_val(uint16_t init_crc, uint8_t *ptr, uint16_t len);
extern uint32_t get_target_udate_addr(void);                /* update.a: 升级目标地址(ctx 建立后有效) */
extern uint32_t dual_bank_update_read_data(uint32_t offset, uint8_t *read_buf, uint32_t read_len);
/* flash 实际容量诊断: 直接读 JEDEC ID(sfc_norflash_api.h 原语, 任务上下文+互斥) */
extern void norflash_mutex_enter(void);
extern void norflash_mutex_exit(void);
extern void norflash_spi_cs(char cs);
extern void norflash_spi_write_byte(unsigned char data);
extern unsigned char norflash_spi_read_byte(void);
extern int norflash_wait_busy(void);

static uint32_t msc_flash_jedec(void)
{
    static uint32_t cached = 0xFFFFFFFF;                  /* 0xFFFFFFFF=未读 */
    if (cached != 0xFFFFFFFF) {
        return cached;
    }
    uint8_t id[3] = {0, 0, 0};
    norflash_mutex_enter();
    norflash_wait_busy();
    norflash_spi_cs(0);
    norflash_spi_write_byte(0x9F);                        /* JEDEC ID 读命令 */
    for (int i = 0; i < 3; i++) {
        id[i] = norflash_spi_read_byte();
    }
    norflash_spi_cs(1);
    norflash_mutex_exit();
    cached = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    return cached;                                        /* 容量 = 1 << id[2]: 0x16=4M 0x17=8M 0x18=16M */
}

/* 状态寄存器 SR1(0x05)+SR2(0x35): BP/CMP 保护位非 0 = 阵列(顶部)区域被写保护 */
static uint32_t msc_flash_status(void)
{
    uint8_t sr[2] = {0, 0};
    norflash_mutex_enter();
    norflash_wait_busy();
    norflash_spi_cs(0);
    norflash_spi_write_byte(0x05);
    sr[0] = norflash_spi_read_byte();
    norflash_spi_cs(1);
    norflash_wait_busy();
    norflash_spi_cs(0);
    norflash_spi_write_byte(0x35);
    sr[1] = norflash_spi_read_byte();
    norflash_spi_cs(1);
    norflash_mutex_exit();
    return ((uint32_t)sr[1] << 8) | sr[0];
}

static struct {
    uint8_t  busy;                       /* 升级进行中(不再响应新检测) */
    uint16_t data_crc;                   /* 头内数据 CRC16 */
    uint32_t file_size;                  /* 含 32B 头 */
    uint32_t first_sector;               /* 文件头所在扇区 */
    volatile uint32_t write_seq;         /* MSC 扇区写计数(静默判定) */
} msc_ota;

/* USB 中断上下文调用: 只计数, 不做任何校验(flash/printf 都不许碰) */
static void msc_update_on_write(uint32_t sector, const uint8_t *buffer, uint32_t length)
{
    (void)sector;
    (void)buffer;
    (void)length;
    msc_ota.write_seq++;
}

/* FAT16 固定根目录里找第一个有效 *.UFW 目录项(8.3), 返回首簇/大小 */
static int msc_ota_find_ufw(uint32_t *first_sector, uint32_t *size)
{
    for (uint32_t e = 0; e < MSC_ROOT_ENTRIES; e++) {
        const uint8_t *p = &msc_ram_disk[MSC_ROOT_START_SECTOR * 512U + e * 32U];
        if (p[0] == 0x00) {
            break;                       /* 目录区结束 */
        }
        if (p[0] == 0xE5 || p[11] == 0x0F || (p[11] & 0x08)) {
            continue;                    /* 已删除 / LFN 长名项 / 卷标 */
        }
        if (memcmp(p + 8, "UFW", 3) != 0) {
            continue;
        }
        uint32_t clu  = p[26] | ((uint32_t)p[27] << 8);
        *size = p[28] | ((uint32_t)p[29] << 8) | ((uint32_t)p[30] << 16) | ((uint32_t)p[31] << 24);
        if (clu >= 2 && *size >= 64U &&
            MSC_DATA_START_SECTOR + (clu - 2U) < MSC_RAM_SECTORS) {
            *first_sector = MSC_DATA_START_SECTOR + (clu - 2U);
            return 1;
        }
    }
    return 0;
}

/* 触发升级: RAM 盘按 512B 流式喂给 net_update(dual_bank 写备用 bank),
 * CRC 过才 net_fclose(0)(烧 boot info + 2s 后复位); CRC 错 net_fclose(1) 放弃 */
static void msc_ota_do_upgrade(void)
{
    printf("[msc-ota] file settled, size %u, flashing...\n", (unsigned)msc_ota.file_size);

    void *fd = net_fopen(CONFIG_UPGRADE_OTA_FILE_NAME, "w");
    if (!fd) {
        printf("[msc-ota] net_fopen fail\n");
        goto out;
    }

    uint16_t crc = 0;
    uint32_t total_sec = (msc_ota.file_size + 511U) / 512U;
    if (msc_ota.file_size < 64U || msc_ota.first_sector + total_sec > MSC_RAM_SECTORS) {
        printf("[msc-ota] bad size/range, drop\n");
        net_fclose(fd, 1);
        goto out;
    }
    uint8_t flushed = 0;                       /* 已成功落盘过(ctx 已建立, 可查目标地址/回读) */
    for (uint32_t i = 0; i < total_sec; i++) {
        uint8_t *sec = &msc_ram_disk[(msc_ota.first_sector + i) * 512U];
        uint32_t n = (i == total_sec - 1U) ?
                     (msc_ota.file_size - (total_sec - 1U) * 512U) : 512U;
        int wret = net_fwrite(fd, sec, (int)n, 0);
        if (wret != (int)n) {
            printf("[msc-ota] net_fwrite fail @%u ret=%d\n", (unsigned)i, wret);
            /* tgt=升级目标地址(写入运行bank=布局错; 备份bank才对)
             * rb=回读校验: -1=未到落盘, -2=读失败, 0=与源一致, 1=不一致 */
            uint32_t tgt = 0;
            int rb = -1, rbo = -1;
            unsigned rbf = 0, rbs = 0;
            if (flushed) {
                tgt = get_target_udate_addr();
                uint8_t rbbuf[64];
                const uint8_t *src = &msc_ram_disk[msc_ota.first_sector * 512U];
                uint32_t rn = dual_bank_update_read_data(0, rbbuf, 64);
                if (rn != 64) {
                    rb = -2;                          /* 回读失败 */
                } else {
                    rb = 0;                           /* 逐字节找首个差异 */
                    for (int k = 0; k < 64; k++) {
                        if (rbbuf[k] != src[k]) {
                            rb = 1;
                            rbo = k;
                            rbf = rbbuf[k];
                            rbs = src[k];
                            break;
                        }
                    }
                }
            }
            printf("[msc-ota] tgt=0x%08x rb=%d@%d f=%02x s=%02x jedec=%06x sr=%04x\n",
                   tgt, rb, rbo, rbf, rbs, (unsigned)msc_flash_jedec(),
                   (unsigned)msc_flash_status());
            net_fclose(fd, 1);
            goto out;
        }
        if (((i + 1U) * 512U) % 4096U == 0U) {
            flushed = 1;                    /* 满 4K 落盘成功, update ctx 必已建立 */
        }
        wdt_clear();
        /* CRC 覆盖载荷 [64B 双头之后, 文件尾), 与内层头 dataCrc 比对(包内约定) */
        crc = calc_crc16_with_init_val(crc, sec + (i ? 0U : 64U),
                                       (uint16_t)(n - (i ? 0U : 64U)));
    }

    if (crc != msc_ota.data_crc) {
        printf("[msc-ota] crc err: calc %04x != head %04x, drop (re-drag file)\n",
               crc, msc_ota.data_crc);
        net_fclose(fd, 1);               /* 不烧写不复位 */
        goto out;
    }

    printf("[msc-ota] crc ok, burn boot info, system will reset soon\n");
    net_fclose(fd, 0);                   /* 内部: 烧 boot info + 2s 后 system_reset */
    while (1) {                          /* 等复位, 不再回 MSC 服务 */
        wdt_clear();
        os_time_dly(10);
    }

out:
    msc_ota.busy = 0;                    /* 允许用户重新拖入(写盘会触发重扫) */
}

static void msc_ota_watch_task(void *arg)
{
    uint32_t quiet_ms = 0;
    uint32_t last_seq = msc_ota.write_seq;
    uint8_t  dirty = 0;                  /* 盘被写过(静默后需要扫描) */

    while (1) {
        os_time_dly(50);                 /* 500ms 周期 */
        if (msc_ota.busy) {
            continue;
        }
        if (msc_ota.write_seq != last_seq) {  /* 有新写入: 重新计时 */
            last_seq = msc_ota.write_seq;
            quiet_ms = 0;
            dirty = 1;
            continue;
        }
        if (!dirty) {
            continue;                    /* 盘没被写过, 不扫 */
        }
        quiet_ms += 500;
        if (quiet_ms < 3000U) {          /* 3s 静默判据 */
            continue;
        }
        dirty = 0;
        quiet_ms = 0;
        printf("[msc-ota] quiet after %u writes, scanning root dir\n",
               (unsigned)msc_ota.write_seq);

        /* 盘被写过且已静默: 扫 FAT 根目录找升级文件(任务上下文) */
        uint32_t first_sector, size;
        if (!msc_ota_find_ufw(&first_sector, &size)) {
            continue;                    /* 无升级文件(普通拷贝), 等下次写盘 */
        }
        JL_FILE_HEAD h;
        memcpy(&h, &msc_ram_disk[first_sector * 512U], sizeof(h));
        int head_ret = jl_file_head_valid_check(&h);
        printf("[msc-ota] .UFW @sec %u size %u head_ret=%d name=%.16s\n",
               (unsigned)first_sector, (unsigned)size, head_ret, h.szFileName);
        if (head_ret != 0 ||
            strncmp(h.szFileName, "update_data", 11) != 0) {
            printf("[msc-ota] .UFW head invalid, ignore\n");
            continue;
        }
        if (h.u32Length > size) {        /* 头声明长度不能超过目录项大小 */
            printf("[msc-ota] head len %u > file %u, ignore\n",
                   (unsigned)h.u32Length, (unsigned)size);
            continue;
        }
        /* 内层 app_core 头(流偏移 32B): 描述写镜像, 其 dataCrc = 镜像 CRC(包内约定),
         * 外层头的 dataCrc 不校验载荷 —— 2026-10-25 实测核对 */
        JL_FILE_HEAD h2;
        memcpy(&h2, &msc_ram_disk[first_sector * 512U + 32U], sizeof(h2));
        if (h2.u32Length != h.u32Length - 64U ||
            strncmp(h2.szFileName, "app_core", 8) != 0) {
            printf("[msc-ota] inner head bad: name=%.16s len=%u (want %u)\n",
                   h2.szFileName, (unsigned)h2.u32Length,
                   (unsigned)(h.u32Length - 64U));
            continue;
        }
        msc_ota.data_crc     = h2.u16DataCrc;
        msc_ota.file_size    = h.u32Length;
        msc_ota.first_sector = first_sector;
        printf("[msc-ota] update file @sec %u size %u payload-crc %04x\n",
               (unsigned)first_sector, (unsigned)msc_ota.file_size, msc_ota.data_crc);
        msc_ota.busy = 1;
        msc_ota_do_upgrade();            /* 成功路径不再返回(等复位) */
    }
}
#endif  /* TCFG_VIR_UPDATE_ENABLE */

#if MINI_USB_CDC_ENABLE
/* ---- 弱回调覆盖: DTR 状态(主机打开/关闭串口), intf0=CDC comm 接口 ---- */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    if (intf == 0) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x01) : (dtr_intf_mask & ~0x01);
        usbd_cdc_send_serial_state(busid, 0, dtr ? 0x0003 : 0x0000); /* DSR|DCD 跟随 DTR */
        printf("[cdc] DTR=%d\n", dtr);
    }
}
#endif

