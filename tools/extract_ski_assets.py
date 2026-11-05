# -*- coding: utf-8 -*-
"""Extract Texture2D from Ski Safari Unity data with name filters."""
import os
import re
import traceback
from collections import Counter

import UnityPy
from PIL import Image

DATA = r"I:\JL-BLE\apk\extracted\assets\bin\Data"
OUT = r"I:\JL-BLE\apk\ski_tex"
os.makedirs(OUT, exist_ok=True)

# interesting name keywords (case-insensitive)
KEYS = re.compile(
    r"ski|sven|player|char|hero|tree|rock|stone|coin|gold|penguin|yeti|eagle|"
    r"avalanche|snow|cloud|ramp|house|cabin|board|ui_|hud|btn|jump|mount|"
    r"obstacle|prop|bg_|back|ground|hill|mountain|ice|log|sled|flame|boost|"
    r"title|logo|safari|sprite|atlas",
    re.I,
)

sources = [
    os.path.join(DATA, "data.unity3d"),
    os.path.join(DATA, "sharedassets0.resource"),
    os.path.join(DATA, "sharedassets1.resource"),
    os.path.join(DATA, "resources.resource"),
]

# also scan any .assets / level* if present
for n in os.listdir(DATA):
    p = os.path.join(DATA, n)
    if os.path.isfile(p) and (n.endswith(".assets") or n.startswith("level") or n.endswith(".resource") or n.endswith(".unity3d")):
        if p not in sources:
            sources.append(p)

names = []
saved = 0
errors = 0

# Prefer loading whole Data dir so .resource streams resolve
print("load dir", DATA)
try:
    env_all = UnityPy.load(DATA)
    envs = [("DIR", env_all)]
except Exception as e:
    print("dir load fail", e)
    envs = []
    for src in sources:
        if not os.path.exists(src):
            continue
        print("load", src, os.path.getsize(src))
        try:
            envs.append((src, UnityPy.load(src)))
        except Exception as e2:
            print("  load fail", e2)

for src_label, env in envs:
    print("scan", src_label, "objects", len(env.objects) if hasattr(env, "objects") else "?")
    for obj in env.objects:
        if obj.type.name != "Texture2D":
            continue
        try:
            data = obj.read()
            name = getattr(data, "m_Name", None) or getattr(data, "name", "") or "tex"
            names.append(name)
            if not KEYS.search(name):
                # still keep small-medium sprites if name empty? skip
                continue
            img = data.image
            if img is None:
                continue
            w, h = img.size
            if w < 8 or h < 8:
                continue
            if w * h > 2048 * 2048:
                continue
            safe = re.sub(r"[^\w\-]+", "_", name)[:80]
            out = os.path.join(OUT, "%s_%dx%d.png" % (safe, w, h))
            # avoid overwrite collisions
            if os.path.exists(out):
                out = os.path.join(OUT, "%s_%dx%d_%d.png" % (safe, w, h, saved))
            img.save(out)
            saved += 1
            if saved <= 40 or saved % 50 == 0:
                print("  saved", name, w, h)
        except Exception:
            errors += 1
            if errors <= 5:
                traceback.print_exc()

print("textures matched/saved", saved, "errors", errors)
print("unique names sample:")
cnt = Counter(names)
for n, c in cnt.most_common(80):
    print(" %4d %s" % (c, n))
print("OUT", OUT, "files", len(os.listdir(OUT)))
