/*
 * 内置音频自检 (MINI_AUDIO_SUPPORT, 默认 0)
 *
 * 模板: 原厂 apps/common/example/Devkitboard_test/mic_test.c(混响回环)。
 * 流程: audio_server("enc") MIC 16k 单声道采 PCM → 环形缓冲 →
 *       audio_server("dec") DAC 播放 → 10s 后停 3s 循环。
 * 用途: 验证内置 MIC 采集与 DAC 播放全链(回环听感/日志判据),
 *       上板时接耳机/喇叭在 MIC 处应听到延迟回声。
 */
#include "app_config.h"

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT

#include "typedef.h"
#include "os/os_api.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "generic/circular_buf.h"
#include "asm/wdt.h"

#define AUDIO_DEMO_RATE       16000
#define AUDIO_DEMO_PLAY_MS    10000   /* 回环时长 */
#define AUDIO_DEMO_IDLE_MS    3000    /* 停顿时长 */
#define CACHE_BUF_LEN         (8 * 1024)

struct audio_demo_hdl {
    struct server *enc_server;
    struct server *dec_server;
    u8 *cache_buf;
    cbuffer_t cbuf;
    OS_SEM r_sem;
    volatile u8 run_flag;
    u8 volume;
};
static struct audio_demo_hdl demo = {
    .volume = 80,
};
#define __this (&demo)

/* ---- vfs: 编码输出 → cbuf → 解码输入 (mic_test.c 同款) ---- */
static int demo_vfs_fwrite(void *file, void *data, u32 len)
{
    cbuf_write(&__this->cbuf, data, len);
    os_sem_set(&__this->r_sem, 0);
    os_sem_post(&__this->r_sem);
    return len;
}

static int demo_vfs_fread(void *file, void *data, u32 len)
{
    u32 rlen;
    do {
        rlen = cbuf_read(&__this->cbuf, data, len);
        if (rlen == len) {
            break;
        }
        os_sem_pend(&__this->r_sem, 30);
    } while (__this->run_flag);
    return len;
}

static int demo_vfs_fclose(void *file)
{
    return 0;
}

static int demo_vfs_flen(void *file)
{
    return 0;
}

static const struct audio_vfs_ops demo_vfs_ops = {
    .fwrite = demo_vfs_fwrite,
    .fread  = demo_vfs_fread,
    .fclose = demo_vfs_fclose,
    .flen   = demo_vfs_flen,
};

static void demo_event_handler(void *priv, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        printf("[audio] server event %d\n", argv[0]);
        __this->run_flag = 0;
        break;
    default:
        break;
    }
}

static void demo_stop(void)
{
    union audio_req req = {0};

    if (!__this->run_flag) {
        return;
    }
    __this->run_flag = 0;
    os_sem_post(&__this->r_sem);

    if (__this->dec_server) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    }
    if (__this->enc_server) {
        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
    }
    if (__this->dec_server) {
        server_close(__this->dec_server);
        __this->dec_server = NULL;
    }
    if (__this->enc_server) {
        server_close(__this->enc_server);
        __this->enc_server = NULL;
    }
    printf("[audio] loopback stopped\n");
}

static int demo_start(void)
{
    union audio_req req = {0};
    int err;

    if (__this->run_flag) {
        return -1;
    }
    printf("[audio] loopback start: mic %dHz -> dac\n", AUDIO_DEMO_RATE);

    if (!__this->enc_server) {
        __this->enc_server = server_open("audio_server", "enc");
        if (!__this->enc_server) {
            printf("[audio] enc server open fail\n");
            return -1;
        }
        server_register_event_handler(__this->enc_server, NULL, demo_event_handler);
    }
    if (!__this->dec_server) {
        __this->dec_server = server_open("audio_server", "dec");
        if (!__this->dec_server) {
            printf("[audio] dec server open fail\n");
            return -1;
        }
    }

    if (!__this->cache_buf) {
        __this->cache_buf = malloc(CACHE_BUF_LEN);
        if (!__this->cache_buf) {
            printf("[audio] no mem\n");
            return -1;
        }
    }
    cbuf_init(&__this->cbuf, __this->cache_buf, CACHE_BUF_LEN);
    os_sem_create(&__this->r_sem, 0);
    __this->run_flag = 1;

    /* MIC 采集(内置模拟 MIC1, 单声道 PCM) */
    req.enc.cmd            = AUDIO_ENC_OPEN;
    req.enc.channel        = 1;
    req.enc.volume         = CONFIG_AUDIO_ADC_GAIN;
    req.enc.output_buf     = NULL;
    req.enc.output_buf_len = 8 * 1024;
    req.enc.sample_rate    = AUDIO_DEMO_RATE;
    req.enc.format         = "pcm";
    req.enc.frame_size     = AUDIO_DEMO_RATE / 100 * 4;
    req.enc.sample_source  = "mic";
    req.enc.vfs_ops        = &demo_vfs_ops;
    req.enc.msec           = 0;              /* 不限时长, 由任务控制启停 */
    err = server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
    if (err) {
        printf("[audio] enc open err %d\n", err);
        goto err;
    }

    /* DAC 播放 */
    memset(&req, 0, sizeof(req));
    req.dec.cmd            = AUDIO_DEC_OPEN;
    req.dec.volume         = __this->volume;
    req.dec.output_buf     = NULL;
    req.dec.output_buf_len = 4 * 1024;
    req.dec.channel        = 1;
    req.dec.sample_rate    = AUDIO_DEMO_RATE;
    req.dec.priority       = 1;
    req.dec.vfs_ops        = &demo_vfs_ops;
    req.dec.dec_type       = "pcm";
    req.dec.sample_source  = "dac";
    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        printf("[audio] dec open err %d\n", err);
        goto err;
    }
    req.dec.cmd = AUDIO_DEC_START;
    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        printf("[audio] dec start err %d\n", err);
        goto err;
    }
    return 0;

err:
    demo_stop();
    return -1;
}

/* 周期回环自检: 10s 开 + 3s 停 */
static void audio_demo_task(void *arg)
{
    (void)arg;
    os_time_dly(300);                        /* 等系统/VM 稳定 */

    while (1) {
        if (demo_start() == 0) {
            int waited = 0;
            while (__this->run_flag && waited < AUDIO_DEMO_PLAY_MS) {
                os_time_dly(10);             /* 100ms */
                waited += 100;
                wdt_clear();
            }
            demo_stop();
        }
        os_time_dly(AUDIO_DEMO_IDLE_MS / 10);
    }
}

void audio_demo_start(void)
{
    printf("[audio] demo task create\n");
    os_task_create(audio_demo_task, NULL, 10, 1024, 0, "audio_demo");
}

#endif  /* MINI_AUDIO_SUPPORT */
