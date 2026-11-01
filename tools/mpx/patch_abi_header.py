#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-shot: patch vendored micropixel_abi.h for pi32v2 (u64 -> 8-byte aligned)."""
import re

PATH = "third/micropixel-abi/micropixel_abi.h"
src = open(PATH, encoding="utf-8").read()
if "micropixel_u64_t" in src:
    print("already patched")
    raise SystemExit(0)
src = src.replace(
    "#include <stdint.h>",
    "#include <stdint.h>\n\n"
    "/* JieLi pi32v2 aligns uint64_t to 4; the wire format (wasm32/xtensa) uses 8.\n"
    " * Forcing 8 keeps every struct layout identical to the reference compilers. */\n"
    "typedef uint64_t micropixel_u64_t __attribute__((aligned(8)));",
)
src = re.sub(r"\buint64_t\b", "micropixel_u64_t", src)
open(PATH, "w", encoding="utf-8").write(src)
print("patched", src.count("micropixel_u64_t"), "refs")
