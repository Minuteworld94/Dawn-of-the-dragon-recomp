"""Decode textures dumped by the plume renderer (env SPYRO_DUMPTEX -> project\\run\\texdump\\*.bin)
to BMP files, to check / experiment with Xbox 360 untiling offline.

Usage: py -3 -I texdump_view.py <tex_xxx.bin> <out.bmp> [variant]
File: 6 fetch-constant dwords, u32 guest address, u32 size (little-endian), raw guest bytes.
Variants (untiling experiments): 'xg' (default, same as xenos_texture.cpp), 'linear'.
"""
import struct
import sys


VARIANT = 'xg'


def tiled_offset(x, y, pitch, bpb):
    log_bpp = {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[bpb]
    if VARIANT == 'log2': log_bpp = 2
    if VARIANT == 'log3': log_bpp = 3
    aligned = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return ((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) + \
        (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)


def swap(b, endian):
    b = bytearray(b)
    if endian == 1:
        for i in range(0, len(b) - 1, 2):
            b[i], b[i + 1] = b[i + 1], b[i]
    elif endian == 2:
        for i in range(0, len(b) - 3, 4):
            b[i:i + 4] = b[i:i + 4][::-1]
    return bytes(b)


def rgb565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def dxt_color(block, out, bx, by, w, four_color_always):
    c0, c1, bits = struct.unpack('<HHI', block)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or four_color_always:
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1)), (0, 0, 0)]
    for i in range(16):
        px, py = bx * 4 + (i & 3), by * 4 + (i >> 2)
        out[py * w + px][0:3] = pal[(bits >> (2 * i)) & 3]


def dxt5_alpha(block, out, bx, by, w):
    a0, a1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], 'little')
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
    for i in range(16):
        px, py = bx * 4 + (i & 3), by * 4 + (i >> 2)
        out[py * w + px][3] = pal[(bits >> (3 * i)) & 7]


def main():
    data = open(sys.argv[1], 'rb').read()
    global VARIANT
    variant = sys.argv[3] if len(sys.argv) > 3 else 'xg'
    VARIANT = variant
    w = struct.unpack('<6I', data[:24])
    raw = data[32:]
    if len(sys.argv) > 3 and sys.argv[3] == 'shift4k':
        raw = raw[4096:] + bytes(4096)
    fmt, endian = w[1] & 63, (w[1] >> 6) & 3
    width, height = (w[2] & 0x1FFF) + 1, ((w[2] >> 13) & 0x1FFF) + 1
    pitch, tiled = ((w[0] >> 22) & 0x1FF) * 32, w[0] >> 31
    block, bpb = {2: (1, 1), 6: (1, 4), 18: (4, 8), 19: (4, 16), 20: (4, 16)}[fmt]
    bw, bh = (width + block - 1) // block, (height + block - 1) // block
    pitch_blocks = max((pitch or width) // block, bw)
    if tiled:
        pitch_blocks = (pitch_blocks + 31) & ~31
    out = [[0, 0, 0, 255] for _ in range(bw * block * bh * block)]
    W = bw * block
    for by in range(bh):
        for bx in range(bw):
            if tiled and variant != 'linear':
                off = tiled_offset(bx, by, pitch_blocks, bpb)
                if variant in ('log2', 'log3'):
                    off = off // (1 << {'log2': 2, 'log3': 3}[variant]) * bpb
                if variant == 'pitch2':
                    off = tiled_offset(bx, by, pitch_blocks * 2, bpb)
            else:
                off = (by * pitch_blocks + bx) * bpb
            blk = swap(raw[off:off + bpb], endian)
            if len(blk) < bpb:
                continue
            if fmt == 2:
                out[by * W + bx][0:3] = [blk[0]] * 3
            elif fmt == 6:
                out[by * W + bx] = list(blk)  # x y z w; for A8R8G8B8 swizzle (z,y,x,w) -> display x=B
                out[by * W + bx] = [blk[2], blk[1], blk[0], blk[3]]
            elif fmt == 18:
                dxt_color(blk, out, bx, by, W, False)
            else:
                dxt_color(blk[8:16], out, bx, by, W, True)
                if fmt == 20:
                    dxt5_alpha(blk[0:8], out, bx, by, W)
    H = bh * block
    # BMP (bottom-up 24-bit; alpha shown as a grey band on the right half? no - just RGB)
    row = (W * 3 + 3) & ~3
    img = bytearray()
    for y in range(H - 1, -1, -1):
        line = bytearray()
        for x in range(W):
            r, g, b, a = out[y * W + x]
            # composite over grey checker so alpha is visible
            bg = 96 if ((x >> 3) + (y >> 3)) & 1 else 160
            line += bytes([(b * a + bg * (255 - a)) // 255, (g * a + bg * (255 - a)) // 255, (r * a + bg * (255 - a)) // 255])
        line += b'\0' * (row - len(line))
        img += line
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(img), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, W, H, 1, 24, 0, len(img), 2835, 2835, 0, 0)
    open(sys.argv[2], 'wb').write(hdr + img)
    print(f'{W}x{H} fmt {fmt} tiled {tiled} pitch {pitch} -> {sys.argv[2]}')


main()
