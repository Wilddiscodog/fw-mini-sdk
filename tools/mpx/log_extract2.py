#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Dump the full exception block from uart19.log."""
data = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
i = data.find("excepXion")
if i < 0:
    i = data.find("exception")
print(data[max(0, i - 300):i + 2500])
