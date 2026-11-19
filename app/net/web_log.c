/*
 * 设备日志环 → 网页 /api/log 拉取
 * 写入端在 putbyte 过滤后调用, 读端 http 任务拷贝; 关中断保护索引。
 */
#include "app_config.h"
#include "web_log.h"
#include "asm/cpu.h"
#include "system/timer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WEB_LOG_SIZE   (24 * 1024)

static char s_buf[WEB_LOG_SIZE];
static volatile u32 s_seq;       /* 已写入总字节(单调) */
static volatile u32 s_len;       /* 环内有效长度 ≤ WEB_LOG_SIZE */

void web_log_putc(char c)
{
    u32 pos;

    local_irq_disable();
    pos = s_seq % WEB_LOG_SIZE;
    s_buf[pos] = c;
    s_seq++;
    if (s_len < WEB_LOG_SIZE) {
        s_len++;
    }
    local_irq_enable();
}

void web_log_puts(const char *s)
{
    if (!s) {
        return;
    }
    while (*s) {
        web_log_putc(*s++);
    }
}

u32 web_log_seq(void)
{
    return s_seq;
}

int web_log_copy(u32 *io_since, char *dst, int dst_len)
{
    u32 since, seq, len, start, avail, i, n;

    if (!io_since || !dst || dst_len < 2) {
        return 0;
    }

    local_irq_disable();
    seq = s_seq;
    len = s_len;
    since = *io_since;

    /* 环被覆盖: since 落后超过 len → 跳到仍可读起点 */
    if (seq > len && since < seq - len) {
        since = seq - len;
    }
    if (since > seq) {
        since = seq;
    }
    avail = seq - since;
    n = avail;
    if (n > (u32)(dst_len - 1)) {
        n = (u32)(dst_len - 1);
    }
    start = since;
    for (i = 0; i < n; i++) {
        dst[i] = s_buf[(start + i) % WEB_LOG_SIZE];
    }
    *io_since = since + n;
    local_irq_enable();

    dst[n] = '\0';
    return (int)n;
}

void web_log_printf(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    int n;

    if (!fmt) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n >= (int)sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    web_log_puts(line);
    if (n == 0 || line[n - 1] != '\n') {
        web_log_putc('\n');
    }
}
