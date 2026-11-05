#!/usr/bin/env python3
"""Generate maze + match3 circular icons (RGB565 swapped chroma)."""
from PIL import Image, ImageDraw
import os
import struct

PNG_OUT = r"D:\fw-mini-sdk\app\watch_ui\assets\png"
C_OUT = r"D:\fw-mini-sdk\app\watch_ui\assets"
ICON = 150
CHROMA_SWAP = 0xE007


def rgb565_swapped(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)


def emit(name, im):
    w, h = im.size
    cx = (w - 1) / 2.0
    cy = (h - 1) / 2.0
    r2 = (min(w, h) / 2.0 - 0.5) ** 2
    px = im.load()
    raw = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy > r2 or a < 128:
                raw += struct.pack("<H", CHROMA_SWAP)
                continue
            v = rgb565_swapped(r, g, b)
            if v == CHROMA_SWAP:
                v = rgb565_swapped(r, g, max(0, b - 8))
            raw += struct.pack("<H", v)
    parts = [
        "/* Auto-generated %s icon */\n" % name,
        '#include "lvgl.h"\n\n',
        "static const LV_ATTRIBUTE_MEM_ALIGN uint8_t watch_icon_%s_map[] = {\n" % name,
    ]
    for i in range(0, len(raw), 16):
        chunk = raw[i : i + 16]
        parts.append("    " + ",".join("0x%02x" % v for v in chunk) + ",\n")
    parts.append("};\n\n")
    parts.append("const lv_img_dsc_t watch_icon_%s = {\n" % name)
    parts.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED,\n")
    parts.append("    .header.always_zero = 0,\n")
    parts.append("    .header.reserved = 0,\n")
    parts.append("    .header.w = %d,\n" % w)
    parts.append("    .header.h = %d,\n" % h)
    parts.append("    .data_size = %d * LV_COLOR_SIZE / 8,\n" % (w * h))
    parts.append("    .data = watch_icon_%s_map,\n};\n" % name)
    path = os.path.join(C_OUT, "watch_icon_%s.c" % name)
    open(path, "w", encoding="utf-8").write("".join(parts))
    im.save(os.path.join(PNG_OUT, "icon_%s.png" % name))
    print("ok", name, len(raw))


def make_maze():
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((2, 2, ICON - 3, ICON - 3), fill=(26, 39, 68, 255))
    d.ellipse((6, 6, ICON - 7, ICON - 7), outline=(91, 141, 239, 255), width=4)
    # mini maze
    walls = [
        (28, 28, 122, 40),
        (28, 28, 40, 122),
        (110, 28, 122, 90),
        (28, 110, 90, 122),
        (55, 55, 95, 67),
        (55, 55, 67, 95),
        (78, 78, 122, 90),
    ]
    for x0, y0, x1, y1 in walls:
        d.rectangle((x0, y0, x1, y1), fill=(91, 141, 239, 255))
    d.ellipse((100, 100, 118, 118), fill=(6, 214, 160, 255))
    d.ellipse((42, 42, 58, 58), fill=(255, 213, 79, 255))
    d.ellipse((45, 45, 51, 51), fill=(255, 255, 255, 220))
    return im


def make_match3():
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((2, 2, ICON - 3, ICON - 3), fill=(255, 244, 236, 255))
    d.ellipse((6, 6, ICON - 7, ICON - 7), outline=(255, 143, 171, 255), width=4)
    colors = [
        (255, 107, 107),
        (255, 209, 102),
        (6, 214, 160),
        (77, 150, 255),
        (166, 108, 255),
    ]
    cs = 22
    ox, oy = 30, 34
    grid = [
        [0, 1, 0, 2],
        [3, 0, 4, 1],
        [1, 2, 0, 3],
        [4, 3, 1, 0],
    ]
    for r, row in enumerate(grid):
        for c, ci in enumerate(row):
            x0 = ox + c * cs
            y0 = oy + r * cs
            d.rounded_rectangle(
                (x0, y0, x0 + cs - 3, y0 + cs - 3), radius=5, fill=colors[ci]
            )
            d.rectangle((x0 + 3, y0 + 3, x0 + 8, y0 + 6), fill=(255, 255, 255, 180))
    return im


def main():
    os.makedirs(PNG_OUT, exist_ok=True)
    emit("maze", make_maze())
    emit("match3", make_match3())


if __name__ == "__main__":
    main()
