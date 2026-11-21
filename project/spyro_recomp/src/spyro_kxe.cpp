// Reading the game's packed data files (spyro_kxe.h, WORK_LOG 154).
#include "spyro_kxe.h"

#include <rex/logging.h>
#include <rex/system/lzx.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace spyro::kxe {
namespace {

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// One block (`data` = its bytes in the file): LZX frames -> `out` (unpacked size).
bool UnpackBlock(const std::vector<uint8_t>& data, size_t start, uint32_t block, std::vector<uint8_t>& out) {
  struct Frame {
    size_t at, len;
  };
  std::vector<Frame> frames;
  size_t at = start;
  while (at + 2 <= data.size()) {
    uint32_t len = uint32_t(data[at]) << 8 | data[at + 1];
    if (len == 0) break;            // padding to the block's end
    if (data[at] == 0xFF) return false;  // "own unpacked size" frame header: not used by this game
    at += 2;
    if (at + len > data.size()) return false;
    frames.push_back({at, len});
    at += len;
  }
  // An odd-sized frame that ends where an odd-sized uncompressed LZX block ends needs the pad byte
  // libmspack skips before the next block header; one ending INSIDE an uncompressed block must not
  // get one. That shows only in the bitstream, so the combinations are tried (all padded first).
  std::vector<size_t> odd;
  for (size_t k = 0; k + 1 < frames.size(); ++k)
    if (frames[k].len & 1) odd.push_back(k);
  if (odd.size() > 8) return false;
  std::vector<uint8_t> lzx;
  for (uint32_t tries = 0, mask = (1u << odd.size()) - 1; tries < (1u << odd.size()); ++tries, --mask) {
    lzx.clear();
    for (size_t k = 0, o = 0; k < frames.size(); ++k) {
      lzx.insert(lzx.end(), data.begin() + frames[k].at, data.begin() + frames[k].at + frames[k].len);
      if (o < odd.size() && odd[o] == k) {
        if (mask >> o & 1) lzx.push_back(0);
        ++o;
      }
    }
    if (lzx_decompress(lzx.data(), lzx.size(), out.data(), out.size(), block, nullptr, 0) == 0) return true;
  }
  return false;
}

}  // namespace

bool Read(const std::filesystem::path& file, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
  std::ifstream f(file, std::ios::binary);
  uint8_t head[16];
  if (!f.read(reinterpret_cast<char*>(head), sizeof(head)) || Be32(head) != 0x0FF512EDu) return false;
  uint32_t flags = Be32(head + 12);
  uint32_t block = 0x8000u << ((flags >> 4) & 3);
  uint32_t count = (flags >> 6) & 0xFFFF;
  uint32_t bits = (flags & 0xC00000) ? 32 : 20;
  size_t header = ((size_t(bits) * count + 31) / 32 + 4) * 4;
  std::vector<uint8_t> table(header - 16);
  if (!f.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()))) return false;
  std::vector<uint32_t> unpacked(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t v = 0;
    for (uint32_t b = 0; b < bits; ++b) {
      size_t bit = size_t(i) * bits + b;
      v = v << 1 | ((table[bit / 8] >> (7 - bit % 8)) & 1);
    }
    unpacked[i] = uint32_t(v);
  }
  out.assign(size, 0);
  size_t done = 0;
  uint64_t block_start = 0;  // unpacked offset of block i
  std::vector<uint8_t> data, plain;
  for (uint32_t i = 0; i < count && done < size; block_start += unpacked[i], ++i) {
    uint64_t block_end = block_start + unpacked[i];
    if (block_end <= offset + done) continue;
    // Block i's bytes in the file.
    size_t from = size_t(i) * block, skip = i == 0 ? header : 0;
    data.assign(block, 0);
    f.clear();
    f.seekg(std::streamoff(from));
    f.read(reinterpret_cast<char*>(data.data()), block);
    data.resize(size_t(f.gcount()));
    if (data.size() <= skip) return false;
    plain.assign(unpacked[i], 0);
    if (!UnpackBlock(data, skip, block, plain)) {
      REXLOG_WARN("[spyro/kxe] {}: block {} did not unpack", file.filename().string(), i);
      return false;
    }
    size_t in_block = size_t(offset + done - block_start);
    size_t take = std::min(size - done, plain.size() - in_block);
    std::memcpy(out.data() + done, plain.data() + in_block, take);
    done += take;
  }
  return done == size;
}

}  // namespace spyro::kxe
