#!/usr/bin/env python3
"""Generate lv_font_watch_16_cn.c from collected UI symbols + SimSun.woff."""
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SYM_FILE = HERE / "watch_ui_symbols.txt"
OUT_C = HERE / "lv_font_watch_16_cn.c"
FONT = pathlib.Path(
    r"D:\fw-mini-sdk\third\lvgl\lvgl\scripts\built_in_font\SimSun.woff"
)


def main():
    symbols = SYM_FILE.read_text(encoding="utf-8")
    for i in range(0x20, 0x7F):
        symbols += chr(i)
    # common CJK punctuation that may appear in UI
    symbols += "，。！？、：；（）【】《》…—·"

    seen = set()
    uniq = []
    for c in symbols:
        if ord(c) < 0x20:
            continue
        if c not in seen:
            seen.add(c)
            uniq.append(c)

    # Prefer -r hex list to avoid Windows cmd metacharacters in --symbols
    cps = sorted({ord(c) for c in uniq})
    # pack consecutive into ranges
    ranges = []
    start = prev = cps[0]
    for cp in cps[1:]:
        if cp == prev + 1:
            prev = cp
            continue
        ranges.append((start, prev))
        start = prev = cp
    ranges.append((start, prev))

    def fmt_range(a, b):
        if a == b:
            return "0x%X" % a
        return "0x%X-0x%X" % (a, b)

    range_arg = ",".join(fmt_range(a, b) for a, b in ranges)

    env = os.environ.copy()
    npm_bin = os.path.join(os.environ.get("APPDATA", ""), "npm")
    env["Path"] = r"C:\Program Files\nodejs;" + npm_bin + ";" + env.get("Path", "")
    conv = os.path.join(npm_bin, "lv_font_conv.cmd")
    if not os.path.isfile(conv):
        conv = "lv_font_conv"

    # Write range to file and invoke via node to avoid cmd length/escape issues
    range_file = HERE / "watch_ui_ranges.txt"
    range_file.write_text(range_arg, encoding="ascii")

    js = HERE / "_run_font_conv.js"
    js.write_text(
        """
const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const ranges = fs.readFileSync(process.argv[2], 'utf8').trim();
const font = process.argv[3];
const out = process.argv[4];
const args = [
  '--no-compress', '--no-prefilter',
  '--bpp', '4', '--size', '16',
  '--font', font,
  '-r', ranges,
  '--format', 'lvgl',
  '--lv-include', 'lvgl.h',
  '-o', out,
  '--force-fast-kern-format'
];
const r = spawnSync('lv_font_conv', args, { stdio: 'inherit', shell: true });
process.exit(r.status === null ? 1 : r.status);
""",
        encoding="utf-8",
    )

    print("codepoints", len(cps), "ranges", len(ranges))
    r = subprocess.run(
        ["node", str(js), str(range_file), str(FONT), str(OUT_C)],
        env=env,
        cwd=str(HERE),
    )
    if r.returncode != 0:
        sys.exit(r.returncode)

    text = OUT_C.read_text(encoding="utf-8")
    m = re.search(r"const lv_font_t (lv_font_[A-Za-z0-9_]+) =", text)
    if m and m.group(1) != "lv_font_watch_16_cn":
        text = text.replace(m.group(1), "lv_font_watch_16_cn")
        print("renamed", m.group(1), "-> lv_font_watch_16_cn")
    if "LV_FONT_DECLARE(lv_font_simsun_16_cjk)" not in text:
        text = text.replace(
            "#if LV_FONT_WATCH_16_CN\n\n/*-----------------\n *    BITMAPS",
            "#if LV_FONT_WATCH_16_CN\n\n"
            "LV_FONT_DECLARE(lv_font_simsun_16_cjk);\n\n"
            "/*-----------------\n *    BITMAPS",
        )
    text = text.replace(".fallback = NULL,", ".fallback = &lv_font_simsun_16_cjk,")
    OUT_C.write_bytes(text.replace("\r\n", "\n").encode("utf-8"))
    print("size", OUT_C.stat().st_size)
    print("ok", OUT_C)


if __name__ == "__main__":
    main()
