# Patch stock uboot.boot so a failed record check cannot skip the app.
# At the original call site, bitbang 'U' on PB3, then call 0x2000120.
import struct
import subprocess
import os

HERE = os.path.dirname(os.path.abspath(__file__))
CLANG = r"C:\JL\pi32\bin\clang.exe"
OBJCOPY = r"C:\JL\pi32\bin\llvm-objcopy.exe"
NM = r"C:\JL\pi32\bin\llvm-nm.exe"
LOAD = 0x1C02000


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def main():
    src = os.path.join(HERE, "uboot.boot")
    ub = bytearray(open(src, "rb").read())
    if ub[0x3244:0x3248] != bytes.fromhex("87e82e00"):
        raise SystemExit("unexpected bytes at 0x3244")
    if ub[0x3294:0x3298] != bytes.fromhex("d0ecc010"):
        raise SystemExit("unexpected bytes at 0x3294")
    ub[0x3244:0x3248] = b"\x00\x00\x00\x00"

    subprocess.check_call([
        CLANG, "-c", "-target", "pi32v2", "-mcpu=r3", "-Oz", "-ffreestanding",
        os.path.join(HERE, "early_uboot.c"),
        "-o", os.path.join(HERE, "early_uboot.o"),
    ])
    blob_path = os.path.join(HERE, "early_uboot.bin")
    subprocess.check_call([
        OBJCOPY, "-O", "binary", "-j", ".text",
        os.path.join(HERE, "early_uboot.o"), blob_path,
    ])
    blob = open(blob_path, "rb").read()
    nm = subprocess.check_output([NM, os.path.join(HERE, "early_uboot.o")], text=True)
    entry = None
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == "early_uboot":
            entry = int(parts[0], 16)
    if entry is None:
        raise SystemExit("early_uboot symbol missing")

    marker = bytes.fromhex("c0ff0050c001")
    at = blob.find(marker)
    if at < 0:
        raise SystemExit("placeholder 0x1C05000 not found")

    stub_file = len(ub)
    if stub_file & 1:
        ub.append(0)
        stub_file += 1
    pc_after = 0x3298
    disp = (stub_file + entry - pc_after) // 2
    ub[0x3294:0x3298] = bytes.fromhex("c0ea") + struct.pack("<h", disp)

    tail_file = stub_file + len(blob)
    tail_cpu = LOAD + tail_file - 0x10
    blob = bytearray(blob)
    blob[at + 2:at + 6] = struct.pack("<I", tail_cpu)
    # r1 = 0x2000120; r0 = r10 + 136; call r1; then hang if it returns
    tail = bytes.fromhex("c1ff20010002" "00e188a0" "c100" "c0eafeff")
    out = ub + blob + tail

    out[2:4] = struct.pack("<H", len(out) - 16)
    out[12:14] = struct.pack("<H", crc16(out[16:]))
    out[14:16] = struct.pack("<H", crc16(out[0:14]))
    dest = os.path.join(HERE, "uboot_lite.boot")
    open(dest, "wb").write(out)
    print("uboot_lite.boot %d bytes, stub 0x%x, tail 0x%x" % (len(out), LOAD + stub_file + entry - 0x10, tail_cpu))


if __name__ == "__main__":
    main()
