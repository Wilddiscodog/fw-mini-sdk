/*
 * CherryUSB 组合设备端点探针 —— 2×CDC ACM + MSC(4MB SDRAM 盘)  (JL WL82 HUSB 高速)
 *
 * 目的: 用组合设备把 HUSB 设备模式的端点通道逐个点亮, 实测可用的通道总数。
 * 原厂 usb_epbuf_manager.c 注释的通道分组:
 *   "ep1/ep2/ep5/ep6 的 dma 地址需 64 字节对齐, ep3/ep4 需 8 对齐"
 * → 推测 EP1/2/5/6 为大 FIFO bulk 组, EP3/4 为小 FIFO(INT)组。
 *
 * 端点规划(EP 重分配: MSC 迁 EP3 双向大缓冲, CDC1 通知上 EP2):
 *   EP1(0x81 IN / 0x01 OUT) bulk 512 —— CDC1 数据   (已验证通道, 基准)
 *   EP2(0x82 IN)            int     —— CDC1 通知   (64B 组上的 INT 探针点)
 *   EP3(0x83 IN / 0x03 OUT) bulk 512 —— MSC, 双向大缓冲(各 4096B, bit14+0x16850)
 *   EP4(0x84 IN)            int     —— CDC2 通知   (bit14 + 大缓冲计数格式)
 *   EP5                     空闲备用
 *   EP6(0x86 IN / 0x06 OUT) bulk 512 —— CDC2 数据
 *
 * 判读(设备管理器/日志):
 *   - 枚举成功 + 2 串口 + 1 U盘(容量 4MB) 全部可用 → 设备模式 ≥7 通道(EP0~EP6)
 *   - 某通道失效(串口打不开/U盘读写失败/枚举失败) → 该 EP 即通道数边界
 *   - CDC1(EP1) 是基准: 它坏了说明问题不在端点数
 *   - 0x83/0x84 两个通知端点互为 A/B: 只有 0x83 出 clear-halt 循环 → EP3 硬件特有;
 *     两个都循环 → INT IN 系统性问题
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/*!< 端点地址(EP 重分配: MSC 上 EP3 双向大缓冲) */
#define CDC1_IN_EP   0x81
#define CDC1_OUT_EP  0x01
#define CDC1_INT_EP  0x82

#define CDC2_IN_EP   0x86
#define CDC2_OUT_EP  0x06
#define CDC2_INT_EP  0x84

#define MSC_IN_EP    0x81
#define MSC_OUT_EP   0x01

#define USBD_VID           0xFFFF
#define USBD_PID           0x0003
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< 5 个接口: CDC1(0 comm + 1 data) CDC2(2 comm + 3 data) MSC(4) */
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
    "WL82 MSC-only 4MB",           /* Product */
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

const struct usb_descriptor msc_only_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

/* ---- CDC 数据缓冲(RAM0 .usb_h_bss; 设备栈为 CPU 搬运, 位置不敏感, 跟随工程约定) ---- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc1_write_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc2_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc2_write_buffer[2048];

volatile bool cdc1_tx_busy = false;
volatile bool cdc2_tx_busy = false;
volatile uint8_t dtr_intf_mask = 0;   /* bit0: CDC1 DTR, bit2: CDC2 DTR */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    (void)event;
}

/* ---- CDC1(EP1, 基准通道) ---- */
void usbd_cdc1_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if (!cdc1_tx_busy) {
        memcpy(cdc1_write_buffer, cdc1_read_buffer, nbytes);
        cdc1_tx_busy = true;
        usbd_ep_start_write(busid, CDC1_IN_EP, cdc1_write_buffer, nbytes); /* 回显 */
    }
    usbd_ep_start_read(busid, CDC1_OUT_EP, cdc1_read_buffer, 2048);
}

void usbd_cdc1_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    if ((nbytes % 512) == 0 && nbytes) {
        usbd_ep_start_write(busid, CDC1_IN_EP, NULL, 0);   /* ZLP 收尾 */
    } else {
        cdc1_tx_busy = false;
    }
}

/* ---- CDC2(EP6, 探针通道) ---- */
void usbd_cdc2_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if (!cdc2_tx_busy) {
        memcpy(cdc2_write_buffer, cdc2_read_buffer, nbytes);
        cdc2_tx_busy = true;
        usbd_ep_start_write(busid, CDC2_IN_EP, cdc2_write_buffer, nbytes); /* 回显 */
    }
    usbd_ep_start_read(busid, CDC2_OUT_EP, cdc2_read_buffer, 2048);
}

void usbd_cdc2_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    if ((nbytes % 512) == 0 && nbytes) {
        usbd_ep_start_write(busid, CDC2_IN_EP, NULL, 0);
    } else {
        cdc2_tx_busy = false;
    }
}

/*!< 端点回调表 */
struct usbd_endpoint cdc1_out_ep = { .ep_addr = CDC1_OUT_EP, .ep_cb = usbd_cdc1_bulk_out };
struct usbd_endpoint cdc1_in_ep  = { .ep_addr = CDC1_IN_EP,  .ep_cb = usbd_cdc1_bulk_in };
struct usbd_endpoint cdc2_out_ep = { .ep_addr = CDC2_OUT_EP, .ep_cb = usbd_cdc2_bulk_out };
struct usbd_endpoint cdc2_in_ep  = { .ep_addr = CDC2_IN_EP,  .ep_cb = usbd_cdc2_bulk_in };

struct usbd_interface intf0; /* CDC1 comm */
struct usbd_interface intf1; /* CDC1 data */
struct usbd_interface intf2; /* CDC2 comm */
struct usbd_interface intf3; /* CDC2 data */
struct usbd_interface intf4; /* MSC */

/* ---- 4MB RAM 盘(8192 扇区 × 512B)。放 SDRAM .bss: RAM0(.usb_h_bss) 容量不足,
 * 且设备栈为 CPU FIFO 搬运, 对缓冲无 DMA 属性要求 ---- */
#define MSC_RAM_SECTORS  8192u
#define MSC_RAM_SIZE     (MSC_RAM_SECTORS * 512u)
static uint8_t msc_ram_disk[MSC_RAM_SIZE];

/* ---- FAT16 格式化(4MB 盘, 1 扇区/簇, 固定根目录) ---- */
static void msc_ram_put16(uint32_t off, uint16_t v)
{
    msc_ram_disk[off] = (uint8_t)v;
    msc_ram_disk[off + 1] = (uint8_t)(v >> 8);
}

static void msc_ram_mkfs16(void)
{
    const uint32_t fat_start_sector = 1U;
    const uint32_t fat_sectors = 32U;
    const uint32_t root_start_sector = fat_start_sector + 2U * fat_sectors;
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
    return 0;
}

void msc_only_init(uint8_t busid, uintptr_t reg_base)
{
    /* Prepare a valid FAT16 superfloppy image before exposing the LUN. */
    msc_ram_mkfs16();

    usbd_desc_register(busid, &msc_only_descriptor);
    usbd_add_interface(busid, usbd_msc_init_intf(busid, &intf4, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

/* ---- 弱回调覆盖: DTR 状态按接口号分流(intf0=CDC1, intf2=CDC2) ---- */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    if (intf == 0) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x01) : (dtr_intf_mask & ~0x01);
    } else if (intf == 2) {
        dtr_intf_mask = dtr ? (dtr_intf_mask | 0x04) : (dtr_intf_mask & ~0x04);
    }
    printf("[cdc] intf%u DTR=%d\n", intf, dtr);
}

