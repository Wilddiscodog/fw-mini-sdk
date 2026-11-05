#include "app_config.h"
#include "system/includes.h"
#include "walkie_priv.h"

#include <string.h>
#include <stdio.h>

/* 松键整包发送: 按下录到 SDRAM, 松开再发; ACK 失败整包重传 */
#define WALKIE_MSG_MAX_SEC     60
#define WALKIE_MSG_MAX_BYTES   ((u32)WALKIE_PCM_RATE * 2 * WALKIE_PCM_CH * WALKIE_MSG_MAX_SEC)
#define WALKIE_MSG_CHUNK_PCM   480
#define WALKIE_MSG_MAX_CHUNKS  ((WALKIE_MSG_MAX_BYTES + WALKIE_MSG_CHUNK_PCM - 1) / WALKIE_MSG_CHUNK_PCM)
#define WALKIE_MSG_RETRY       3
#define WALKIE_MSG_ACK_TO_MS   1000

enum {
    MSG_ST_IDLE = 0,
    MSG_ST_REC,
    MSG_ST_SEND,
};

static u8 *s_tx_buf;
static u32 s_tx_cap;
static u32 s_tx_len;
static u8 *s_rx_buf;
static u32 s_rx_cap;
static u32 s_rx_len;
static u32 s_rx_expect;
static u16 s_rx_chunks;
static u16 s_rx_unique;
static u8 s_rx_map[(WALKIE_MSG_MAX_CHUNKS + 7) / 8];
static u16 s_rx_msg_id;
static u32 s_rx_from;

static volatile u8 s_st;
static volatile u8 s_ack_ok;
static volatile u8 s_ack_got;
static volatile u16 s_ack_wait_id;
static u16 s_msg_id_gen;
static volatile u8 s_send_run;
static u8 s_last_send_ok = 1;

static u32 now_ms(void)
{
    return timer_get_ms();
}

static int ensure_tx_buf(void)
{
    if (s_tx_buf) {
        return 0;
    }
    s_tx_cap = WALKIE_MSG_MAX_BYTES;
    s_tx_buf = malloc(s_tx_cap);
    if (!s_tx_buf) {
        s_tx_cap = WALKIE_MSG_MAX_BYTES / 2; /* 退化到 30s */
        s_tx_buf = malloc(s_tx_cap);
    }
    if (!s_tx_buf) {
        printf("[walkie] msg tx buf alloc fail\n");
        s_tx_cap = 0;
        return -1;
    }
    printf("[walkie] msg tx buf %uKB\n", (unsigned)(s_tx_cap / 1024));
    return 0;
}

int walkie_msg_init(void)
{
    s_st = MSG_ST_IDLE;
    s_tx_len = 0;
    s_rx_len = 0;
    s_send_run = 0;
    s_last_send_ok = 1;
    return ensure_tx_buf();
}

void walkie_msg_deinit(void)
{
    s_send_run = 0;
    s_st = MSG_ST_IDLE;
    if (s_tx_buf) {
        free(s_tx_buf);
        s_tx_buf = NULL;
        s_tx_cap = 0;
    }
    if (s_rx_buf) {
        free(s_rx_buf);
        s_rx_buf = NULL;
        s_rx_cap = 0;
    }
}

u8 walkie_msg_busy(void)
{
    return (s_st != MSG_ST_IDLE) || s_send_run;
}

u8 walkie_msg_recording(void)
{
    return s_st == MSG_ST_REC;
}

u8 walkie_msg_sending(void)
{
    return s_st == MSG_ST_SEND || s_send_run;
}

u8 walkie_msg_last_send_ok(void)
{
    return s_last_send_ok;
}

int walkie_msg_tx_begin(void)
{
    if (s_st != MSG_ST_IDLE || s_send_run) {
        return -1;
    }
    if (ensure_tx_buf()) {
        return -1;
    }
    s_tx_len = 0;
    s_st = MSG_ST_REC;
    return 0;
}

void walkie_msg_tx_pcm(const void *pcm, u16 len)
{
    u32 room;

    if (s_st != MSG_ST_REC || !s_tx_buf || !pcm || !len) {
        return;
    }
    room = s_tx_cap - s_tx_len;
    if (len > room) {
        len = (u16)room;
    }
    if (!len) {
        return; /* 已达最长时长上限 */
    }
    memcpy(s_tx_buf + s_tx_len, pcm, len);
    s_tx_len += len;
}

static void msg_save_tx_local(void)
{
    u32 off = 0;
    u16 n;

    if (!s_tx_len) {
        return;
    }
    walkie_rec_tx_start();
    while (off < s_tx_len) {
        n = (u16)((s_tx_len - off) > 512 ? 512 : (s_tx_len - off));
        walkie_rec_tx_pcm(s_tx_buf + off, n);
        off += n;
    }
    walkie_rec_tx_end();
}

static int send_one_attempt(u16 msg_id, u32 dip)
{
    u16 chunks, i, n;
    u32 off;
    u32 t0;
    struct walkie_msg_begin b;
    struct walkie_msg_end e;

    chunks = (u16)((s_tx_len + WALKIE_MSG_CHUNK_PCM - 1) / WALKIE_MSG_CHUNK_PCM);
    if (!chunks) {
        return -1;
    }

    memset(&b, 0, sizeof(b));
    b.msg_id = msg_id;
    b.total_bytes = s_tx_len;
    b.total_chunks = chunks;
    b.sample_rate = WALKIE_PCM_RATE;
    if (walkie_net_send_msg(dip, WALKIE_PKT_MSG_BEGIN, &b, sizeof(b))) {
        return -1;
    }

    off = 0;
    for (i = 0; i < chunks; i++) {
        u8 pkt[sizeof(struct walkie_msg_chunk) + WALKIE_MSG_CHUNK_PCM];
        struct walkie_msg_chunk *c = (struct walkie_msg_chunk *)pkt;

        n = (u16)((s_tx_len - off) > WALKIE_MSG_CHUNK_PCM ?
                  WALKIE_MSG_CHUNK_PCM : (s_tx_len - off));
        c->msg_id = msg_id;
        c->chunk_idx = i;
        c->nbytes = n;
        memcpy(pkt + sizeof(*c), s_tx_buf + off, n);
        if (walkie_net_send_msg(dip, WALKIE_PKT_MSG_DATA, pkt,
                                (u16)(sizeof(*c) + n))) {
            return -1;
        }
        off += n;
        /* 让出一点 CPU, 减轻 UDP 丢包 */
        if ((i & 7) == 7) {
            os_time_dly(1);
        }
    }

    memset(&e, 0, sizeof(e));
    e.msg_id = msg_id;
    e.total_chunks = chunks;
    e.total_bytes = s_tx_len;
    if (walkie_net_send_msg(dip, WALKIE_PKT_MSG_END, &e, sizeof(e))) {
        return -1;
    }

    s_ack_got = 0;
    s_ack_ok = 0;
    s_ack_wait_id = msg_id;
    t0 = now_ms();
    while ((now_ms() - t0) < WALKIE_MSG_ACK_TO_MS) {
        if (s_ack_got && s_ack_wait_id == msg_id) {
            return s_ack_ok ? 0 : -1;
        }
        os_time_dly(2);
    }
    return -1; /* timeout */
}

static void walkie_msg_send_task(void *arg)
{
    u32 dip = (u32)arg;
    u16 msg_id;
    int try, ok = 0;

    s_st = MSG_ST_SEND;
    msg_id = ++s_msg_id_gen;
    if (!msg_id) {
        msg_id = ++s_msg_id_gen;
    }

    for (try = 0; try < WALKIE_MSG_RETRY; try++) {
        printf("[walkie] msg send id=%u try=%d bytes=%u\n",
               msg_id, try + 1, (unsigned)s_tx_len);
        if (send_one_attempt(msg_id, dip) == 0) {
            ok = 1;
            break;
        }
        os_time_dly(10);
    }

    s_last_send_ok = ok ? 1 : 0;
    printf("[walkie] msg send %s id=%u\n", ok ? "ok" : "fail", msg_id);

    /* 本端始终落盘, 聊天气泡可点播 */
    msg_save_tx_local();

    s_tx_len = 0;
    s_st = MSG_ST_IDLE;
    s_send_run = 0;
}

void walkie_msg_tx_commit(void)
{
    u32 dip;

    if (s_st != MSG_ST_REC) {
        s_st = MSG_ST_IDLE;
        return;
    }
    if (s_tx_len < (WALKIE_PCM_RATE * 2 / 10)) {
        /* <100ms 丢弃 */
        printf("[walkie] msg too short %u, drop\n", (unsigned)s_tx_len);
        s_tx_len = 0;
        s_st = MSG_ST_IDLE;
        return;
    }
    dip = walkie_peer_selected_ip();
    if (!dip) {
        printf("[walkie] msg no peer, keep local only\n");
        msg_save_tx_local();
        s_tx_len = 0;
        s_st = MSG_ST_IDLE;
        s_last_send_ok = 0;
        return;
    }
    if (s_send_run) {
        return;
    }
    s_send_run = 1;
    if (thread_fork("wk_msg_tx", 14, 2048, 0, 0, walkie_msg_send_task, (void *)dip)) {
        printf("[walkie] msg send task fail\n");
        s_send_run = 0;
        msg_save_tx_local();
        s_tx_len = 0;
        s_st = MSG_ST_IDLE;
        s_last_send_ok = 0;
    }
}

void walkie_msg_tx_cancel(void)
{
    if (s_st == MSG_ST_REC) {
        s_tx_len = 0;
        s_st = MSG_ST_IDLE;
    }
}

void walkie_msg_on_ack(u16 msg_id, u8 ok)
{
    if (s_ack_wait_id == msg_id) {
        s_ack_ok = ok ? 1 : 0;
        s_ack_got = 1;
    }
}

static void rx_reset(void)
{
    s_rx_len = 0;
    s_rx_expect = 0;
    s_rx_chunks = 0;
    s_rx_unique = 0;
    s_rx_msg_id = 0;
    s_rx_from = 0;
    memset(s_rx_map, 0, sizeof(s_rx_map));
}

void walkie_msg_on_begin(u32 from_ip, const struct walkie_msg_begin *b)
{
    if (!b || !b->total_bytes || b->total_bytes > WALKIE_MSG_MAX_BYTES) {
        return;
    }
    if (s_st == MSG_ST_REC || s_send_run) {
        /* 本端正忙, 拒收 */
        struct walkie_msg_ack a = { .msg_id = b->msg_id, .ok = 0 };
        walkie_net_send_msg(from_ip, WALKIE_PKT_MSG_ACK, &a, sizeof(a));
        return;
    }
    if (!s_rx_buf || s_rx_cap < b->total_bytes) {
        if (s_rx_buf) {
            free(s_rx_buf);
            s_rx_buf = NULL;
        }
        s_rx_cap = b->total_bytes;
        s_rx_buf = malloc(s_rx_cap);
        if (!s_rx_buf) {
            s_rx_cap = 0;
            return;
        }
    }
    memset(s_rx_buf, 0, b->total_bytes);
    memset(s_rx_map, 0, sizeof(s_rx_map));
    s_rx_expect = b->total_bytes;
    s_rx_chunks = b->total_chunks;
    s_rx_unique = 0;
    s_rx_len = 0;
    s_rx_msg_id = b->msg_id;
    s_rx_from = from_ip;
}

void walkie_msg_on_data(u32 from_ip, const struct walkie_msg_chunk *c, u16 plen)
{
    u32 off;
    u16 n;
    const u8 *pcm;

    (void)from_ip;
    if (!c || !s_rx_buf || c->msg_id != s_rx_msg_id) {
        return;
    }
    n = c->nbytes;
    if (n > WALKIE_MSG_CHUNK_PCM || (u16)(sizeof(*c) + n) > plen) {
        return;
    }
    off = (u32)c->chunk_idx * WALKIE_MSG_CHUNK_PCM;
    if (off + n > s_rx_expect) {
        return;
    }
    pcm = (const u8 *)c + sizeof(*c);
    memcpy(s_rx_buf + off, pcm, n);
    if (off + n > s_rx_len) {
        s_rx_len = off + n;
    }
    if (c->chunk_idx < WALKIE_MSG_MAX_CHUNKS) {
        u16 bi = c->chunk_idx >> 3;
        u8 mask = (u8)(1u << (c->chunk_idx & 7));
        if (!(s_rx_map[bi] & mask)) {
            s_rx_map[bi] |= mask;
            s_rx_unique++;
        }
    }
}

void walkie_msg_on_end(u32 from_ip, const struct walkie_msg_end *e)
{
    struct walkie_msg_ack a;
    u32 off;
    u16 n;
    int ok;

    if (!e || e->msg_id != s_rx_msg_id || !s_rx_buf) {
        return;
    }
    ok = (e->total_bytes == s_rx_expect && e->total_bytes > 0 &&
          e->total_chunks == s_rx_chunks &&
          s_rx_unique == s_rx_chunks && s_rx_len >= e->total_bytes);
    a.msg_id = e->msg_id;
    a.ok = ok ? 1 : 0;
    walkie_net_send_msg(from_ip, WALKIE_PKT_MSG_ACK, &a, sizeof(a));

    if (!ok) {
        printf("[walkie] msg rx incomplete id=%u got=%u expect=%u\n",
               e->msg_id, (unsigned)s_rx_len, (unsigned)e->total_bytes);
        rx_reset();
        return;
    }

    off = 0;
    while (off < s_rx_len) {
        n = (u16)((s_rx_len - off) > 512 ? 512 : (s_rx_len - off));
        walkie_rec_rx_pcm(s_rx_buf + off, n);
        off += n;
    }
    walkie_rec_rx_end(from_ip ? from_ip : s_rx_from);
    printf("[walkie] msg rx ok id=%u bytes=%u\n", e->msg_id, (unsigned)s_rx_len);
    rx_reset();
}
