// Xbox 360 (Xenos) texture decoding for the plume renderer (WORK_LOG 104): reads a texture
// fetch constant (the 6 dwords the GPU uses to describe a texture) and turns the guest texture
// data - tiled, big-endian - into linear little-endian rows plume can upload.
//
// Fetch constant layout (dwords as 32-bit values, bit 0 = least significant):
//   dword0: type 0-1 (2 = texture), clamp x/y/z 10-12 / 13-15 / 16-18, pitch 22-30 (in 32-texel
//           units), tiled 31
//   dword1: format 0-5, endianness 6-7, base address 12-31 (4 KB pages, physical)
//   dword2: 2D size: width-1 0-12, height-1 13-25
//   dword3: swizzle 1-12 (3 bits per output component: 0-3 = x/y/z/w, 4 = 0, 5 = 1),
//           mag filter 19-20, min filter 21-22, mip filter 23-24 (0 point, 1 linear, 2 base
//           level only), aniso filter 25-27 (0 off, 1-5 = 1x..16x)
//   dword4: mip min level 2-5, mip max level 6-9, LOD bias 12-21 (signed, 1/32 steps)
//   dword5: dimension 9-10 (0 1D, 1 2D, 2 3D, 3 cube), packed mips 11, mip address 12-31
//
// Mip levels (WORK_LOG 110; layout from Xenia's texture_util GetGuestTextureLayout /
// GetPackedMipOffset): levels 1+ live at the mip address, each padded to power-of-two size,
// 32x32-block tiles and 4 KB; once a level's shorter side is 16 texels or less, it and all the
// smaller ones share one tile (the packed mip tail) at fixed block offsets.

#pragma once

#include <cstdint>
#include <vector>

#include "plume_render_interface_types.h"

namespace spyro::xenos {

struct TextureInfo {
  uint32_t width = 0, height = 0;
  uint32_t pitch = 0;  // row length in texels (tiled / linear layout width)
  bool tiled = false;
  uint32_t format = 0, endian = 0, dimension = 0;
  uint32_t swizzle = 0;  // 12 bits
  uint32_t clamp_x = 0, clamp_y = 0, clamp_z = 0;
  uint32_t mag_filter = 0, min_filter = 0, mip_filter = 0;
  uint32_t aniso_filter = 0;               // 0 off, 1 1x, 2 2x, 3 4x, 4 8x, 5 16x
  uint32_t mip_min_level = 0, mip_max_level = 0;
  float lod_bias = 0.0f;
  bool packed_mips = false;
  uint32_t mip_address = 0;  // dword5 bits 12-31 as stored (0 = no mip levels)
};

TextureInfo DecodeFetchConstant(const uint32_t words[6]);

struct FormatInfo {
  plume::RenderFormat format = plume::RenderFormat::UNKNOWN;
  uint32_t block = 1;        // block width/height in texels (4 for DXT)
  uint32_t block_bytes = 0;  // bytes per block (or per texel)
  bool supported = false;
};

FormatInfo GetFormat(uint32_t xenos_format);

// Number of mip levels the guest stores (1 = base only).
uint32_t LevelCount(const TextureInfo& info);

// Where level `level` is stored: in the base data or the mip data, at `offset` bytes, in a
// surface `pitch_blocks` blocks wide, starting at block (x_blocks, y_blocks); `extent` = bytes
// from the start of that data that must be readable.
struct LevelLocation {
  bool in_mips = false;
  uint32_t offset = 0, pitch_blocks = 0, x_blocks = 0, y_blocks = 0, extent = 0;
};
bool LocateLevel(const TextureInfo& info, const FormatInfo& fmt, uint32_t level, LevelLocation& loc);

// One level -> linear rows (each `row_pitch` bytes, a multiple of 256 as D3D12 wants), byte
// order fixed for the host: `width_blocks` x `height_blocks` blocks of the host mip, read from
// `data` (the base or mip data LocateLevel named). Returns false for unsupported layouts.
bool DecodeLevel(const uint8_t* data, const TextureInfo& info, const FormatInfo& fmt, const LevelLocation& loc,
                 uint32_t width_blocks, uint32_t height_blocks, std::vector<uint8_t>& out, uint32_t& row_pitch);

// Shader-visible component mapping from the fetch constant swizzle.
plume::RenderComponentMapping ComponentMapping(uint32_t swizzle);

plume::RenderTextureAddressMode AddressMode(uint32_t clamp);

}  // namespace spyro::xenos
