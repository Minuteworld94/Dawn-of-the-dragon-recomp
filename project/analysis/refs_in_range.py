"""Which addresses inside a range are referenced from anywhere in the image?
Usage: py -3 -I refs_in_range.py <image_dump.bin> <lo-hex> <hi-hex>
Reports, for each referenced address in [lo,hi), the data words and lis/addi pairs that hold it.
"""
import struct
import sys

IMG_BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
n = len(img) // 4
words = struct.unpack(f">{n}I", img[: n * 4])

data_hits = {}
for i, v in enumerate(words):
    if lo <= v < hi:
        data_hits.setdefault(v, []).append(IMG_BASE + i * 4)

code_hits = {}
for i in range(n - 1):
    v = words[i]
    if (v >> 26) != 15 or ((v >> 16) & 31) != 0:
        continue
    rd, h = (v >> 21) & 31, v & 0xFFFF
    for j in range(i + 1, min(i + 10, n)):
        w = words[j]
        op = w >> 26
        if op == 14 and ((w >> 16) & 31) == rd:
            x = w & 0xFFFF
            val = ((h << 16) + (x - 0x10000 if x & 0x8000 else x)) & 0xFFFFFFFF
        elif op == 24 and ((w >> 21) & 31) == rd:
            val = (h << 16) | (w & 0xFFFF)
        else:
            continue
        if lo <= val < hi:
            code_hits.setdefault(val, []).append(IMG_BASE + i * 4)
        break

for a in sorted(set(data_hits) | set(code_hits)):
    d = data_hits.get(a, [])
    c = code_hits.get(a, [])
    print(f"  0x{a:08X}  (+0x{a - lo:03X})  data:{len(d)} {[hex(x) for x in d[:4]]}  code:{len(c)} {[hex(x) for x in c[:4]]}")
if not data_hits and not code_hits:
    print("  (nothing in this range is referenced by absolute address)")
