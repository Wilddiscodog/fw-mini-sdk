#ifndef SYS_POWER_H
#define SYS_POWER_H

#include "generic/typedef.h"

/* P33 软关机(真关机): 等电源键抬起后 power_set_soft_poweroff(0) */
void sys_power_poweroff(void);
void sys_power_poweroff_wait_powerkey_up(void);

/* 软关机前旁路 BT 跨核 bt_lock, 避免 enter_soft_hold 与 softIRQ 死锁 */
void sys_power_bt_lock_bypass(u8 on);

#endif
