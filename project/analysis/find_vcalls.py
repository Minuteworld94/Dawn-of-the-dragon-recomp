"""Find virtual calls through a given vtable byte offset:  lwz rX, OFF(rY) ... mtctr rX ... bctrl
Usage: py -3 -I find_vcalls.py <image_dump.bin> <offset-hex>
Prints each call site and the .pdata function that contains it.
"""
import bisect
import struct
import sys

IMG_BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
off = int(sys.argv[2], 16)
n = len(img) // 4
w = struct.unpack(f">{n}I", img[: n * 4])

pd = []
for i in range(0x1CA08 // 8):
    b, d = struct.unpack(">II", img[0x88800 + i * 8:0x88800 + i * 8 + 8])
    pd.append((b, b + (((d >> 8) & 0x3FFFFF) * 4 or 4)))
pd.sort()
st = [p[0] for p in pd]


def owner(a):
    i = bisect.bisect_left(st, a + 1) - 1
    return pd[i][0] if i >= 0 and pd[i][0] <= a < pd[i][1] else None


hits = {}
for i in range((0x820B0000 - IMG_BASE) >> 2, (0x826DF1BC - IMG_BASE) >> 2):
    v = w[i]
    if (v >> 26) != 32 or (v & 0xFFFF) != off:
        continue
    rx = (v >> 21) & 31
    for j in range(i + 1, min(i + 6, n)):
        if w[j] == (0x7C0903A6 | (rx << 21)):           # mtctr rX
            for k in range(j + 1, min(j + 6, n)):
                if w[k] == 0x4E800421:                    # bctrl
                    a = IMG_BASE + i * 4
                    hits.setdefault(owner(a), []).append(a)
                    break
            break

print(f"virtual calls through vtable offset 0x{off:X}: {sum(len(v) for v in hits.values())} sites "
      f"in {len(hits)} functions")
for f, sites in sorted(hits.items(), key=lambda kv: kv[0] or 0):
    name = f"sub_{f:08X}" if f else "(no pdata function)"
    print(f"  {name}: {', '.join(hex(s) for s in sites)}")
