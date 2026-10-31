#ifndef WATCH_BATTERY_H
#define WATCH_BATTERY_H

#include "typedef.h"

/* PA0: TP4054 CHRG(脚1) 外置上拉充满检测 — 充电中拉低, 充满/空闲为高
 * PA8: USB_DET — 1=插入充电器, 0=无 USB 5V
 * VBAT: 片内 AD_CH_VBAT(1/4 分压) → mV
 */
void watch_battery_init(void);

/* 电压 mV; 电量 0..100; USB 插入; 充满指示(USB 在位且 CHRG 释放) */
u16 watch_battery_voltage_mv(void);
u8  watch_battery_percent(void);
u8  watch_battery_usb_in(void);
u8  watch_battery_charge_full(void);
u8  watch_battery_charging(void);

#endif
