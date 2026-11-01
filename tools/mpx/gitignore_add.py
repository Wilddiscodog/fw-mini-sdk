#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Append debug-artifact ignores to .gitignore."""
import io

s = io.open("D:/fw-mini-sdk/.gitignore", encoding="utf-8").read()
if "mpx/wasm 调试临时产物" in s:
    print("already present")
else:
    s += ("\n# ================= mpx/wasm 调试临时产物 =================\n"
          "build_mpx*.log\n"
          "uart19.log\n"
          "cdc44.log\n"
          "ota_push*.log\n"
          "isd_flash.log\n"
          ".zcode/\n")
    io.open("D:/fw-mini-sdk/.gitignore", "w", encoding="utf-8").write(s)
    print("gitignore updated")
