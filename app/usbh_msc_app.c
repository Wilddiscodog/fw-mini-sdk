/*
 * CherryUSB Host MSC 演示 —— U盘读写 (JL WL82 HUSB)
 *
 * 行为：U盘插入 → 枚举 → MSC 类连接 → 打印容量 → 读 0 号扇区打印
 * 依赖：usbh_core(内部创建 usbh 线程) + usbh_hub + usbh_msc + HC 移植层
 */
#include "usbh_core.h"
#include "usbh_msc.h"
#include "fw_version.h"
#define f_mount usbh_fatfs_mount
#define f_open usbh_fatfs_open
#define f_close usbh_fatfs_close
#define f_read usbh_fatfs_read
#define f_write usbh_fatfs_write
#include "ff.h"
#include "diskio.h"

extern unsigned int os_time_get(void);

#define JL82_HUSB_BASE 0x16000UL
#define MSC_BENCH_FILE_SIZE (1024U * 1024U)
#define MSC_BENCH_CHUNK_SIZE (64U * 1024U)
#define MSC_BENCH_TICKS_PER_SEC 100U
#include <stdio.h>
#include <string.h>

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t sector_buf[512];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_msc_bench_src[MSC_BENCH_CHUNK_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_msc_bench_dst[MSC_BENCH_CHUNK_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX FATFS g_msc_fatfs;
static struct usbh_msc *g_msc_disk;

static void msc_fill_pattern(uint8_t *buf, uint32_t offset, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(((offset + i) * 29U + 0x5AU) & 0xFFU);
    }
}

static void msc_print_rate(const char *operation, uint32_t bytes, uint32_t ticks)
{
    uint64_t rate_x100;

    if (!ticks) {
        printf("[usbh-msc] %s rate unavailable (below one tick)\n", operation);
        return;
    }

    rate_x100 = ((uint64_t)bytes * MSC_BENCH_TICKS_PER_SEC * 100U) /
                ((uint64_t)ticks * 1000000U);
    printf("[usbh-msc] %s speed: %u.%02u MB/s (%u ticks)\n",
           operation, (unsigned)(rate_x100 / 100U), (unsigned)(rate_x100 % 100U), ticks);
}

static int msc_benchmark_run(struct usbh_msc *msc_class)
{
    FIL file;
    FRESULT result;
    UINT transferred;
    uint32_t offset;
    uint32_t chunk_size;
    uint32_t start_ticks;
    uint32_t elapsed_ticks;
    uint32_t candidate;
    char path[20];
    int ret = -1;

    if (!msc_class || !msc_class->blocknum) {
        return -1;
    }

    g_msc_disk = msc_class;
    result = f_mount(&g_msc_fatfs, "2:", 1);
    if (result != FR_OK) {
        printf("[usbh-msc] FAT mount failed: %u; no write test performed\n", (unsigned)result);
        g_msc_disk = NULL;
        return -1;
    }

    candidate = (os_time_get() ^ msc_class->blocknum) % 10000000U;
    for (uint32_t attempt = 0; attempt < 128U; attempt++) {
        snprintf(path, sizeof(path), "2:H%07lu.BIN", (unsigned long)candidate);
        result = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
        if (result != FR_EXIST) {
            break;
        }
        candidate = (candidate + 1U) % 10000000U;
    }
    if (result != FR_OK) {
        printf("[usbh-msc] cannot create a new benchmark file: %u; existing files untouched\n",
               (unsigned)result);
        goto unmount;
    }

    printf("[usbh-msc] writing new file %s (%u bytes)\n", path, MSC_BENCH_FILE_SIZE);
    start_ticks = os_time_get();
    for (offset = 0; offset < MSC_BENCH_FILE_SIZE; offset += MSC_BENCH_CHUNK_SIZE) {
        chunk_size = MSC_BENCH_FILE_SIZE - offset;
        if (chunk_size > MSC_BENCH_CHUNK_SIZE) {
            chunk_size = MSC_BENCH_CHUNK_SIZE;
        }
        msc_fill_pattern(g_msc_bench_src, offset, chunk_size);
        result = f_write(&file, g_msc_bench_src, chunk_size, &transferred);
        if (result != FR_OK || transferred != chunk_size) {
            printf("[usbh-msc] file write failed: result=%u bytes=%u\n",
                   (unsigned)result, (unsigned)transferred);
            f_close(&file);
            goto unmount;
        }
    }
    result = f_close(&file);
    elapsed_ticks = os_time_get() - start_ticks;
    if (result != FR_OK) {
        printf("[usbh-msc] file close after write failed: %u\n", (unsigned)result);
        goto unmount;
    }
    msc_print_rate("write", MSC_BENCH_FILE_SIZE, elapsed_ticks);

    result = f_open(&file, path, FA_OPEN_EXISTING | FA_READ);
    if (result != FR_OK) {
        printf("[usbh-msc] benchmark file reopen failed: %u\n", (unsigned)result);
        goto unmount;
    }
    start_ticks = os_time_get();
    for (offset = 0; offset < MSC_BENCH_FILE_SIZE; offset += MSC_BENCH_CHUNK_SIZE) {
        chunk_size = MSC_BENCH_FILE_SIZE - offset;
        if (chunk_size > MSC_BENCH_CHUNK_SIZE) {
            chunk_size = MSC_BENCH_CHUNK_SIZE;
        }
        result = f_read(&file, g_msc_bench_dst, chunk_size, &transferred);
        if (result != FR_OK || transferred != chunk_size) {
            printf("[usbh-msc] file read failed: result=%u bytes=%u\n",
                   (unsigned)result, (unsigned)transferred);
            f_close(&file);
            goto unmount;
        }
        msc_fill_pattern(g_msc_bench_src, offset, chunk_size);
        if (memcmp(g_msc_bench_dst, g_msc_bench_src, chunk_size) != 0) {
            printf("[usbh-msc] benchmark verify failed at offset %u\n", offset);
            f_close(&file);
            goto unmount;
        }
    }
    result = f_close(&file);
    elapsed_ticks = os_time_get() - start_ticks;
    if (result != FR_OK) {
        printf("[usbh-msc] file close after read failed: %u\n", (unsigned)result);
        goto unmount;
    }
    msc_print_rate("read", MSC_BENCH_FILE_SIZE, elapsed_ticks);
    printf("[usbh-msc] benchmark verify ok; file retained: %s\n", path);
    ret = 0;

unmount:
    f_mount(NULL, "2:", 0);
    g_msc_disk = NULL;
    return ret;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return (pdrv == 2U && g_msc_disk && g_msc_disk->blocksize == 512U) ? 0U : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return disk_initialize(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 2U || !g_msc_disk || !count || sector >= g_msc_disk->blocknum ||
        count > g_msc_disk->blocknum - sector) {
        return RES_PARERR;
    }
    return usbh_msc_scsi_read10(g_msc_disk, sector, buff, count) < 0 ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 2U || !g_msc_disk || !count || sector >= g_msc_disk->blocknum ||
        count > g_msc_disk->blocknum - sector) {
        return RES_PARERR;
    }
    return usbh_msc_scsi_write10(g_msc_disk, sector, (uint8_t *)buff, count) < 0 ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 2U || !g_msc_disk) {
        return RES_NOTRDY;
    }
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = g_msc_disk->blocknum;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)g_msc_disk->blocksize;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

static void usbh_event(struct usbh_bus *busid_port, uint8_t event)
{
    (void)busid_port;
    switch (event) {
        case USBH_EVENT_DEVICE_CONNECTED:
            printf("[usbh] device connected\n");
            break;
        case USBH_EVENT_DEVICE_DISCONNECTED:
            printf("[usbh] device disconnected\n");
            break;
        case USBH_EVENT_DEVICE_CONFIGURED:
            printf("[usbh] device configured\n");
            break;
        default:
            break;
    }
}

/* MSC 类连接完成(枚举+set_config 之后)由栈回调, 在 usbh 线程上下文。
 * 必须先跑 usbh_msc_scsi_init(TUR/INQUIRY/READ CAPACITY): 它填 blocksize,
 * read10 的 CBW dDataLength = blocksize*nsectors 依赖此值——跳过它时
 * dDataLength=0, 设备按 MSC BOT 规范判相位错误, 在 CSW 阶段 STALL bulk-in
 * (即 V0.3.8 的 err=-8)。 */
void usbh_msc_run(struct usbh_msc *msc_class)
{
    int ret = usbh_msc_scsi_init(msc_class);
    if (ret < 0) {
        printf("[usbh-msc] scsi_init fail %d\n", ret);
        return;
    }

    printf("[usbh-msc] U-disk online: blocknum=%u blocksize=%u (%u MB)\n",
           msc_class->blocknum, msc_class->blocksize,
           (unsigned)((uint64_t)msc_class->blocknum * msc_class->blocksize / 1024 / 1024));

    ret = usbh_msc_scsi_read10(msc_class, 0, sector_buf, 1);
    if (ret < 0) {
        printf("[usbh-msc] read sector0 fail %d\n", ret);
        return;
    }
    printf("[usbh-msc] sector0 head:");
    for (int i = 0; i < 16; i++) {
        printf(" %02x", sector_buf[i]);
    }
    printf("\n");

    msc_benchmark_run(msc_class);
    printf("[usbh-msc] demo done, class=%p\n", msc_class);
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    printf("[usbh-msc] stop %p\n", msc_class);
}

void usbh_host_demo_start(void)
{
    printf("[usbh] initialize host on HUSB(USB1) fw=%s\n", FW_VERSION);
    printf("[usbh] host stack ready for CDC/HID/MSC class devices on the same bus\n");
    usbh_initialize(0, JL82_HUSB_BASE, usbh_event);
}

void usbh_msc_demo_start(void)
{
    usbh_host_demo_start();
}
