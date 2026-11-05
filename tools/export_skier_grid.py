# -*- coding: utf-8 -*-
from PIL import Image, ImageDraw, ImageFont
import os

src = r"I:\JL-BLE\apk\ski_tex\player_skier_default_512x512.png"
out = r"D:\fw-mini-sdk\app\watch_ui\assets\ski_src\skier_cells"
os.makedirs(out, exist_ok=True)
im = Image.open(src).convert("RGBA")
cols, rows = 8, 6
cw, ch = im.size[0] // cols, im.size[1] // rows
# big labeled sheet
big = Image.new("RGB", (cols * (cw + 4), rows * (ch + 18)), (30, 40, 55))
for r in range(rows):
    for c in range(cols):
        cell = im.crop((c * cw, r * ch, (c + 1) * cw, (r + 1) * ch))
        idx = r * cols + c
        cell.save(os.path.join(out, "f%02d_r%dc%d.png" % (idx, r, c)))
        x = c * (cw + 4)
        y = r * (ch + 18)
        big.paste(cell.convert("RGB"), (x, y + 14))
        d = ImageDraw.Draw(big)
        d.text((x + 2, y), "%d" % idx, fill=(255, 220, 80))
big.save(os.path.join(out, "_grid.png"))
print("cells", cols * rows, "->", out, "cell", cw, ch)
