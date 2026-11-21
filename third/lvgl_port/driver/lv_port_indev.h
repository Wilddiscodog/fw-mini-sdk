#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "generic/typedef.h"

void lv_port_indev_init(void);
/* block=1 息屏屏蔽触摸; block=0 亮屏恢复 */
void lv_port_indev_block(u8 block);

#endif
