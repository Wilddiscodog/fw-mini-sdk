# -*- coding: utf-8 -*-
"""regs_csfr.py — 解析官方 SDK csfr.h（核 SFR：CMNG / CACHE 相关 / corex2 / q32DSP）。

数据源: include_lib/driver/cpu/wl82/asm/csfr.h
csfr_base = 0x1ee0000；map_adr(grp,adr)=(64*grp+adr)*4

结构体含 REV_*[N] 填充与 ___u8 成员：每槽仍按 4 字节对齐计数（与 ___u8 宏一致）。
"""
import re

SDK = r"D:\fw-AC79_AIoT_SDK\include_lib\driver\cpu\wl82\asm\csfr.h"
CSFR_BASE = 0x1EE0000


def _eval_expr(s):
    s = s.strip().replace(" ", "")
    # 仅允许十六进制/十进制与 + - *
    if not re.fullmatch(r"[0-9a-fxA-FX+\-*()]+", s):
        return None
    return eval(s, {"__builtins__": {}}, {})


def _parse():
    src = open(SDK, encoding="utf-8", errors="replace").read()

    bases = {
        "JL_CMNG": CSFR_BASE + (64 * 0x00 + 0x00) * 4,
        "JL_SDTAP": CSFR_BASE + (64 * 0x01 + 0x00) * 4,
        "JL_MBIS": CSFR_BASE + (64 * 0x02 + 0x00) * 4,
        "JL_HMEM": CSFR_BASE + (64 * 0x03 + 0x00) * 4,
        "JL_FFT": CSFR_BASE + (64 * 0x04 + 0x00) * 4,
        "corex2": CSFR_BASE + 0xE000,       # corex2_sfr_base; n=0
        "q32DSP0": CSFR_BASE + 0xF000,      # q32DSP(0)
        "q32DSP1": CSFR_BASE + 0xF000 + 0x200,
    }

    structs = {}
    for m in re.finditer(
            r'typedef\s+struct\s*\{(.*?)\}\s*(JL_\w+_TypeDef|JL_TypeDef_\w+)\s*;',
            src, re.S):
        body, tname = m.group(1), m.group(2)
        members = []
        # 普通寄存器: ___RW ___u32(NAME) 或 ___RW ___u8(NAME)
        # 填充: ___RO ___u32(REV_xx[expr]);
        for mm in re.finditer(
                r'___(RW|RO|WO)\s+___(u8|u16|u32)\((\w+)(?:\[([^\]]+)\])?\)',
                body):
            mode, width, name, arr = mm.group(1), mm.group(2), mm.group(3), mm.group(4)
            if arr is not None:
                n = _eval_expr(arr)
                if n is None:
                    continue
                for i in range(int(n)):
                    members.append(("%s[%d]" % (name, i), mode, width, True))
            else:
                members.append((name, mode, width, False))
        if members:
            structs[tname] = members

    return bases, structs


BASES, STRUCTS = _parse()

# 逻辑名 → typedef
_ALIAS = {
    "CMNG": "JL_CMNG_TypeDef",
    "SDTAP": "JL_SDTAP_TypeDef",
    "MBIS": "JL_MBIS_TypeDef",
    "HMEM": "JL_HMEM_TypeDef",
    "FFT": "JL_FFT_TypeDef",
    "corex2": "JL_TypeDef_corex2",
    "q32DSP": "JL_TypeDef_q32DSP",
}


def regtable(prefix, skip_rev=True):
    """[(offset_bytes, name, rw, width)]；skip_rev 省略 REV_* 填充槽。"""
    tname = _ALIAS.get(prefix, prefix if prefix.startswith("JL_") else None)
    if not tname:
        return None
    members = STRUCTS.get(tname)
    if not members:
        return None
    out = []
    off = 0
    for name, mode, width, is_pad in members:
        if not (skip_rev and (is_pad or name.startswith("REV_"))):
            out.append((off, name, mode, width))
        off += 4
    return out


def base_of(prefix):
    return BASES.get("JL_%s" % prefix) or BASES.get(prefix)


def fmt_base(prefix):
    b = base_of(prefix)
    return "0x%07X" % b if b is not None else "—"


def corex2_interesting():
    """corex2 常用寄存器（去掉大量 LIMIT 副本后的摘要键）。"""
    t = regtable("corex2", skip_rev=True)
    if not t:
        return []
    keep_prefix = (
        "C0_CON", "C1_CON", "CACHE_CON", "DCACHE_WAY", "ICACHE_WAY",
        "DBG_", "WR_LIMIT0_", "WR_ALLOW_ID0", "C0_WR_LIMIT", "C1_WR_LIMIT",
        "PRP_", "AXI_", "PC_LIMIT",
    )
    out = []
    for off, name, mode, width in t:
        if any(name.startswith(p) for p in keep_prefix):
            out.append((off, name, mode, width))
    return out


if __name__ == "__main__":
    print("bases", {k: hex(v) for k, v in BASES.items()})
    for k in ["CMNG", "FFT", "corex2", "q32DSP"]:
        t = regtable(k)
        print(k, fmt_base(k if k != "q32DSP" else "q32DSP0"),
              "regs", len(t) if t else 0)
        if t:
            print(" ", t[:6], "...")
