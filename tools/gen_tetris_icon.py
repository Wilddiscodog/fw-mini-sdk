#!/usr/bin/env python3
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


def main():
    os.makedirs(PNG_OUT, exist_ok=True)
    im = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((2, 2, ICON - 3, ICON - 3), fill=(255, 244, 214, 255))
    d.ellipse((6, 6, ICON - 7, ICON - 7), outline=(255, 180, 120, 255), width=4)
    colors = [
        (255, 107, 129),
        (255, 209, 102),
        (6, 214, 160),
        (77, 150, 255),
        (162, 89, 255),
    ]
    cells = [
        (3, 8, colors[0]), (4, 8, colors[0]), (5, 8, colors[0]), (4, 7, colors[0]),
        (6, 8, colors[1]), (7, 8, colors[1]), (6, 7, colors[1]), (7, 7, colors[1]),
        (2, 8, colors[2]), (2, 7, colors[2]), (2, 6, colors[2]), (2, 5, colors[2]),
        (5, 7, colors[3]), (5, 6, colors[4]), (4, 6, colors[4]), (3, 6, colors[3]),
    ]
    cs, ox, oy = 12, 18, 22
    for cx, cy, col in cells:
        x0 = ox + cx * cs
        y0 = oy + cy * cs
        d.rounded_rectangle((x0, y0, x0 + cs - 2, y0 + cs - 2), radius=3, fill=col)
        d.rectangle((x0 + 2, y0 + 2, x0 + 5, y0 + 4), fill=(255, 255, 255, 180))
    im.save(os.path.join(PNG_OUT, "icon_tetris.png"))

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
        "/* Auto-generated tetris icon */\n",
        "#include \"lvgl.h\"\n\n",
        "static const LV_ATTRIBUTE_MEM_ALIGN uint8_t watch_icon_tetris_map[] = {\n",
    ]
    for i in range(0, len(raw), 16):
        chunk = raw[i:i + 16]
        parts.append("    " + ",".join("0x%02x" % v for v in chunk) + ",\n")
    parts.append("};\n\n")
    parts.append("const lv_img_dsc_t watch_icon_tetris = {\n")
    parts.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED,\n")
    parts.append("    .header.always_zero = 0,\n")
    parts.append("    .header.reserved = 0,\n")
    parts.append("    .header.w = %d,\n" % w)
    parts.append("    .header.h = %d,\n" % h)
    parts.append("    .data_size = %d * LV_COLOR_SIZE / 8,\n" % (w * h))
    parts.append("    .data = watch_icon_tetris_map,\n};\n")
    open(os.path.join(C_OUT, "watch_icon_tetris.c"), "w", encoding="utf-8").write("".join(parts))
    print("icon ok", w, h, len(raw))


if __name__ == "__main__":
    main()
