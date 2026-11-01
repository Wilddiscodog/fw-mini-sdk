#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wifi + updater state in the last stretch of uart19.log."""
import re

data = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
# 最近 400 行里找 wifi/portal/updater 相关行
lines = [l for l in data.split("\n") if l.strip()][-400:]
pat = re.compile(r"wifi|assoic|assoc|disconn|DISCONN|portal|ota-http|prov|B\d+|ERR|OK|dhcp|DHCP|link", re.I)
hits = [l.replace("\r", "")[:150] for l in lines if pat.search(l)]
for h in hits[-30:]:
    print(h)
print("=== total lines now:", len(lines), "===")
# 找最后一次 B 系列烧写进度
bmark = re.findall(r"B\d+", data)
print("burn progress marks (last 15):", bmark[-15:])
