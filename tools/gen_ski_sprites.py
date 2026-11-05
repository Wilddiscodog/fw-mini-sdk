# -*- coding: utf-8 -*-
"""Slice Ski Safari sheets with stable frame indices for animation FSM."""
from PIL import Image
import os
import struct

SRC = r"I:\JL-BLE\apk\ski_tex"
OUT_C = r"D:\fw-mini-sdk\app\watch_ui\assets\ski_sprites.c"
OUT_H = r"D:\fw-mini-sdk\app\watch_ui\assets\ski_sprites.h"
PREV = r"D:\fw-mini-sdk\app\watch_ui\assets\ski_src\frames"
os.makedirs(PREV, exist_ok=True)
CHROMA = 0xE007


def rgb565_swapped(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)


def to_bytes(im):
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 28 or (r < 10 and g < 10 and b < 10):
                data += struct.pack("<H", CHROMA)
                continue
            v = rgb565_swapped(r, g, b)
            if v == CHROMA:
                v = rgb565_swapped(r, g, max(0, b - 8))
            data += struct.pack("<H", v)
    return w, h, bytes(data)


def trim(im, pad=1):
    im = im.convert("RGBA")
    bb = im.getbbox()
    if not bb:
        return im
    x0, y0, x1, y1 = bb
    return im.crop((max(0, x0 - pad), max(0, y0 - pad),
                    min(im.size[0], x1 + pad), min(im.size[1], y1 + pad)))


def cell(im, cols, rows, idx, scale):
    cw, ch = im.size[0] // cols, im.size[1] // rows
    r, c = divmod(idx, cols) if False else (idx // cols, idx % cols)
    # idx = row*cols + col
    row = idx // cols
    col = idx % cols
    fr = trim(im.crop((col * cw, row * ch, (col + 1) * cw, (row + 1) * ch)))
    if fr.size[0] < 4:
        return None
    fr.thumbnail((scale, scale), Image.LANCZOS)
    return fr


def emit(parts, hdr, name, frames):
    hdr.append("extern const lv_img_dsc_t * const %s_tab[];\n" % name)
    hdr.append("#define %s_N %d\n" % (name.upper(), len(frames)))
    names = []
    for i, fr in enumerate(frames):
        sym = "%s_%d" % (name, i)
        names.append(sym)
        w, h, raw = to_bytes(fr)
        parts.append("static const LV_ATTRIBUTE_MEM_ALIGN uint8_t %s_map[] = {\n" % sym)
        for off in range(0, len(raw), 16):
            chunk = raw[off:off + 16]
            parts.append("    " + ",".join("0x%02x" % b for b in chunk) + ",\n")
        parts.append("};\n")
        parts.append("const lv_img_dsc_t %s = {\n" % sym)
        parts.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED,\n")
        parts.append("    .header.always_zero = 0,\n")
        parts.append("    .header.reserved = 0,\n")
        parts.append("    .header.w = %d,\n" % w)
        parts.append("    .header.h = %d,\n" % h)
        parts.append("    .data_size = %d * %d * LV_COLOR_SIZE / 8,\n" % (w, h))
        parts.append("    .data = %s_map,\n};\n\n" % sym)
        fr.save(os.path.join(PREV, "%s.png" % sym))
    parts.append("const lv_img_dsc_t * const %s_tab[] = {\n" % name)
    for n in names:
        parts.append("    &%s,\n" % n)
    parts.append("};\n\n")
    return sum(len(to_bytes(f)[2]) for f in frames)


skier = Image.open(os.path.join(SRC, "player_skier_default_512x512.png"))
# 8x6 sheet indices from labeled grid:
# ski 0-7, flip 16-21, air 22, land 24-25, jump 28-29, crash 32-34, upright 36-39
SKI_IDX = [0, 1, 2, 3, 4, 5]
FLIP_IDX = [16, 17, 18, 19, 20, 21]
AIR_IDX = [22, 28, 29]
LAND_IDX = [24, 25, 26]
CRASH_IDX = [32, 33, 34]
UP_IDX = [36, 37, 38, 39]

parts = ['#include "lvgl.h"\n\n']
hdr = ['#ifndef SKI_SPRITES_H\n#define SKI_SPRITES_H\n#include "lvgl.h"\n\n']

total = 0
groups = [
    ("ski_run", [cell(skier, 8, 6, i, 48) for i in SKI_IDX]),
    ("ski_flip", [cell(skier, 8, 6, i, 48) for i in FLIP_IDX]),
    ("ski_air", [cell(skier, 8, 6, i, 48) for i in AIR_IDX]),
    ("ski_land", [cell(skier, 8, 6, i, 48) for i in LAND_IDX]),
    ("ski_crash", [cell(skier, 8, 6, i, 48) for i in CRASH_IDX]),
    ("ski_up", [cell(skier, 8, 6, i, 48) for i in UP_IDX]),
]

# obstacles
coin = Image.open(os.path.join(SRC, "env_coin_256x256.png"))
groups.append(("ski_coin", [cell(coin, 4, 4, i, 20) for i in range(4)]))

tree = Image.open(os.path.join(SRC, "env_trees_01_256x256.png"))
# 4x2 or just left half trees - use 2 big cells
tf = []
for i in [0, 1]:
    fr = cell(tree, 2, 2, i, 52)
    if fr:
        tf.append(fr)
groups.append(("ski_tree", tf))

rock = Image.open(os.path.join(SRC, "env_hazards_rocks_256x256.png")).convert("RGBA")
rf = []
for box, sc in [((8, 8, 130, 150), 42), ((130, 10, 250, 130), 38)]:
    fr = trim(rock.crop(box))
    fr.thumbnail((sc, sc), Image.LANCZOS)
    rf.append(fr)
groups.append(("ski_rock", rf))

peng = Image.open(os.path.join(SRC, "npc_penguin_256x256.png"))
groups.append(("ski_peng", [cell(peng, 4, 4, i, 34) for i in (0, 1, 4)]))

yeti = Image.open(os.path.join(SRC, "npc_yeti_512x512.png")).convert("RGBA")
face = trim(yeti.crop((20, 10, 200, 190)))
face.thumbnail((26, 26), Image.LANCZOS)
groups.append(("ski_yeti", [face]))

for name, frames in groups:
    frames = [f for f in frames if f is not None]
    print(name, len(frames))
    total += emit(parts, hdr, name, frames)

# anim index helpers as comments in header
hdr.append("\n/* skier sheet map: run=0-5 flip=16-21 air=22/28/29 land=24-26 crash=32-34 up=36-39 */\n")
hdr.append("#endif\n")
open(OUT_C, "w", encoding="utf-8").write("".join(parts))
open(OUT_H, "w", encoding="utf-8").write("".join(hdr))
print("total KB", total // 1024, OUT_C)
