/*
 * CherryUSB 组合设备 —— CDC ACM(系统日志口) + MSC(4MB RAM 盘, 虚拟U盘自动升级)
 *
 * 端点规划(沿用 V0.4.x 实测结论):
 *   EP1(0x81 IN / 0x01 OUT) bulk 512 —— CDC 数据(日志口, 原基准通道)
 *   EP2(0x82 IN)            int     —— CDC 通知(不注册端点, 不发 SERIAL_STATE)
 *   EP3(0x83 IN / 0x03 OUT) bulk 512 —— MSC, 双向大缓冲(各 4096B, ep_open 自动)
 *
 * 日志链路(原厂 debug_user.c 机制): lib 逐字符回调 putbyte() → 镜像 UART0 +
 * 写 200K 环形 lg_buf → usb_log_task 100ms 增量 cdc_write_data() → 本口 CDC IN。
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
#include <string.h>
#include <stdbool.h>

#if TCFG_VIR_UPDATE_ENABLE
/* 虚拟U盘升级: 定义与任务在本文件尾部(见 msc_only_init 后的升级模块段) */
static void msc_update_on_write(uint32_t sector, const uint8_t *buffer, uint32_t length);
static void msc_ota_watch_task(void *arg);
#endif

/*!< 端点地址: CDC 数据 EP1 + 通知 EP2; MSC EP3 大缓冲 */
#define CDC1_IN_EP   0x81
#define CDC1_OUT_EP  0x01
#define CDC1_INT_EP  0x82

#define MSC_IN_EP    0x83
#define MSC_OUT_EP   0x03

#define USBD_VID           0xFFFF
#define USBD_PID           0x0003
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

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

const struct usb_descriptor cdc_msc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

/* ---- CDC 数据缓冲(RAM0 .usb_h_bss; 设备栈为 CPU 搬运, 位置不敏感, 跟随工程约定) ---- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_write_buffer[2048];

volatile bool cdc1_tx_busy = false;
volatile uint8_t dtr_intf_mask = 0;   /* bit0: CDC DTR(主机打开串口) */
volatile uint8_t cdc_configured = 0;  /* SET_CONFIGURATION 完成 */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        cdc_configured = 1;
        cdc1_tx_busy = false;
        usbd_ep_start_read(busid, CDC1_OUT_EP, cdc1_read_buffer, 2048);
        break;
    case USBD_EVENT_DISCONNECTED:
        cdc_configured = 0;
        break;
    default:
        break;
    }
}

/* ---- CDC(EP1): 日志输出口。主机发来的数据只接收丢弃(日志口无命令处理) ---- */
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

struct usbd_interface intf0; /* CDC comm */
struct usbd_interface intf1; /* CDC data */
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

void cdc_msc_init(uint8_t busid, uintptr_t reg_base)
{
    /* Prepare a valid FAT16 superfloppy image before exposing the LUN. */
    msc_ram_mkfs16();

    usbd_desc_register(busid, &cdc_msc_descriptor);

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc1_out_ep);
    usbd_add_endpoint(busid, &cdc1_in_ep);
    usbd_add_interface(busid, usbd_msc_init_intf(busid, &intf_msc, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);

#if TCFG_VIR_UPDATE_ENABLE
    /* 虚拟U盘升级看门: 500ms 轮询, 文件静默 3s + FAT 目录项就绪后触发升级 */
    os_task_create(msc_ota_watch_task, NULL, 9, 1024, 0, "msc_ota");
#endif
}

/* ---- CDC 日志输出口: 原厂 debug_user.c usb_log_task 调用(usb_id 忽略, 单口)
 *     512B 分块 + 完成等待; 串口未打开(DTR=0)/未配置/写超时 → 丢弃返回 len */
uint32_t cdc_write_data(int usb_id, uint8_t *buf, uint32_t len)
{
    (void)usb_id;
    if (!cdc_configured || !(dtr_intf_mask & 0x01)) {
        return len;    /* 主机未打开串口: 丢弃, 与原厂行为一致 */
    }
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t n = (len - sent > 512U) ? 512U : (len - sent);
        if (cdc1_tx_busy) {                     /* 上次写未完成(不应发生, 防御) */
            return sent;
        }
        memcpy(cdc1_write_buffer, buf + sent, n);
        cdc1_tx_busy = true;
        usbd_ep_start_write(0, CDC1_IN_EP, cdc1_write_buffer, n);
        int wait = 0;
        while (cdc1_tx_busy && ++wait < 500) {  /* 5s 超时(拔线保护) */
            os_time_dly(1);
        }
        if (cdc1_tx_busy) {
            cdc1_tx_busy = false;
            return sent;
        }
        sent += n;
    }
    return sent;
}

/*============================ 虚拟U盘自动升级(双备份) ============================
 * 原厂对应: apps/common/usb/device/ram_disk_update.c(扇区流拦截) —— 本工程把
 * "边拷边写 flash"改为"拷贝完整落盘后再升级"(用户需求: 文件 3s 大小不变即认为
 * 写完), 触发时机更稳(Windows 目录项已收敛), flash 写入链路仍走原厂
 * net_fopen/net_fwrite/net_fclose(dual_bank 双备份烧写 + 2s 后自动复位)。
 *
 * 检测: 主机每写 RAM 盘若干扇区, 逐扇区找 JL_FILE_HEAD(szFileName=="update_data",
 * isd_download 生成的 db_update_files_data.bin 即该格式, 32B 头);
 * 触发: 3s 无任何扇区写 + FAT16 根目录出现 *.UFW 目录项且首簇/大小与检测到的
 * 流一致 → 按 512B 流式过 CRC16 校验 → net_fwrite 写入 → 烧 boot info → 复位。 */
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

extern int jl_file_head_valid_check(void *head);             /* update.a */
extern uint16_t calc_crc16_with_init_val(uint16_t init_crc, uint8_t *ptr, uint16_t len);

static struct {
    uint8_t  detected;                   /* 已在扇区流中看到升级文件头 */
    uint8_t  busy;                       /* 升级进行中(不再响应新检测) */
    uint16_t data_crc;                   /* 头内数据 CRC16 */
    uint32_t file_size;                  /* 含 32B 头 */
    uint32_t first_sector;               /* 文件头所在扇区 */
    volatile uint32_t write_seq;         /* MSC 扇区写计数(静默判定) */
} msc_ota;

static void msc_update_on_write(uint32_t sector, const uint8_t *buffer, uint32_t length)
{
    msc_ota.write_seq++;
    if (msc_ota.busy || msc_ota.detected) {
        return;
    }
    /* 主机一次可写多扇区(EP 大缓冲), 逐 512B 找升级文件头 */
    for (uint32_t off = 0; off + 512U <= length; off += 512U) {
        JL_FILE_HEAD h;
        memcpy(&h, buffer + off, sizeof(h));
        if (jl_file_head_valid_check(&h) == 0 &&
            strncmp(h.szFileName, "update_data", 11) == 0) {
            msc_ota.detected     = 1;
            msc_ota.data_crc     = h.u16DataCrc;
            msc_ota.file_size    = h.u32Length;
            msc_ota.first_sector = sector + off / 512U;
            printf("[msc-ota] head @sec %u size %u crc %04x\n",
                   (unsigned)msc_ota.first_sector, (unsigned)msc_ota.file_size,
                   msc_ota.data_crc);
            break;
        }
    }
}

/* FAT16 固定根目录里找 *.UFW 目录项, 首簇/大小须与扇区流检测一致 */
static int msc_ota_root_entry_ok(void)
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
        uint32_t size = p[28] | ((uint32_t)p[29] << 8) | ((uint32_t)p[30] << 16) | ((uint32_t)p[31] << 24);
        if (clu >= 2 && MSC_DATA_START_SECTOR + (clu - 2U) == msc_ota.first_sector &&
            size == msc_ota.file_size) {
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

    uint32_t total_sec = (msc_ota.file_size + 511U) / 512U;
    if (msc_ota.file_size < 64U || msc_ota.first_sector + total_sec > MSC_RAM_SECTORS) {
        printf("[msc-ota] bad size/range, drop\n");
        net_fclose(fd, 1);
        goto out;
    }

    uint16_t crc = 0;
    for (uint32_t i = 0; i < total_sec; i++) {
        uint8_t *sec = &msc_ram_disk[(msc_ota.first_sector + i) * 512U];
        uint32_t n = (i == total_sec - 1U) ?
                     (msc_ota.file_size - (total_sec - 1U) * 512U) : 512U;
        if (net_fwrite(fd, sec, (int)n, 0) != (int)n) {
            printf("[msc-ota] net_fwrite fail @%u\n", (unsigned)i);
            net_fclose(fd, 1);
            goto out;
        }
        wdt_clear();
        /* CRC 覆盖 [32B 头之后, 文件尾), 与原厂 ram_disk_update 一致 */
        crc = calc_crc16_with_init_val(crc, sec + (i ? 0U : 32U),
                                       (uint16_t)(n - (i ? 0U : 32U)));
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
    msc_ota.detected = 0;                /* 允许用户重新拖入 */
    msc_ota.busy = 0;
}

static void msc_ota_watch_task(void *arg)
{
    uint32_t quiet_ms = 0;
    uint32_t last_seq = msc_ota.write_seq;

    while (1) {
        os_time_dly(50);                 /* 500ms 周期 */
        if (!msc_ota.detected || msc_ota.busy) {
            quiet_ms = 0;
            last_seq = msc_ota.write_seq;
            continue;
        }
        if (msc_ota.write_seq != last_seq) {  /* 拷贝仍在进行 */
            last_seq = msc_ota.write_seq;
            quiet_ms = 0;
            continue;
        }
        quiet_ms += 500;
        if (quiet_ms < 3000U) {          /* 3s 静默判据 */
            continue;
        }
        if (!msc_ota_root_entry_ok()) {  /* 目录项未收敛(拷贝未真正结束) */
            if (quiet_ms >= 60000U) {    /* 60s 仍未就绪: 放弃本次, 需重新拖入 */
                printf("[msc-ota] dir entry never settled, drop\n");
                msc_ota.detected = 0;
                quiet_ms = 0;
            }
            continue;
        }
        msc_ota.busy = 1;
        msc_ota_do_upgrade();            /* 成功路径不再返回(等复位) */
        quiet_ms = 0;
    }
}
#endif  /* TCFG_VIR_UPDATE_ENABLE */

/* ---- 弱回调覆盖: DTR 状态(主机打开/关闭串口), intf0=CDC comm 接口 ---- */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    if (intf == 0) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x01) : (dtr_intf_mask & ~0x01);
        printf("[cdc] DTR=%d\n", dtr);
    }
}

