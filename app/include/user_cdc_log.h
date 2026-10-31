#ifndef USER_CDC_LOG_H
#define USER_CDC_LOG_H

/*
 * CDC2 用户专用日志口(与系统 UART0 / printf 隔离)。
 * 仅当主机打开 CDC2(DTR) 时发送; 未连接或忙则丢弃/跳过。
 * USB 关闭时为空操作, 避免 watch_ui 在 bisect<2 链接失败。
 */
#if (defined MINI_USB_ENABLE) && MINI_USB_ENABLE
int user_cdc_puts(const char *s);
int user_cdc_printf(const char *fmt, ...);
#else
static inline int user_cdc_puts(const char *s)
{
	(void)s;
	return 0;
}
static inline int user_cdc_printf(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}
#endif

#endif
