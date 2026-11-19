#ifndef WEB_LOG_H
#define WEB_LOG_H

#include "generic/typedef.h"

/* 网页可读的设备日志环(与串口同源, 经 wifi_log_mute 过滤后写入) */

void web_log_putc(char c);
void web_log_puts(const char *s);

/* 单调序号: 每写入 1 字节 +1, 供网页 since= 增量拉取 */
u32 web_log_seq(void);

/* 从 *io_since 起拷贝新日志到 dst; 返回写入字节数(可含 \\0 前的原文)
 * *io_since 更新为本次已消费到的序号。环覆盖时自动跳到仍可读起点。
 */
int web_log_copy(u32 *io_since, char *dst, int dst_len);

/* hub/排障一行摘要 */
void web_log_printf(const char *fmt, ...);

#endif
