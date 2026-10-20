/*
 * CherryUSB Host MSC 演示 —— U盘读写 (JL WL82 HUSB)
 *
 * 行为：U盘插入 → 枚举 → MSC 类连接 → 打印容量 → 读 0 号扇区打印
 * 依赖：usbh_core(内部创建 usbh 线程) + usbh_hub + usbh_msc + HC 移植层
 */
#include "usbh_core.h"
#include "usbh_msc.h"
#include "fw_version.h"

#define JL82_HUSB_BASE 0x16000UL
#include <stdio.h>
#include <string.h>

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t sector_buf[512];

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

/* MSC 类连接完成(scsi_init 之后)由栈回调, 在 usbh 线程上下文 */
void usbh_msc_run(struct usbh_msc *msc_class)
{
    printf("[usbh-msc] U-disk online: blocknum=%u blocksize=%u (%u MB)\n",
           msc_class->blocknum, msc_class->blocksize,
           (unsigned)((uint64_t)msc_class->blocknum * msc_class->blocksize / 1024 / 1024));

    int ret = usbh_msc_scsi_read10(msc_class, 0, sector_buf, 1);
    if (ret < 0) {
        printf("[usbh-msc] read sector0 fail %d\n", ret);
        return;
    }
    printf("[usbh-msc] sector0 head:");
    for (int i = 0; i < 16; i++) {
        printf(" %02x", sector_buf[i]);
    }
    printf("\n[usbh-msc] demo done, class=%p\n", msc_class);
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    printf("[usbh-msc] stop %p\n", msc_class);
}

void usbh_msc_demo_start(void)
{
    printf("[usbh] initialize host on HUSB(USB1) fw=%s\n", FW_VERSION);
    usbh_initialize(0, JL82_HUSB_BASE, usbh_event);
}
