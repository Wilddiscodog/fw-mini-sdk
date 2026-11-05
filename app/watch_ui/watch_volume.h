#ifndef WATCH_VOLUME_H
#define WATCH_VOLUME_H

#include "generic/typedef.h"

void watch_volume_init(void);
u8 watch_volume_get(void);
/* 仅改运行时音量, 不写 VM; 调用方在防抖后 watch_volume_persist */
void watch_volume_set(u8 vol);
void watch_volume_persist(void);

#endif
