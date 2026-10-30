#!/usr/bin/env python3
"""Generate LVGL RGB565 chroma-keyed circle icons for watch app tiles."""
import math
import os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "app", "watch_ui", "assets")
W = H = 48
R = 22.0
CX = CY = 23.5

ICONS = [
    ("gyro", 0x00BCD4),
    ("net", 0x7C4DFF),
    ("bt", 0x2979FF),
    ("walkie", 0xFF6D00),
    ("rec", 0xFF1744),
    ("settings", 0x00E676),
]


def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    parts = []
    for name, rgb in ICONS:
        r = (rgb >> 16) & 0xFF
        g = (rgb >> 8) & 0xFF
        b = rgb & 0xFF
        raw = bytearray()
        for y in range(H):
            for x in range(W):
                dx = x - CX
                dy = y - CY
                d2 = dx * dx + dy * dy
                if d2 <= R * R:
                    d = math.sqrt(d2)
                    if d > R - 1:
                        f = R - d
                        rr = int(r * f)
                        gg = int(g * f)
                        bb = int(b * f)
                    else:
                        rr, gg, bb = r, g, b
                    if d2 < (R * 0.35) ** 2:
                        rr = min(255, rr + 40)
                        gg = min(255, gg + 40)
                        bb = min(255, bb + 40)
                    c = rgb888_to_rgb565(rr, gg, bb)
                else:
                    c = rgb888_to_rgb565(0, 255, 0)  # chroma key
                raw.append(c & 0xFF)
                raw.append((c >> 8) & 0xFF)
        arr = f"watch_icon_{name}_map"
        rows = []
        for i in range(0, len(raw), 16):
            chunk = raw[i : i + 16]
            rows.append(",".join(f"0x{b:02x}" for b in chunk))
        parts.append((name, arr, ",\n    ".join(rows), len(raw)))

    hpath = os.path.join(OUT_DIR, "watch_icons.h")
    cpath = os.path.join(OUT_DIR, "watch_icons.c")
    with open(hpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("#ifndef WATCH_ICONS_H\n")
        f.write("#define WATCH_ICONS_H\n")
        f.write("#include \"lvgl.h\"\n\n")
        for name, _, _, _ in parts:
            f.write(f"LV_IMG_DECLARE(watch_icon_{name});\n")
        f.write("\n#endif\n")

    with open(cpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n")
        f.write("#include \"lvgl.h\"\n")
        f.write("#include \"watch_icons.h\"\n\n")
        for name, arr, hexdata, n in parts:
            f.write(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {arr}[] = {{\n    {hexdata}\n}};\n\n")
            f.write(f"const lv_img_dsc_t watch_icon_{name} = {{\n")
            f.write("    .header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED,\n")
            f.write("    .header.always_zero = 0,\n")
            f.write("    .header.reserved = 0,\n")
            f.write(f"    .header.w = {W},\n")
            f.write(f"    .header.h = {H},\n")
            f.write(f"    .data_size = {n},\n")
            f.write(f"    .data = {arr},\n")
            f.write("};\n\n")
        f.write("#ifdef __cplusplus\n}\n#endif\n")
    print("wrote", cpath, "and", hpath, "icons=", len(parts))


if __name__ == "__main__":
    main()
