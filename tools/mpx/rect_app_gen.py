#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hand-assembled guest: open Graphics, draw two rects, present.

Imports only micropixel.service_open / service_call / service_submit / log_write.
Panel is the JD9853 296x240. Writes app/mpx/app_wasm_bundle.h.
"""
import os
import struct
import sys

PANEL_W = 296
PANEL_H = 240
GRAPHICS = 16
IFACE = 0x00010000  # major 1, minor 0
FMT_RGB565 = 3
METHOD_CREATE = 3
METHOD_PRESENT = 4
CHANNEL_RASTER = 2
RASTER_MAGIC = 0x5352504D
RECORD_RECT = 4
LOG_INFO = 2

INFO = 0x100
CREATE_REQ = 0x140
CREATE_RESP = 0x160
SIZE_OUT = 0x180
PRESENT = 0x184
RASTER = 0x1B0
DUMMY = 0x1F0
STR_BASE = 0x200


def uleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def sleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if (n == 0 and (b & 0x40) == 0) or (n == -1 and (b & 0x40) != 0):
            out.append(b)
            return bytes(out)
        out.append(b | 0x80)


def section(sid, payload):
    return bytes([sid]) + uleb(len(payload)) + payload


def vec(items):
    return uleb(len(items)) + b"".join(items)


def name(s):
    raw = s.encode()
    return uleb(len(raw)) + raw


def functype(params, results):
    return bytes([0x60]) + vec([bytes([p]) for p in params]) + vec([bytes([r]) for r in results])


I32 = 0x7F


class Asm(object):
    def __init__(self):
        self.b = bytearray()

    def op(self, *xs):
        self.b.extend(xs)
        return self

    def const(self, n):
        self.b.append(0x41)
        self.b.extend(sleb(n))
        return self

    def load(self, addr):
        return self.const(addr).op(0x28, 0x02, 0x00)

    def store(self, addr):
        # value is on stack; push addr first by swapping via a local? caller
        # emits: const addr; <value>; store. This helper is not used that way.
        raise AssertionError("use raw store")

    def call(self, idx):
        self.b.append(0x10)
        self.b.extend(uleb(idx))
        return self


def rect(x, y, w, h, color):
    return struct.pack("<BBBBhhHHHH", RECORD_RECT, 0, 255, 0, x, y, w, h, color, 0)


def build_data():
    image = bytearray(STR_BASE + 64)
    struct.pack_into("<HHIIIII", image, CREATE_REQ, 24, 0, PANEL_W, PANEL_H, FMT_RGB565, 1, 0)
    struct.pack_into("<HHIIIIIIII", image, PRESENT, 36, 0, 0, 0, 0, 0, PANEL_W * 2, PANEL_W, PANEL_H, 0)
    header_and_recs = struct.pack("<IIIIHHI", RASTER_MAGIC, 56, 0, 0, 2, 0, 0)
    header_and_recs += rect(0, 0, PANEL_W, PANEL_H, 0x0010)
    header_and_recs += rect(40, 50, 216, 140, 0xF800)
    assert len(header_and_recs) == 56
    image[RASTER:RASTER + 56] = header_and_recs
    strings = {
        "open": b"open",
        "Eopen": b"Eopen",
        "create": b"create",
        "Ecreate": b"Ecreate",
        "raster": b"Eraster",  # placeholder, real keys below
    }
    blobs = [b"open", b"Eopen", b"create", b"Ecreate", b"raster", b"Eraster", b"present", b"Epresent"]
    offs = {}
    cursor = STR_BASE
    names = ["open", "Eopen", "create", "Ecreate", "raster", "Eraster", "present", "Epresent"]
    for nm, blob in zip(names, blobs):
        image[cursor:cursor + len(blob)] = blob
        offs[nm] = (cursor, len(blob))
        cursor += len(blob)
    return bytes(image[INFO:cursor]), offs


def build_code(offs):
    a = Asm()
    # locals: 3 x i32 ($status, $gfx, $surface)
    body = bytearray()

    def log(key):
        addr, ln = offs[key]
        a.const(LOG_INFO).const(addr).const(ln).call(3).op(0x1A)

    def fail_return(key):
        log(key)
        a.op(0x0F)

    log("open")
    a.const(GRAPHICS).const(IFACE).const(INFO).const(48).call(0)
    a.op(0x21, 0x00)  # local.set 0
    a.op(0x20, 0x00)  # local.get 0
    a.op(0x04, 0x40)  # if
    fail_return("Eopen")
    a.op(0x0B)  # end

    a.const(INFO + 8).op(0x28, 0x02, 0x00).op(0x21, 0x01)  # gfx handle

    log("create")
    a.op(0x20, 0x01).const(METHOD_CREATE).const(CREATE_REQ).const(24)
    a.const(CREATE_RESP).const(20).const(SIZE_OUT).call(1)
    a.op(0x21, 0x00).op(0x20, 0x00).op(0x04, 0x40)
    fail_return("Ecreate")
    a.op(0x0B)

    a.const(CREATE_RESP + 4).op(0x28, 0x02, 0x00).op(0x21, 0x02)
    # raster header.surface_handle and present.surface_handle
    a.const(RASTER + 8).op(0x20, 0x02).op(0x36, 0x02, 0x00)
    a.const(PRESENT + 4).op(0x20, 0x02).op(0x36, 0x02, 0x00)

    log("raster")
    a.op(0x20, 0x01).const(CHANNEL_RASTER).const(RASTER).const(56).call(2)
    a.op(0x21, 0x00).op(0x20, 0x00).op(0x04, 0x40)
    fail_return("Eraster")
    a.op(0x0B)

    a.op(0x20, 0x01).const(METHOD_PRESENT).const(PRESENT).const(36)
    a.const(DUMMY).const(4).const(SIZE_OUT).call(1)
    a.op(0x21, 0x00).op(0x20, 0x00).op(0x04, 0x40)
    fail_return("Epresent")
    a.op(0x0B)

    log("present")
    a.op(0x0B)  # function end

    local_decl = bytes([0x01, 0x03, 0x7F])
    func = local_decl + bytes(a.b)
    return uleb(len(func)) + func


def build_wasm():
    data, offs = build_data()
    types = vec([
        functype([I32, I32, I32, I32], [I32]),
        functype([I32] * 7, [I32]),
        functype([I32, I32, I32], [I32]),
        functype([], []),
    ])
    imports = vec([
        name("micropixel") + name("service_open") + bytes([0x00]) + uleb(0),
        name("micropixel") + name("service_call") + bytes([0x00]) + uleb(1),
        name("micropixel") + name("service_submit") + bytes([0x00]) + uleb(0),
        name("micropixel") + name("log_write") + bytes([0x00]) + uleb(2),
    ])
    funcs = vec([uleb(3)])
    memory = bytes([0x01, 0x00, 0x01])
    exports = vec([name("__micropixel_start") + bytes([0x00]) + uleb(4)])
    code = vec([build_code(offs)])
    # active data seg: flags 0, i32.const INFO, end, bytes
    data_body = bytes([0x00, 0x41]) + sleb(INFO) + bytes([0x0B]) + uleb(len(data)) + data
    data_sec = vec([data_body])
    wasm = b"\x00asm\x01\x00\x00\x00"
    wasm += section(1, types)
    wasm += section(2, imports)
    wasm += section(3, funcs)
    wasm += section(5, memory)
    wasm += section(7, exports)
    wasm += section(10, code)
    wasm += section(11, data_sec)
    return wasm


def fnv1a32(data):
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def build_bundle(wasm):
    # Layout matches mpx_bundle_open(), not the packed struct in bundle_format.h.
    header = bytearray(128)
    header[0:8] = b"MPXBNDL\x00"
    struct.pack_into("<I", header, 12, 1)       # version
    struct.pack_into("<I", header, 16, 128)     # header_size
    struct.pack_into("<I", header, 24, 128)     # toc offset
    struct.pack_into("<I", header, 28, 1)       # framework abi
    app_id = b"com.rect"
    header[32:32 + len(app_id)] = app_id
    struct.pack_into("<I", header, 40, len(app_id))
    struct.pack_into("<I", header, 44, 1)       # one section
    wasm_off = 192  # 64-aligned, past the 48-byte TOC entry
    blob = bytearray(wasm_off + len(wasm))
    struct.pack_into("<I", header, 20, len(blob))
    # hash with the field zeroed
    struct.pack_into("<I", header, 88, fnv1a32(header))
    blob[0:128] = header
    sec = bytearray(48)
    struct.pack_into("<IIIIII", sec, 0, 5, 0, wasm_off, len(wasm), fnv1a32(wasm), 12)
    # width/height/stride at +24 stay 0; flags at +36 stay 0
    struct.pack_into("<I", sec, 40, 1 << 2)  # reserved0 = WASM_INTERP
    blob[128:176] = sec
    blob[wasm_off:] = wasm
    return bytes(blob)


def check_bundle(data):
    assert data[0:8] == b"MPXBNDL\x00"
    assert struct.unpack_from("<I", data, 12)[0] == 1
    assert struct.unpack_from("<I", data, 16)[0] == 128
    assert struct.unpack_from("<I", data, 28)[0] == 1
    for off in (96, 100, 104, 108, 112):
        assert struct.unpack_from("<I", data, off)[0] == 0
    stored = struct.unpack_from("<I", data, 88)[0]
    hdr = bytearray(data[:128])
    hdr[88:92] = b"\x00\x00\x00\x00"
    assert fnv1a32(hdr) == stored
    assert struct.unpack_from("<I", data, 20)[0] <= len(data)
    assert struct.unpack_from("<I", data, 24)[0] == 128
    count = struct.unpack_from("<I", data, 44)[0]
    assert count == 1
    sec = data[128:176]
    kind, sid, off, size, hsh, fmt = struct.unpack_from("<IIIIII", sec, 0)
    assert (kind, sid, fmt) == (5, 0, 12)
    assert struct.unpack_from("<I", sec, 36)[0] == 0
    assert struct.unpack_from("<I", sec, 40)[0] == 4
    assert struct.unpack_from("<I", sec, 44)[0] == 0
    assert off % 64 == 0 and off >= 176
    assert fnv1a32(data[off:off + size]) == hsh
    assert data[off:off + 4] == b"\x00asm"


def emit_header(data, path):
    lines = [
        "/* Auto-generated by tools/mpx/rect_app_gen.py - do not edit. */",
        "#ifndef _MPX_APP_WASM_BUNDLE_H_",
        "#define _MPX_APP_WASM_BUNDLE_H_",
        "",
        "static const unsigned char k_mpx_app_bundle[] __attribute__((aligned(4))) = {",
    ]
    hexvals = ["0x%02x" % b for b in data]
    for i in range(0, len(hexvals), 12):
        lines.append("    " + ", ".join(hexvals[i:i + 12]) + ",")
    lines += [
        "};",
        "#define MPX_APP_BUNDLE_SIZE (sizeof(k_mpx_app_bundle))",
        "",
        "#endif /* _MPX_APP_WASM_BUNDLE_H_ */",
        "",
    ]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    wasm = build_wasm()
    bundle = build_bundle(wasm)
    check_bundle(bundle)
    out = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..",
                                        "app", "mpx", "app_wasm_bundle.h"))
    emit_header(bundle, out)
    print("wasm", len(wasm), "bundle", len(bundle), "->", out)


if __name__ == "__main__":
    sys.exit(main())
