#include "app_config.h"
#include "system/includes.h"
#include "net_music.h"
#include "walkie_priv.h"

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#include "server/audio_server.h"
#include "server/server_core.h"
#include "generic/circular_buf.h"
#include "device/gpio.h"
#include <string.h>

#define TX_CACHE  (4 * 1024)
#define RX_CACHE  (8 * 1024)

/* 与 board.c dac_data 一致: PH7 MUTE=低; 半双工硬件 MUTE, 不改动 PCM */
#define WALKIE_PA_IO        IO_PORTH_07
#define WALKIE_PA_MUTE_LVL  0

struct walkie_av {
    struct server *enc;
    struct server *dec;
    u8 *tx_buf;
    u8 *rx_buf;
    cbuffer_t tx_cbuf;
    cbuffer_t rx_cbuf;
    OS_SEM rx_sem;
    volatile u8 run;
    volatile u8 tx_on;
    volatile u8 local_play;
    u8 volume;
};
static struct walkie_av s_av = { .volume = 80 };

extern void bt_music_audio_takeover(void);
extern void adc_multiplex_set_gain(const char *source, u8 channel_bit_map, u8 gain);

static void walkie_pa_hw_mute(u8 mute)
{
    gpio_direction_output(WALKIE_PA_IO,
                          mute ? WALKIE_PA_MUTE_LVL : !WALKIE_PA_MUTE_LVL);
}

static void walkie_spk_pause(u8 pause)
{
    union audio_req req = {0};

    if (pause) {
        walkie_pa_hw_mute(1);
        if (!s_av.dec) {
            return;
        }
        cbuf_clear(&s_av.rx_cbuf);
        req.dec.cmd = AUDIO_DEC_PAUSE;
        server_request(s_av.dec, AUDIO_REQ_DEC, &req);
    } else {
        if (s_av.dec) {
            req.dec.cmd = AUDIO_DEC_START;
            server_request(s_av.dec, AUDIO_REQ_DEC, &req);
        }
        walkie_pa_hw_mute(0);
    }
}

/* 按下仅写入 SDRAM 缓存, 松开后再整包发送(见 walkie_msg) */
static int av_vfs_fwrite(void *file, void *data, u32 len)
{
    (void)file;
    if (!s_av.tx_on || !s_av.run || !data || !len) {
        return (int)len;
    }
    if (len <= 512) {
        walkie_msg_tx_pcm(data, (u16)len);
    } else {
        u32 off = 0;
        while (off < len) {
            u16 chunk = (u16)((len - off) > 512 ? 512 : (len - off));
            walkie_msg_tx_pcm((u8 *)data + off, chunk);
            off += chunk;
        }
    }
    return (int)len;
}

static int av_vfs_fread(void *file, void *data, u32 len)
{
    u32 rlen;
    (void)file;
    do {
        rlen = cbuf_read(&s_av.rx_cbuf, data, len);
        if (rlen == len) {
            break;
        }
        os_sem_pend(&s_av.rx_sem, 20);
    } while (s_av.run && !s_av.tx_on);
    return (int)len;
}

static int av_vfs_fclose(void *file)
{
    (void)file;
    return 0;
}

static int av_vfs_flen(void *file)
{
    (void)file;
    return 0;
}

static const struct audio_vfs_ops s_av_vfs = {
    .fwrite = av_vfs_fwrite,
    .fread = av_vfs_fread,
    .fclose = av_vfs_fclose,
    .flen = av_vfs_flen,
};

static void av_event(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    if (!argv) {
        return;
    }
    /* 原厂 VAD 事件, 对讲半双工仍由 PTT 控制收发 */
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_SPEAK_START:
    case AUDIO_SERVER_EVENT_SPEAK_STOP:
    case AUDIO_SERVER_EVENT_END:
    case AUDIO_SERVER_EVENT_ERR:
    default:
        break;
    }
}

void walkie_av_stop(void); /* walkie_av_start 失败回滚用 */

int walkie_av_start(void)
{
    union audio_req req = {0};

    if (s_av.run) {
        return 0;
    }

    bt_music_audio_takeover();

    s_av.tx_buf = zalloc(TX_CACHE);
    s_av.rx_buf = zalloc(RX_CACHE);
    if (!s_av.tx_buf || !s_av.rx_buf) {
        free(s_av.tx_buf);
        free(s_av.rx_buf);
        s_av.tx_buf = s_av.rx_buf = NULL;
        return -1;
    }
    cbuf_init(&s_av.tx_cbuf, s_av.tx_buf, TX_CACHE);
    cbuf_init(&s_av.rx_cbuf, s_av.rx_buf, RX_CACHE);
    os_sem_create(&s_av.rx_sem, 0);

    s_av.enc = server_open("audio_server", "enc");
    s_av.dec = server_open("audio_server", "dec");
    if (!s_av.enc || !s_av.dec) {
        printf("[walkie] audio_server open fail\n");
        walkie_av_stop();
        return -1;
    }
    /* 切勿把事件注册到 lcd_test: PTT 在 UI 任务里 server_request(ENC_CLOSE)
     * 时 wait_enc_stop 会等事件, 事件又等 lcd_test → 死锁卡死. */
    server_register_event_handler_to_task(s_av.enc, NULL, av_event, "app_core");
    server_register_event_handler_to_task(s_av.dec, NULL, av_event, "app_core");

    memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = s_av.volume;
    req.dec.output_buf_len = 6 * 1024;
    req.dec.channel = WALKIE_PCM_CH;
    req.dec.sample_rate = WALKIE_PCM_RATE;
    req.dec.vfs_ops = &s_av_vfs;
    req.dec.dec_type = "pcm";
    req.dec.sample_source = CONFIG_AUDIO_DEC_PLAY_SOURCE;
    if (server_request(s_av.dec, AUDIO_REQ_DEC, &req)) {
        printf("[walkie] dec open fail\n");
        walkie_av_stop();
        return -1;
    }
    req.dec.cmd = AUDIO_DEC_START;
    server_request(s_av.dec, AUDIO_REQ_DEC, &req);

    s_av.run = 1;
    s_av.tx_on = 0;
    printf("[walkie] av ready (dns/vad official path)\n");
    return 0;
}

void walkie_av_stop(void)
{
    union audio_req req = {0};

    s_av.run = 0;
    s_av.tx_on = 0;
    s_av.local_play = 0;
    os_sem_post(&s_av.rx_sem);

    if (s_av.enc) {
        memset(&req, 0, sizeof(req));
        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(s_av.enc, AUDIO_REQ_ENC, &req);
        server_close(s_av.enc);
        s_av.enc = NULL;
    }
    if (s_av.dec) {
        memset(&req, 0, sizeof(req));
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(s_av.dec, AUDIO_REQ_DEC, &req);
        server_close(s_av.dec);
        s_av.dec = NULL;
    }
    free(s_av.tx_buf);
    free(s_av.rx_buf);
    s_av.tx_buf = s_av.rx_buf = NULL;
}

/* 对讲用 PTT 门控, 不依赖 VAD; DNS 失败则回退裸 PCM, 保证能说话 */
static int walkie_enc_open_try(u8 dns)
{
    union audio_req req = {0};
    u16 frame = (u16)(WALKIE_PCM_RATE / 100 * 4 * WALKIE_PCM_CH);
    int err;

    memset(&req, 0, sizeof(req));
    req.enc.cmd = AUDIO_ENC_OPEN;
    req.enc.channel = WALKIE_PCM_CH;
#ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
    req.enc.channel_bit_map = BIT(CONFIG_PHONE_CALL_ADC_CHANNEL);
#endif
    req.enc.volume = CONFIG_AUDIO_ADC_GAIN;
    req.enc.frame_size = frame;
    /* DNS 流水线缓冲要更大, 原厂 demo 常用 8K 量级 */
    req.enc.output_buf_len = 8 * 1024;
    req.enc.sample_rate = WALKIE_PCM_RATE;
    req.enc.format = "pcm";
    req.enc.sample_source = "mic";
    req.enc.vfs_ops = &s_av_vfs;
    req.enc.msec = 0;
#if defined CONFIG_DNS_ENC_ENABLE
    req.enc.dns_enable = dns ? 1 : 0;
#else
    (void)dns;
#endif

    err = server_request(s_av.enc, AUDIO_REQ_ENC, &req);
    if (err) {
        memset(&req, 0, sizeof(req));
        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(s_av.enc, AUDIO_REQ_ENC, &req);
        printf("[walkie] enc open fail dns=%d err=%d\n", dns, err);
    } else {
        printf("[walkie] enc open ok dns=%d\n", dns);
    }
    return err;
}

void walkie_av_tx_start(void)
{
    if (!s_av.run || s_av.tx_on || !s_av.enc) {
        return;
    }
    if (walkie_msg_tx_begin()) {
        printf("[walkie] msg tx begin fail\n");
        return;
    }

    /* 半双工: 硬 MUTE 功放, 避免 PA→MIC 啸叫(硬件侧, 非改 PCM) */
    walkie_spk_pause(1);
    os_time_dly(3);

#ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
    adc_multiplex_set_gain("mic", BIT(CONFIG_PHONE_CALL_ADC_CHANNEL),
                           CONFIG_AUDIO_ADC_ANALOG_GAIN);
#endif

#if defined CONFIG_DNS_ENC_ENABLE
    if (walkie_enc_open_try(1) == 0) {
        s_av.tx_on = 1;
        return;
    }
#endif
    if (walkie_enc_open_try(0) == 0) {
        s_av.tx_on = 1;
        return;
    }

    walkie_msg_tx_cancel();
    walkie_spk_pause(0);
}

void walkie_av_tx_stop(void)
{
    union audio_req req = {0};

    if (!s_av.tx_on) {
        walkie_msg_tx_cancel();
        return;
    }
    s_av.tx_on = 0;
    if (s_av.enc) {
        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(s_av.enc, AUDIO_REQ_ENC, &req);
    }
    /* 松键: 停止采集后整包发送(+ACK 重传) */
    walkie_msg_tx_commit();
    walkie_spk_pause(0);
}

void walkie_av_rx_pcm(const void *pcm, u16 len)
{
    if (!s_av.run || s_av.tx_on || s_av.local_play || !pcm || !len) {
        return;
    }
    cbuf_write(&s_av.rx_cbuf, (void *)pcm, len);
    os_sem_set(&s_av.rx_sem, 0);
    os_sem_post(&s_av.rx_sem);
}

void walkie_av_local_play_begin(void)
{
    if (!s_av.run) {
        return;
    }
    s_av.local_play = 1;
    cbuf_clear(&s_av.rx_cbuf);
}

void walkie_av_local_play_end(void)
{
    s_av.local_play = 0;
}

u8 walkie_av_local_play_is_on(void)
{
    return s_av.local_play;
}

int walkie_av_local_play_write(const void *pcm, u16 len)
{
    u32 free;
    int guard = 0;

    if (!s_av.run || !s_av.local_play || !pcm || !len) {
        return -1;
    }
    while (s_av.run && s_av.local_play) {
        free = cbuf_is_write_able(&s_av.rx_cbuf, len);
        if (free >= len) {
            break;
        }
        os_time_dly(1);
        if (++guard > 200) {
            break;
        }
    }
    if (cbuf_write(&s_av.rx_cbuf, (void *)pcm, len) != len) {
        return -1;
    }
    os_sem_set(&s_av.rx_sem, 0);
    os_sem_post(&s_av.rx_sem);
    return 0;
}

void walkie_av_local_play_drain(void)
{
    int guard = 0;

    while (s_av.run && s_av.local_play && cbuf_get_data_len(&s_av.rx_cbuf) > 0) {
        os_time_dly(1);
        if (++guard > 500) {
            break;
        }
    }
}

#else /* !MINI_AUDIO_SUPPORT */

int walkie_av_start(void)
{
    return -1;
}
void walkie_av_stop(void) {}
void walkie_av_tx_start(void) {}
void walkie_av_tx_stop(void) {}
void walkie_av_rx_pcm(const void *pcm, u16 len)
{
    (void)pcm;
    (void)len;
}
void walkie_av_local_play_begin(void) {}
void walkie_av_local_play_end(void) {}
u8 walkie_av_local_play_is_on(void)
{
    return 0;
}
int walkie_av_local_play_write(const void *pcm, u16 len)
{
    (void)pcm;
    (void)len;
    return -1;
}
void walkie_av_local_play_drain(void) {}

#endif
