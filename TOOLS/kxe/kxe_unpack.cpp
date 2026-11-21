// kxe_unpack - research tool: unpacks the game's .KXE files (Xbox 360 "XCTD" compressed title data).
// Works on the player's own game files; its output is game data - never commit or ship it.
//
// Format (found in the game's own reader, sub_82414A50 / sub_82416CC8, WORK_LOG 154):
//   header, big-endian:  u32 magic 0x0FF512ED, u32 version 1, u32 (checksum?), u32 flags
//     flags bits 4-5   window / block size = 0x8000 << n (128 KB in this game)
//     flags bits 6-21  block count
//     flags bits 22-23 non-zero: 32-bit table entries, else 20-bit
//   then the block table: one entry per block = its UNPACKED size, bit-packed MSB first,
//   padded to whole u32 (header size = ((bits * count + 31) / 32 + 4) * 4 bytes).
//   Block i fills file bytes [i * block, (i + 1) * block) (block 0 after the header): LZX frames,
//   each a u16 big-endian compressed size + data (32 KB unpacked each, the block's last one
//   shorter), then zero padding. Every block is its own LZX stream (window = block size).
//   Frames of odd size need a pad byte for libmspack (it realigns to 16 bits between frames).
//
// Usage: kxe_unpack <file.kxe> [out.bin]       (no out = list the blocks)
//        kxe_unpack <file.kxe> <out.bin> --blocks <first> <count>
// Build: TOOLS\kxe\build.ps1 (links the reXglue SDK's lzx_decompress = libmspack's LZX decoder).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int lzx_decompress(const void* lzx_data, size_t lzx_len, void* dest, size_t dest_len, uint32_t window_size,
                   void* window_data, size_t window_data_len);

static uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: kxe_unpack <file.kxe> [out.bin] [--blocks first count]\n");
    return 2;
  }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) return std::fprintf(stderr, "cannot open %s\n", argv[1]), 1;
  std::fseek(f, 0, SEEK_END);
  size_t size = size_t(std::ftell(f));
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> file(size);
  if (std::fread(file.data(), 1, size, f) != size) return std::fprintf(stderr, "read failed\n"), 1;
  std::fclose(f);
  if (size < 16 || Be32(&file[0]) != 0x0FF512EDu) return std::fprintf(stderr, "not a KXE (XCTD) file\n"), 1;
  uint32_t flags = Be32(&file[12]);
  uint32_t block = 0x8000u << ((flags >> 4) & 3);
  uint32_t count = (flags >> 6) & 0xFFFF;
  uint32_t bits = (flags & 0xC00000) ? 32 : 20;
  size_t header = ((size_t(bits) * count + 31) / 32 + 4) * 4;
  std::vector<uint32_t> unpacked(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t v = 0;
    for (uint32_t b = 0; b < bits; ++b) {
      size_t bit = size_t(i) * bits + b;
      v = v << 1 | ((file[16 + bit / 8] >> (7 - bit % 8)) & 1);
    }
    unpacked[i] = uint32_t(v);
  }
  uint64_t total = 0;
  for (uint32_t u : unpacked) total += u;
  std::printf("%s: %zu bytes, %u blocks of %u (entries %u bits), header %zu, unpacked %llu\n", argv[1], size, count,
              block, bits, header, static_cast<unsigned long long>(total));

  uint32_t first = 0, n = count;
  for (int a = 2; a < argc; ++a)
    if (!std::strcmp(argv[a], "--blocks") && a + 2 < argc) {
      first = uint32_t(std::atoi(argv[a + 1]));
      n = uint32_t(std::atoi(argv[a + 2]));
    }
  FILE* out = argc >= 3 && argv[2][0] != '-' ? std::fopen(argv[2], "wb") : nullptr;
  uint64_t offset = 0;
  int failures = 0;
  std::vector<uint8_t> lzx, dest;
  for (uint32_t i = 0; i < count; ++i) {
    if (i < first || i >= first + n) {
      offset += unpacked[i];
      continue;
    }
    size_t at = std::max(size_t(i) * block, header), end = std::min(size_t(i + 1) * block, size);
    struct Frame {
      size_t at, len;
    };
    std::vector<Frame> list;
    while (at + 2 <= end) {
      uint32_t len = uint32_t(file[at]) << 8 | file[at + 1];
      if (len == 0) break;  // padding to the block's end
      if (file[at] == 0xFF) return std::fprintf(stderr, "block %u: 0xFF frame header (not handled)\n", i), 1;
      at += 2;
      if (at + len > end) return std::fprintf(stderr, "block %u: frame runs past the block\n", i), 1;
      list.push_back({at, len});
      at += len;
    }
    int frames = int(list.size());
    // An odd-sized frame that ends where an odd-sized uncompressed LZX block ends needs the pad
    // byte libmspack skips before the next block header; one that ends INSIDE an uncompressed
    // block (it goes on in the next frame) must not get one. Which it is shows only in the
    // bitstream, so the combinations are tried (all padded first) until one decodes.
    std::vector<size_t> odd;
    for (size_t k = 0; k + 1 < list.size(); ++k)
      if (list[k].len & 1) odd.push_back(k);
    int r = -1;
    dest.assign(unpacked[i], 0);
    for (uint32_t mask = (1u << odd.size()) - 1, tries = 0; tries < (1u << odd.size()); ++tries, --mask) {
      lzx.clear();
      for (size_t k = 0; k < list.size(); ++k) {
        lzx.insert(lzx.end(), file.begin() + list[k].at, file.begin() + list[k].at + list[k].len);
        for (size_t o = 0; o < odd.size(); ++o)
          if (odd[o] == k && (mask >> o & 1)) lzx.push_back(0);
      }
      r = lzx_decompress(lzx.data(), lzx.size(), dest.data(), dest.size(), block, nullptr, 0);
      if (r == 0) break;
      // Diagnosis: the first 32 KB frame that does not decode with this padding.
      for (size_t k = 1; k <= list.size(); ++k) {
        size_t want = std::min(k * size_t(0x8000), dest.size());
        if (lzx_decompress(lzx.data(), lzx.size(), dest.data(), want, block, nullptr, 0) != 0) {
          std::printf("  block %u mask %u: frame %zu does not decode\n", i, mask, k - 1);
          break;
        }
      }
    }
    if (r != 0) ++failures;
    if (!out || r != 0)
      std::printf("  block %4u: unpacked offset 0x%09llX size %7u, %2d frames, %6zu bytes LZX%s (code %d)\n", i,
                  static_cast<unsigned long long>(offset), unpacked[i], frames, lzx.size(), r ? "  FAILED" : "", r);
    if (out) std::fwrite(dest.data(), 1, dest.size(), out);
    offset += unpacked[i];
  }
  if (out) std::fclose(out);
  std::printf("%d block(s) failed\n", failures);
  return failures ? 1 : 0;
}
