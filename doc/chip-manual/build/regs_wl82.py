# -*- coding: utf-8 -*-
"""regs_wl82.py — 从官方 SDK 的 WL82.h 解析全部外设寄存器结构体。

数据源: include_lib/driver/cpu/wl82/asm/WL82.h (fw-AC79_AIoT_SDK 原始导出)。
解析规则(与 SDK 宏一致):
  - 基址 = 段基址 + map_adr(grp, adr) = 段基址 + (64*grp + adr)*4
  - 结构体成员按声明顺序排列, 每成员 4 字节(与 ___u8/___u16/___u32 打包宏一致)
  - 成员读写属性: ___RW / ___RO / ___WO
"""
import os
import re

SDK = r"D:\fw-AC79_AIoT_SDK\include_lib\driver\cpu\wl82\asm\WL82.h"

# 段基址(WL82.h 原文)
SEG_BASE = {
    "lsfr_base": 0x10000,   # LSB 低速外设段
    "hsfr_base": 0x40000,   # HSB 高速外设段
    "psfr_base": 0x50000,   # PORT 端口段
}

# 手动核对的关键基址表(WL82.h #define JL_xxx_BASE 提取)
def _parse():
    src = open(SDK, encoding="utf-8", errors="replace").read()

    bases = {}
    for m in re.finditer(
            r'#define\s+(JL_\w+)_BASE\s*\(\s*(lsfr_base|hsfr_base|psfr_base)'
            r'\s*\+\s*map_adr\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)\s*\)',
            src):
        name, seg, grp, adr = m.group(1), m.group(2), m.group(3), m.group(4)
        addr = SEG_BASE[seg] + (64 * int(grp, 16) + int(adr, 16)) * 4
        bases[name] = addr

    # 结构体成员: typedef struct { ... } JL_xxx_TypeDef;
    structs = {}
    for m in re.finditer(
            r'typedef\s+struct\s*\{(.*?)\}\s*(JL_\w+_TypeDef)\s*;',
            src, re.S):
        body, tname = m.group(1), m.group(2)
        members = []
        for mm in re.finditer(r'___(RW|RO|WO)\s+___(u8|u16|u32)\((\w+)\)', body):
            members.append((mm.group(3), mm.group(1), mm.group(2)))
        if members:
            structs[tname] = members

    return bases, structs


BASES, STRUCTS = _parse()


# 实例名 → 结构体名映射(共享 typedef 的外设: UART0-2 同用 JL_UART_TypeDef 等)
_TYPEDEF_ALIAS = {
    "UART0": "UART", "UART1": "UART", "UART2": "UART",
    "TIMER0": "TIMER", "TIMER1": "TIMER", "TIMER2": "TIMER",
    "TIMER3": "TIMER", "TIMER4": "TIMER", "TIMER5": "TIMER",
    "SPI0": "SPI", "SPI1": "SPI", "SPI2": "SPI", "SPI3": "SPI",
    "SD0": "SD", "SD1": "SD",
    "USB": "USB", "USB1": "USB",
    "IIC": "IIC",
    "PORTA": "PORT_FLASH", "PORTB": "PORT_FLASH", "PORTC": "PORT_FLASH",
    "PORTD": "PORT_FLASH", "PORTE": "PORT_FLASH", "PORTF": "PORT_FLASH",
    "PORTG": "PORT_FLASH", "PORTH": "PORT_FLASH",
    "PLNK0": "PLNK", "PLNK1": "PLNK",
    "ALNK0": "ALNK", "ALNK1": "ALNK",
    "SHA": "SHA",
}


def regtable(prefix):
    """外设寄存器表: [(offset, name, rw, width)]"""
    name = _TYPEDEF_ALIAS.get(prefix, prefix)
    members = STRUCTS.get("JL_%s_TypeDef" % name)
    if not members:
        return None
    out = []
    off = 0
    for mname, mode, width in members:
        out.append((off, mname, mode, width))
        off += 4
    return out


def base_of(prefix):
    """外设基址(如 base_of('UART0')), 找不到返回 None"""
    return BASES.get("JL_%s" % prefix) or BASES.get("JL_%s_BASE" % prefix)


def fmt_base(prefix):
    b = base_of(prefix)
    return "0x%05X" % b if b is not None else "—"


if __name__ == "__main__":
    print("bases:", len(BASES), "structs:", len(STRUCTS))
    for k in ["UART0", "TIMER0", "PORTA", "SPI0", "EMI", "AUDIO", "MCPWM", "ADC"]:
        print(k, fmt_base(k), regtable(k) and len(regtable(k)))
