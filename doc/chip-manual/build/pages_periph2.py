# -*- coding: utf-8 -*-
"""pages_periph2.py — ADC / 音频 / USB / SDMMC（加深）"""
from gen_core import *
import regs_wl82 as R
import regs_bits as B

CHIP_NOTE = callout("note",
    "依据官方 SDK <code>asm/ladc.h·dac.h·audio.h·usb.h·sdmmc.h</code>、"
    "<code>server/audio_server.h</code> 及 fw-mini-sdk 移植实测。")


# ================================================================ ADC
def build_adc():
    body = ""
    body += sec("inst", "实例 / 基址",
        base_badge("ADC")
        + p("LADC 数字侧仅 2 寄存器；通道/增益由 ANA（"
          + R.fmt_base("ANA") + "，54 寄存器）配置。"
          "应用经 <code>adc_api.h</code> / audio enc 访问。IRQ_SARADC=24。"))

    body += sec("regs", "寄存器",
        sdk_regs("ADC", {"CON": "控制（位域未公开）", "RES": "转换结果"})
        + callout("note", B.unpublished_note("ADC/ANA"))
        + table(["类型", "通道", "引脚"], [
            ("MIC 差分", "LADC_CH_MIC0_P_N", "PA2 / PA1"),
            ("MIC 差分", "LADC_CH_MIC1_P_N", "PH8 / PH9"),
            ("MIC 差分", "LADC_CH_MIC2_P_N", "PA4 / PA3"),
            ("MIC 差分", "LADC_CH_MIC3_P_N", "PH5 / PH6"),
            ("LINEIN", "LADC_CH_AUX0~3", "PA0 / PH7 / PA5 / PH4"),
        ]))

    body += sec("init", "用法",
        code("""adc_init();
u32 v = adc_get_value(ch);
/* 直通: ladc_mic_to_daclr() / ladc_linein_to_daclr()
 * 录音: audio_server enc sample_source=\"mic\"/\"linein\" */"""))

    body += sec("note", "约束【实测】",
        callout("note", "adkey：PB01 电阻梯，10bit 空闲≈1014 / KEY1≈162 / KEY2≈0；"
                "10ms 扫描 + 中点阈值表。采样前 gpio_set_die(脚,0)。"))
    body += CHIP_NOTE
    page("adc.html", "ADC", "LADC 采集", "寄存器、通道、adkey 实测", body)


# ================================================================ AUDIO
def build_audio():
    s = Svg(920, 360)
    s.box(20, 20, 120, 45, ["MIC/LINEIN", "LADC+ANA"], "ana", fs=11)
    s.box(20, 80, 120, 45, ["PLNK0/1", "PDM 数字麦"], "ana", fs=11)
    s.box(20, 140, 120, 45, ["IIS0/1", "外置 codec"], "ana", fs=11)
    s.box(20, 200, 120, 45, ["SPDIF"], "ana", fs=11)
    s.box(180, 80, 140, 70, ["AUDIO 引擎", R.fmt_base("AUDIO")], "peri", fs=12)
    for y in (42, 102, 162, 222):
        s.arrow(140, y, 180, 115, color="#7c2d92")
    s.box(360, 20, 130, 45, ["HW EQ", R.fmt_base("EQ") if R.base_of("EQ") else "0x1E000"], "mem", fs=11)
    s.box(360, 80, 130, 45, ["ANC", R.fmt_base("ANC") if R.base_of("ANC") else "0x1F000"], "mem", fs=11)
    s.box(360, 140, 130, 45, ["SRC", R.fmt_base("SRC") if R.base_of("SRC") else "—"], "peri", fs=11)
    s.box(360, 200, 130, 45, ["SBC/FFT", "硬加速"], "peri", fs=11)
    s.box(360, 260, 130, 45, ["ALNK0/1", "链路 IRQ"], "peri", fs=11)
    s.arrow(320, 115, 360, 42); s.arrow(320, 115, 360, 100)
    s.arrow(320, 115, 360, 160); s.arrow(320, 115, 360, 220)
    s.box(540, 70, 160, 90, ["audio_server.a", "DEC/ENC/MIX", "闭源服务"], "cpu", fs=12)
    s.arrow(490, 115, 540, 115)
    s.box(740, 40, 150, 55, ["DAC 播放", "sample_source"], "ana", fs=11)
    s.box(740, 120, 150, 55, ["编码文件/流", "opus/mp3/…"], "peri", fs=11)
    s.box(740, 200, 150, 55, ["BT/UAC/AI", "通话·播歌"], "rf", fs=11)
    s.arrow(700, 100, 740, 65); s.arrow(700, 115, 740, 145); s.arrow(700, 130, 740, 225)
    s.text(20, 340, "软件音效: EQ/DRC/动态EQ/混响/回声/变声/VBass…（effects_adj）+ AEC/NLP/ANS/DNS/VAD（lib）", 11)

    body = ""
    body += sec("arch", "架构总览",
        fig(s.render(), "采集 → 硬加速/audio_server → 播放/编码/协议栈")
        + p("应用只面对 <code>server_open(\"audio_server\", …)</code> + "
            "<code>union audio_req</code>（DEC/ENC/IOCTL）。"
            "硬件位域多未公开；音效/通话算法在闭源库 + 可选 HW EQ/ANC/SRC/SBC。"))

    body += sec("hw", "硬件模块与中断",
        table(["模块", "基址(约)", "IRQ", "职责"], [
            ("AUDIO", R.fmt_base("AUDIO"), "13", "数字音频引擎 FIFO/通路"),
            ("ANA", R.fmt_base("ANA"), "—", "模拟前端（MIC/增益等，54 寄存器）"),
            ("ADC/LADC", R.fmt_base("ADC"), "24", "逐次逼近采样数字侧"),
            ("ALNK0/1", "见 WL82.h", "11/12", "音频链路中断"),
            ("PLNK0/1", "见 WL82.h", "38/39", "PDM 数字麦"),
            ("SRC", R.fmt_base("SRC") if R.base_of("SRC") else "—", "58", "硬件变采样"),
            ("EQ + EQ_RAM", R.fmt_base("EQ") if R.base_of("EQ") else "0x1E000", "60",
             "硬件 IIR EQ，最多约 20 段（EQ_SECTION_MAX）"),
            ("ANC + ANC_RAM", R.fmt_base("ANC") if R.base_of("ANC") else "0x1F000", "—",
             "主动降噪协处理（系数在 ANC_RAM）"),
            ("SBC", R.fmt_base("SBC") if R.base_of("SBC") else "—", "46", "SBC/MSBC 硬编解码"),
            ("FFT", "csfr JL_FFT", "57", "频谱/算法加速"),
        ])
        + sdk_regs("AUDIO")
        + callout("note", B.unpublished_note("AUDIO/ANA/EQ/ANC CON")))

    body += sec("feat-dec", "原厂能力①：解码播放（audio_server DEC）",
        table(["能力", "开关/字段", "说明"], [
            ("多格式解码", "dec_type", "pcm/mp3/wav/ogg/amr/opus/aac/sbc/…（按链接的 decoder）"),
            ("输出设备", "sample_source", '"dac" / "iis0" / "iis1"'),
            ("叠音 MIX", "CONFIG_AUDIO_MIX_ENABLE", "多路 DEC 混到同一 DAC；任务 audio_mix 优先级 28"),
            ("EQ", "AUDIO_ATTR_EQ_EN", "硬件/文件系数；可 EQ32BIT_EN"),
            ("DRC", "AUDIO_ATTR_DRC_EN", "须同时开 EQ"),
            ("变速变调", "AUDIO_ATTR_PS_EN + speedV/pitchV", "CONFIG_AUDIO_PS_ENABLE"),
            ("伴奏/声道", "LR_SUB / LR_ADD", "双声道伴奏或左右叠加"),
            ("淡入淡出", "AUDIO_ATTR_FADE_INOUT", "模拟音量 fade"),
            ("循环/AB 复读", "REPEAT / AB_REPEAT_*", "本地教育机常用"),
            ("断点续播", "audio_dec_breakpoint", "ape 等大断点缓冲"),
            ("频谱/数字音量", "AUDIO_EFFECT_*", "SPECTRUM_FFT / DIGITAL_VOL"),
            ("蓝牙 AAC", "AUDIO_ATTR_BT_AAC_EN", "BT 播歌"),
            ("实时性", "AUDIO_ATTR_REAL_TIME", "BT 时钟同步，解码不可堵"),
            ("强制变采样", "orig_sr / force_sr", "混响等场景"),
        ])
        + code("""/* local_music.c 典型 */
req.dec.attr = AUDIO_ATTR_PS_EN;
#if TCFG_EQ_ENABLE
req.dec.attr |= AUDIO_ATTR_EQ_EN | AUDIO_ATTR_EQ32BIT_EN;
#endif
req.dec.sample_source = "dac";
server_request(dec, AUDIO_REQ_DEC, &req);"""))

    body += sec("feat-eq", "原厂能力②：EQ / DRC / 在线音效工具链",
        p("头文件：<code>media/eq/audio_eq.h</code>、<code>media/drc/*</code>、"
            "<code>media/effects_adj.h</code>、<code>media/audio_eff_default_parm.h</code>。")
        + ul([
            "<b>硬件 EQ</b>：IIR 类型 HP/LP/BP/HighShelf/LowShelf；"
            "段数 ≤ EQ_SECTION_MAX(20)；增益约 ±12dB；支持异步 EQ、淡入更新系数",
            "<b>预设模式</b>：NORMAL/ROCK/POP/CLASSIC/JAZZ/COUNTRY/CUSTOM",
            "<b>系数来源</b>：EQ_TYPE_FILE（eq_cfg_hw.bin）/ ONLINE（PC 在线调）/ MODE_TAB",
            "<b>高低音</b>：high_bass（默认约 125Hz / 12kHz，增益 ±12）",
            "<b>动态 EQ</b>：DynamicEQ_api + Detection",
            "<b>多段 DRC</b>：低/中/高/全带 + crossover；FR/RL/RR 分通路",
            "<b>MIC 混响链</b>（effects_adj 命令字）：Plate Reverb、Echo、NoiseGate、"
            "啸叫抑制(Howling/Notch)、VoiceChanger、Mix Gain（KTV 类）",
            "<b>音乐链扩展</b>：VBass 虚拟低音、Surround、声道互换、LPF、"
            "DAC/ADC PGA 工具参数、通话上下行宽/窄带 EQ+DRC",
            "<b>配置</b>：TCFG_EQ_ENABLE / TCFG_EQ_FILE_ENABLE / TCFG_EQ_ONLINE_ENABLE；"
            "文件路径如 <code>mnt/sdfile/res/cfg/eq_cfg_hw.bin</code>",
        ])
        + callout("note", "大量音效模块在 effects_adj.h 中以宏注释关闭"
                  "（howling/reverb/vbass…），量产工程按产品打开对应 .a/.o。"))

    body += sec("feat-enc", "原厂能力③：录音编码 / VAD / AEC / DNS",
        table(["能力", "字段", "说明"], [
            ("采集源", "sample_source",
             '"mic","linein","plnk0/1","iis0/1","spdif","virtual"'),
            ("编码格式", "format", "pcm/opus/speex/amr/adpcm/msbc/sbc/ogg…"),
            ("VAD", "use_vad=1/2", "旧算法/JL 新算法；事件 SPEAK_START/STOP"),
            ("AEC", "aec_enable + aec_attr", "蓝牙通话回声消除（需回采通道）"),
            ("DNS", "dns_enable", "神经网络降噪（单/双麦，syscfg SMS/DMS_DNS）"),
            ("直通 DAC", "direct2dac / high_gain", "AD 直通播放"),
            ("通道交换", "ch_data_exchange", "差分回采与 MIC 数据交换"),
        ])
        + p("<code>struct aec_s_attr</code>（audio_dev.h）模块位（demo 定义）：")
        + code("""#define AEC_EN   BIT(0)   /* 线性 AEC */
#define NLP_EN   BIT(1)   /* 非线性处理 */
#define ANS_EN   BIT(2)   /* 传统降噪 */
/* AEC_MODE_ADVANCE = AEC|NLP|ANS ; REDUCE = NLP|ANS ; SIMPLEX = ANS */
aec_param.EnableBit = AEC_MODE_ADVANCE;
aec_param.wideband = 1;           /* 16k */
aec_param.ul_eq_en = 1;           /* 上行 EQ */
aec_param.agc_en = …;             /* AGC 一组 dB 参数 */
/* 另有 ES_* / ANS_* / AEC_DT_* / DNS_gain_floor / DNS_over_drive */""")
        + callout("note", "【实测】AEC 依赖链：audio_server.a + libaec.a + libdns.a "
                  "+ libjlsp.a + jl_math(FFT)。回采通道 "
                  "<code>channel_bit_map |= BIT(CONFIG_AEC_ADC_CHANNEL)</code>。"))

    body += sec("tasks", "音频相关任务（task_info_table）",
        table(["任务名", "典型优先级", "角色"], [
            ("audio_server", "16", "服务主循环，接 server_request"),
            ("audio_decoder", "30", "解码线程（很高，保证播歌实时）"),
            ("audio_encoder", "12", "编码线程"),
            ("audio_mix", "28", "叠音混合"),
            ("aec_encoder", "13", "AEC 处理"),
            ("dns_encoder", "13", "DNS 降噪"),
            ("vad_encoder", "14~16", "VAD"),
            ("*_encoder", "10~16", "mp3/opus/speex/amr/sbc/msbc/…"),
            ("echo_deal", "11", "回声相关处理"),
            ("uac_play/record", "7~26", "USB Audio 类"),
            ("resample_task", "8", "软变采样（部分工程）"),
        ])
        + p("库内按<strong>名字</strong> <code>task_create</code>，表项缺失会创建失败。"
            "demo_audio / demo_DevKitBoard / fw-mini-sdk（MINI_AUDIO_SUPPORT）同款登记。"))

    body += sec("cpu-audio", "做音频时双核如何分工",
        p("WL82 无「音频固定跑 CPU1」的硬绑定；分工靠 "
            "<b>任务名绑核前缀</b>、<b>irq_info_table</b> 与 <b>优先级</b>。")
        + table(["机制", "写法", "音频含义"], [
            ("任务绑核", '"#C0name" / "#C1name"',
             "OS 按名前缀把任务钉在 CPU0/1；"
             "原厂 demo 里 BT 常用 #C0btctrler/#C0btstack，"
             "<b>音频任务名一般不带 #Cx</b>（可迁移）"),
            ("中断绑核", "irq_info_table {irq, prio, cpu}",
             "AUDIO/EQ/SRC/ALNK 等 demo 未强制；"
             "SOFT5→CPU0、SOFT4→CPU1 常见"),
            ("优先级隔离", "decoder=30, mix=28",
             "播歌时抢占 app_core(15)/WiFi tasklet(10)，降低断音"),
        ])
        + ul([
            "<b>播歌/本地解码</b>：audio_decoder + audio_mix 吃满一个核的算力窗口；"
            "另一核继续跑 app_core / 网络 / UI。EQ 尽量走 <b>HW EQ</b>，减 CPU 负担",
            "<b>蓝牙通话</b>：BT 控制器/协议栈常钉 <b>CPU0</b>（#C0）；"
            "MIC→aec_encoder→dns_encoder→编码 与 DAC 回放在高优先级音频任务上，"
            "与 BT 同核时需靠优先级错开，或把重算法任务显式 #C1",
            "<b>AEC/DNS</b>：软件算法（可借 FFT 硬核），CPU 密集；"
            "宽频 16k + AEC|NLP|ANS|DNS 同时开时建议避免同核再跑 WiFi 重负载",
            "<b>KTV/多段音效</b>：MIC 链（混响/啸叫）+ 音乐链（EQ/DRC/VBass）"
            "数据流通路长，优先 HW EQ + 降低段数/关 ONLINE 调试",
            "<b>UAC</b>：uac_play/record 与 usb_server；双核时可用 #C0usb_msd0 模式减轻冲突",
        ])
        + code("""/* 可选：把 AEC 钉到 CPU1，与 #C0 BT 错开（需改任务名并同步库创建名） */
{"#C1aec_encoder", 13, 1024, 0},
{"#C1dns_encoder", 13, 512, 0},
/* 中断示例：EQ 硬中断放到 CPU1 */
{ IRQ_EQ_IDX /*60*/, 5, 1 },"""))

    body += sec("cpu-daily", "日常状态下双核分工（非音频专用）",
        table(["角色", "CPU0（习惯主核）", "CPU1（习惯从核）"], [
            ("系统", "app_core、sys_event、sys_timer、多数驱动回调",
             "可跑协处理任务；软中断 SOFT4 常挂此核"),
            ("连接", "BT：#C0btctrler / #C0btstack / #C0btencry（原厂双核 demo）",
             "未钉核的 WiFi tasklet/MLME/CMD 可被调度过来"),
            ("网络", "部分工程把 WiFi 留在默认核；与 BT 同核时靠优先级",
             "适合扛 RtmpMlmeTask / 重收发（需实测调 prio）"),
            ("音视频", "audio_server 控制面；USB/UI 事件",
             "高负载 decoder/enc/jpg 可自然漂过来，或显式 #C1"),
            ("中断", "EXCEPTION 等关键；irq 未列表则多落创建核策略",
             "irq_info 第三列 =1 强制到 CPU1"),
        ])
        + callout("warn", "「日常」并非芯片固化分工，而是 SDK demo 约定："
                  "CPU0=交互+协议栈控制面，CPU1=可扩展算力。"
                  "音频是否占满某一核取决于是否开 MIX/EQ/AEC/DNS 与采样率。"
                  "改绑核后必须保证 <code>task_info_table</code> 名字与库内 "
                  "<code>task_create(\"…\")</code> 一致（含 #C0/#C1 前缀）。"))

    body += sec("init", "最小闭环示例",
        code("""/* 播放 */
req.dec.cmd = AUDIO_DEC_OPEN;
req.dec.dec_type = "pcm";
req.dec.sample_source = "dac";
req.dec.volume = 80;
server_request(dec, AUDIO_REQ_DEC, &req);
req.dec.cmd = AUDIO_DEC_START;
server_request(dec, AUDIO_REQ_DEC, &req);

/* 录音 + AEC */
req.enc.cmd = AUDIO_ENC_OPEN;
req.enc.sample_source = "mic";
req.enc.format = "pcm";
req.enc.sample_rate = 16000;
req.enc.aec_enable = 1;
req.enc.aec_attr = &aec_param;
req.enc.channel_bit_map |= BIT(CONFIG_AEC_ADC_CHANNEL);
server_request(enc, AUDIO_REQ_ENC, &req);"""))

    body += sec("note", "约束与库依赖",
        ul([
            "设备表需注册 {\"audio\", &audio_dev_ops}（ops 在 audio_server.a）",
            "板级 dac/adc/iis/plnk 平台数据见 asm/audio.h → audio_pf_data",
            "模拟直通：ladc_mic_to_daclr() / ladc_linein_to_daclr()",
            "ANC 硬件块在 WL82.h，上层 API/产品化路径因方案而异，故事机等工程不一定启用",
        ]))
    body += CHIP_NOTE
    page("audio.html", "Audio", "音频系统",
         "原厂 DEC/ENC/EQ/DRC/AEC/DNS 能力清单 + 双核分工", body)


# ================================================================ USB
def build_usb():
    body = ""
    body += sec("inst", "双控制器",
        table(["控制器", "基址", "规格", "IRQ"], [
            ("USB0 FUSB", R.fmt_base("USB"), "FS 12Mbps", "8/9 SOF/CTRL"),
            ("USB1 HUSB", R.fmt_base("USB1"), "HS 480Mbps", "53/54"),
        ])
        + callout("warn",
                  "共用 D+/D-，不可同时启动。"
                  "高速 USB 需 USB PLL3=480 MHz；官方配套 "
                  "<code>SYS_CLK=240M</code> + <code>SDRAM_PLL3_EN=1</code>"
                  "（SDR=PLL3/2=240M）。详见 <a href=\"clock.html#usb240\">时钟章</a>。"))

    body += sec("regs", "HUSB 寄存器(USB1)",
        sdk_regs("USB1", {
            "FADDR": "设备地址", "POWER": "电源/HS",
            "INTRTX": "TX 中断", "INTRRX": "RX 中断",
            "INTRUSB": "USB 中断", "INTRUSBE": "USB 中断使能",
            "FRAME": "帧号", "INDEX": "端点窗口",
            "TXMAXP": "TX 最大包", "TXCSR": "TX 状态",
            "RXMAXP": "RX 最大包", "RXCSR": "RX 状态",
            "RXCOUNT": "RX 计数", "TXTYPE": "主机 TX 类型",
            "TXINTERVAL": "轮询间隔", "RXTYPE": "主机 RX 类型",
        })
        + callout("note", B.unpublished_note("USB CON 细位（按 MUSB 语义 + JL 差异）")))

    body += sec("dma", "EP DMA 桥与流程【实测】",
        table(["地址", "名称", "说明"], [
            ("0x16800", "桥配置", "EP DMA 总开关"),
            ("0x16804+4×ep", "TXCNT", "先于 TXRDY 写长度"),
            ("0x16820+8×(ep-1)", "TADDR", "TX 缓冲（须 RAM0）"),
            ("0x16824+8×(ep-1)", "RADDR", "RX 缓冲（须 RAM0）"),
            ("0x16850/54", "EP3/4 大缓冲", "TXCSR/RXCSR bit14"),
        ])
        + code("""/* 设备 OUT→应用（示意） */
flushinv_dcache(rx_buf, len);          /* 或 NO_CACHE_ADDR */
/* 硬件写入 RADDR 后读 RXCOUNT；缓冲必须在 0x1C00000 RAM0 */""")
        + ul([
            "EP≥1 无 FIFO 端口，仅 RAM DMA；TXIS/RXIS 读清（非 W1C）",
            "EP1/2/5/6：64B 对齐；EP3/4 大缓冲：8B 对齐，单次可达 4096B",
            "【实测】SDRAM 缓冲静默丢数",
        ]))

    body += sec("note", "协议栈",
        ul([
            "官方 lib_usb_syn.a；fw-mini-sdk 用 CherryUSB port/jl82",
            "【实测】INT 端点需 TXCSR.H bit14；主机 EP0 完成中断不触发→轮询 CSR0",
        ]))
    body += CHIP_NOTE
    page("usb.html", "USB", "USB 双控制器",
         "寄存器、EP RAM DMA 桥、RAM0 约束【实测】", body)


# ================================================================ SDMMC
def build_sd():
    body = ""
    body += sec("inst", "实例",
        table(["实例", "基址", "IRQ"], [
            ("SD0", R.fmt_base("SD0"), "17"),
            ("SD1", R.fmt_base("SD1"), "18"),
        ])
        + p("1/4 线、CMD/IO 检卡；板级 <code>SDn_PLATFORM_DATA</code>。"))

    body += sec("regs", "寄存器",
        sdk_regs("SD0", {
            "CON": "使能/位宽/时钟/忙",
            "CNT": "块计数", "ARG": "命令参数", "CMD": "命令与响应",
            "BUF": "FIFO", "DMA": "数据搬运",
        })
        + callout("note", B.unpublished_note("SD CON")))

    body += sec("init", "用法",
        code("""SD0_PLATFORM_DATA_BEGIN(sd0_data)
    .port = 'A', .priority = 3, .data_width = 1,
    .speed = 20000000, .detect_mode = SD_CMD_DECT,
SD0_PLATFORM_DATA_END();
/* fs: fopen(\"storage/sd0/C/...\") */"""))

    body += sec("note", "约束",
        callout("note", "【实测】sd_update2.bin / usb_update2.bin 为 uboot 离线升级载荷，"
                "非运行时 API。"))
    body += CHIP_NOTE
    page("sdmmc.html", "SDMMC", "SD 卡控制器", "双实例寄存器 + 平台数据", body)


if __name__ == "__main__":
    build_adc(); build_audio(); build_usb(); build_sd()
