#ifndef APP_ROOBO_ASR_H
#define APP_ROOBO_ASR_H

#include "generic/typedef.h"

/* 命中回调: text 为 UTF-8 中文(静态或内部缓冲, 勿 free) */
typedef void (*roobo_asr_result_cb)(int word_id, const char *text_utf8, void *priv);

int roobo_asr_start(u16 sample_rate, roobo_asr_result_cb cb, void *priv);
void roobo_asr_stop(void);
u8 roobo_asr_is_running(void);

#endif
