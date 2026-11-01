/* 把官方 App Bundle 写到双 bank 代码区后面的空 flash，重启后还在。
 * 低 bank / 高 bank 各留前 2.5MB 给固件(当前镜像约 2.3MB)。目录在 0x280000。
 */
#ifndef _MPX_STORE_H_
#define _MPX_STORE_H_

#include <stdint.h>

#define MPX_STORE_ID_MAX 39

void mpx_store_set_busy(int busy);
int mpx_store_is_busy(void);

/* 把 id 交给 mpx_task。guest 正在跑或已有排队时返回 -1。 */
int mpx_store_request_launch(const char *id);
int mpx_store_take_launch(char *out, unsigned cap);

/* 读出 wasm 到 malloc 的缓冲。调用方 free。0 成功。 */
int mpx_store_load_wasm(const char *id, uint8_t **out, uint32_t *size_out);

/* 已安装的 id 列表, 一行一个。返回写入字节数。 */
int mpx_store_list(char *buf, unsigned cap);

/* 按顺序取第 index 个应用 id。cap 至少 MPX_STORE_ID_MAX+1。没有则返回 -1。 */
int mpx_store_app_at(unsigned index, char *id, unsigned cap);

/* 内嵌包里的启动 PNG。data 指向固件映像，不要 free。0 成功。 */
int mpx_store_launch_png(const char *id, const uint8_t **data, uint32_t *size,
                         uint32_t *width, uint32_t *height);

/* 当前正在跑的包。guest 退出后清掉。bundle 指向固件映像。 */
void mpx_store_use_bundle(const uint8_t *bundle, uint32_t size);
int mpx_store_live_png(uint32_t asset_id, const uint8_t **data, uint32_t *size,
                       uint32_t *width, uint32_t *height);

#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
int mpx_http_handle(int conn, char *req, int hdr_end, int got);
#endif

#endif
