#ifndef WALKIE_HUB_H
#define WALKIE_HUB_H

/* datahub 归档客户端 — 对接 AR9331 luci-app-datahub
 *   松键后异步归档(A-law + 限长 + 本地队列重试); UDP 实时对讲不受影响
 *   DEVID = walkie_self_name(); 联系人须填对方对讲名
 */

#include "generic/typedef.h"

/* ===== 兜底配置: UDP DISCOVER 失败时使用 ===== */
#define WALKIE_HUB_IP       "192.168.5.11"        /* 家庭网 AR9331 STA IP */
#define WALKIE_HUB_KEY      "495f66430e261791"    /* LuCI 数据中枢 key; DISCOVER 优先 */
#define WALKIE_HUB_PORT     80                   /* AR9331 uhttpd */
#define WALKIE_HUB_TIMEOUT  8000                 /* HTTP 超时 ms */
#define WALKIE_HUB_MAX_SEC  15                   /* 归档最长秒数(超出截断) */
#define WALKIE_HUB_Q_DEPTH  2                    /* 待发队列深度 */
#define WALKIE_HUB_PUT_RETRY 3                   /* 单条 put 最大尝试次数 */
/* ================================ */

/* 同步状态(聊天页状态栏用) — 仅反映本机「上传归档」, 不含后台 pull */
#define WALKIE_HUB_ST_IDLE     0   /* 空闲 */
#define WALKIE_HUB_ST_SYNCING  1   /* 正在上传 */
#define WALKIE_HUB_ST_PENDING  2   /* 有待上传 */
#define WALKIE_HUB_ST_FAIL     3   /* 最近上传失败且无待发 */

/* 上传一段 PCM: 内部截断→A-law→入队, 由常驻 worker 发送
 * 返回 0=已入队  <0=未受理(无网/内存/队列满挤掉后仍失败)
 */
int walkie_hub_put_voice(const void *pcm, u32 len, u16 msg_id, const char *to_name);

/* 请求拉取漏收消息(入队, 与 put 共用 worker) */
int walkie_hub_pull(void);

u8 walkie_hub_idle(void);
u8 walkie_hub_status(void);    /* WALKIE_HUB_ST_* */
u8 walkie_hub_pending(void);   /* 队列中未完成条数 */

/* 供曲库等复用: DISCOVER 优先, 否则宏兜底 */
const char *walkie_hub_ip(void);
const char *walkie_hub_key(void);
int walkie_hub_port(void);

/* 网页诊断: 填 JSON 片段到 buf, 返回写入长度 */
int walkie_hub_diag_json(char *buf, int buflen);
/* 丢弃未完成 put 并标失败(网页「清除待同步」) */
int walkie_hub_flush_pending(void);

#endif
