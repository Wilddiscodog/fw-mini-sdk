#include "app_config.h"
#include "system/includes.h"
#include "walkie_priv.h"
#include "walkie_hub.h"
#include "net/http/http_cli.h"
#include "lwip/sockets.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============ datahub 归档客户端 ============
 * 挂接: walkie_msg.c 的 walkie_msg_send_task() 中,
 *       msg_save_tx_local() 之后加一行:
 *           walkie_hub_put_voice(s_tx_buf, s_tx_len, msg_id,
 *                                walkie_peer_name_by_ip(dip));
 *
 * 协议: POST http://HUB_IP/cgi-bin/hub?cmd=put&... body=PCM裸流
 *       hub 侧按 fmt=pcm&rate=16000 记录, get 时自动包 44 字节 WAV 头
 */

struct hub_put_job {
    u8   *buf;
    u32   len;
    char  url[288];
};

static volatile u8 s_hub_busy;

/* ============ hub 自动发现 (UDP 广播, 零配置) ============
 * 广播 "DISCOVER" → 3722, hub 应答 "DATAHUB <ip> <key>"
 * 找不到时回退宏 WALKIE_HUB_IP/WALKIE_HUB_KEY
 */
static char s_hub_ip[16];
static char s_hub_key[24];
static u32 s_disc_ms;

static int hub_discover(void)
{
    struct sockaddr_in dst, from;
    socklen_t flen;
    char buf[64];
    int s, i, n, ok = -1;
    int bcast = 1;
    struct timeval tv = { 0, 800000 };

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(3722);
    dst.sin_addr.s_addr = INADDR_BROADCAST;
    for (i = 0; i < 3 && ok; i++) {
        if (sendto(s, "DISCOVER", 8, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            break;
        }
        flen = sizeof(from);
        n = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
        if (n > 8 && memcmp(buf, "DATAHUB ", 8) == 0) {
            char *p1, *p2;
            buf[n] = '\0';
            p1 = buf + 8;
            p2 = strchr(p1, ' ');
            if (p2) {
                *p2 = '\0';
                p2++;
                snprintf(s_hub_ip, sizeof(s_hub_ip), "%s", p1);
                snprintf(s_hub_key, sizeof(s_hub_key), "%s", p2);
                ok = 0;
            }
        }
    }
    close(s);
    return ok;
}

static const char *hub_ip(void)
{
    u32 now = timer_get_ms();

    if (s_hub_ip[0]) {
        return s_hub_ip;
    }
    if ((u32)(now - s_disc_ms) > 60000) {   /* 节流: 至多每分钟尝试一次 */
        s_disc_ms = now;
        if (hub_discover() == 0) {
            printf("[walkie] hub found by discovery: %s\n", s_hub_ip);
        }
    }
    return s_hub_ip[0] ? s_hub_ip : WALKIE_HUB_IP;
}

static const char *hub_key(void)
{
    return s_hub_key[0] ? s_hub_key : WALKIE_HUB_KEY;
}

/* ============ HTTP GET 收集器: 整包 body 收进 RAM ============ */

struct hub_dl {
    u8  *buf;
    u32  cap;
    u32  len;
    int  ok;      /* 1=完成 0=进行中 -1=失败 */
};

static int hub_dl_cb(void *cctx, void *buf, unsigned int size, void *priv, httpin_status st)
{
    struct hub_dl *dl = (struct hub_dl *)priv;
    httpcli_ctx *ctx = (httpcli_ctx *)cctx;

    (void)ctx;
    switch (st) {
    case HTTPIN_PROGRESS:
        if (!dl->buf) {
            dl->cap = ctx->content_length > 0 ? (u32)ctx->content_length + 16 : 8192;
            dl->buf = malloc(dl->cap);
            if (!dl->buf) {
                dl->ok = -1;
                return -1;
            }
        }
        if (dl->len + size > dl->cap) {
            dl->ok = -1;
            return -1;
        }
        memcpy(dl->buf + dl->len, buf, size);
        dl->len += size;
        break;
    case HTTPIN_FINISHED:
        dl->ok = 1;
        break;
    case HTTPIN_ABORT:
    case HTTPIN_ERROR:
        dl->ok = -1;
        break;
    default:
        break;
    }
    return 0;
}

static int hub_get(const char *url, struct hub_dl *dl)
{
    httpcli_ctx ctx;
    httpin_error e;

    memset(dl, 0, sizeof(*dl));
    memset(&ctx, 0, sizeof(ctx));
    ctx.url             = url;
    ctx.cb              = hub_dl_cb;
    ctx.priv            = dl;
    ctx.timeout_millsec = WALKIE_HUB_TIMEOUT;
    ctx.connection      = "close";
    e = httpcli_get(&ctx);
    if (e != HERROR_OK || dl->ok != 1) {
        if (dl->buf) {
            free(dl->buf);
            dl->buf = NULL;
        }
        return -1;
    }
    return 0;
}

/* ============ 极简 JSON 取值(CGI 输出格式固定, 扁平对象) ============ */

static const char *jfind(const char *p, const char *end, const char *key)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *hit = strstr(p, pat);
    if (hit && end && hit >= end) {
        return NULL;
    }
    return hit;
}

static long jnum(const char *p, const char *end, const char *key, long def)
{
    const char *hit = jfind(p, end, key);
    return hit ? strtol(hit + strlen(key) + 3, NULL, 10) : def;
}

/* 取字符串值到 out(不处理转义; 值均为安全字符/UTF-8 原文) */
static int jstr(const char *p, const char *end, const char *key, char *out, int outlen)
{
    const char *hit = jfind(p, end, key);
    const char *s, *e;
    int n;

    out[0] = '\0';
    if (!hit) {
        return -1;
    }
    s = strchr(hit + strlen(key) + 3, '"');
    if (!s || (end && s >= end)) {
        return -1;
    }
    s++;
    e = strchr(s, '"');
    if (!e || (end && e > end)) {
        return -1;
    }
    n = (int)(e - s);
    if (n >= outlen) {
        n = outlen - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return 0;
}

/* ============ 名字 → peer ip(气泡/记录元数据用, 找不到返回 0) ============ */

static u32 hub_name_to_ip(const char *name)
{
    int i, n;
    u8 mac[6], alive;
    u32 ip;
    char nm[WALKIE_PEER_NAME_LEN];

    n = walkie_contact_count();
    for (i = 0; i < n; i++) {
        if (walkie_contact_info(i, mac, nm, sizeof(nm), &alive, &ip) == 0 &&
            strcmp(nm, name) == 0) {
            return ip;
        }
    }
    n = walkie_peer_count();
    for (i = 0; i < n; i++) {
        const struct walkie_peer *pr = walkie_peer_get(i);
        if (pr && pr->used && strcmp(pr->name, name) == 0) {
            return pr->ip;
        }
    }
    return 0;
}

/* ============ 上传(归档) ============ */

/* 把不安全字符编成 %XX (标识符只需 ASCII 安全集) */
static void urlenc(char *dst, int dstlen, const char *src)
{
    static const char *hex = "0123456789ABCDEF";
    char c;

    while ((c = *src++) && dstlen > 3) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            *dst++ = c;
            dstlen--;
        } else {
            *dst++ = '%';
            *dst++ = hex[(u8)c >> 4];
            *dst++ = hex[(u8)c & 15];
            dstlen -= 3;
        }
    }
    *dst = '\0';
}

u8 walkie_hub_idle(void)
{
    return !s_hub_busy;
}

static void hub_put_task(void *arg)
{
    struct hub_put_job *job = (struct hub_put_job *)arg;
    httpcli_ctx ctx;
    httpin_error e;

    printf("[walkie] hub put %u bytes ...\n", (unsigned)job->len);

    memset(&ctx, 0, sizeof(ctx));
    ctx.url            = job->url;
    ctx.post_data      = (const char *)job->buf;
    ctx.data_len       = (int)job->len;
    ctx.data_format    = "application/octet-stream";
    ctx.timeout_millsec = WALKIE_HUB_TIMEOUT;
    ctx.connection     = "close";

    e = httpcli_post(&ctx);
    /* 只看传输层结果; hub 业务错误会以 4xx 返回, 归档尽力而为不重试 */
    printf("[walkie] hub put %s (e=%d)\n", e == HERROR_OK ? "ok" : "fail", (int)e);

    free(job->buf);
    free(job);
    s_hub_busy = 0;
    while (1) {
        os_time_dly(1000);   /* 杰理线程不退出, 常驻空转 */
    }
}

int walkie_hub_put_voice(const void *pcm, u32 len, u16 msg_id, const char *to_name)
{
    struct hub_put_job *job;
    char enc_to[64];
    u32 dur_ms;

    if (!pcm || !len) {
        return -1;
    }
    if (!walkie_net_has_ip()) {
        printf("[walkie] hub: no ip, skip archive\n");
        return -2;
    }
    if (s_hub_busy) {
        printf("[walkie] hub: busy, skip archive\n");
        return -3;
    }

    job = malloc(sizeof(*job));
    if (!job) {
        return -4;
    }
    job->buf = malloc(len);
    if (!job->buf) {
        free(job);
        return -4;
    }
    memcpy(job->buf, pcm, len);
    job->len = len;

    urlenc(enc_to, sizeof(enc_to), to_name ? to_name : "");
    dur_ms = len / (WALKIE_PCM_RATE * 2 * WALKIE_PCM_CH / 1000); /* s16 */

    snprintf(job->url, sizeof(job->url),
             "http://%s:%d/cgi-bin/hub"
             "?cmd=put&key=%s"
             "&dev=%s&ch=audio&dtype=file&fmt=pcm"
             "&rate=%d&dur=%u&uid=%s-%u&txt=%s",
             hub_ip(), WALKIE_HUB_PORT, hub_key(),
             WALKIE_HUB_DEVID, WALKIE_PCM_RATE,
             (unsigned)dur_ms,
             WALKIE_HUB_DEVID, (unsigned)msg_id, enc_to);

    s_hub_busy = 1;
    if (thread_fork("wk_hub", 13, 3072, 0, 0, hub_put_task, job)) {
        printf("[walkie] hub: task fork fail\n");
        free(job->buf);
        free(job);
        s_hub_busy = 0;
        return -5;
    }
    return 0;
}

/* ============ 拉取漏收消息(中央历史 → 聊天气泡) ============ */

#define HUB_PULL_LIMIT   8      /* 单轮最多处理条数 */
#define HUB_PULL_MAX_B   (2 * 1024 * 1024)  /* 单条上限 2MB(≈64s PCM) */

static int hub_ack(u32 id)
{
    char url[192];
    struct hub_dl dl;

    snprintf(url, sizeof(url),
             "http://%s:%d/cgi-bin/hub?cmd=ack&key=%s"
             "&id=%u&ackdev=%s", hub_ip(), WALKIE_HUB_PORT, hub_key(),
             (unsigned)id, WALKIE_HUB_DEVID);
    return hub_get(url, &dl);   /* 应答内容无所谓, 成功即可 */
}

static void hub_pull_task(void *arg)
{
    char url[224];
    struct hub_dl ldl, gdl;
    int got = 0;

    (void)arg;
    os_time_dly(100);   /* 等 1s: 让 wifi/dhcp 完全就绪 */

    while (got < HUB_PULL_LIMIT) {
        /* 我的未读列表(发送者不限; 定向给别人由 txt 过滤) */
        snprintf(url, sizeof(url),
                 "http://%s:%d/cgi-bin/hub?cmd=list&key=%s"
                 "&dev=%s&ch=audio&unread=1&since=0&limit=4",
                 hub_ip(), WALKIE_HUB_PORT, hub_key(), WALKIE_HUB_DEVID);
        if (hub_get(url, &ldl)) {
            printf("[walkie] hub pull: list fail\n");
            break;
        }
        const char *p = strstr((const char *)ldl.buf, "\"rows\":[");
        if (!p) {
            free(ldl.buf);
            break;
        }

        const char *obj = strstr(p + 8, "{\"id\":");
        int handled = 0;
        while (obj && got < HUB_PULL_LIMIT) {
            const char *objend = strchr(obj, '}');
            if (!objend) {
                break;
            }
            long id = jnum(obj, objend, "id", 0);
            long bytes = jnum(obj, objend, "bytes", 0);
            char from[32], to[32];
            jstr(obj, objend, "devid", from, sizeof(from));
            jstr(obj, objend, "txt", to, sizeof(to));

            if (to[0] && strcmp(to, WALKIE_HUB_DEVID) != 0) {
                /* 发给别人的定向消息: 不取不 ack, 留给收件人 */
            } else if (bytes <= 0 || bytes > HUB_PULL_MAX_B) {
                printf("[walkie] hub pull: id=%u bad size %ld, ack+skip\n",
                       (unsigned)id, bytes);
                hub_ack((u32)id);
            } else {
                snprintf(url, sizeof(url),
                         "http://%s:%d/cgi-bin/hub?cmd=get&key=%s"
                         "&id=%ld&raw=1", hub_ip(), WALKIE_HUB_PORT, hub_key(), id);
                if (hub_get(url, &gdl) == 0 && gdl.len == (u32)bytes) {
                    /* 整包到手 → 走 REC 落盘, 聊天气泡出现 */
                    u32 off = 0;
                    while (off < gdl.len) {
                        u16 n = (gdl.len - off) > 512 ? 512 : (gdl.len - off);
                        walkie_rec_rx_pcm(gdl.buf + off, n);
                        off += n;
                    }
                    walkie_rec_rx_end(hub_name_to_ip(from));
                    hub_ack((u32)id);
                    got++;
                    printf("[walkie] hub pull: id=%u from %s ok (%uB)\n",
                           (unsigned)id, from, (unsigned)bytes);
                } else {
                    printf("[walkie] hub pull: id=%u download fail, retry next round\n",
                           (unsigned)id);
                }
                if (gdl.buf) {
                    free(gdl.buf);
                    gdl.buf = NULL;
                }
            }
            handled++;
            obj = strstr(objend, "{\"id\":");
        }

        free(ldl.buf);
        if (!handled || got >= HUB_PULL_LIMIT) {
            break;
        }
    }

    printf("[walkie] hub pull done: %d msg(s)\n", got);
    s_hub_busy = 0;
    while (1) {
        os_time_dly(1000);
    }
}

int walkie_hub_pull(void)
{
    if (!walkie_net_has_ip()) {
        return -2;
    }
    if (s_hub_busy) {
        return -3;
    }
    s_hub_busy = 1;
    if (thread_fork("wk_hub2", 13, 4096, 0, 0, hub_pull_task, NULL)) {
        s_hub_busy = 0;
        return -5;
    }
    return 0;
}
