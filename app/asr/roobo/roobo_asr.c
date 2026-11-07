#include "server/audio_server.h"
#include "server/server_core.h"
#include "generic/circular_buf.h"
#include "os/os_api.h"
#include "app_config.h"
#include "fig_asr_api.h"
#include "roobo_asr.h"
#include <string.h>

#if (defined CONFIG_ASR_ALGORITHM) && (CONFIG_ASR_ALGORITHM == ROOBO_ALGORITHM)

#include "resmapping_table.h"

#define ONCE_SR_POINTS  160   /* 10ms @16k, 与官方 roobo.c 一致 */
#define ASR_BUF_SIZE    (ONCE_SR_POINTS * 2 * 4)

#ifndef CONFIG_AISP_MIC0_ADC_CHANNEL
#define CONFIG_AISP_MIC0_ADC_CHANNEL   CONFIG_PHONE_CALL_ADC_CHANNEL
#endif
#ifndef CONFIG_AISP_MIC_ADC_GAIN
#define CONFIG_AISP_MIC_ADC_GAIN       80
#endif
#ifndef CONFIG_AISP_MIC_ANALOG_GAIN
#define CONFIG_AISP_MIC_ANALOG_GAIN    12
#endif

extern void adc_multiplex_set_gain(const char *source, u8 channel_bit_map, u8 gain);

/* 引擎输出 GBK, UI 需要 UTF-8 */
static const char gbk_xiao_ai[] = {0xd0, 0xa1, 0xb0, 0xac, 0xd0, 0xa1, 0xb0, 0xac, 0};
static const char gbk_play[]    = {0xb2, 0xa5, 0xb7, 0xc5, 0xd2, 0xf4, 0xc0, 0xd6, 0};
static const char gbk_pause[]   = {0xd4, 0xdd, 0xcd, 0xa3, 0xd2, 0xf4, 0xc0, 0xd6, 0};
static const char gbk_vol_up[]  = {0xd4, 0xf6, 0xb4, 0xf3, 0xd2, 0xf4, 0xc1, 0xbf, 0};
static const char gbk_vol_dn[]  = {0xbc, 0xf5, 0xd0, 0xa1, 0xd2, 0xf4, 0xc1, 0xbf, 0};
static const char gbk_prev[]    = {0xc9, 0xcf, 0xd2, 0xbb, 0xc7, 0xfa, 0};
static const char gbk_next[]    = {0xcf, 0xc2, 0xd2, 0xbb, 0xc7, 0xfa, 0};

static const char *gbk_to_utf8(const char *gbk)
{
    if (!gbk) {
        return "未知指令";
    }
    if (!memcmp(gbk, gbk_xiao_ai, 8)) {
        return "小艾小艾";
    }
    if (!memcmp(gbk, gbk_play, 8)) {
        return "播放音乐";
    }
    if (!memcmp(gbk, gbk_pause, 8)) {
        return "暂停音乐";
    }
    if (!memcmp(gbk, gbk_vol_up, 8)) {
        return "增大音量";
    }
    if (!memcmp(gbk, gbk_vol_dn, 8)) {
        return "减小音量";
    }
    if (!memcmp(gbk, gbk_prev, 6)) {
        return "上一曲";
    }
    if (!memcmp(gbk, gbk_next, 6)) {
        return "下一曲";
    }
    return "识别到命令";
}

static struct {
    int pid;
    u16 sample_rate;
    u8 volatile exit_flag;
    u8 volatile run_flag;
    u8 opened;
    OS_SEM sem;
    s16 mic_buf[ASR_BUF_SIZE];
    void *mic_enc;
    cbuffer_t mic_cbuf;
    FIG_INST fig;
    roobo_asr_result_cb result_cb;
    void *result_priv;
} s_asr;

u8 roobo_asr_is_running(void)
{
    return s_asr.opened && !s_asr.exit_flag;
}

static void asr_enc_event(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    (void)argv;
}

static int asr_vfs_fwrite(void *file, void *data, u32 len)
{
    cbuffer_t *cbuf = (cbuffer_t *)file;
    u32 wlen = cbuf_write(cbuf, data, len);

    if (wlen != len) {
        cbuf_clear(&s_asr.mic_cbuf);
    }
    os_sem_set(&s_asr.sem, 0);
    os_sem_post(&s_asr.sem);
    return (int)len;
}

static int asr_vfs_fclose(void *file)
{
    (void)file;
    return 0;
}

static const struct audio_vfs_ops asr_vfs_ops = {
    .fwrite = asr_vfs_fwrite,
    .fclose = asr_vfs_fclose,
};

static void asr_enc_stop(void)
{
    union audio_req req = {0};

    if (!s_asr.run_flag || !s_asr.mic_enc) {
        s_asr.run_flag = 0;
        return;
    }
    s_asr.run_flag = 0;
    req.enc.cmd = AUDIO_ENC_STOP;
    server_request(s_asr.mic_enc, AUDIO_REQ_ENC, &req);
    cbuf_clear(&s_asr.mic_cbuf);
}

static int asr_enc_start(void)
{
    union audio_req req = {0};

    if (s_asr.run_flag || !s_asr.mic_enc) {
        return 0;
    }

#ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
    adc_multiplex_set_gain("mic", BIT(CONFIG_AISP_MIC0_ADC_CHANNEL),
                           CONFIG_AISP_MIC_ANALOG_GAIN);
#endif

    s_asr.run_flag = 1;
    os_sem_set(&s_asr.sem, 0);
    os_sem_post(&s_asr.sem);

    req.enc.cmd = AUDIO_ENC_OPEN;
    req.enc.channel = 1;
#ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
    req.enc.channel_bit_map = BIT(CONFIG_AISP_MIC0_ADC_CHANNEL);
#endif
    req.enc.frame_size = ONCE_SR_POINTS * 2;
    req.enc.volume = CONFIG_AISP_MIC_ADC_GAIN;
    req.enc.sample_rate = s_asr.sample_rate;
    req.enc.format = "pcm";
    req.enc.sample_source = "mic";
    req.enc.vfs_ops = &asr_vfs_ops;
    req.enc.output_buf_len = req.enc.frame_size * 8;
    req.enc.file = (FILE *)&s_asr.mic_cbuf;

    if (server_request(s_asr.mic_enc, AUDIO_REQ_ENC, &req)) {
        s_asr.run_flag = 0;
        printf("[roobo] enc open fail\n");
        return -1;
    }
    printf("[roobo] enc open ok sr=%u\n", s_asr.sample_rate);
    return 0;
}

static int asr_engine_open(void)
{
    int ret = FigCreateInst(&s_asr.fig, (const char *)nnet, (const char *)graph_cmd);
    if (ret) {
        printf("[roobo] FigCreateInst fail %d\n", ret);
        s_asr.fig = NULL;
        return -1;
    }
    ret = FigStartProcess(s_asr.fig);
    if (ret) {
        printf("[roobo] FigStartProcess fail %d\n", ret);
        FigDestroyInst(s_asr.fig);
        s_asr.fig = NULL;
        return -1;
    }
    printf("[roobo] fig asr ready\n");
    return 0;
}

static void asr_engine_close(void)
{
    if (!s_asr.fig) {
        return;
    }
    FigStopProcess(s_asr.fig);
    FigDestroyInst(s_asr.fig);
    s_asr.fig = NULL;
}

static void asr_task(void *priv)
{
    (void)priv;

    if (asr_engine_open()) {
        goto __exit;
    }
    if (asr_enc_start()) {
        goto __exit;
    }

    while (!s_asr.exit_flag) {
        if (!s_asr.run_flag) {
            os_sem_pend(&s_asr.sem, 0);
            continue;
        }
        if (cbuf_get_data_size(&s_asr.mic_cbuf) < ONCE_SR_POINTS * 2) {
            os_sem_pend(&s_asr.sem, 0);
            continue;
        }

        short frame[ONCE_SR_POINTS];
        PAsrResult pResult = NULL;
        int ret;

        if (!cbuf_read(&s_asr.mic_cbuf, frame, sizeof(frame))) {
            continue;
        }

        ret = FigWriteAudio(s_asr.fig, (char *)frame, (int)sizeof(frame), 0, &pResult);
        if (ret != 0) {
            printf("[roobo] FigWriteAudio err %d\n", ret);
            continue;
        }
        if (pResult) {
            const char *utf8 = gbk_to_utf8(pResult->szText);
            printf("[roobo] hit id=%d text=%s\n", pResult->nWordId, utf8);
            if (s_asr.result_cb) {
                s_asr.result_cb(pResult->nWordId, utf8, s_asr.result_priv);
            }
        }
    }

__exit:
    asr_enc_stop();
    asr_engine_close();
    s_asr.run_flag = 0;
    printf("[roobo] task exit\n");
}

int roobo_asr_start(u16 sample_rate, roobo_asr_result_cb cb, void *priv)
{
    if (s_asr.opened) {
        return 0;
    }

    memset(&s_asr, 0, sizeof(s_asr));
    s_asr.sample_rate = sample_rate ? sample_rate : 16000;
    s_asr.result_cb = cb;
    s_asr.result_priv = priv;

    s_asr.mic_enc = server_open("audio_server", "enc");
    if (!s_asr.mic_enc) {
        printf("[roobo] audio_server open fail\n");
        return -1;
    }
    server_register_event_handler_to_task(s_asr.mic_enc, NULL, asr_enc_event, "app_core");
    cbuf_init(&s_asr.mic_cbuf, s_asr.mic_buf, sizeof(s_asr.mic_buf));
    os_sem_create(&s_asr.sem, 0);

    if (thread_fork("roobo_asr", 3, 3072, 0, &s_asr.pid, asr_task, NULL)) {
        server_close(s_asr.mic_enc);
        s_asr.mic_enc = NULL;
        printf("[roobo] thread fork fail\n");
        return -1;
    }
    s_asr.opened = 1;
    printf("[roobo] start ok\n");
    return 0;
}

void roobo_asr_stop(void)
{
    if (!s_asr.opened) {
        return;
    }

    asr_enc_stop();
    s_asr.exit_flag = 1;
    os_sem_post(&s_asr.sem);

    if (s_asr.mic_enc) {
        server_close(s_asr.mic_enc);
        s_asr.mic_enc = NULL;
    }
    thread_kill(&s_asr.pid, KILL_WAIT);
    s_asr.opened = 0;
    s_asr.result_cb = NULL;
    s_asr.result_priv = NULL;
    printf("[roobo] stop ok\n");
}

#else /* !ROOBO */

u8 roobo_asr_is_running(void)
{
    return 0;
}

int roobo_asr_start(u16 sample_rate, roobo_asr_result_cb cb, void *priv)
{
    (void)sample_rate;
    (void)cb;
    (void)priv;
    return -1;
}

void roobo_asr_stop(void)
{
}

#endif
