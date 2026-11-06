import os
base = r"D:\fw-mini-sdk\cpu\wl82\liba"
need = [
    b"[SEND FIN]",
    b"Subscription request",
    b"Action_name",
    b"*******gena_notify",
    b"dac open end",
    b"dac sample rate",
    b"net_download_dev_open",
    b"---dlna url---",
    b"etharp_raw: sending",
    b"udp_bind: bound",
]
for l in sorted(os.listdir(base)):
    if not l.endswith(".a"):
        continue
    p = os.path.join(base, l)
    d = open(p, "rb").read()
    hits = [n.decode() for n in need if n in d]
    if hits:
        print(l + ":", ", ".join(hits))

for l in ["lwip_2_1_3.a", "wl_wifi.a", "libdlna.a", "cpu.a", "common_lib.a", "network_download.a"]:
    p = os.path.join(base, l)
    if not os.path.exists(p):
        continue
    d = open(p, "rb").read()
    print("---", l)
    for s in [b"()()", b"()\x00", b"IP\x00", b"()\n", b"IP\n", b"() ", b"putbyte"]:
        c = d.count(s)
        if c:
            print(" ", repr(s), c)
