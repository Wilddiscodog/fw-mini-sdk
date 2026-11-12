#ifndef WALKIE_HUB_H
#define WALKIE_HUB_H

/* datahub (AR9331 家庭数据中枢) 归档客户端
 *   松键对讲消息发送后, 异步整包上传 hub 留档(尽力而为):
 *   - UDP 实时对讲路径完全不变, HTTP 失败只影响归档不影响通话
 *   - uid 幂等: 重传/重复上传 hub 只存一份
 *   - 二期: walkie_hub_poll() 从 hub 拉取漏收的消息补进聊天气泡
 */

#include "generic/typedef.h"

/* ===== 每台手表按需修改这里 ===== */
#define WALKIE_HUB_IP       "192.168.1.214"      /* AR9331 hub 的 IP */
#define WALKIE_HUB_KEY      "305ae3cae41093f3"   /* uci get datahub.hub.key */
#define WALKIE_HUB_DEVID    "watch-a1"           /* 本机在 hub 的设备ID(每台唯一) */
/* ================================ */

#define WALKIE_HUB_PORT     80
#define WALKIE_HUB_TIMEOUT  8000                  /* HTTP 超时 ms */

/* 上传一段已完成的 PCM 语音到 hub (audio 通道, 文件型记录)
 * pcm/len: 16kHz/mono/s16le 裸数据 (调用返回后即可复用缓冲, 内部已拷贝)
 * msg_id:  本地消息号, 与本机名拼成幂等 uid
 * to_name: 收件人名字 (存入 hub 的 txt 字段, 空串=广播); 可为 NULL
 * 返回 0=已受理(异步)  <0=未受理(没IP/忙/内存不足), 不影响对讲
 */
int walkie_hub_put_voice(const void *pcm, u32 len, u16 msg_id, const char *to_name);

/* 从 hub 拉取漏收的语音消息(后台线程, 与 put 共用一个工作位):
 *   list(dev=本机, unread) → 过滤收件人(txt=空或=本机) → get 整包 PCM
 *   → walkie_rec_rx_pcm/rx_end 落盘聊天气泡 → ack 标记已取
 *   定向给别人(txt=其他名字)的消息跳过且不 ack, 留给真正收件人
 * 建议挂接: 进聊天页 / WiFi 就绪 时各调一次
 * 返回 0=已启动  <0=没IP或忙
 */
int walkie_hub_pull(void);

/* 归档线程是否空闲 */
u8 walkie_hub_idle(void);

#endif
