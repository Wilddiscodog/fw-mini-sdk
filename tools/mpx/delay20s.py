#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Delay mpx_task test to 20s after boot (OTA window + avoid CDC re-init race)."""
p = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = open(p, encoding="utf-8").read()
old = """    (void)p;
    /* 等显示/USB 先起,日志才有出口 */
    os_time_dly(300);"""
new = """    (void)p;
    /* 开机 20s 后再跑测试: 1) 给网页 OTA 留升级窗口(崩溃也能重刷);
     * 2) 避开 ~3.1s 处 USB CDC 重建窗口与 printf 的竞争(此前该窗口内
     *    打印触发 CPU1 写总线异常导致崩溃环) */
    os_time_dly(2000);"""
if old in s:
    s = s.replace(old, new)
    open(p, "w", encoding="utf-8").write(s)
    print("delayed to 20s")
elif "os_time_dly(2000)" in s:
    print("already delayed")
else:
    raise SystemExit("pattern not found")
