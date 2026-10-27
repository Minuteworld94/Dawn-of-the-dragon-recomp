"""Read-only XEX2 header dump. Usage: py -3 -I xex_info.py <path to .xex>"""
import struct
import sys

path = sys.argv[1]
with open(path, "rb") as f:
    data = f.read()

magic = data[:4]
print("magic:", magic)
if magic != b"XEX2":
    sys.exit("not an XEX2 file")

module_flags, pe_off, _res, sec_off, opt_count = struct.unpack(">IIIII", data[4:24])
print(f"file size: {len(data)}  module_flags=0x{module_flags:08X} pe_data_offset=0x{pe_off:X} "
      f"security_offset=0x{sec_off:X} optional_headers={opt_count}")
flag_names = {0x1: "title module", 0x2: "exports to title", 0x4: "system debugger", 0x8: "dll module",
              0x10: "module patch", 0x20: "patch full", 0x40: "patch delta", 0x80: "user mode"}
print("module type flags:", [n for b, n in flag_names.items() if module_flags & b] or "none")

opts = {}
for i in range(opt_count):
    key, val = struct.unpack(">II", data[24 + i * 8: 32 + i * 8])
    opts[key] = val

print("optional header keys:", ", ".join(f"0x{k:X}" for k in sorted(opts)))


def blob(key):
    """Optional header whose low byte is 0x00/0x01 holds the value inline; 0xFF = offset to size-prefixed
    data; other low bytes = offset to fixed-size data (size = low byte * 4)."""
    v = opts[key]
    low = key & 0xFF
    if low == 0xFF:
        size = struct.unpack(">I", data[v:v + 4])[0]
        return data[v:v + size]
    return None


# Execution info (0x40006)
if 0x40006 in opts:
    o = opts[0x40006]
    media_id, version, base_version, title_id, platform, exec_type, disc_no, disc_cnt = struct.unpack(
        ">IIIIBBBB", data[o:o + 20])

    def ver(v):
        return f"{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}"
    print(f"\nEXECUTION INFO\n  title id : {title_id:08X}  (publisher prefix '{chr((title_id >> 24) & 0xFF)}"
          f"{chr((title_id >> 16) & 0xFF)}', game number {title_id & 0xFFFF})")
    print(f"  media id : {media_id:08X}\n  version  : {ver(version)}  (raw 0x{version:08X})\n"
          f"  base ver : {ver(base_version)}  (raw 0x{base_version:08X})\n  disc     : {disc_no}/{disc_cnt}"
          f"  platform={platform} exec_type={exec_type}")

if 0x10001 in opts:
    print(f"\nBASE ADDRESS  : 0x{opts[0x10001]:08X}")
if 0x10100 in opts:
    print(f"ENTRY POINT   : 0x{opts[0x10100]:08X}")
if 0x10201 in opts:
    print(f"IMAGE BASE    : 0x{opts[0x10201]:08X}")
if 0x20200 in opts:
    print(f"DEFAULT STACK : 0x{opts[0x20200]:X}")
if 0x20301 in opts:
    print(f"DEFAULT HEAP  : 0x{opts[0x20301]:X}")
if 0x30100 in opts:
    print(f"SYSTEM FLAGS  : 0x{opts[0x30100]:08X}")
if 0x18102 in opts:
    print(f"(0x18102 alt) : 0x{opts[0x18102]:08X}")

for k, label in ((0x183FF, "ORIGINAL PE NAME"),):
    if k in opts:
        b = blob(k)
        print(f"{label}: {b[4:].split(chr(0).encode())[0].decode(errors='replace')}")

# File format info (0x3FF): encryption + compression
if 0x3FF in opts:
    o = opts[0x3FF]
    size, enc, comp = struct.unpack(">IHH", data[o:o + 8])
    enc_n = {0: "none", 1: "normal (AES)"}.get(enc, str(enc))
    comp_n = {0: "none", 1: "basic (zero-run)", 2: "normal (LZX)", 3: "delta"}.get(comp, str(comp))
    print(f"\nFILE FORMAT: encryption={enc_n}  compression={comp_n}")

# Import libraries (0x103FF)
if 0x103FF in opts:
    o = opts[0x103FF]
    size, str_tbl_size, lib_count = struct.unpack(">III", data[o:o + 12])
    strs = data[o + 12:o + 12 + str_tbl_size].split(b"\x00")
    strs = [s.decode(errors="replace") for s in strs if s]
    print(f"\nIMPORT LIBRARIES ({lib_count}): {strs}")
    p = o + 12 + str_tbl_size
    for _ in range(lib_count):
        size = struct.unpack(">I", data[p:p + 4])[0]
        name_idx = struct.unpack(">H", data[p + 0x24:p + 0x26])[0] if size >= 0x28 else None
        count = struct.unpack(">H", data[p + 0x26:p + 0x28])[0]
        lib = strs[name_idx & 0xFF] if name_idx is not None and (name_idx & 0xFF) < len(strs) else "?"
        ver_t = struct.unpack(">I", data[p + 0x1C:p + 0x20])[0]
        print(f"  {lib:14s} imports={count:4d}  built-against version {ver_t >> 28}.{(ver_t >> 24) & 0xF}.{(ver_t >> 8) & 0xFFFF}.{ver_t & 0xFF}")
        p += size

# Security info: image size, load address
so = sec_off
hdr_size, image_size = struct.unpack(">II", data[so:so + 8])
load_addr = struct.unpack(">I", data[so + 0x110:so + 0x114])[0]
print(f"\nSECURITY INFO: image_size=0x{image_size:X}  load_address=0x{load_addr:08X}")
