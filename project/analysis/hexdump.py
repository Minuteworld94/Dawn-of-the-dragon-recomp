"""Hexdump a guest address range from the image dump.
Usage: py -3 -I hexdump.py <image_dump.bin> <addr-hex> [length-hex]
"""
import struct
import sys

img = open(sys.argv[1], "rb").read()
addr = int(sys.argv[2], 16)
length = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x80
off = addr - 0x82000000
for r in range(0, length, 16):
    chunk = img[off + r: off + r + 16]
    if len(chunk) < 16:
        break
    w = struct.unpack(">4I", chunk)
    asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
    print(f"  {addr + r:08X}  {w[0]:08X} {w[1]:08X} {w[2]:08X} {w[3]:08X}  |{asc}|")
