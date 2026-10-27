"""Find every place the guest image refers to an address.

Usage: py -3 -I xrefs.py <image_dump.bin> <addr-hex> [more addrs...]

Reports three kinds of reference:
  data    - a 32-bit word holding the address (vtable slot, pointer table, string pointer)
  code    - lis/addi or lis/ori pair that materialises the address in a register
  branch  - a bl/b instruction targeting it
Addresses are printed with the enclosing function from .pdata when there is one.
"""
import bisect
import struct
import sys

IMG_BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
targets = [int(a, 16) for a in sys.argv[2:]]

n = len(img) // 4
words = struct.unpack(f">{n}I", img[: n * 4])

lf = struct.unpack("<I", img[0x3C:0x40])[0]
nsec = struct.unpack("<H", img[lf + 6:lf + 8])[0]
optsz = struct.unpack("<H", img[lf + 20:lf + 22])[0]
exc_rva, exc_size = struct.unpack("<II", img[lf + 24 + 96 + 24:lf + 24 + 96 + 32])
code = []
for i in range(nsec):
    o = lf + 24 + optsz + i * 40
    vs, va = struct.unpack("<II", img[o + 8:o + 16])
    if struct.unpack("<I", img[o + 36:o + 40])[0] & 0x20:
        code.append((IMG_BASE + va, IMG_BASE + va + vs))
CODE_LO, CODE_HI = min(c[0] for c in code), max(c[1] for c in code)

pd = []
for i in range(exc_size // 8):
    b, data = struct.unpack(">II", img[exc_rva + i * 8:exc_rva + i * 8 + 8])
    pd.append((b, b + (((data >> 8) & 0x3FFFFF) * 4 or 4)))
pd.sort()
pd_starts = [p[0] for p in pd]


def owner(addr):
    i = bisect.bisect_left(pd_starts, addr) - 1
    if i >= 0 and pd[i][0] <= addr < pd[i][1]:
        return f"in sub_{pd[i][0]:08X}"
    return ""


for t in targets:
    print(f"\n==== references to 0x{t:08X}")
    for i, v in enumerate(words):
        a = IMG_BASE + i * 4
        if v == t and not (CODE_LO <= a < CODE_HI):
            print(f"  data   0x{a:08X}")
    hi_want, lo_want = (t >> 16) & 0xFFFF, t & 0xFFFF
    # addi sign-extends its immediate, so when the low half has bit 15 set the compiler
    # emits lis with hi+1 and a negative addi. Checking only hi_want misses those.
    hi_carry = (hi_want + 1) & 0xFFFF
    for i in range((CODE_LO - IMG_BASE) >> 2, min((CODE_HI - IMG_BASE) >> 2, n)):
        v = words[i]
        if (v >> 26) != 15 or ((v >> 16) & 31) != 0:
            continue
        rd, hi = (v >> 21) & 31, v & 0xFFFF
        if hi not in (hi_want, hi_carry):
            continue
        for j in range(i + 1, min(i + 10, n)):
            w = words[j]
            op = w >> 26
            if op == 14 and ((w >> 16) & 31) == rd:
                lo = w & 0xFFFF
                val = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            elif op == 24 and ((w >> 21) & 31) == rd:
                val = (hi << 16) | (w & 0xFFFF)
            else:
                continue
            if val == t:
                a = IMG_BASE + i * 4
                print(f"  code   0x{a:08X}  {owner(a)}")
            break
    for i in range((CODE_LO - IMG_BASE) >> 2, min((CODE_HI - IMG_BASE) >> 2, n)):
        v = words[i]
        if (v >> 26) != 18:
            continue
        off = v & 0x03FFFFFC
        if off & 0x02000000:
            off -= 0x04000000
        a = IMG_BASE + i * 4
        dest = (a + off) & 0xFFFFFFFF if not (v & 2) else off & 0xFFFFFFFF
        if dest == t:
            kind = "bl" if v & 1 else "b "
            print(f"  {kind}     0x{a:08X}  {owner(a)}")
