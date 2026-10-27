"""Find functions that load given struct offsets (lwz/lhz/lbz/lfs rD, OFF(rA)).

Usage: py -3 -I find_field_users.py <image_dump.bin> <lo-hex> <hi-hex> <off-hex> [<off-hex>...]

Scans code in [lo, hi), groups load instructions by .pdata function, and lists the functions that
load ALL of the requested offsets (then those that load most of them). Useful for locating the
code that consumes a known structure layout, e.g. a renderer reading a text node's font + string.
"""
import bisect
import struct
import sys

IMG_BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
offs = [int(x, 16) for x in sys.argv[4:]]
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


LOADS = {32, 34, 40, 48}  # lwz, lbz, lhz, lfs
found = {}
for i in range((lo - IMG_BASE) >> 2, (hi - IMG_BASE) >> 2):
    v = w[i]
    if (v >> 26) in LOADS and (v & 0xFFFF) in offs:
        f = owner(IMG_BASE + i * 4)
        if f:
            found.setdefault(f, set()).add(v & 0xFFFF)

ranked = sorted(found.items(), key=lambda kv: (-len(kv[1]), kv[0]))
print(f"functions loading any of {[hex(o) for o in offs]} in [{lo:#x},{hi:#x}): {len(found)}")
for f, s in ranked[:30]:
    print(f"  sub_{f:08X}  loads {sorted(hex(x) for x in s)}")
