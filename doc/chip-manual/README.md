# AC7912BB（WL82 / AC791N）芯片参考手册

依据官方 SDK（fw-AC79_AIoT_SDK 原始导出）头文件逆向整理的 HTML 版芯片参考手册。
寄存器偏移/基址均可复核：`include_lib/driver/cpu/wl82/asm/WL82.h`、`csfr.h`
（本仓库旁路路径 `D:\fw-AC79_AIoT_SDK`）；标注【实测】的条目来自 fw-mini-sdk
移植与闭源库反汇编验证（USB EP DMA 桥、EMI 写语义、UART putbyte、AEC 等）。

## 阅读

顶栏导航按「核心 / 基础外设 / 多媒体 / 系统」分组：核心页平铺，其余下拉；窄屏用 ☰ 打开全部章节。

直接双击 `index.html`（纯静态页面，无外部依赖），或：

```
cd doc/chip-manual
python -m http.server 8931
# 浏览器打开 http://127.0.0.1:8931/
```

## 章节目录（21 页）

| 文件 | 内容 |
|---|---|
| index.html | 芯片总览、三总线外设地址映射、启动/固件布局 |
| cpu-mem.html | 双核、映射、**JL_SDR/PSRAM 寄存器**、isd/API、链接槽位 |
| cache-mmu.html | **I$/D$ way、非缓存窗口、MMU/TLB、corex2/EMU、DBG_MSG 对照** |
| clock.html | 时钟树（OSC/PLL/PLL2/PLL3→SYS/HSB/LSB）、isd 烧录、分频公式、JL_CLOCK 位域、运行时 API |
| gpio.html | PORT A~H 寄存器、IOMAP、唤醒 |
| irq.html | 66 个中断源、request_irq 绑核 |
| uart.html | UART0-2、CON0 位域、环形 DMA、putbyte【实测】 |
| spi.html | SPI0-3、API 标志、DMA/ioctl |
| iic.html | 硬件 CON0 位域 + 软件 I²C |
| timer.html | TIMER0-5、PCNT/GPCNT |
| mcpwm.html | 全寄存器表、通道掩码、ioctl |
| adc.html | LADC、通道、adkey 实测 |
| audio.html | 原厂 DEC/ENC/**EQ·DRC·音效**/AEC·DNS·VAD 清单 + **双核分工** |
| usb.html | FUSB+HUSB、EP RAM DMA 桥、RAM0 约束【实测】 |
| sdmmc.html | SD0/SD1、检卡、离线升级联动 |
| sfc-sec.html | SFC/SFCENC、VM、AES/SHA/CRC/RAND/EFUSE |
| display.html | EMI/IMD、**ui_platform pi32v2 加速**、**LVGL 官方绑定** |
| camera.html | ISC/JPEG、ISP、平台数据 |
| power.html | 电源域、P33、WDT、充电 |
| wifi-bt.html | WiFi4+BT/BLE：WL 表、库任务、共存 |
| misc.html | WDT/RDEC/IR/CTMU/FFT/烧录速览 |

## 重新生成

页面由 Python 生成器产出（改内容请编辑生成器后重跑，勿直接改 HTML）：

```
cd doc/chip-manual/build
python build.py
```

- `gen_core.py` —— 页面模板 / `bit_table`·`reg_detail`·`sdk_csfr_regs` / SVG DSL
- `regs_wl82.py` —— 解析 **WL82.h**：外设基址 + 结构体偏移
- `regs_csfr.py` —— 解析 **csfr.h**：CMNG/FFT/corex2/q32DSP（含 REV_* 填充）
- `regs_bits.py` —— 位宏表（头文件明确位 + debug.c 异常名；未公开则标注）
- `pages_core.py` —— 总览 / CPU / **Cache-MMU** / 时钟 / GPIO / 中断
- `pages_periph1.py` —— UART / SPI / IIC / TIMER / MCPWM
- `pages_periph2.py` —— ADC / 音频 / USB / SDMMC
- `pages_sys.py` —— Flash 加密 / 显示 / 摄像头 / 电源 / WiFi-BT / 其他

## 准确性说明

- 外设：偏移 = `WL82.h` 成员序×4；基址 = 段基址 + (64×grp + adr)×4。
- 核 SFR：`csfr_base=0x1EE0000`；corex2 @ +0xE000；q32DSP(n) @ +0xF000+n×0x200。
- 位定义仅收录 SDK 头文件 / `debug.c` 明确给出的部分；ANA/MCPWM/WL 等未公开位以 API 为准。
- 【实测】来自 fw-mini-sdk 上板/反汇编，换板时注意甄别。
