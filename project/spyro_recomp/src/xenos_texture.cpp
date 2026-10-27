#include "xenos_texture.h"

#include <cstring>

namespace spyro::xenos {

TextureInfo DecodeFetchConstant(const uint32_t w[6]) {
  TextureInfo t;
  t.clamp_x = (w[0] >> 10) & 7;
  t.clamp_y = (w[0] >> 13) & 7;
  t.clamp_z = (w[0] >> 16) & 7;
  t.pitch = ((w[0] >> 22) & 0x1FF) * 32;
  t.tiled = (w[0] >> 31) & 1;
  t.format = w[1] & 0x3F;
  t.endian = (w[1] >> 6) & 3;
  t.width = (w[2] & 0x1FFF) + 1;
  t.height = ((w[2] >> 13) & 0x1FFF) + 1;
  t.swizzle = (w[3] >> 1) & 0xFFF;
  t.mag_filter = (w[3] >> 19) & 3;
  t.min_filter = (w[3] >> 21) & 3;
  t.mip_filter = (w[3] >> 23) & 3;
  t.aniso_filter = (w[3] >> 25) & 7;
  t.mip_min_level = (w[4] >> 2) & 0xF;
  t.mip_max_level = (w[4] >> 6) & 0xF;
  t.lod_bias = float(int32_t(w[4] << 10) >> 22) / 32.0f;
  t.dimension = (w[5] >> 9) & 3;
  t.packed_mips = (w[5] >> 11) & 1;
  t.mip_address = w[5] & 0xFFFFF000u;
  return t;
}

FormatInfo GetFormat(uint32_t f) {
  using F = plume::RenderFormat;
  switch (f) {
    case 2:  // k_8
      return {F::R8_UNORM, 1, 1, true};
    case 6:  // k_8_8_8_8 (component order fixed by the fetch swizzle)
      return {F::R8G8B8A8_UNORM, 1, 4, true};
    case 10:  // k_8_8
      return {F::R8G8_UNORM, 1, 2, true};
    case 18:  // k_DXT1
      return {F::BC1_UNORM, 4, 8, true};
    case 19:  // k_DXT2_3
      return {F::BC2_UNORM, 4, 16, true};
    case 20:  // k_DXT4_5
      return {F::BC3_UNORM, 4, 16, true};
    case 49:  // k_DXN
      return {F::BC5_UNORM, 4, 16, true};
    default:
      return {};
  }
}

namespace {

// Byte offset of block (x, y) in a tiled Xenos surface whose rows are `pitch` blocks long
// (the XGAddress2DTiledOffset formula from the Xbox 360 SDK documentation).
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t block_bytes) {
  uint32_t log_bpp = block_bytes == 1 ? 0 : block_bytes == 2 ? 1 : block_bytes == 4 ? 2 : block_bytes == 8 ? 3 : 4;
  uint32_t aligned_width = (pitch + 31) & ~31u;
  uint32_t macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7);
  uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  return ((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

void SwapBlock(uint8_t* p, uint32_t bytes, uint32_t endian) {
  switch (endian) {
    case 1:  // 8 in 16
      for (uint32_t i = 0; i + 1 < bytes; i += 2) std::swap(p[i], p[i + 1]);
      break;
    case 2:  // 8 in 32
      for (uint32_t i = 0; i + 3 < bytes; i += 4) {
        std::swap(p[i], p[i + 3]);
        std::swap(p[i + 1], p[i + 2]);
      }
      break;
    case 3:  // 16 in 32
      for (uint32_t i = 0; i + 3 < bytes; i += 4) {
        std::swap(p[i], p[i + 2]);
        std::swap(p[i + 1], p[i + 3]);
      }
      break;
    default:
      break;
  }
}

uint32_t Log2Ceil(uint32_t v) {
  uint32_t n = 0;
  while ((1u << n) < v) ++n;
  return n;
}

uint32_t Log2Floor(uint32_t v) {
  uint32_t n = 0;
  while (v > 1) {
    v >>= 1;
    ++n;
  }
  return n;
}

uint32_t NextPow2(uint32_t v) { return 1u << Log2Ceil(v); }

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

// First level stored in the packed mip tail (Xenia GetPackedMipLevel).
uint32_t PackedLevel(const TextureInfo& info) {
  if (!info.packed_mips) return UINT32_MAX;
  uint32_t log2_size = Log2Ceil(std::min(info.width, info.height));
  return log2_size > 4 ? log2_size - 4 : 0;
}

// Block offset of a level inside the packed mip tail (Xenia GetPackedMipOffset, 2D).
void PackedOffset(const TextureInfo& info, const FormatInfo& fmt, uint32_t level, uint32_t& x, uint32_t& y) {
  uint32_t log2_w = Log2Ceil(info.width), log2_h = Log2Ceil(info.height);
  uint32_t log2_size = std::min(log2_w, log2_h);
  uint32_t base = log2_size > 4 ? log2_size - 4 : 0;
  uint32_t packed = level - base;
  bool wide = log2_w > std::max(log2_h, 1u);
  if (packed < 3) {
    x = wide ? 0 : 16u >> packed;
    y = wide ? 16u >> packed : 0;
  } else {
    uint32_t offset = (1u << ((wide ? log2_w : log2_h) - base)) >> (packed - 2);
    x = wide ? offset : 0;
    y = wide ? 0 : offset;
  }
  uint32_t shift = Log2Floor(fmt.block);
  x >>= shift;
  y >>= shift;
}

// Pitch (blocks) and padded height (block rows) of a mip level >= 1, from the power-of-two size.
void MipLevelSize(const TextureInfo& info, const FormatInfo& fmt, uint32_t level, uint32_t& pitch_blocks,
                  uint32_t& rows) {
  uint32_t w = std::max(NextPow2(info.width) >> level, 1u), h = std::max(NextPow2(info.height) >> level, 1u);
  uint32_t align = info.tiled ? 32 : std::max(256 / fmt.block_bytes, 32u);
  pitch_blocks = AlignUp((w + fmt.block - 1) / fmt.block, align);
  rows = AlignUp((h + fmt.block - 1) / fmt.block, 32);
}

}  // namespace

uint32_t LevelCount(const TextureInfo& info) {
  if (!info.mip_address || info.dimension != 1) return 1;
  uint32_t max_for_size = Log2Floor(std::max(info.width, info.height));
  return 1 + std::min(info.mip_max_level, max_for_size);
}

bool LocateLevel(const TextureInfo& info, const FormatInfo& fmt, uint32_t level, LevelLocation& loc) {
  if (!fmt.supported || info.dimension != 1) return false;
  uint32_t packed_level = PackedLevel(info);
  uint32_t level_w = std::max(info.width >> level, 1u), level_h = std::max(info.height >> level, 1u);
  uint32_t bw = (level_w + fmt.block - 1) / fmt.block, bh = (level_h + fmt.block - 1) / fmt.block;
  loc = LevelLocation{};
  if (level == 0 || packed_level == 0) {
    // The base (or, when the whole texture is packed, every level) is in the base data.
    uint32_t pitch_texels = level == 0 && info.pitch ? info.pitch : NextPow2(info.width);
    loc.pitch_blocks = AlignUp(std::max((pitch_texels + fmt.block - 1) / fmt.block, bw), 32);
    if (!info.tiled && level == 0) loc.pitch_blocks = std::max((pitch_texels + fmt.block - 1) / fmt.block, bw);
    if (packed_level == 0) PackedOffset(info, fmt, level, loc.x_blocks, loc.y_blocks);
  } else {
    if (!info.mip_address) return false;
    // Levels 1 .. packed_level - 1 one after another, then the packed tail.
    uint32_t stored = std::min(level, packed_level);
    for (uint32_t l = 1; l < stored; ++l) {
      uint32_t pitch, rows;
      MipLevelSize(info, fmt, l, pitch, rows);
      loc.offset += AlignUp(pitch * rows * fmt.block_bytes, 4096);
    }
    uint32_t rows;
    MipLevelSize(info, fmt, stored, loc.pitch_blocks, rows);
    if (level >= packed_level) PackedOffset(info, fmt, level, loc.x_blocks, loc.y_blocks);
    loc.in_mips = true;
  }
  uint32_t last_x = loc.x_blocks + bw - 1, last_y = loc.y_blocks + bh - 1;
  uint32_t rows_total = info.tiled ? AlignUp(last_y + 1, 32) : last_y + 1;
  loc.extent = loc.offset + (info.tiled ? loc.pitch_blocks * rows_total * fmt.block_bytes
                                        : (last_y * loc.pitch_blocks + last_x + 1) * fmt.block_bytes);
  return true;
}

bool DecodeLevel(const uint8_t* data, const TextureInfo& info, const FormatInfo& fmt, const LevelLocation& loc,
                 uint32_t width_blocks, uint32_t height_blocks, std::vector<uint8_t>& out, uint32_t& row_pitch) {
  if (!fmt.supported || info.dimension != 1) return false;
  const uint8_t* src = data + loc.offset;
  uint32_t row_bytes = width_blocks * fmt.block_bytes;
  row_pitch = (row_bytes + 255) & ~255u;
  out.assign(size_t(row_pitch) * height_blocks, 0);
  // Blocks past what the guest stores for this level (the host mip can be a block bigger when
  // the base size isn't a multiple of 4) stay zero.
  for (uint32_t y = 0; y < height_blocks; ++y) {
    uint8_t* dst_row = out.data() + size_t(y) * row_pitch;
    for (uint32_t x = 0; x < width_blocks; ++x) {
      uint32_t sx = x + loc.x_blocks, sy = y + loc.y_blocks;
      uint32_t off = info.tiled ? TiledOffset(sx, sy, loc.pitch_blocks, fmt.block_bytes)
                                : (sy * loc.pitch_blocks + sx) * fmt.block_bytes;
      if (loc.offset + off + fmt.block_bytes > loc.extent) continue;  // beyond what the guest stores
      uint8_t* d = dst_row + x * fmt.block_bytes;
      std::memcpy(d, src + off, fmt.block_bytes);
      SwapBlock(d, fmt.block_bytes, info.endian);
    }
  }
  return true;
}

plume::RenderComponentMapping ComponentMapping(uint32_t swizzle) {
  using S = plume::RenderSwizzle;
  auto one = [](uint32_t c) {
    switch (c & 7) {
      case 0: return S::R;
      case 1: return S::G;
      case 2: return S::B;
      case 3: return S::A;
      case 4: return S::ZERO;
      default: return S::ONE;
    }
  };
  return plume::RenderComponentMapping(one(swizzle), one(swizzle >> 3), one(swizzle >> 6), one(swizzle >> 9));
}

plume::RenderTextureAddressMode AddressMode(uint32_t clamp) {
  using A = plume::RenderTextureAddressMode;
  switch (clamp) {
    case 0: return A::WRAP;
    case 1: return A::MIRROR;
    case 2: case 4: return A::CLAMP;
    case 3: case 5: return A::MIRROR_ONCE;
    default: return A::BORDER;
  }
}

}  // namespace spyro::xenos
