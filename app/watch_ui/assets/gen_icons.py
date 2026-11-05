#!/usr/bin/env python3
"""Convert cartoon JPG assets into PNG + LVGL RGB565 C arrays.

本工程 lv_conf.h: LV_COLOR_16_SWAP != 0, 图像数据必须按 LVGL
swapped RGB565 字节序存储, 色键绿也要写成交换后的 0xE007。
"""
from PIL import Image, ImageDraw
import os
import struct

SRC = r"C:\Users\ding\.cursor\projects\d-fw-mini-sdk\assets"
PNG_OUT = r"D:\fw-mini-sdk\app\watch_ui\assets\png"
C_OUT = r"D:\fw-mini-sdk\app\watch_ui\assets"

LCD_W, LCD_H = 296, 240
ICON = 150
# LV_COLOR_CHROMA_KEY = 0x00FF00, 在 SWAP 模式下 .full == 0xE007
CHROMA_SWAP = 0xE007


def rgb565_swapped(r, g, b):
    """标准 RGB565 后再字节交换, 匹配 LV_COLOR_16_SWAP!=0。"""
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)


def to_rgb565_bytes(im, circle=False, chroma=True):
    im = im.convert("RGBA")
    w, h = im.size
    data = bytearray()
    cx = (w - 1) / 2.0
    cy = (h - 1) / 2.0
    r2 = (min(w, h) / 2.0 - 0.5) ** 2
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            outside = False
            if circle:
                dx = x - cx
                dy = y - cy
                if dx * dx + dy * dy > r2:
                    outside = True
            if outside or a < 128:
                if chroma:
                    data += struct.pack("<H", CHROMA_SWAP)
                else:
                    # 不透明底: 奶油色页面背景
                    data += struct.pack("<H", rgb565_swapped(0xFF, 0xF4, 0xE8))
                continue
            v = rgb565_swapped(r, g, b)
            if chroma and v == CHROMA_SWAP:
                v = rgb565_swapped(r, g, max(0, b - 8))
            data += struct.pack("<H", v)
    return bytes(data)


def emit_array(parts, name, raw):
    parts.append("static const LV_ATTRIBUTE_MEM_ALIGN uint8_t %s[] = {\n" % name)
    for i in range(0, len(raw), 16):
        chunk = raw[i:i + 16]
        parts.append("    " + ",".join("0x%02x" % v for v in chunk) + ",\n")
    parts.append("};\n\n")


def remove_bg_and_circle(path, size, plate_rgb=None):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    im = im.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)

    # 角落采样去底
    corners = [
        im.getpixel((2, 2)),
        im.getpixel((size - 3, 2)),
        im.getpixel((2, size - 3)),
        im.getpixel((size - 3, size - 3)),
    ]
    br = sum(c[0] for c in corners) // 4
    bg = sum(c[1] for c in corners) // 4
    bb = sum(c[2] for c in corners) // 4
    px = im.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            d = abs(r - br) + abs(g - bg) + abs(b - bb)
            if d < 100:
                px[x, y] = (0, 0, 0, 0)

    # 彩色圆盘底板 + 白边, 保证圆形观感
    plate = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(plate)
    if plate_rgb is None:
        plate_rgb = (255, 200, 100)
    draw.ellipse((2, 2, size - 3, size - 3), fill=plate_rgb + (255,))
    draw.ellipse((2, 2, size - 3, size - 3), outline=(255, 255, 255, 255), width=5)

    # 内容缩进贴在圆盘上
    content = im.resize((int(size * 0.78), int(size * 0.78)), Image.LANCZOS)
    cx = (size - content.size[0]) // 2
    cy = (size - content.size[1]) // 2
    plate.paste(content, (cx, cy), content)

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse((1, 1, size - 2, size - 2), fill=255)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(plate, (0, 0))
    out.putalpha(mask)
    return out


def main():
    os.makedirs(PNG_OUT, exist_ok=True)
    icons = [
        ("icon_gyro.jpg", "watch_icon_gyro", (0, 188, 212)),
        ("icon_net.jpg", "watch_icon_net", (124, 77, 255)),
        ("icon_bt.jpg", "watch_icon_bt", (41, 121, 255)),
        ("icon_walkie.jpg", "watch_icon_walkie", (255, 109, 0)),
        ("icon_rec.jpg", "watch_icon_rec", (255, 23, 68)),
        ("icon_settings.jpg", "watch_icon_settings", (0, 230, 118)),
        ("icon_ball.jpg", "watch_icon_ball", (255, 214, 0)),
    ]

    bg = Image.open(os.path.join(SRC, "watch_bg_dog.jpg")).convert("RGB")
    tw, th = LCD_W, LCD_H
    bw, bh = bg.size
    scale = max(tw / float(bw), th / float(bh))
    nw, nh = int(bw * scale + 0.5), int(bh * scale + 0.5)
    bg = bg.resize((nw, nh), Image.LANCZOS)
    left = (nw - tw) // 2
    top = (nh - th) // 2
    bg = bg.crop((left, top, left + tw, top + th))
    bg.save(os.path.join(PNG_OUT, "watch_bg_dog.png"), "PNG")
    print("bg", bg.size)

    parts = []
    parts.append(
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "#include \"lvgl.h\"\n"
        "#include \"watch_icons.h\"\n\n"
        "/* RGB565 byte-swapped for LV_COLOR_16_SWAP */\n\n"
    )
    decls_h = [
        "#ifndef WATCH_ICONS_H",
        "#define WATCH_ICONS_H",
        '#include "lvgl.h"',
        "",
        "LV_IMG_DECLARE(watch_bg_dog);",
    ]

    bg_bytes = bytearray()
    for y in range(th):
        for x in range(tw):
            r, g, b = bg.getpixel((x, y))
            bg_bytes += struct.pack("<H", rgb565_swapped(r, g, b))
    bb = bytes(bg_bytes)
    emit_array(parts, "watch_bg_dog_map", bb)
    parts.append(
        "const lv_img_dsc_t watch_bg_dog = {\n"
        "    .header.cf = LV_IMG_CF_TRUE_COLOR,\n"
        "    .header.always_zero = 0,\n"
        "    .header.reserved = 0,\n"
        "    .header.w = %d,\n"
        "    .header.h = %d,\n"
        "    .data_size = %d,\n"
        "    .data = watch_bg_dog_map,\n"
        "};\n\n" % (tw, th, len(bb))
    )

    for src_name, sym, plate in icons:
        img = remove_bg_and_circle(os.path.join(SRC, src_name), ICON, plate_rgb=plate)
        img.save(os.path.join(PNG_OUT, src_name.replace(".jpg", ".png")), "PNG")
        raw = to_rgb565_bytes(img, circle=True, chroma=True)
        emit_array(parts, sym + "_map", raw)
        parts.append(
            "const lv_img_dsc_t %s = {\n"
            "    .header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED,\n"
            "    .header.always_zero = 0,\n"
            "    .header.reserved = 0,\n"
            "    .header.w = %d,\n"
            "    .header.h = %d,\n"
            "    .data_size = %d,\n"
            "    .data = %s_map,\n"
            "};\n\n" % (sym, ICON, ICON, len(raw), sym)
        )
        decls_h.append("LV_IMG_DECLARE(%s);" % sym)
        # 校验色键字节
        assert raw[0] == 0x07 and raw[1] == 0xE0, (sym, raw[0], raw[1])
        print("icon", sym, "chroma_ok", len(raw))

    parts.append("#ifdef __cplusplus\n}\n#endif\n")
    decls_h += ["", "#endif", ""]

    with open(os.path.join(C_OUT, "watch_icons.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))
    with open(os.path.join(C_OUT, "watch_icons.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(decls_h))
    print("done", os.path.getsize(os.path.join(C_OUT, "watch_icons.c")))


if __name__ == "__main__":
    main()
