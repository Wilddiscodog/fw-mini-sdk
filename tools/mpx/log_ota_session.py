#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Show the last OTA http session from uart19.log."""
d = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
i = d.rfind("[ota-http] upload start")
if i < 0:
    print("no ota-http session found")
else:
    print(d[i:i + 900].replace("\r", ""))
