// Small 2D layer for plume overlays (spyro_ui.h, WORK_LOG 144).
#include "spyro_ui.h"

#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

#include "spyro_platform.h"

// A private copy of stb_truetype (ImGui's bundled one), internal to this file.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace spyro::ui {
namespace {

// ---- textures ----------------------------------------------------------------------------------
std::mutex g_tex_mutex;
uint32_t g_next_texture = 1;  // 0 = white (plume makes it)
std::vector<TextureUpload> g_uploads;

// ---- fonts -------------------------------------------------------------------------------------
// Segoe UI (the Xbox 360 dashboard's font family), read from the Windows font folder - nothing
// ships with the Windows release. Linux: Segoe UI or Selawik in fonts\ next to the game, else a
// system font (spyro_platform.cpp FontFiles). Baked once at kBasePx into one 2048x2048 alpha
// atlas, 2x2 oversampled, characters 32..255 (ASCII + Latin-1); drawn scaled.
constexpr float kBasePx = 40.0f;
constexpr int kAtlas = 2048;
constexpr int kFirstChar = 32, kCharCount = 224;
struct FontData {
  bool ok = false;
  stbtt_packedchar chars[kCharCount];
  float ascent = 0;  // pixels at kBasePx
};
FontData g_fonts[kFontCount];
uint32_t g_font_texture = 0;
bool g_fonts_tried = false;

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void LoadFonts() {
  g_fonts_tried = true;
  std::vector<uint8_t> ttf[kFontCount];
  std::string used;
  for (int i = 0; i < kFontCount; ++i) {
    for (const std::filesystem::path& p : platform::FontFiles(i)) {
      ttf[i] = ReadFile(p);
      if (!ttf[i].empty()) {
        if (i == 0) used = p.string();
        break;
      }
    }
    if (ttf[i].empty() && i > 0) ttf[i] = ttf[0];  // no semibold / bold: regular instead
  }
  if (ttf[0].empty()) {
    REXLOG_WARN("[spyro/ui] no overlay font found (Segoe UI / fonts folder / system fonts) - overlay text disabled");
    return;
  }
  std::vector<uint8_t> atlas(size_t(kAtlas) * kAtlas, 0);
  stbtt_pack_context pack;
  if (!stbtt_PackBegin(&pack, atlas.data(), kAtlas, kAtlas, 0, 1, nullptr)) return;
  stbtt_PackSetOversampling(&pack, 2, 2);
  for (int i = 0; i < kFontCount; ++i) {
    FontData& f = g_fonts[i];
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf[i].data(), stbtt_GetFontOffsetForIndex(ttf[i].data(), 0))) continue;
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    f.ascent = float(ascent) * stbtt_ScaleForPixelHeight(&info, kBasePx);
    f.ok = stbtt_PackFontRange(&pack, ttf[i].data(), 0, kBasePx, kFirstChar, kCharCount, f.chars) != 0;
  }
  stbtt_PackEnd(&pack);
  if (!g_fonts[0].ok) {
    REXLOG_WARN("[spyro/ui] font baking failed - overlay text disabled");
    return;
  }
  for (int i = 1; i < kFontCount; ++i)
    if (!g_fonts[i].ok) g_fonts[i] = g_fonts[0];
  g_font_texture = CreateTexture(kAtlas, kAtlas, TextureFormat::kAlpha8, atlas.data());
  REXLOG_INFO("[spyro/ui] font baked from {} ({}x{} atlas, texture {})", used, kAtlas, kAtlas, g_font_texture);
}

// ---- frame -------------------------------------------------------------------------------------
DrawData g_draw;
float g_width = 0, g_height = 0;
std::vector<std::array<int32_t, 4>> g_clips;

uint32_t ToVertexColor(uint32_t argb) {
  uint32_t a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
  return r | (g << 8) | (b << 16) | (a << 24);
}

const int32_t* CurrentClip() {
  static int32_t full[4];
  if (!g_clips.empty()) return g_clips.back().data();
  full[0] = 0;
  full[1] = 0;
  full[2] = int32_t(g_width);
  full[3] = int32_t(g_height);
  return full;
}

// Starts (or continues) a command for this texture / clip; returns the first new vertex index.
uint32_t Begin(uint32_t texture) {
  const int32_t* clip = CurrentClip();
  if (g_draw.commands.empty() || g_draw.commands.back().texture != texture ||
      std::memcmp(g_draw.commands.back().clip, clip, sizeof(int32_t) * 4) != 0) {
    Command c{texture, uint32_t(g_draw.indices.size()), 0, {clip[0], clip[1], clip[2], clip[3]}};
    g_draw.commands.push_back(c);
  }
  return uint32_t(g_draw.vertices.size());
}

void AddIndices(std::initializer_list<uint32_t> idx, uint32_t base) {
  for (uint32_t i : idx) g_draw.indices.push_back(base + i);
  g_draw.commands.back().index_count += uint32_t(idx.size());
}

void Quad(uint32_t texture, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
          uint32_t top, uint32_t bottom) {
  uint32_t base = Begin(texture);
  uint32_t t = ToVertexColor(top), b = ToVertexColor(bottom);
  g_draw.vertices.push_back({x0, y0, u0, v0, t});
  g_draw.vertices.push_back({x1, y0, u1, v0, t});
  g_draw.vertices.push_back({x1, y1, u1, v1, b});
  g_draw.vertices.push_back({x0, y1, u0, v1, b});
  AddIndices({0, 1, 2, 0, 2, 3}, base);
}

// Outline points of a rounded rectangle, clockwise from the top-left corner's start.
void RoundPoints(float x, float y, float w, float h, float r, std::vector<std::pair<float, float>>& pts) {
  pts.clear();
  r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
  constexpr int kSeg = 8;
  const float cx[4] = {x + r, x + w - r, x + w - r, x + r};
  const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
  const float start[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
  for (int c = 0; c < 4; ++c)
    for (int s = 0; s <= kSeg; ++s) {
      float a = start[c] + 1.57079633f * float(s) / kSeg;
      pts.emplace_back(cx[c] + std::cos(a) * r, cy[c] + std::sin(a) * r);
    }
}

void RoundFan(uint32_t texture, float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom,
              float u0, float v0, float u1, float v1) {
  static thread_local std::vector<std::pair<float, float>> pts;
  RoundPoints(x, y, w, h, radius, pts);
  uint32_t base = Begin(texture);
  auto vertex = [&](float px, float py) {
    float ty = h > 0 ? (py - y) / h : 0;
    float tx = w > 0 ? (px - x) / w : 0;
    uint8_t mix = uint8_t(std::clamp(ty, 0.0f, 1.0f) * 255.0f);
    auto lerp = [&](int shift) {
      int a = (top >> shift) & 0xFF, b = (bottom >> shift) & 0xFF;
      return uint32_t(a + (b - a) * mix / 255) << shift;
    };
    uint32_t c = lerp(24) | lerp(16) | lerp(8) | lerp(0);
    g_draw.vertices.push_back({px, py, u0 + (u1 - u0) * tx, v0 + (v1 - v0) * ty, ToVertexColor(c)});
  };
  vertex(x + w * 0.5f, y + h * 0.5f);
  for (auto& p : pts) vertex(p.first, p.second);
  uint32_t n = uint32_t(pts.size());
  for (uint32_t i = 0; i < n; ++i) {
    g_draw.indices.push_back(base);
    g_draw.indices.push_back(base + 1 + i);
    g_draw.indices.push_back(base + 1 + (i + 1) % n);
  }
  g_draw.commands.back().index_count += n * 3;
}

// UTF-8 -> code points 32..255 (others as '?'); CR / LF / tabs skipped.
template <typename F>
void ForEachChar(std::string_view s, F&& f) {
  for (size_t i = 0; i < s.size();) {
    uint8_t c = uint8_t(s[i]);
    uint32_t cp = c;
    int len = 1;
    if (c >= 0xF0) cp = c & 0x07, len = 4;
    else if (c >= 0xE0) cp = c & 0x0F, len = 3;
    else if (c >= 0xC0) cp = c & 0x1F, len = 2;
    for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (uint8_t(s[i + k]) & 0x3F);
    i += len;
    if (cp == '\r' || cp == '\n' || cp == '\t') continue;
    if (cp == 0x2019 || cp == 0x2018) cp = '\'';
    if (cp < uint32_t(kFirstChar) || cp >= uint32_t(kFirstChar + kCharCount)) cp = '?';
    f(cp);
  }
}

}  // namespace

// ---- textures ----------------------------------------------------------------------------------
uint32_t CreateTexture(uint32_t width, uint32_t height, TextureFormat format, const uint8_t* data) {
  std::lock_guard<std::mutex> lock(g_tex_mutex);
  uint32_t id = g_next_texture++;
  size_t bytes = size_t(width) * height * (format == TextureFormat::kRgba8 ? 4 : 1);
  g_uploads.push_back({id, width, height, format, std::vector<uint8_t>(data, data + bytes)});
  return id;
}

void UpdateTexture(uint32_t id, uint32_t width, uint32_t height, TextureFormat format, const uint8_t* data) {
  std::lock_guard<std::mutex> lock(g_tex_mutex);
  size_t bytes = size_t(width) * height * (format == TextureFormat::kRgba8 ? 4 : 1);
  g_uploads.push_back({id, width, height, format, std::vector<uint8_t>(data, data + bytes)});
}

std::vector<TextureUpload> TakeTextureUploads() {
  std::lock_guard<std::mutex> lock(g_tex_mutex);
  std::vector<TextureUpload> out;
  out.swap(g_uploads);
  return out;
}

// ---- frame -------------------------------------------------------------------------------------
void BeginFrame(float width, float height) {
  if (!g_fonts_tried) LoadFonts();
  g_width = width;
  g_height = height;
  g_draw.vertices.clear();
  g_draw.indices.clear();
  g_draw.commands.clear();
  g_clips.clear();
}

const DrawData& EndFrame() { return g_draw; }

bool FontsReady() { return g_font_texture != 0; }

void Rect(float x, float y, float w, float h, uint32_t color) { Quad(0, x, y, x + w, y + h, 0, 0, 1, 1, color, color); }

void RectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
  Quad(0, x, y, x + w, y + h, 0, 0, 1, 1, top, bottom);
}

void RoundRect(float x, float y, float w, float h, float radius, uint32_t color) {
  RoundFan(0, x, y, w, h, radius, color, color, 0, 0, 1, 1);
}

void RoundRectGradient(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom) {
  RoundFan(0, x, y, w, h, radius, top, bottom, 0, 0, 1, 1);
}

void RoundRectOutline(float x, float y, float w, float h, float radius, float thickness, uint32_t color) {
  static thread_local std::vector<std::pair<float, float>> outer, inner;
  RoundPoints(x, y, w, h, radius, outer);
  RoundPoints(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, std::max(0.0f, radius - thickness),
              inner);
  uint32_t base = Begin(0), c = ToVertexColor(color), n = uint32_t(outer.size());
  for (auto& p : outer) g_draw.vertices.push_back({p.first, p.second, 0, 0, c});
  for (auto& p : inner) g_draw.vertices.push_back({p.first, p.second, 0, 0, c});
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t j = (i + 1) % n;
    for (uint32_t k : {i, j, n + j, i, n + j, n + i}) g_draw.indices.push_back(base + k);
  }
  g_draw.commands.back().index_count += n * 6;
}

void Image(uint32_t texture, float x, float y, float w, float h, uint32_t color, float u0, float v0, float u1, float v1) {
  Quad(texture, x, y, x + w, y + h, u0, v0, u1, v1, color, color);
}

void RoundImage(uint32_t texture, float x, float y, float w, float h, float radius, uint32_t color, float u0, float v0,
                float u1, float v1) {
  RoundFan(texture, x, y, w, h, radius, color, color, u0, v0, u1, v1);
}

float TextWidth(Font font, float size, std::string_view utf8) {
  const FontData& f = g_fonts[font];
  if (!f.ok) return 0;
  float s = size / kBasePx, w = 0;
  ForEachChar(utf8, [&](uint32_t cp) { w += f.chars[cp - kFirstChar].xadvance * s; });
  return w;
}

float Text(Font font, float size, float x, float y, std::string_view utf8, uint32_t color) {
  const FontData& f = g_fonts[font];
  if (!f.ok || !g_font_texture) return 0;
  float s = size / kBasePx, pen = x, baseline = y + f.ascent * s;
  constexpr float kInv = 1.0f / kAtlas;
  ForEachChar(utf8, [&](uint32_t cp) {
    const stbtt_packedchar& c = f.chars[cp - kFirstChar];
    if (c.x1 > c.x0)
      Quad(g_font_texture, pen + c.xoff * s, baseline + c.yoff * s, pen + c.xoff2 * s, baseline + c.yoff2 * s,
           c.x0 * kInv, c.y0 * kInv, c.x1 * kInv, c.y1 * kInv, color, color);
    pen += c.xadvance * s;
  });
  return pen - x;
}

float TextClipped(Font font, float size, float x, float y, float max_width, std::string_view utf8, uint32_t color) {
  if (TextWidth(font, size, utf8) <= max_width) return Text(font, size, x, y, utf8, color);
  float dots = TextWidth(font, size, "...");
  std::string cut;
  float w = 0;
  const FontData& f = g_fonts[font];
  float s = size / kBasePx;
  // Whole UTF-8 sequences only.
  for (size_t i = 0; i < utf8.size();) {
    size_t len = 1;
    uint8_t c = uint8_t(utf8[i]);
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    std::string_view piece = utf8.substr(i, len);
    float pw = 0;
    ForEachChar(piece, [&](uint32_t cp) { pw += f.chars[cp - kFirstChar].xadvance * s; });
    if (w + pw + dots > max_width) break;
    cut.append(piece);
    w += pw;
    i += len;
  }
  cut += "...";
  return Text(font, size, x, y, cut, color);
}

int TextWrapped(Font font, float size, float x, float y, float max_width, float line_gap, std::string_view utf8,
                uint32_t color, int max_lines) {
  std::string line, word;
  int lines = 0;
  float yy = y;
  auto flush = [&](bool last) {
    if (max_lines && lines == max_lines - 1 && !last) {
      TextClipped(font, size, x, yy, max_width, line + " " + word + "...", color);
      return false;
    }
    Text(font, size, x, yy, line, color);
    ++lines;
    yy += size + line_gap;
    return true;
  };
  size_t i = 0;
  while (i <= utf8.size()) {
    size_t j = utf8.find_first_of(" \r\n", i);
    if (j == std::string_view::npos) j = utf8.size();
    word = std::string(utf8.substr(i, j - i));
    i = j + 1;
    if (word.empty()) {
      if (j >= utf8.size()) break;
      continue;
    }
    std::string trial = line.empty() ? word : line + " " + word;
    if (!line.empty() && TextWidth(font, size, trial) > max_width) {
      if (!flush(false)) return lines + 1;
      line = word;
    } else {
      line = trial;
    }
    if (j >= utf8.size()) break;
  }
  if (!line.empty()) flush(true);
  return lines;
}

void PushClip(float x, float y, float w, float h) {
  const int32_t* cur = CurrentClip();
  std::array<int32_t, 4> c{std::max(cur[0], int32_t(x)), std::max(cur[1], int32_t(y)),
                           std::min(cur[2], int32_t(x + w)), std::min(cur[3], int32_t(y + h))};
  if (c[2] < c[0]) c[2] = c[0];
  if (c[3] < c[1]) c[3] = c[1];
  g_clips.push_back(c);
}

void PopClip() {
  if (!g_clips.empty()) g_clips.pop_back();
}

}  // namespace spyro::ui
