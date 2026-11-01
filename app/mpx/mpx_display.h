/* app/mpx/mpx_display.h - Graphics 服务硬件侧(JD9853 EMI 8080 并口) */
#ifndef _MPX_DISPLAY_H_
#define _MPX_DISPLAY_H_

#include <stdint.h>

uint16_t mpx_display_width(void);
uint16_t mpx_display_height(void);
/* 上屏前把 CPU 写过的帧缓冲刷出 D-cache */
void mpx_display_flush_fb(void *pixels, uint32_t nbytes);

#endif /* _MPX_DISPLAY_H_ */
