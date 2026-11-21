// Reading the game's packed data files (.KXE = Xbox 360 "XCTD" compressed title data, WORK_LOG 154).
//
// Layout (from the game's own reader, sub_82414A50 / sub_82416CC8): a 16-byte big-endian header
// (magic 0x0FF512ED, version 1, ?, flags: bits 4-5 block size 0x8000 << n, bits 6-21 block count,
// bits 22-23 non-zero = 32-bit table entries else 20-bit), then a bit-packed table of each block's
// unpacked size. Block i fills file bytes [i * block, (i + 1) * block) (block 0 after the header):
// LZX frames, each a u16 big-endian size + data (32 KB unpacked, the block's last one shorter),
// then zero padding. Each block is its own LZX stream - decoded with the reXglue SDK's
// lzx_decompress (libmspack). TOOLS\kxe\kxe_unpack is the same reader as a research tool.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace spyro::kxe {

// `size` bytes at `offset` in the file's UNPACKED data (only the blocks holding them are read and
// unpacked). false = not a KXE file, out of range, or a block did not unpack.
bool Read(const std::filesystem::path& file, uint64_t offset, size_t size, std::vector<uint8_t>& out);

}  // namespace spyro::kxe
