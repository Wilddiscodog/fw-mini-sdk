# -*- coding: utf-8 -*-
"""gen_core.py — AC7912BB(WL82) 手册生成框架：页面模板 / 组件 / SVG 绘图 DSL
   (风格移植自 QQ-SDK AC6956C 手册, 品牌与组件按 AC7912BB 调整)"""

import html as _html
import os

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# (file, 短标题, 英文副标, icon) — 顺序即上一页/下一页
PAGES = [
    ("index.html",     "芯片总览",   "Overview",            "🏛"),
    ("cpu-mem.html",   "CPU 与存储", "Dual-Core / SDRAM",   "🧠"),
    ("cache-mmu.html", "Cache / MMU", "I$/D$ / TLB / EMU",  "🗄"),
    ("clock.html",     "时钟系统",   "CLK / PLL",           "⏱"),
    ("gpio.html",      "GPIO 与复用", "PORT A-H / IOMAP",   "🔌"),
    ("irq.html",       "中断系统",   "66 Sources",          "⚡"),
    ("uart.html",      "UART 串口",  "UART ×3",             "📡"),
    ("spi.html",       "SPI 总线",   "SPI ×4",              "🔗"),
    ("iic.html",       "IIC 总线",   "I²C",                 "🔗"),
    ("timer.html",     "定时器",     "TIMER ×6 / PCNT",     "⏲"),
    ("mcpwm.html",     "MCPWM",      "PWM",                 "📊"),
    ("adc.html",       "ADC 采集",   "LADC / AUX",          "🎚"),
    ("audio.html",     "音频系统",   "AUDIO / DAC / ALNK",  "🎵"),
    ("usb.html",       "USB 双控",   "FUSB + HUSB",         "🔌"),
    ("sdmmc.html",     "SD 卡",      "SDMMC ×2",            "💾"),
    ("sfc-sec.html",   "Flash 与加密", "SFC / AES / SHA",   "🔏"),
    ("display.html",   "显示接口",   "EMI / PAP / IMD",     "🖥"),
    ("camera.html",    "摄像头与编解码", "ISC / JPEG",      "📷"),
    ("power.html",     "电源管理",   "PMU / P33",           "🔋"),
    ("wifi-bt.html",   "WiFi / BT 射频", "WL Baseband",     "📶"),
    ("misc.html",      "其他外设",   "WDT/RTC/RDEC/...",    "📦"),
]

# 顶栏分组：首组平铺，其余下拉（避免标签溢出）
NAV_GROUPS = [
    ("核心", ["index.html", "cpu-mem.html", "cache-mmu.html", "clock.html"]),
    ("基础外设", ["gpio.html", "irq.html", "uart.html", "spi.html",
               "iic.html", "timer.html", "mcpwm.html"]),
    ("多媒体", ["adc.html", "audio.html", "usb.html", "sdmmc.html",
              "display.html", "camera.html"]),
    ("系统", ["sfc-sec.html", "power.html", "wifi-bt.html", "misc.html"]),
]

_PAGE_TITLE = {f: t for f, t, e, i in PAGES}


def esc(s):
    return _html.escape(str(s))


def _nav_html(fname):
    """分组导航：核心平铺 + 其余下拉菜单。"""
    parts = []
    for gi, (gname, files) in enumerate(NAV_GROUPS):
        in_group = fname in files
        if gi == 0:
            for f in files:
                act = " active" if f == fname else ""
                parts.append(f'<a class="nav-a{act}" href="{f}">{esc(_PAGE_TITLE[f])}</a>')
            continue
        # 下拉
        act = " open-active" if in_group else ""
        items = "".join(
            f'<a href="{f}"{" class=active" if f == fname else ""}>{esc(_PAGE_TITLE[f])}</a>'
            for f in files)
        cur = _PAGE_TITLE[fname] if in_group else gname
        parts.append(
            f'<div class="nav-drop{act}">'
            f'<button type="button" class="nav-drop-btn" aria-expanded="false">'
            f'{esc(gname)}'
            f'<span class="nav-drop-cur">{esc(cur) if in_group else ""}</span>'
            f'<span class="caret">▾</span></button>'
            f'<div class="nav-drop-menu">{items}</div></div>')
    # 移动端：全部章节面板
    all_items = []
    for gname, files in NAV_GROUPS:
        all_items.append(f'<div class="nav-all-g">{esc(gname)}</div>')
        for f in files:
            act = " class=active" if f == fname else ""
            all_items.append(f'<a href="{f}"{act}>{esc(_PAGE_TITLE[f])}</a>')
    parts.append(
        '<button type="button" class="nav-more-btn" aria-label="全部章节">☰</button>'
        f'<div class="nav-all-panel">{"".join(all_items)}</div>')
    return "".join(parts)


def page(fname, crumb, title, lead, body):
    idx = [p[0] for p in PAGES].index(fname)
    prev_p = PAGES[idx - 1] if idx > 0 else None
    next_p = PAGES[idx + 1] if idx < len(PAGES) - 1 else None

    nav = _nav_html(fname)

    foot_prev = (f'<a href="{prev_p[0]}">← {esc(prev_p[1])}</a>' if prev_p else '<span></span>')
    foot_next = (f'<a href="{next_p[0]}">{esc(next_p[1])} →</a>' if next_p else '<span></span>')

    doc = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)} — AC7912BB 参考手册</title>
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<nav class="topbar"><div class="topbar-inner">
  <a class="brand" href="index.html">
    <span class="logo">AC7912BB<em>·WL82</em></span>
    <span class="sub">芯片参考手册</span>
  </a>
  <div class="navlinks">{nav}</div>
</div></nav>
<script>
(function(){{
  document.querySelectorAll('.nav-drop-btn').forEach(function(btn){{
    btn.addEventListener('click', function(e){{
      e.stopPropagation();
      var drop = btn.parentElement;
      var open = drop.classList.contains('open');
      document.querySelectorAll('.nav-drop.open').forEach(function(d){{ d.classList.remove('open'); }});
      document.querySelector('.nav-all-panel') && document.querySelector('.nav-all-panel').classList.remove('open');
      if (!open) drop.classList.add('open');
      btn.setAttribute('aria-expanded', (!open).toString());
    }});
  }});
  var more = document.querySelector('.nav-more-btn');
  var panel = document.querySelector('.nav-all-panel');
  if (more && panel) {{
    more.addEventListener('click', function(e){{
      e.stopPropagation();
      document.querySelectorAll('.nav-drop.open').forEach(function(d){{ d.classList.remove('open'); }});
      panel.classList.toggle('open');
    }});
  }}
  document.addEventListener('click', function(){{
    document.querySelectorAll('.nav-drop.open').forEach(function(d){{ d.classList.remove('open'); }});
    if (panel) panel.classList.remove('open');
  }});
}})();
</script>

<div class="wrap">
  <header class="hero">
    <div class="crumb">{esc(crumb)}</div>
    <h1>{esc(title)}</h1>
    <p>{lead}</p>
  </header>

  <div class="layout">
    <main class="main">
{body}
      <div class="footer">
        {foot_prev}
        <span class="src">依据 JieLi 官方 SDK 头文件整理 · AC7912BB (WL82 / AC791N) · 仅供开发参考</span>
        {foot_next}
      </div>
    </main>
    <aside class="toc"><div class="t">本页目录</div>{toc_of(body)}</aside>
  </div>
</div>
</body>
</html>"""
    path = os.path.join(OUT, fname)
    with open(path, "w", encoding="utf-8") as f:
        f.write(doc)
    print("write", fname)


import re
def toc_of(body):
    out = []
    for m in re.finditer(r'<h2 id="([^"]+)"[^>]*>(.*?)</h2>', body):
        out.append(f'<a href="#{m.group(1)}">{re.sub("<[^>]+>", "", m.group(2))}</a>')
    return "".join(out)


# ---------------------------------------------------------------- 组件
def sec(id_, title, inner):
    return f'<section class="card" id="{id_}">\n<h2 id="{id_}">{title}</h2>\n{inner}\n</section>\n'


def p(inner):
    return f"<p>{inner}</p>\n"


def ul(items):
    return "<ul>" + "".join(f"<li>{i}</li>" for i in items) + "</ul>\n"


def table(headers, rows, cls=""):
    h = "".join(f"<th>{c}</th>" for c in headers)
    b = ""
    for r in rows:
        b += "<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>\n"
    return (f'<div class="tbl-wrap"><table class="tbl {cls}">'
            f"<thead><tr>{h}</tr></thead><tbody>\n{b}</tbody></table></div>\n")


def rw(tag):
    m = {"RW": "rw", "RO": "ro", "WO": "wo"}
    return f'<span class="rw {m.get(tag, "rw")}">{tag}</span>'


def regtable(regs):
    """寄存器总表: (offset, name, rw, desc)"""
    rows = []
    for off, name, mode, desc in regs:
        rows.append((f'<code class="rg">0x{off:02X}</code>',
                     f'<code class="rg">{name}</code>', rw(mode), desc))
    return table(["偏移", "寄存器", "属性", "说明"], rows)


def bits(rows):
    rr = [(f'<span class="bits-chip">{b}</span>', f"<code class='rg'>{n}</code>", d)
          for b, n, d in rows]
    return table(["位域", "名称", "说明"], rr)


def bit_table(key_or_rows, title=None):
    """位域表：传入 regs_bits 键名或 [(bits,name,desc),...]。"""
    import regs_bits as B
    if isinstance(key_or_rows, str):
        rows = B.bits_of(key_or_rows)
        if not rows:
            return callout("warn", B.unpublished_note(key_or_rows))
        head = title or ("位域 — %s" % key_or_rows)
    else:
        rows = key_or_rows
        head = title or "位域"
    body = bits(rows)
    return (f"<h3>{esc(head)}</h3>\n" if head else "") + body


def reg_detail(regs, bit_key=None):
    """寄存器总表 [(off,name,rw,desc)] + 可选关键位域键。"""
    html = regtable(regs)
    if bit_key:
        html += bit_table(bit_key)
    return html


def code(txt, lang="c"):
    txt = esc(txt)
    return f"<pre><code>{txt}</code></pre>\n"


def callout(kind, text):
    return f'<div class="{kind}">{text}</div>\n'


def grid(cards, cols="g3"):
    inner = "".join(
        f'<{"a href=" + chr(34) + c[3] + chr(34) if len(c) > 3 else "div"} class="pcard">'
        f'<div class="ic">{c[0]}</div><div class="nm">{c[1]}</div>'
        f'<div class="ds">{c[2]}</div>'
        f'{"</a>" if len(c) > 3 else "</div>"}'
        for c in cards)
    return f'<div class="grid {cols}">{inner}</div>\n'


def kpis(items):
    inner = "".join(f'<div class="kpi"><div class="v">{v}</div>'
                    f'<div class="k">{k}</div></div>' for v, k in items)
    return f'<div class="grid g4">{inner}</div>\n'


def fig(svg, cap=""):
    c = f'<div class="figcap">{cap}</div>' if cap else ""
    return f'<div class="fig">{svg}{c}</div>\n'


# ---------------------------------------------------------------- SVG DSL
class Svg:
    PALETTE = {
        "cpu":   ("#2456d6", "#e8eefc"),
        "bus":   ("#8a94a6", "#eef1f7"),
        "peri":  ("#1e7f4f", "#e6f5ec"),
        "mem":   ("#b7791f", "#fdf3e3"),
        "ana":   ("#7c2d92", "#f4ebfa"),
        "dark":  ("#14264d", "#dbe4f5"),
        "rf":    ("#b83232", "#fceaea"),
    }

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.parts = []

    def box(self, x, y, w, h, lines, kind="peri", fs=13, bold_first=True, rx=9):
        stroke, fill = self.PALETTE[kind]
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>')
        n = len(lines)
        lh = fs + 4
        y0 = y + h / 2 - (n - 1) * lh / 2 + fs * 0.35
        for i, t in enumerate(lines):
            weight = "800" if i == 0 and bold_first and n > 1 else "500"
            size = fs if i == 0 and bold_first else fs - 1.5
            color = "#14264d" if i == 0 else "#4a5568"
            self.parts.append(
                f'<text x="{x + w / 2}" y="{y0 + i * lh}" text-anchor="middle" '
                f'font-family="Segoe UI,PingFang SC,Microsoft YaHei,sans-serif" '
                f'font-size="{size}" font-weight="{weight}" fill="{color}">{esc(t)}</text>')
        return self

    def arrow(self, x1, y1, x2, y2, label="", dashed=False, color="#5b6b8c"):
        dash = ' stroke-dasharray="5,4"' if dashed else ""
        self.parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" '
            f'stroke-width="1.6" marker-end="url(#arw)"{dash}/>')
        if label:
            mx, my = (x1 + x2) / 2, (y1 + y2) / 2
            self.parts.append(
                f'<text x="{mx}" y="{my - 5}" text-anchor="middle" font-size="11" '
                f'fill="#5b6b8c" font-family="Segoe UI,sans-serif">{esc(label)}</text>')
        return self

    def line(self, x1, y1, x2, y2, color="#5b6b8c", dashed=False, width=1.6):
        dash = ' stroke-dasharray="5,4"' if dashed else ""
        self.parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" '
            f'stroke-width="{width}"{dash}/>')
        return self

    def text(self, x, y, s, fs=11, color="#4a5568", anchor="middle", bold=False):
        w = '800' if bold else '500'
        self.parts.append(
            f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-size="{fs}" '
            f'fill="{color}" font-weight="{w}" '
            f'font-family="Segoe UI,PingFang SC,Microsoft YaHei,sans-serif">{esc(s)}</text>')
        return self

    def render(self):
        return (f'<svg viewBox="0 0 {self.w} {self.h}" xmlns='
                f'"http://www.w3.org/2000/svg" role="img">'
                f'<defs><marker id="arw" markerWidth="9" markerHeight="7" '
                f'refX="8" refY="3.5" orient="auto"><polygon points="0 0, 9 3.5, 0 7" '
                f'fill="#5b6b8c"/></marker></defs>'
                f'{"".join(self.parts)}</svg>')


# ---------------------------------------------------------------- SDK 寄存器表助手
import regs_wl82 as R
import regs_csfr as C


def sdk_regs(prefix, descs=None, title_suffix="", bit_key=None):
    """从 WL82.h 自动生成寄存器表; descs: {name: 说明} 覆盖；可选 bit_key 挂位域表。"""
    t = R.regtable(prefix)
    if not t:
        return callout("warn", "该外设结构体未在 WL82.h 中公开, 见对应 API 头文件。")
    descs = descs or {}
    rows = []
    for off, name, mode, width in t:
        d = descs.get(name, "")
        if not d and width != "u32":
            d = f"({width})"
        rows.append((off, name, mode, d))
    html = regtable(rows)
    if bit_key:
        html += bit_table(bit_key)
    return html


def sdk_csfr_regs(prefix, descs=None, skip_rev=True, limit=None):
    """从 csfr.h 生成核 SFR 寄存器表。"""
    t = C.regtable(prefix, skip_rev=skip_rev)
    if not t:
        return callout("warn", "该结构体未在 csfr.h 中解析到。")
    if limit:
        t = t[:limit]
    descs = descs or {}
    rows = []
    for off, name, mode, width in t:
        d = descs.get(name, "")
        if not d and width != "u32":
            d = f"({width})"
        rows.append((off, name, mode, d))
    return regtable(rows)


def base_badge(prefix, csfr=False):
    if csfr:
        return f'<p>基址: <code class="rg">{C.fmt_base(prefix)}</code>' \
               f'（csfr_base=0x1EE0000 + map_adr / 固定偏移，见 csfr.h）</p>\n'
    return f'<p>基址: <code class="rg">{R.fmt_base(prefix)}</code>' \
           f'(段基址 + (64×grp + adr)×4, 见 WL82.h)</p>\n'
