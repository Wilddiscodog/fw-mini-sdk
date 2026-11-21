#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "walkie_priv.h"
#include "walkie.h"
#include "walkie_hub.h"
#include "net/http/http_cli.h"
#include "lwip/sockets.h"
#include "lwip.h"
#include "wifi/wifi_connect.h"
#include "web_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============ datahub 归档客户端 (AR9331 luci-app-datahub) ============
 * A-law 压缩 + 限长 + 本地队列 + 常驻 worker + 失败重试
 * put:  fmt=alaw&rate=16000; pull raw=1 后按 fmt 解码再落盘
 */

enum {
    HUB_JOB_PUT = 1,
    HUB_JOB_PULL = 2,
};

struct hub_job {
    u8   used;
    u8   kind;
    u8   retries;
    u16  msg_id;
    char to[WALKIE_PEER_NAME_LEN];
    char fmt[8];     /* 上传时填写; put 入队时为空=worker 内再压 alaw */
    u8  *buf;        /* put: 裸 PCM s16le (在 worker 里编码), 非已压缩 */
    u32  len;
    u32  dur_ms;
};

static OS_SEM   s_hub_sem;
static OS_MUTEX s_hub_mtx;
static volatile u8 s_hub_inited;
static volatile u8 s_hub_worker;
static volatile u8 s_hub_working;      /* put worker 忙 */
static volatile u8 s_hub_pull_busy;    /* pull 独立线程忙 */
static volatile u8 s_hub_pull_abort;   /* put 到来时打断 pull */
static volatile u8 s_hub_work_kind;    /* HUB_JOB_* while put working */
static volatile u8 s_hub_st = WALKIE_HUB_ST_IDLE;
static volatile u8 s_hub_last_fail;    /* 仅 put 失败 */
static volatile u32 s_hub_put_ok;
static volatile u32 s_hub_put_fail;
static volatile u32 s_hub_last_rc;     /* 最近 put 返回码 */
static volatile u32 s_hub_pending_since_ms;
static volatile u16 s_hub_last_msg_id;
static struct hub_job s_q[WALKIE_HUB_Q_DEPTH];

/* ============ hub 自动发现 (UDP 广播) ============
 * 手表广播 DISCOVER → AR9331 hub-announce 应答 DATAHUB <ip> <key>
 * 先发现、后兜底; 热路径可跳过已缓存地址
 */

static char s_hub_ip[16];
static char s_hub_key[24];
static u32 s_disc_ms;
static u8  s_hub_from_disc; /* 1=来自 DISCOVER */

static u32 hub_local_ip(void)
{
    char ip[16];

    Get_IPAddress(WIFI_NETIF, ip);
    return inet_addr(ip);
}

static int hub_parse_announce(char *buf, int n)
{
    char *p1, *p2;

    if (n <= 8 || memcmp(buf, "DATAHUB ", 8) != 0) {
        return -1;
    }
    buf[n] = '\0';
    p1 = buf + 8;
    p2 = strchr(p1, ' ');
    if (!p2 || p2 == p1) {
        return -1;
    }
    *p2 = '\0';
    p2++;
    if (!p1[0] || !p2[0]) {
        return -1;
    }
    snprintf(s_hub_ip, sizeof(s_hub_ip), "%s", p1);
    snprintf(s_hub_key, sizeof(s_hub_key), "%s", p2);
    s_hub_from_disc = 1;
    return 0;
}

static int hub_discover(void)
{
    struct sockaddr_in dst, from, bind_addr;
    socklen_t flen;
    char buf[96];
    int s, i, n, ok = -1;
    int bcast = 1;
    u32 t0, lip, subnet_bc;
    u32 targets[2];
    int nt = 0;

    lip = hub_local_ip();
    if (!lip || lip == 0xFFFFFFFFu) {
        printf("[walkie] hub discover: no local IP yet\n");
        web_log_printf("[hub] discover skip: no IP");
        return -1;
    }

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("[walkie] hub discover: socket fail\n");
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(0);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    (void)bind(s, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    /* 255.255.255.255 + 同网段广播, 提高跨 AP 命中率 */
    targets[nt++] = INADDR_BROADCAST;
    subnet_bc = (lip & 0x00FFFFFFu) | 0xFF000000u;
    if (subnet_bc != INADDR_BROADCAST) {
        targets[nt++] = subnet_bc;
    }

    printf("[walkie] hub discover: probing udp/3722 …\n");
    web_log_printf("[hub] discover probing 3722");

    /* MSG_DONTWAIT 轮询; 多轮给 hub-announce/socat 启动留时间 */
    for (i = 0; i < 4 && ok; i++) {
        int t;
        for (t = 0; t < nt; t++) {
            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port = htons(3722);
            dst.sin_addr.s_addr = targets[t];
            (void)sendto(s, "DISCOVER", 8, 0,
                         (struct sockaddr *)&dst, sizeof(dst));
        }
        t0 = timer_get_ms();
        while ((u32)(timer_get_ms() - t0) < 500) {
            flen = sizeof(from);
            n = recvfrom(s, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                         (struct sockaddr *)&from, &flen);
            if (n > 0 && hub_parse_announce(buf, n) == 0) {
                ok = 0;
                break;
            }
            os_time_dly(1);
        }
    }
    close(s);
    if (ok) {
        printf("[walkie] hub discover: no reply\n");
        web_log_printf("[hub] discover miss");
    }
    return ok;
}

/* 仅读缓存; 空则写入编译期兜底 (不发起 DISCOVER) */
static const char *hub_ip(void)
{
    if (s_hub_ip[0]) {
        return s_hub_ip;
    }
    snprintf(s_hub_ip, sizeof(s_hub_ip), "%s", WALKIE_HUB_IP);
    s_hub_from_disc = 0;
    printf("[walkie] hub use fallback ip %s\n", s_hub_ip);
    web_log_printf("[hub] fallback ip %s", s_hub_ip);
    return s_hub_ip;
}

/* 广播发现; force=1 忽略节流. 失败保留旧 IP, 若无旧 IP 则兜底 */
static void hub_discover_try(int force)
{
    u32 now = timer_get_ms();
    char old_ip[16];
    char old_key[24];
    u8 old_disc;

    if (!force && s_disc_ms && (u32)(now - s_disc_ms) < 15000) {
        return;
    }
    s_disc_ms = now ? now : 1;

    snprintf(old_ip, sizeof(old_ip), "%s", s_hub_ip);
    snprintf(old_key, sizeof(old_key), "%s", s_hub_key);
    old_disc = s_hub_from_disc;

    if (hub_discover() == 0) {
        printf("[walkie] hub found by discovery: %s key=%s\n",
               s_hub_ip, s_hub_key[0] ? s_hub_key : "(empty)");
        web_log_printf("[hub] discovered %s", s_hub_ip);
        return;
    }

    if (old_ip[0]) {
        snprintf(s_hub_ip, sizeof(s_hub_ip), "%s", old_ip);
        snprintf(s_hub_key, sizeof(s_hub_key), "%s", old_key);
        s_hub_from_disc = old_disc;
        printf("[walkie] hub discover miss, keep %s\n", s_hub_ip);
        web_log_printf("[hub] discover miss keep %s", s_hub_ip);
    } else {
        (void)hub_ip();
    }
}

/* 需要通信前调用: 优先 DISCOVER, 再兜底 */
static void hub_ensure_addr(int force_disc)
{
    if (force_disc || !s_hub_ip[0] || !s_hub_from_disc) {
        hub_discover_try(force_disc || !s_hub_ip[0]);
    }
    if (!s_hub_ip[0]) {
        (void)hub_ip();
    }
}

static const char *hub_key(void)
{
    return s_hub_key[0] ? s_hub_key : WALKIE_HUB_KEY;
}

static const char *hub_devid(void)
{
    return walkie_self_name();
}

/* 失败后清缓存, 下次强制再 DISCOVER */
static void hub_forget(void)
{
    printf("[walkie] hub forget %s (was %s)\n",
           s_hub_ip[0] ? s_hub_ip : "-",
           s_hub_from_disc ? "discovered" : "fallback");
    web_log_printf("[hub] forget %s", s_hub_ip[0] ? s_hub_ip : "-");
    s_hub_ip[0] = '\0';
    s_hub_key[0] = '\0';
    s_hub_from_disc = 0;
    s_disc_ms = 0;
}

/* ============ G.711 A-law (ITU) ============ */

static const s16 s_alaw_seg_end[8] = {
    0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF
};

static u8 linear_to_alaw(s16 pcm)
{
    int mask, seg, aval;
    int pcm_val = pcm;

    if (pcm_val >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm_val = -pcm_val - 1;
        if (pcm_val < 0) {
            pcm_val = 0;
        }
    }
    if (pcm_val > 32635) {
        pcm_val = 32635;
    }
    pcm_val >>= 3;
    for (seg = 0; seg < 8; seg++) {
        if (pcm_val <= s_alaw_seg_end[seg]) {
            break;
        }
    }
    if (seg >= 8) {
        seg = 7;
    }
    if (seg < 2) {
        aval = (seg << 4) | ((pcm_val >> 1) & 0x0F);
    } else {
        aval = (seg << 4) | ((pcm_val >> seg) & 0x0F);
    }
    return (u8)(aval ^ mask);
}

static s16 alaw_to_linear(u8 a_val)
{
    int t, seg;

    a_val ^= 0x55;
    t = (a_val & 0x0F) << 4;
    seg = (a_val & 0x70) >> 4;
    switch (seg) {
    case 0:
        t += 8;
        break;
    case 1:
        t += 0x108;
        break;
    default:
        t += 0x108;
        t <<= (seg - 1);
        break;
    }
    return (a_val & 0x80) ? (s16)t : (s16)(-t);
}

static u8 *hub_encode_alaw(const s16 *pcm, u32 samples, u32 *out_len)
{
    u8 *dst;
    u32 i;

    dst = malloc(samples);
    if (!dst) {
        return NULL;
    }
    for (i = 0; i < samples; i++) {
        dst[i] = linear_to_alaw(pcm[i]);
    }
    *out_len = samples;
    return dst;
}

static u8 *hub_decode_alaw(const u8 *src, u32 src_len, u32 *out_len)
{
    s16 *dst;
    u32 i;

    dst = (s16 *)malloc(src_len * 2);
    if (!dst) {
        return NULL;
    }
    for (i = 0; i < src_len; i++) {
        dst[i] = alaw_to_linear(src[i]);
    }
    *out_len = src_len * 2;
    return (u8 *)dst;
}

/* ============ HTTP GET ============ */

struct hub_dl {
    u8  *buf;
    u32  cap;
    u32  len;
    int  ok;
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

/* ============ 极简 JSON ============ */

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

/* ============ 原始 TCP POST(无 Expect) ============ */

/* 用官方 httpcli_post: 本平台 SO_RCVTIMEO 常失效, 自写 recv 会立刻得到空响应 → http=-1 */
static int hub_put_raw(const char *url, const u8 *body, u32 body_len)
{
    httpcli_ctx ctx;
    struct hub_dl dl;
    httpin_error e;
    char snip[96];
    int n;

    if (!url || !body || !body_len) {
        return -1;
    }

    memset(&dl, 0, sizeof(dl));
    memset(&ctx, 0, sizeof(ctx));
    ctx.url             = url;
    ctx.cb              = hub_dl_cb;
    ctx.priv            = &dl;
    ctx.timeout_millsec = WALKIE_HUB_TIMEOUT;
    ctx.connection      = "close";
    ctx.post_data       = (const char *)body;
    ctx.data_len        = (int)body_len;
    ctx.data_format     = "application/octet-stream";

    e = httpcli_post(&ctx);

    snip[0] = '\0';
    if (dl.buf && dl.len) {
        n = (int)dl.len;
        if (n > (int)sizeof(snip) - 1) {
            n = (int)sizeof(snip) - 1;
        }
        memcpy(snip, dl.buf, (size_t)n);
        snip[n] = '\0';
    }

    printf("[walkie] hub put httpcli e=%d ok=%d body=[%s]\n",
           (int)e, dl.ok, snip);
    web_log_printf("[hub] httpcli e=%d ok=%d | %s", (int)e, dl.ok, snip);

    if (dl.buf) {
        free(dl.buf);
        dl.buf = NULL;
    }

    if (e == HERROR_OK && dl.ok == 1) {
        return 0;
    }
    if (strstr(snip, "\"ok\":1")) {
        return 0;
    }
    return (e == HERROR_OK) ? -8 : (int)e;
}

/* ============ 队列 / 状态 / worker ============ */

static u8 hub_q_count(void)
{
    int i;
    u8 n = 0;

    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (s_q[i].used) {
            n++;
        }
    }
    return n;
}

/* 仅统计待上传; pull 不进状态栏, 避免接收端进聊天拉漏收时一直「同步中」 */
static u8 hub_q_put_count(void)
{
    int i;
    u8 n = 0;

    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (s_q[i].used && s_q[i].kind == HUB_JOB_PUT) {
            n++;
        }
    }
    return n;
}

static void hub_refresh_st(void)
{
    u8 nput = hub_q_put_count();
    u8 putting = s_hub_working ? 1 : 0;
    u32 now = timer_get_ms();

    /* 状态栏只反映上传; pull 在独立线程, 不再造成「待同步」假死 */
    if (putting) {
        s_hub_st = WALKIE_HUB_ST_SYNCING;
        s_hub_pending_since_ms = 0;
    } else if (nput) {
        if (s_hub_st != WALKIE_HUB_ST_PENDING) {
            s_hub_pending_since_ms = now ? now : 1;
        }
        s_hub_st = WALKIE_HUB_ST_PENDING;
    } else if (s_hub_last_fail) {
        s_hub_st = WALKIE_HUB_ST_FAIL;
        s_hub_pending_since_ms = 0;
    } else {
        s_hub_st = WALKIE_HUB_ST_IDLE;
        s_hub_pending_since_ms = 0;
    }
}

static void hub_job_free(struct hub_job *j)
{
    if (j->buf) {
        free(j->buf);
        j->buf = NULL;
    }
    memset(j, 0, sizeof(*j));
}

/* 待同步卡住: 踢醒 worker; 超过 45s 仍有 put 且不在上传 → 丢弃并标失败 */
static void hub_watchdog(void)
{
    u32 now, age;
    int i, dropped = 0;

    if (s_hub_st != WALKIE_HUB_ST_PENDING || !s_hub_pending_since_ms) {
        return;
    }
    now = timer_get_ms();
    age = (u32)(now - s_hub_pending_since_ms);

    /* >3s 仍排队: 可能丢了 sem, 每 2s 踢一次 */
    if (age > 3000 && s_hub_inited && !s_hub_working) {
        static u32 s_last_kick_ms;
        if (!s_last_kick_ms || (u32)(now - s_last_kick_ms) > 2000) {
            s_last_kick_ms = now;
            os_sem_post(&s_hub_sem);
            web_log_printf("[hub] kick worker age=%ums pending=%u",
                           (unsigned)age, (unsigned)hub_q_put_count());
        }
    }
    if (age < 45000) {
        return;
    }
    if (s_hub_working) {
        return; /* pull/put 进行中, 再等等 */
    }

    os_mutex_pend(&s_hub_mtx, 0);
    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (s_q[i].used && s_q[i].kind == HUB_JOB_PUT) {
            printf("[walkie] hub watchdog drop put id=%u age=%ums\n",
                   (unsigned)s_q[i].msg_id, (unsigned)age);
            web_log_printf("[hub] watchdog drop put id=%u age=%ums",
                           (unsigned)s_q[i].msg_id, (unsigned)age);
            hub_job_free(&s_q[i]);
            dropped++;
        }
    }
    if (dropped) {
        s_hub_last_fail = 1;
        s_hub_put_fail += (u32)dropped;
    }
    hub_refresh_st();
    os_mutex_post(&s_hub_mtx);
}

static int hub_q_push(struct hub_job *src)
{
    int i, slot = -1;

    os_mutex_pend(&s_hub_mtx, 0);
    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (!s_q[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* 队列满: 丢掉最旧的 put, 给新消息让路 */
        for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
            if (s_q[i].used && s_q[i].kind == HUB_JOB_PUT) {
                printf("[walkie] hub q full, drop oldest put id=%u\n",
                       (unsigned)s_q[i].msg_id);
                hub_job_free(&s_q[i]);
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        os_mutex_post(&s_hub_mtx);
        return -1;
    }
    s_q[slot] = *src;
    s_q[slot].used = 1;
    hub_refresh_st();
    os_mutex_post(&s_hub_mtx);
    os_sem_post(&s_hub_sem);
    return 0;
}

/* put worker 只取上传任务; pull 已改独立线程 */
static int hub_q_take_put(struct hub_job *out)
{
    int i, got = 0;

    os_mutex_pend(&s_hub_mtx, 0);
    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (s_q[i].used && s_q[i].kind == HUB_JOB_PUT) {
            *out = s_q[i];
            memset(&s_q[i], 0, sizeof(s_q[i]));
            got = 1;
            break;
        }
    }
    hub_refresh_st();
    os_mutex_post(&s_hub_mtx);
    return got ? 0 : -1;
}

u8 walkie_hub_idle(void)
{
    return !s_hub_working && hub_q_count() == 0;
}

u8 walkie_hub_status(void)
{
    hub_watchdog();
    return s_hub_st;
}

u8 walkie_hub_pending(void)
{
    hub_watchdog();
    return hub_q_count() + (s_hub_working ? 1 : 0);
}

const char *walkie_hub_ip(void)
{
    hub_ensure_addr(0);
    return hub_ip();
}

const char *walkie_hub_ip_cached(void)
{
    return hub_ip();
}

const char *walkie_hub_key(void)
{
    return hub_key();
}

int walkie_hub_port(void)
{
    return WALKIE_HUB_PORT;
}

int walkie_hub_diag_json(char *buf, int buflen)
{
    static const char *st_name[] = { "idle", "syncing", "pending", "fail" };
    u8 st;
    u32 age = 0;

    if (!buf || buflen < 64) {
        return 0;
    }
    hub_watchdog();
    st = s_hub_st;
    if (st > WALKIE_HUB_ST_FAIL) {
        st = WALKIE_HUB_ST_IDLE;
    }
    if (s_hub_pending_since_ms) {
        age = (u32)(timer_get_ms() - s_hub_pending_since_ms);
    }
    return snprintf(buf, buflen,
                    "{\"st\":%u,\"st_name\":\"%s\",\"pending\":%u,\"working\":%u,"
                    "\"pull_busy\":%u,\"last_fail\":%u,\"put_ok\":%u,\"put_fail\":%u,"
                    "\"last_rc\":%d,\"last_id\":%u,\"pending_age_ms\":%u,"
                    "\"ip\":\"%s\",\"port\":%d,\"key\":\"%s\",\"devid\":\"%s\","
                    "\"discovered\":%u,\"worker\":%u}",
                    (unsigned)st, st_name[st],
                    (unsigned)hub_q_put_count(),
                    (unsigned)s_hub_working,
                    (unsigned)s_hub_pull_busy,
                    (unsigned)s_hub_last_fail,
                    (unsigned)s_hub_put_ok,
                    (unsigned)s_hub_put_fail,
                    (int)s_hub_last_rc,
                    (unsigned)s_hub_last_msg_id,
                    (unsigned)age,
                    hub_ip(), WALKIE_HUB_PORT, hub_key(), hub_devid(),
                    (unsigned)s_hub_from_disc,
                    (unsigned)s_hub_worker);
}

int walkie_hub_flush_pending(void)
{
    int i, n = 0;

    if (!s_hub_inited) {
        return 0;
    }
    os_mutex_pend(&s_hub_mtx, 0);
    for (i = 0; i < WALKIE_HUB_Q_DEPTH; i++) {
        if (s_q[i].used && s_q[i].kind == HUB_JOB_PUT) {
            hub_job_free(&s_q[i]);
            n++;
        }
    }
    s_hub_last_fail = n ? 1 : s_hub_last_fail;
    hub_refresh_st();
    os_mutex_post(&s_hub_mtx);
    if (n) {
        printf("[walkie] hub flush %d pending put(s)\n", n);
        web_log_printf("[hub] flush %d pending put(s)", n);
    }
    return n;
}

/* ============ put / pull 执行 ============ */

#define HUB_PULL_LIMIT   8
/* 兼容旧 pcm 归档; alaw 更小 */
#define HUB_PULL_MAX_B   (WALKIE_HUB_MAX_SEC * WALKIE_PCM_RATE * 2 + 64)

static int hub_do_put(struct hub_job *job)
{
    char url[384], enc_to[64];
    const char *fmt = "pcm";
    u8 *payload;
    u32 plen;
    u8 *enc = NULL;
    u32 enc_len = 0;
    int rc;

    if (!job->buf || !job->len) {
        return -1;
    }

    /* 无已发现地址时再 DISCOVER; 已发现则直接用缓存 */
    hub_ensure_addr(0);

    payload = job->buf;
    plen = job->len;
    printf("[walkie] hub put encode id=%u pcm=%u\n",
           (unsigned)job->msg_id, (unsigned)job->len);
    web_log_printf("[hub] put encode id=%u pcm=%u",
                   (unsigned)job->msg_id, (unsigned)job->len);
    if ((job->len & 1) == 0) {
        enc = hub_encode_alaw((const s16 *)job->buf, job->len / 2, &enc_len);
        if (enc && enc_len) {
            payload = enc;
            plen = enc_len;
            fmt = "alaw";
        }
    }
    snprintf(job->fmt, sizeof(job->fmt), "%s", fmt);

    urlenc(enc_to, sizeof(enc_to), job->to);
    snprintf(url, sizeof(url),
             "http://%s:%d/cgi-bin/hub"
             "?cmd=put&key=%s"
             "&dev=%s&ch=audio&dtype=file&fmt=%s"
             "&rate=%d&dur=%u&uid=%s-%u&txt=%s",
             hub_ip(), WALKIE_HUB_PORT, hub_key(),
             hub_devid(), fmt,
             WALKIE_PCM_RATE, (unsigned)job->dur_ms,
             hub_devid(), (unsigned)job->msg_id, enc_to);

    printf("[walkie] hub put %uB fmt=%s id=%u -> %s ...\n",
           (unsigned)plen, fmt, (unsigned)job->msg_id, hub_ip());
    web_log_printf("[hub] put start id=%u %uB fmt=%s -> %s",
                   (unsigned)job->msg_id, (unsigned)plen, fmt, hub_ip());
    rc = hub_put_raw(url, payload, plen);
    s_hub_last_rc = (u32)rc;
    s_hub_last_msg_id = job->msg_id;
    printf("[walkie] hub put %s (rc=%d)\n", rc == 0 ? "ok" : "fail", rc);
    web_log_printf("[hub] put %s id=%u rc=%d",
                   rc == 0 ? "ok" : "fail", (unsigned)job->msg_id, rc);
    if (enc) {
        free(enc);
    }
    if (rc != 0) {
        hub_forget();
    }
    return rc;
}

static int hub_ack(u32 id)
{
    char url[192];
    struct hub_dl dl;
    int rc;

    snprintf(url, sizeof(url),
             "http://%s:%d/cgi-bin/hub?cmd=ack&key=%s"
             "&id=%u&ackdev=%s", hub_ip(), WALKIE_HUB_PORT, hub_key(),
             (unsigned)id, hub_devid());
    rc = hub_get(url, &dl);
    if (dl.buf) {
        free(dl.buf);
    }
    return rc;
}

static void hub_do_pull(void)
{
    char url[224];
    struct hub_dl ldl, gdl;
    int got = 0;
    int dl_fail = 0;

    printf("[walkie] hub pull begin\n");
    web_log_printf("[hub] pull begin");
    /* pull 前强制 DISCOVER, 适应 HUB IP 变更 */
    hub_ensure_addr(1);

    while (got < HUB_PULL_LIMIT && !dl_fail) {
        int mine = 0;
        int skipped = 0;

        if (s_hub_pull_abort) {
            printf("[walkie] hub pull abort (put pending)\n");
            web_log_printf("[hub] pull abort");
            break;
        }

        snprintf(url, sizeof(url),
                 "http://%s:%d/cgi-bin/hub?cmd=list&key=%s"
                 "&dev=%s&ch=audio&unread=1&since=0&limit=4",
                 hub_ip(), WALKIE_HUB_PORT, hub_key(), hub_devid());
        if (hub_get(url, &ldl)) {
            printf("[walkie] hub pull: list fail\n");
            web_log_printf("[hub] pull list fail");
            hub_forget();
            break;
        }
        const char *p = strstr((const char *)ldl.buf, "\"rows\":[");
        if (!p) {
            free(ldl.buf);
            break;
        }

        const char *obj = strstr(p + 8, "{\"id\":");
        while (obj && got < HUB_PULL_LIMIT && !dl_fail) {
            const char *objend = strchr(obj, '}');
            char from[32], to[32], fmt[8];
            long id, bytes;
            u8 *pcm;
            u32 pcm_len;

            if (!objend) {
                break;
            }
            id = jnum(obj, objend, "id", 0);
            bytes = jnum(obj, objend, "bytes", 0);
            jstr(obj, objend, "devid", from, sizeof(from));
            jstr(obj, objend, "txt", to, sizeof(to));
            jstr(obj, objend, "fmt", fmt, sizeof(fmt));

            if (to[0] && strcmp(to, hub_devid()) != 0) {
                /* 发给别人的: 不取. 自己发出的则 ack, 避免占满 unread */
                skipped++;
                if (from[0] && strcmp(from, hub_devid()) == 0) {
                    hub_ack((u32)id);
                }
            } else if (bytes <= 0 || bytes > HUB_PULL_MAX_B) {
                printf("[walkie] hub pull: id=%u bad size %ld, ack+skip\n",
                       (unsigned)id, bytes);
                hub_ack((u32)id);
                mine++;
            } else {
                mine++;
                snprintf(url, sizeof(url),
                         "http://%s:%d/cgi-bin/hub?cmd=get&key=%s"
                         "&id=%ld&raw=1", hub_ip(), WALKIE_HUB_PORT, hub_key(), id);
                if (hub_get(url, &gdl) == 0 && gdl.len == (u32)bytes) {
                    pcm = NULL;
                    pcm_len = 0;
                    if (fmt[0] == '\0' || strcmp(fmt, "pcm") == 0) {
                        pcm = gdl.buf;       /* 接管 */
                        pcm_len = gdl.len;
                        gdl.buf = NULL;
                    } else if (strcmp(fmt, "alaw") == 0) {
                        pcm = hub_decode_alaw(gdl.buf, gdl.len, &pcm_len);
                    } else {
                        printf("[walkie] hub pull: id=%u unknown fmt %s, ack\n",
                               (unsigned)id, fmt);
                        hub_ack((u32)id);
                    }
                    if (pcm && pcm_len) {
                        u32 off = 0;
                        while (off < pcm_len) {
                            u16 n = (pcm_len - off) > 512 ? 512 : (pcm_len - off);
                            walkie_rec_rx_pcm(pcm + off, n);
                            off += n;
                        }
                        walkie_rec_rx_end(hub_name_to_ip(from));
                        hub_ack((u32)id);
                        got++;
                        printf("[walkie] hub pull: id=%u from %s ok (%uB %s)\n",
                               (unsigned)id, from, (unsigned)bytes, fmt[0] ? fmt : "pcm");
                        free(pcm);
                    } else if (pcm) {
                        free(pcm);
                    }
                } else {
                    /* pull 失败不打上传态「同步失败」 */
                    printf("[walkie] hub pull: id=%u download fail, stop\n",
                           (unsigned)id);
                    dl_fail = 1;
                    hub_forget();
                }
                if (gdl.buf) {
                    free(gdl.buf);
                    gdl.buf = NULL;
                }
            }
            obj = strstr(objend, "{\"id\":");
        }

        free(ldl.buf);
        if (!mine || got >= HUB_PULL_LIMIT || dl_fail) {
            if (skipped && !mine) {
                printf("[walkie] hub pull: %d directed-to-others, stop\n", skipped);
            }
            break;
        }
    }

    printf("[walkie] hub pull done: %d msg(s)\n", got);
    web_log_printf("[hub] pull done n=%d", got);
}

/* 常驻: 只处理 PUT, 不被 pull HTTP 堵住 */
static void hub_put_worker(void *arg)
{
    struct hub_job job;

    (void)arg;
    printf("[walkie] hub put worker up\n");
    web_log_printf("[hub] put worker up");
    /* 开机联网后优先 DISCOVER, 失败才用兜底 IP */
    hub_ensure_addr(1);
    while (1) {
        os_sem_pend(&s_hub_sem, 0);
        while (hub_q_take_put(&job) == 0) {
            int rc;

            s_hub_working = 1;
            s_hub_work_kind = HUB_JOB_PUT;
            hub_refresh_st();
            printf("[walkie] hub put worker take id=%u\n",
                   (unsigned)job.msg_id);
            web_log_printf("[hub] put worker take id=%u",
                           (unsigned)job.msg_id);

            rc = hub_do_put(&job);
            if (rc != 0 && job.retries + 1 < WALKIE_HUB_PUT_RETRY) {
                job.retries++;
                job.used = 0;
                printf("[walkie] hub put retry %u/%u\n",
                       (unsigned)job.retries, WALKIE_HUB_PUT_RETRY);
                web_log_printf("[hub] put retry %u/%u id=%u",
                               (unsigned)job.retries, WALKIE_HUB_PUT_RETRY,
                               (unsigned)job.msg_id);
                os_time_dly(100); /* ~1s */
                if (hub_q_push(&job) == 0) {
                    job.buf = NULL;
                } else {
                    s_hub_last_fail = 1;
                    s_hub_put_fail++;
                }
            } else if (rc != 0) {
                s_hub_last_fail = 1;
                s_hub_put_fail++;
            } else {
                s_hub_last_fail = 0;
                s_hub_put_ok++;
            }
            hub_job_free(&job);

            s_hub_working = 0;
            s_hub_work_kind = 0;
            hub_refresh_st();
        }
    }
}

static void hub_pull_task(void *arg)
{
    (void)arg;
    hub_do_pull();
    s_hub_pull_busy = 0;
    s_hub_pull_abort = 0;
}

static int hub_ensure_worker(void)
{
    if (!s_hub_inited) {
        os_sem_create(&s_hub_sem, 0);
        os_mutex_create(&s_hub_mtx);
        memset(s_q, 0, sizeof(s_q));
        s_hub_inited = 1;
    }
    if (!s_hub_worker) {
        /* 24KB: A-law + httpcli_post */
        if (thread_fork("wk_hub", 13, 24 * 1024, 0, 0, hub_put_worker, NULL)) {
            printf("[walkie] hub: put worker fork fail\n");
            web_log_printf("[hub] put worker fork fail");
            return -1;
        }
        s_hub_worker = 1;
    }
    return 0;
}

/* ============ 对外 API ============ */

int walkie_hub_put_voice(const void *pcm, u32 len, u16 msg_id, const char *to_name)
{
    struct hub_job job;
    u32 max_bytes, use_len, samples, dur_ms;

    if (!pcm || !len) {
        return -1;
    }
    if (!walkie_net_has_ip()) {
        printf("[walkie] hub: no ip, skip archive\n");
        return -2;
    }
    if (hub_ensure_worker()) {
        return -5;
    }

    max_bytes = (u32)WALKIE_HUB_MAX_SEC * WALKIE_PCM_RATE * 2 * WALKIE_PCM_CH;
    use_len = len > max_bytes ? max_bytes : len;
    use_len &= ~1u; /* s16 对齐 */
    if (use_len < (WALKIE_PCM_RATE * 2 / 10)) {
        return -1;
    }
    samples = use_len / 2;
    dur_ms = samples * 1000 / WALKIE_PCM_RATE;

    /* 仅拷贝 PCM 入队; 压缩放到 hub worker, 防止 wk_msg_tx(2KB栈) 溢出卡死队列 */
    memset(&job, 0, sizeof(job));
    job.kind = HUB_JOB_PUT;
    job.msg_id = msg_id;
    job.dur_ms = dur_ms;
    snprintf(job.to, sizeof(job.to), "%s", to_name ? to_name : "");
    job.buf = malloc(use_len);
    if (!job.buf) {
        return -4;
    }
    memcpy(job.buf, pcm, use_len);
    job.len = use_len;

    /* 有 pull 在跑时请求尽早退出, 但 put 已在独立线程, 不必等它 */
    if (s_hub_pull_busy) {
        s_hub_pull_abort = 1;
    }

    if (hub_q_push(&job)) {
        free(job.buf);
        s_hub_last_fail = 1;
        hub_refresh_st();
        return -3;
    }
    printf("[walkie] hub queued put id=%u %uB pcm ~%us\n",
           (unsigned)msg_id, (unsigned)use_len, (unsigned)(dur_ms / 1000));
    web_log_printf("[hub] queued put id=%u %uB ~%us to=%s",
                   (unsigned)msg_id, (unsigned)use_len,
                   (unsigned)(dur_ms / 1000), to_name ? to_name : "");
    return 0;
}

int walkie_hub_pull(void)
{
    static u32 s_last_pull_ms;
    u32 now;

    if (!walkie_net_has_ip()) {
        return -2;
    }
    if (hub_ensure_worker()) {
        return -5;
    }
    now = timer_get_ms();
    if (s_last_pull_ms && (u32)(now - s_last_pull_ms) < 15000) {
        return -6;
    }
    /* 有待上传时不拉, 给带宽/HTTP 给 put */
    if (hub_q_put_count() || s_hub_working) {
        return -7;
    }
    if (s_hub_pull_busy) {
        return -3;
    }

    s_hub_pull_busy = 1;
    s_hub_pull_abort = 0;
    if (thread_fork("wk_hpull", 12, 10 * 1024, 0, 0, hub_pull_task, NULL)) {
        s_hub_pull_busy = 0;
        printf("[walkie] hub: pull task fork fail\n");
        web_log_printf("[hub] pull fork fail");
        return -5;
    }
    s_last_pull_ms = now;
    return 0;
}
