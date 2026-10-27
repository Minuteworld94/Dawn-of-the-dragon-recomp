"""Scan the dumped guest image for pointers into the code range that the recompiler's analysis did
not register as functions (vtable entries, callback tables).  Read-only.

Usage: py -3 -I find_func_pointers.py <image_dump.bin> <codegen.partition.json> [out.toml]

For each candidate it uses the game's own .pdata table (exact function extents) to
  - skip addresses that fall strictly INSIDE an existing function (can't be split off safely),
  - give the rest an explicit 'end' (next known/candidate function start, clipped to the code section).
Writes manifest-ready [functions.XXXXXXXX] entries (an include file for [entrypoint].includes).
"""
import bisect
import json
import struct
import sys

IMG_BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
known = sorted(int(k, 16) for k in json.load(open(sys.argv[2]))["assignments"])
known_set = set(known)
out_path = sys.argv[3] if len(sys.argv) > 3 else None

# --- PE sections / .pdata ---------------------------------------------------------------
lf = struct.unpack("<I", img[0x3C:0x40])[0]
nsec, optsz = struct.unpack("<H", img[lf + 6:lf + 8])[0], struct.unpack("<H", img[lf + 20:lf + 22])[0]
opt = lf + 24
exc_rva, exc_size = struct.unpack("<II", img[opt + 96 + 3 * 8:opt + 96 + 3 * 8 + 8])
code_secs = []
for i in range(nsec):
    o = lf + 24 + optsz + i * 40
    name = img[o:o + 8].rstrip(b"\0").decode()
    vs, va = struct.unpack("<II", img[o + 8:o + 16])
    ch = struct.unpack("<I", img[o + 36:o + 40])[0]
    if ch & 0x20:  # IMAGE_SCN_CNT_CODE
        code_secs.append((IMG_BASE + va, IMG_BASE + va + vs, name))
CODE_LO = min(s[0] for s in code_secs)
CODE_HI = max(s[1] for s in code_secs)


def code_end(addr):
    for lo, hi, _ in code_secs:
        if lo <= addr < hi:
            return hi
    return None


pdata = []
base = exc_rva
for i in range(exc_size // 8):
    b, data = struct.unpack(">II", img[base + i * 8:base + i * 8 + 8])
    length = ((data >> 8) & 0x3FFFFF) * 4 or 4
    pdata.append((b, b + length))
pdata.sort()
pdata_starts = [p[0] for p in pdata]


def inside_function(t):
    i = bisect.bisect_left(pdata_starts, t) - 1
    return i >= 0 and pdata[i][0] < t < pdata[i][1]


n = len(img) // 4
words = struct.unpack(f">{n}I", img[: n * 4])


def word_at(addr):
    i = (addr - IMG_BASE) >> 2
    return words[i] if 0 <= i < n else None


def in_code(v):
    return CODE_LO <= v < CODE_HI and v % 4 == 0


def ends_function(prev):
    if prev is None:
        return False
    return (prev in (0x4E800020, 0x4E800420, 0x00000000, 0x60000000, 0xFFFFFFFF, 0x7FE00008)
            or (prev >> 26) == 18
            or (prev & 0xFFFF0000) == 0x4E800000)


cands = {}
for i, v in enumerate(words):
    addr = IMG_BASE + i * 4
    if CODE_LO <= addr < CODE_HI:
        continue
    if in_code(v):
        cands.setdefault(v, []).append(addr)

MTCTR_R11, BCTR = 0x7D6903A6, 0x4E800420


def is_import_thunk(t):
    """Kernel import stub: <2 XEX import descriptor words> mtctr r11 ; bctr. The SDK registers these
    itself as imports; redefining them as functions makes the link fail ('outranked by import').
    Must NOT match a virtual-call thunk, which ends in the same two instructions."""
    w0, w1 = word_at(t), word_at(t + 4)
    return (word_at(t + 8) == MTCTR_R11 and word_at(t + 12) == BCTR
            and w0 is not None and w1 is not None
            and (w0 >> 24) == 0x01 and (w1 >> 24) == 0x02)


def find_vcall_thunks():
    """C++ virtual dispatch stubs: lwz r12,0(rN) ; lwz r11,off(r12) ; mtctr r11 ; bctr.
    Exactly 16 bytes, reached only through a vtable, so analysis rarely finds them."""
    found = []
    for addr in range(CODE_LO, CODE_HI - 16, 4):
        if word_at(addr + 8) != MTCTR_R11 or word_at(addr + 12) != BCTR:
            continue
        w0, w1 = word_at(addr), word_at(addr + 4)
        if (w0 >> 26) == 32 and (w1 >> 26) == 32:  # lwz, lwz
            found.append(addr)
    return found


# Second source: addresses materialised in code as  lis rD,hi ; addi/ori rX,rD,lo  (callbacks passed to
# registration functions, CRT init tables built at runtime, ...).  Only code-range, 4-aligned results.
code_refs = {}
LOOKAHEAD = 12
for i in range((CODE_LO - IMG_BASE) >> 2, min((CODE_HI - IMG_BASE) >> 2, n)):
    v = words[i]
    if (v >> 26) != 15 or ((v >> 16) & 31) != 0:        # lis rD, imm  == addis rD, 0, imm
        continue
    rd, hi = (v >> 21) & 31, v & 0xFFFF
    for j in range(i + 1, min(i + 1 + LOOKAHEAD, n)):
        w = words[j]
        op = w >> 26
        if op == 14 and ((w >> 16) & 31) == rd:          # addi rX, rD, simm
            lo = w & 0xFFFF
            lo = lo - 0x10000 if lo & 0x8000 else lo
            val = ((hi << 16) + lo) & 0xFFFFFFFF
        elif op == 24 and ((w >> 21) & 31) == rd:        # ori rX, rS=rD, uimm
            val = (hi << 16) | (w & 0xFFFF)
        else:
            if ((w >> 21) & 31) == rd and op in (14, 15, 32, 34, 36, 40, 44, 24, 25):
                pass
            continue
        if in_code(val):
            code_refs.setdefault(val, []).append(IMG_BASE + i * 4)
        break

for t, refs in code_refs.items():
    cands.setdefault(t, []).extend(refs)

try:
    banned = {int(x, 16) for x in open(__file__.rsplit("\\", 1)[0] + "\\banned_candidates.txt").read().split()}
except FileNotFoundError:
    banned = set()

missing = {t: r for t, r in cands.items() if t not in known_set and not is_import_thunk(t) and t not in banned}
strong = sorted(t for t in missing if ends_function(word_at(t - 4)))
weak = sorted(t for t in missing if t not in set(strong))
vcall_set = set(find_vcall_thunks())  # emitted separately below, with an exact size
inner = [t for t in strong if inside_function(t)]
usable = [t for t in strong if not inside_function(t) and t not in vcall_set]

print(f"pdata functions: {len(pdata)}   code sections: {[(hex(a), hex(b), n) for a, b, n in code_secs]}")
print(f"pointer targets: {len(cands)}   known: {len(cands) - len(missing)}   MISSING: {len(missing)} "
      f"(strong {len(strong)}, weak {len(weak)})")
print(f"strong candidates INSIDE an existing function (skipped): {len(inner)} {[hex(t) for t in inner[:8]]}")
print(f"usable strong candidates: {len(usable)}")

vcalls = sorted(t for t in vcall_set
                if t not in known_set and t not in banned and not inside_function(t))
print(f"virtual-call thunks: total {len(vcall_set)}, adding {len(vcalls)}")

if out_path:
    starts = sorted(set(known) | set(usable) | set(vcalls))
    with open(out_path, "w") as f:
        f.write("# AUTO-GENERATED by project/analysis/find_func_pointers.py - do not hand edit.\n")
        f.write("# Function entry points the analysis missed: reached only via vtables/pointers.\n")
        for t in usable:
            nxt = starts[bisect.bisect_right(starts, t)] if bisect.bisect_right(starts, t) < len(starts) else None
            lim = code_end(t)
            end = min(x for x in (nxt, lim) if x is not None)
            f.write(f"[functions.{t:08X}]\nend = 0x{end:08X}\n")
        for t in vcalls:  # always exactly 4 instructions
            f.write(f"[functions.{t:08X}]\nsize = 16\n")
    print(f"wrote {len(usable) + len(vcalls)} entries to {out_path}")
