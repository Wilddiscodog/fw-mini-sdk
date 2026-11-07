#ifndef WATCH_FALL_H
#define WATCH_FALL_H

#include "generic/typedef.h"

/* 摔倒提醒: 仅计步 App 内测试用, 退出 App 即停止 */
void watch_fall_init(void);
void watch_fall_alert_set(u8 on);
u8 watch_fall_alert_get(void);
void watch_fall_stop(void); /* 退出 App 时调用: 关检测并停播 */

#endif
