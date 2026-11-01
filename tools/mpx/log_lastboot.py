#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Show wifi/portal progress after the LAST reset marker in uart19.log."""
d = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
i = d.rfind("system reset reason")
seg = d[i:i + 9000]
pats = ["wifi_module_enter", "joining", "assoic", "assoc", "DHCP", "dhcp",
        "ip_change", "portal", "sta_up", "link", "GOT_IP", "[prov]",
        "ota-http", "wl_init", "_connected", "SCAN", "Set_SSID"]
for line in seg.split("\n"):
    l = line.replace("\r", "")
    if any(p in l for p in pats):
        print(l[:150])
