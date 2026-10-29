#ifndef SYS_POWER_H
#define SYS_POWER_H

/* P33 软关机(真关机): 等电源键抬起后 power_set_soft_poweroff(0) */
void sys_power_poweroff(void);
void sys_power_poweroff_wait_powerkey_up(void);

#endif
