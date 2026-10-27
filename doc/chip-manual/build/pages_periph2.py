# -*- coding: utf-8 -*-
"""pages_periph2.py — ADC / 音频 / USB(双控) / SDMMC"""
from gen_core import *
import regs_wl82 as R

CHIP_NOTE = callout("note",
    "依据官方 SDK <code>asm/ladc.h·dac.h·audio.h·usb.h·sdmmc.h</code>、"
    "<code>server/audio_server.h</code> 及 fw-mini-sdk 移植实测整理。")


# ================================================================ ADC
def build_adc():
    body = base_badge("ADC") + p(
        "LADC 逐次逼近 ADC, 硬件寄存器仅 2 个(0x13100 区), 通道/增益等由 ANA 模拟前端"
        "(0x11900 区, 54 寄存器)配置, 应用层全部经 <code>asm/adc_api.h</code> + "
        "audio 框架的 adc 驱动访问。")
    body += sec("ch", "通道映射(ladc.h)", table(
        ["类型", "通道", "引脚"], [
            ("MIC 差分", "LADC_CH_MIC0_P_N", "PA2 / PA1"),
            ("MIC 差分", "LADC_CH_MIC1_P_N", "PH8 / PH9"),
            ("MIC 差分", "LADC_CH_MIC2_P_N", "PA4 / PA3"),
            ("MIC 差分", "LADC_CH_MIC3_P_N", "PH5 / PH6"),
            ("LINEIN(AUX)", "LADC_CH_AUX0~3", "PA0 / PH7 / PA5 / PH4"),
        ])
        + p("模拟直通: ladc_mic_to_daclr()/ladc_linein_to_daclr(MIC/LINEIN 不经数字域直达 DAC)。")
        + callout("note", "【实测】ADC 按键(adkey): PB01 挂电阻梯, 10bit 原始值 "
                  "空闲 1014(3.13V)/按键1 162(0.5V)/按键2 0(0V), 10ms 扫描+中点阈值表匹配。"))

    body += sec("api", "ADC API", code("""/* adc_api.h */
u32 adc_get_value(u32 ch);       /* 通道原始值 */
adc_init();                      /* 早期初始化 */
/* 录音路径: audio_server enc 请求 sample_source="mic"/"linein"(详见音频章) */"""))
    body += CHIP_NOTE
    page("adc.html", "ADC", "LADC 采集", "4 组差分 MIC + 4 路 AUX, ANA 前端, adkey 实测参数", body)


# ================================================================ AUDIO
def build_audio():
    s = Svg(900, 330)
    s.box(30, 30, 130, 50, ["MIC ×4 差分", "LADC"], "ana")
    s.box(30, 100, 130, 50, ["PLNK0/1", "数字麦 PDM"], "ana")
    s.box(30, 170, 130, 50, ["IIS0/1", "外部codec"], "ana")
    s.box(30, 240, 130, 50, ["SPDIF"], "ana")
    s.box(220, 90, 150, 60, ["AUDIO 引擎", "0x12F00 · 23 寄存器"], "peri")
    for y in (55, 125, 195, 265):
        s.arrow(160, y, 220, 120, color="#7c2d92")
    s.box(420, 30, 150, 50, ["SRC 变采样", "0x14300"], "peri")
    s.box(420, 100, 150, 50, ["EQ/ANC", "0x1E000/0x1F000"], "peri")
    s.box(420, 170, 150, 50, ["SBC 编解码", "0x13D00"], "peri")
    s.box(420, 240, 150, 50, ["ALNK0/1", "音频链路中断"], "peri")
    s.arrow(370, 120, 420, 55); s.arrow(370, 120, 420, 125)
    s.arrow(370, 120, 420, 195); s.arrow(370, 120, 420, 265)
    s.box(630, 90, 150, 60, ["DAC", "hw_channel 0x05", "4 数字通道"], "ana")
    s.arrow(570, 55, 630, 105); s.arrow(570, 125, 630, 120); s.arrow(570, 265, 630, 140)
    s.text(450, 315, "服务化框架: audio_server.a — enc/dec 请求 + AEC/DNS/VAD(闭源 aec_enc.c)", 12)

    body = fig(s.render(), "音频子系统框图") + p(
        "音频走 <b>server 框架</b>(liba/audio_server.a): 应用以 union audio_req 向 "
        "audio_server 发 ENC/DEC 请求; 采集源 mic/linein/plnk/iis/spdif, 播放走 DAC。"
        "AEC 回采=编码请求 aec_enable=1 + aec_s_attr(server/audio_dev.h), "
        "回采通道 BIT(CONFIG_AEC_ADC_CHANNEL)。")
    body += sec("dac", "DAC 播放", base_badge("ANA") + table(["参数", "值(DevKitBoard 参考)", "说明"], [
        ("hw_channel", "0x05", "DAC 模拟通道"),
        ("ch_num", "4", "数字通道数"),
        ("pa_auto_mute/pa_mute_port", "1 / PB02", "功放 MUTE 控制(按板改)"),
        ("vcm_init_delay_ms", "1000", "VCM 稳定等待"),
        ("fade/mute_delay", "40 / 200 ms", "模拟音量淡入淡出"),
    ]) + code("""/* 播放(Deckitboard_test/mic_test.c 同款) */
req.dec.cmd = AUDIO_DEC_OPEN;  req.dec.dec_type = "pcm";
req.dec.sample_source = "dac"; req.dec.volume = 80;
req.dec.vfs_ops = &my_vfs_ops;          /* 数据源回调 */
server_request(dec, AUDIO_REQ_DEC, &req);
req.dec.cmd = AUDIO_DEC_START; server_request(dec, AUDIO_REQ_DEC, &req);"""))
    body += sec("enc", "MIC 录音与 AEC", code("""req.enc.cmd = AUDIO_ENC_OPEN;
req.enc.sample_source = "mic";  req.enc.format = "pcm";
req.enc.sample_rate = 16000;    req.enc.channel = 1;
req.enc.channel_bit_map = BIT(1);            /* 通话 MIC 通道 */
/* AEC(参考 demo_edr/bt_decode.c): */
struct aec_s_attr a = {0};
a.EnableBit = AEC_EN|NLP_EN|ANS_EN;          /* AEC+NLP+ANS */
a.wideband = 1;  a.hw_delay_offset = 50;     /* 16k 宽带 */
req.enc.aec_attr = &a;  req.enc.aec_enable = 1;
req.enc.channel_bit_map |= BIT(CONFIG_AEC_ADC_CHANNEL);  /* 回采通道 */""") +
        callout("note", "【实测】AEC 依赖链: audio_server.a + libaec.a + libdns.a + libjlsp.a "
                  "+ 源码 jl_math(flrfft/flrifft)。VAD 也走 enc(use_vad 字段)。"))
    body += sec("regs", "AUDIO/ANA 寄存器(自动提取)", sdk_regs("AUDIO") +
                callout("note", "ANA 54 寄存器与 ALNK/PLNK/SRC/SBC/EQ/ANC 各自独立基址, "
                          "位域未公开, 全部经驱动 API 访问。"))
    body += CHIP_NOTE
    page("audio.html", "Audio", "音频系统",
         "LADC/PLNK/IIS 采集 → SRC/EQ/ANC → DAC, audio_server 框架与 AEC 回采", body)


# ================================================================ USB
def build_usb():
    body = p("两个独立 USB 控制器共用 D+/D- 引脚(不可同时启动):")
    body += table(["控制器", "基址", "规格", "角色"], [
        ("USB0 (FUSB)", "<code>0x11800</code>", "全速 12Mbps", "设备/主机(小缓冲)"),
        ("USB1 (HUSB)", "<code>0x12800</code>", "高速 480Mbps", "设备/主机/OTG"),
    ])
    body += sec("regs", "HUSB 寄存器(USB1, 自动提取)", sdk_regs("USB1", {
        "FADDR/POWER/INTRTX/INTRRX": "标准 MUSB 风格: 地址/电源/端点中断",
        "FRAME/INDEX/TXMAXP/CSRL": "帧号/端点窗口/端点最大包/状态",
        "RXMAXP/RXCSRH/RXCOUNT": "OUT 端点配置与计数",
        "TXFIFOsz 配置由硬件固定": "JL 实现无 FIFOSZ 寄存器",
    }) + callout("warn", "【实测·重要】MUSB 兼容内核的三处 JL 差异(逆向 usb.a 得出): "
        "① TXIS/RXIS 窗口读自动清除(写 1 清除无效); "
        "② EP0 只有 FIFO 端口 0x16020, EP≥1 无 FIFO 端口——数据面走 RAM DMA 缓冲; "
        "③ 无 FIFOSZ 寄存器, EP FIFO 容量硬件固定。"))

    body += sec("dma", "EP 数据面 RAM DMA【实测逆向】",
        p("EP≥1 的收发不经 FIFO 端口, 而是桥接寄存器指向的 RAM 缓冲(<b>仅 RAM0 段</b>):")
        + table(["地址", "名称", "说明"], [
            ("<code>0x16800</code>", "桥接配置", "EP DMA 总开关/映射"),
            ("<code>0x16804+4×ep</code>", "TXCNT(ep)", "发送字节计数(必须先于 TXRDY 写, 否则发 0 长度包)"),
            ("<code>0x16820+8×(ep-1)</code>", "TADDR(ep)", "发送缓冲地址(RAM0!)"),
            ("<code>0x16824+8×(ep-1)</code>", "RADDR(ep)", "接收缓冲地址(RAM0!)"),
            ("<code>0x16850 / 0x16854</code>", "EP3/EP4 大缓冲模式", "TXCSR/RXCSR bit14 使能"),
        ])
        + ul([
            "EP1/2/5/6: 普通 DMA 模式, 缓冲需 64 字节对齐",
            "EP3/4: 大缓冲模式, 计数打包 (dma_size&lt;&lt;16)|len|0x80000000, 8 字节对齐, 单次可搬 4096B",
            "主机模式(MRX): 多包聚合, Inclusive/Exclusive 分组读",
            "【实测】SDRAM 缓冲会静默丢数据——DMA 只能到 0x1C00000 RAM0 段",
        ]))

    body += sec("stack", "协议栈与实测结论",
        ul([
            "官方栈(liba/lib_usb_syn.a + driver/usb): 多复合设备(CDC×n+MSC+UAC…), usb_epbuf_manager 用 lbuf 统一分发 EP 缓冲",
            "fw-mini-sdk 换用 CherryUSB(port/jl82 适配上述 DMA 语义), MSC 4MB RAM 盘 + 虚拟U盘 OTA 实测通过",
            "【实测】INT 端点需置 TXCSR.H bit14(CSRH bit6), 否则 clear-halt 死循环",
            "【实测】主机 EP0 控制传输完成中断不触发, 需轮询 CSR0",
        ]))
    body += CHIP_NOTE
    page("usb.html", "USB", "USB 双控制器",
         "FUSB1.1 + HUSB2.0: MUSB 差异、EP RAM DMA 桥(0x16800)、EP3/4 大缓冲【实测逆向】", body)


# ================================================================ SDMMC
def build_sd():
    body = base_badge("SD0") + p(
        "SD0/SD1 两组(0x11400/0x11500 区)。SDK 支持 1/4 线、CMD 检卡或 IO 检卡; "
        "板级 sd0_data 定义端口组('A'~'D')与优先级。")
    body += sec("regs", "SD 寄存器", sdk_regs("SD0", {
        "CON": "使能/位宽/时钟/忙检测",
        "CNT": "块计数", "ARG": "命令参数", "CMD": "命令与响应",
        "BUF": "数据 FIFO", "DMA": "数据搬运",
    }))
    body += sec("api", "板级与检测", code("""SD0_PLATFORM_DATA_BEGIN(sd0_data)
    .port = 'A', .priority = 3, .data_width = 1,
    .speed = 20000000, .detect_mode = SD_CMD_DECT,
SD0_PLATFORM_DATA_END();
/* fs 挂载: fopen("storage/sd0/C/...") */""") +
        callout("note", "【实测】sd_update2.bin/usb_update2.bin 是 isd_download 产出的 "
                  "U 盘/SD 卡离线升级载荷(uboot 读取), 非运行时 API。"))
    body += CHIP_NOTE
    page("sdmmc.html", "SDMMC", "SD 卡控制器", "SD0/SD1: 寄存器、位宽、检卡方式、离线升级联动", body)


if __name__ == "__main__":
    build_adc(); build_audio(); build_usb(); build_sd()
