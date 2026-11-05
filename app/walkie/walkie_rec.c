#include "app_config.h"
#include "system/includes.h"
#include "fs/nor_fs.h"
#include "fs/sdfile.h"
#include "bt_music.h"
#include "walkie_priv.h"
#include <string.h>
#include <stdio.h>

/*
 * 对讲录音: 原厂 phone_rec_fs (内置 Flash REC 预留区)
 * isd: REC_ADR=AUTO / REC_LEN=4M / REC_OPT=1
 * 每段 ≤ 约 48KB PCM (~1.5s@16k mono), 最多 WALKIE_REC_MAX 条元数据。
 */
#define WALKIE_REC_MAX       16
#define WALKIE_REC_CAPACITY  (4 * 1024 * 1024)
#define WALKIE_REC_CHUNK     512

struct walkie_rec_meta {
    u8 used;
    u8 dir;      /* 0=TX 1=RX */
    u16 sec;
    u32 peer_ip;
    u32 ticks;
    u16 fs_index;
    u32 pcm_bytes;
};

/* phone_rec_fs.c 公开 API (部分未进 nor_fs.h) */
extern u32 sdfile_rec_createfile(void);
extern u32 sdfile_rec_write(void *buff, u32 len);
extern u32 sdfile_rec_read(void *buff, u32 len);
extern u32 sdfile_rec_open(u32 index);
extern u32 sdfile_rec_close(void);
extern u32 sdfile_rec_seek(int offsize, int type);
extern u32 sdfile_rec_flen(void);
extern void sdfile_rec_set_priv_info(u8 *info);
extern void sdfile_rec_get_priv_info(u8 *info);
extern void sdfile_rec_set_sr(u16 sr);
extern RECFILESYSTEM phone_recfs;
extern REC_FILE phone_recfile;

struct walkie_priv_blob {
    u8 dir;
    u8 sec;
    u16 magic;   /* 'WK' */
    u32 peer_ip;
    u32 ticks;
};

#define WALKIE_PRIV_MAGIC  0x4B57u  /* 'WK' LE */

static u8 s_ready;
static u8 s_rec_open;
static u8 s_dir;
static u32 s_peer_ip;
static u32 s_start_ms;
static u32 s_bytes;
static struct walkie_rec_meta s_list[WALKIE_REC_MAX];
static int s_list_n;

static volatile u8 s_play_alive;
static volatile u8 s_play_cancel;
static int s_play_idx;
static volatile u8 s_rx_notify;
static volatile u32 s_rx_notify_ip;

static u32 now_ms(void)
{
    return timer_get_ms();
}

static void priv_pack(u8 *out, u8 dir, u16 sec, u32 peer_ip, u32 ticks)
{
    struct walkie_priv_blob b;

    memset(&b, 0, sizeof(b));
    b.dir = dir;
    b.sec = (u8)(sec > 255 ? 255 : sec);
    b.magic = WALKIE_PRIV_MAGIC;
    b.peer_ip = peer_ip;
    b.ticks = ticks;
    memset(out, 0, NORFS_DATA_LEN);
    memcpy(out, &b, sizeof(b) <= NORFS_DATA_LEN ? sizeof(b) : NORFS_DATA_LEN);
}

static int priv_unpack(const u8 *in, struct walkie_rec_meta *m)
{
    struct walkie_priv_blob b;

    memcpy(&b, in, sizeof(b));
    if (b.magic != WALKIE_PRIV_MAGIC) {
        return -1;
    }
    m->dir = b.dir;
    m->sec = b.sec ? b.sec : 1;
    m->peer_ip = b.peer_ip;
    m->ticks = b.ticks;
    return 0;
}

static void list_rebuild(void)
{
    u16 first, last, i;
    u8 priv[NORFS_DATA_LEN];
    struct walkie_rec_meta m;
    u32 got;

    s_list_n = 0;
    memset(s_list, 0, sizeof(s_list));
    if (!s_ready || phone_recfs.total_file == 0) {
        return;
    }
    last = phone_recfs.index.index;
    if (last < phone_recfs.total_file) {
        first = 1;
    } else {
        first = (u16)(last - phone_recfs.total_file + 1);
    }
    for (i = first; i <= last && s_list_n < WALKIE_REC_MAX; i++) {
        u32 flen;
        got = sdfile_rec_open(i);
        if (got != i) {
            continue;
        }
        memset(priv, 0, sizeof(priv));
        sdfile_rec_get_priv_info(priv);
        flen = sdfile_rec_flen();
        sdfile_rec_close();
        memset(&m, 0, sizeof(m));
        if (priv_unpack(priv, &m) != 0) {
            m.dir = 0;
            m.sec = 1;
            m.peer_ip = 0;
            m.ticks = 0;
        }
        m.used = 1;
        m.fs_index = i;
        m.pcm_bytes = (flen > sizeof(SDFILE_FILE_HEAD)) ?
                      (flen - sizeof(SDFILE_FILE_HEAD)) : 0;
        s_list[s_list_n++] = m;
    }
}

int walkie_rec_init(void)
{
    int cap;

    if (s_ready) {
        list_rebuild();
        return 0;
    }

    memset(s_list, 0, sizeof(s_list));
    s_list_n = 0;
    s_rec_open = 0;
    s_bytes = 0;
    s_ready = 0;

    cap = set_rec_capacity(WALKIE_REC_CAPACITY);
    if (cap <= 0) {
        printf("[walkie] rec: REC zone not ready (need full burn with REC_LEN)\n");
        return -1;
    }
    _sdfile_rec_init();
    s_ready = 1;
    list_rebuild();
    printf("[walkie] rec store: nor_fs REC %dKB, files=%d\n",
           cap / 1024, s_list_n);
    return 0;
}

int walkie_rec_ensure(void)
{
    return walkie_rec_init();
}

int walkie_rec_clear_all(void)
{
    if (walkie_rec_ensure()) {
        return -1;
    }
    /* 停播放/录音, 再清 nor_fs 文件头索引 */
    s_play_cancel = 1;
    while (s_play_alive) {
        os_time_dly(1);
    }
    if (s_rec_open) {
        sdfile_rec_close();
        s_rec_open = 0;
        s_bytes = 0;
    }
    rec_clear_norfs_fileindex();
    memset(s_list, 0, sizeof(s_list));
    s_list_n = 0;
    printf("[walkie] rec cleared all\n");
    return 0;
}

void walkie_rec_deinit(void)
{
    s_play_cancel = 1;
    while (s_play_alive) {
        os_time_dly(1);
    }
    if (s_rec_open) {
        sdfile_rec_close();
        s_rec_open = 0;
    }
    /* 保持 s_ready, 供网页继续导出录音 */
}

static void rec_begin(u8 dir, u32 peer_ip)
{
    u8 priv[NORFS_DATA_LEN];
    u32 idx;

    if (!s_ready || s_rec_open || s_play_alive) {
        return;
    }
    s_dir = dir;
    s_peer_ip = peer_ip;
    s_start_ms = now_ms();
    s_bytes = 0;

    idx = sdfile_rec_createfile();
    if (idx == 0) {
        printf("[walkie] rec create fail\n");
        return;
    }
    priv_pack(priv, dir, 0, peer_ip, s_start_ms);
    sdfile_rec_set_priv_info(priv);
    sdfile_rec_set_sr(WALKIE_PCM_RATE);
    s_rec_open = 1;
}

static void rec_write(const void *pcm, u16 len)
{
    u16 off = 0;
    u16 n;

    if (!s_rec_open || !pcm || !len) {
        return;
    }
    while (off < len) {
        n = (u16)((len - off) > WALKIE_REC_CHUNK ? WALKIE_REC_CHUNK : (len - off));
        if (sdfile_rec_write((void *)((u8 *)pcm + off), n) != n) {
            break;
        }
        off += n;
        s_bytes += n;
    }
}

static void rec_end(void)
{
    u16 sec;
    u8 priv[NORFS_DATA_LEN];
    u32 idx;
    struct walkie_rec_meta m;

    if (!s_rec_open) {
        return;
    }
    /* 整包落盘几乎瞬间完成, 不能用墙钟; 按 PCM 字节算时长 */
    {
        u32 bytes_per_sec = (u32)WALKIE_PCM_RATE * 2 * WALKIE_PCM_CH;
        u32 sec32 = bytes_per_sec ? (s_bytes + bytes_per_sec / 2) / bytes_per_sec : 0;
        if (sec32 == 0 && s_bytes > 0) {
            sec32 = 1;
        }
        if (sec32 > 255) {
            sec32 = 255; /* priv 里 sec 为 u8 */
        }
        sec = (u16)sec32;
    }
    priv_pack(priv, s_dir, sec, s_peer_ip, s_start_ms);
    sdfile_rec_set_priv_info(priv);
    idx = phone_recfile.index.index;
    sdfile_rec_close();
    s_rec_open = 0;

    if (s_bytes == 0) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.used = 1;
    m.dir = s_dir;
    m.sec = sec;
    m.peer_ip = s_peer_ip;
    m.ticks = s_start_ms;
    m.fs_index = (u16)idx;
    m.pcm_bytes = s_bytes;
    if (s_list_n >= WALKIE_REC_MAX) {
        memmove(&s_list[0], &s_list[1], sizeof(s_list[0]) * (WALKIE_REC_MAX - 1));
        s_list_n = WALKIE_REC_MAX - 1;
        memset(&s_list[s_list_n], 0, sizeof(s_list[0]));
    }
    s_list[s_list_n++] = m;
    s_bytes = 0;
}

void walkie_rec_tx_start(void)
{
    rec_begin(0, walkie_peer_selected_ip());
}

void walkie_rec_tx_pcm(const void *pcm, u16 len)
{
    rec_write(pcm, len);
}

void walkie_rec_tx_end(void)
{
    rec_end();
}

void walkie_rec_rx_pcm(const void *pcm, u16 len)
{
    if (!s_rec_open) {
        rec_begin(1, 0);
    }
    rec_write(pcm, len);
}

void walkie_rec_rx_end(u32 peer_ip)
{
    int n_before = s_list_n;

    if (s_rec_open) {
        s_peer_ip = peer_ip;
        rec_end();
        if (s_list_n > n_before) {
            s_rx_notify_ip = peer_ip;
            s_rx_notify = 1;
        }
    }
}

u8 walkie_rec_rx_notify_take(u32 *peer_ip)
{
    if (!s_rx_notify) {
        return 0;
    }
    s_rx_notify = 0;
    if (peer_ip) {
        *peer_ip = s_rx_notify_ip;
    }
    return 1;
}

int walkie_rec_count(void)
{
    return s_list_n;
}

int walkie_rec_info(int idx, char *line, int line_len)
{
    struct walkie_rec_meta *m;
    if (idx < 0 || idx >= s_list_n || !line || line_len < 8) {
        return -1;
    }
    m = &s_list[idx];
    snprintf(line, line_len, "%s %u.%u.%u.%u %us",
             m->dir ? "RX" : "TX",
             (unsigned)((m->peer_ip) & 0xff),
             (unsigned)((m->peer_ip >> 8) & 0xff),
             (unsigned)((m->peer_ip >> 16) & 0xff),
             (unsigned)((m->peer_ip >> 24) & 0xff),
             (unsigned)m->sec);
    return 0;
}

int walkie_rec_meta(int idx, u8 *dir, u16 *sec, u32 *peer_ip)
{
    struct walkie_rec_meta *m;
    if (idx < 0 || idx >= s_list_n) {
        return -1;
    }
    m = &s_list[idx];
    if (dir) {
        *dir = m->dir;
    }
    if (sec) {
        *sec = m->sec;
    }
    if (peer_ip) {
        *peer_ip = m->peer_ip;
    }
    return 0;
}

static void walkie_rec_play_task(void *arg)
{
    u8 buf[WALKIE_REC_CHUNK];
    u32 got, nread;
    u16 fs_index;
    (void)arg;

    fs_index = s_list[s_play_idx].fs_index;
    walkie_ptt_stop();
    /* 蓝牙音乐正在播: 先静音/暂停, 播完对讲后再还 */
    bt_music_ui_duck_begin();
    if (walkie_av_start()) {
        printf("[walkie] play av start fail\n");
        goto out;
    }
    walkie_av_local_play_begin();

    got = sdfile_rec_open(fs_index);
    if (got != fs_index) {
        printf("[walkie] play open fail idx=%d fs=%u\n", s_play_idx, fs_index);
        goto out;
    }
    /* SEEK_SET 0 → 跳过文件头, 落在 PCM 起点 */
    sdfile_rec_seek(0, NOR_FS_SEEK_SET);

    while (!s_play_cancel && s_ready) {
        nread = sdfile_rec_read(buf, WALKIE_REC_CHUNK);
        if (nread == 0 || nread > WALKIE_REC_CHUNK) {
            break;
        }
        if (walkie_av_local_play_write(buf, (u16)nread)) {
            break;
        }
    }
    sdfile_rec_close();
    if (!s_play_cancel) {
        walkie_av_local_play_drain();
    }

out:
    walkie_av_local_play_end();
    /* 释放 DAC, 等 0.5s 再恢复蓝牙音乐 */
    if (!walkie_ptt_is_on()) {
        walkie_av_stop();
    }
    os_time_dly(50);
    bt_music_ui_duck_end();
    s_play_alive = 0;
    printf("[walkie] play done\n");
}

int walkie_rec_play(int idx)
{
    if (!s_ready || idx < 0 || idx >= s_list_n || !s_list[idx].used) {
        return -1;
    }
    if (s_rec_open) {
        printf("[walkie] play busy: recording\n");
        return -1;
    }
    /* 打断上一段回放 */
    if (s_play_alive) {
        s_play_cancel = 1;
        while (s_play_alive) {
            os_time_dly(1);
        }
    }
    s_play_cancel = 0;
    s_play_idx = idx;
    s_play_alive = 1;
    if (thread_fork("wk_play", 14, 1024, 0, 0, walkie_rec_play_task, NULL)) {
        s_play_alive = 0;
        printf("[walkie] play task fork fail\n");
        return -1;
    }
    return 0;
}

u32 walkie_rec_pcm_bytes(int idx)
{
    if (idx < 0 || idx >= s_list_n || !s_list[idx].used) {
        return 0;
    }
    return s_list[idx].pcm_bytes;
}

static void wav_put_u16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)(v >> 8);
}

static void wav_put_u32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
    p[2] = (u8)((v >> 16) & 0xff);
    p[3] = (u8)((v >> 24) & 0xff);
}

static void wav_make_hdr(u8 *h, u32 pcm_bytes, u32 rate)
{
    u32 byte_rate = rate * 2; /* mono s16 */
    memset(h, 0, 44);
    memcpy(h + 0, "RIFF", 4);
    wav_put_u32(h + 4, 36 + pcm_bytes);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    wav_put_u32(h + 16, 16);          /* PCM fmt chunk */
    wav_put_u16(h + 20, 1);           /* PCM */
    wav_put_u16(h + 22, 1);           /* mono */
    wav_put_u32(h + 24, rate);
    wav_put_u32(h + 28, byte_rate);
    wav_put_u16(h + 32, 2);           /* block align */
    wav_put_u16(h + 34, 16);          /* bits */
    memcpy(h + 36, "data", 4);
    wav_put_u32(h + 40, pcm_bytes);
}

int walkie_rec_export_wav(int idx,
                          int (*write_cb)(void *ctx, const void *data, u32 len),
                          void *ctx)
{
    u8 hdr[44];
    u8 buf[WALKIE_REC_CHUNK];
    u32 got, nread, pcm_bytes, left;
    u16 fs_index;

    if (!write_cb || walkie_rec_ensure()) {
        return -1;
    }
    if (idx < 0 || idx >= s_list_n || !s_list[idx].used) {
        return -1;
    }
    if (s_rec_open || s_play_alive) {
        return -2; /* busy */
    }

    fs_index = s_list[idx].fs_index;
    got = sdfile_rec_open(fs_index);
    if (got != fs_index) {
        return -1;
    }
    {
        u32 flen = sdfile_rec_flen();
        pcm_bytes = (flen > sizeof(SDFILE_FILE_HEAD)) ?
                    (flen - sizeof(SDFILE_FILE_HEAD)) : 0;
    }
    if (!pcm_bytes) {
        sdfile_rec_close();
        return -1;
    }
    s_list[idx].pcm_bytes = pcm_bytes;

    wav_make_hdr(hdr, pcm_bytes, WALKIE_PCM_RATE);
    if (write_cb(ctx, hdr, 44) < 0) {
        sdfile_rec_close();
        return -1;
    }

    sdfile_rec_seek(0, NOR_FS_SEEK_SET);
    left = pcm_bytes;
    while (left) {
        u32 want = left > WALKIE_REC_CHUNK ? WALKIE_REC_CHUNK : left;
        nread = sdfile_rec_read(buf, want);
        if (nread == 0 || nread > want) {
            break;
        }
        if (write_cb(ctx, buf, nread) < 0) {
            sdfile_rec_close();
            return -1;
        }
        left -= nread;
        wdt_clear();
    }
    sdfile_rec_close();
    return left ? -1 : 0;
}
