// Small immediate-mode 2D layer drawn by plume on top of the game picture (WORK_LOG 144): filled
// and rounded rectangles (with vertical gradients), images and Segoe UI text. Used by the
// achievement popup and list (spyro_achievements.cpp). Render thread only: the overlay code builds
// a frame between BeginFrame() and EndFrame(); plume_renderer.cpp uploads the textures and draws it.
//
// Not ImGui: reXglue's own ImGui (in rexruntime.dll) switches the global ImGui context from the
// window thread, so a second user on the render thread would race it.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spyro::ui {

// Colours are 0xAARRGGBB.
constexpr uint32_t Rgba(uint32_t argb, float alpha) {
  uint32_t a = uint32_t(float(argb >> 24) * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha) + 0.5f);
  return (argb & 0x00FFFFFFu) | (a << 24);
}

enum Font { kRegular, kSemibold, kBold, kFontCount };

struct Vertex {
  float x, y, u, v;
  uint32_t rgba;  // R8G8B8A8 (r in the low byte)
};

struct Command {
  uint32_t texture;      // ui texture id (0 = white)
  uint32_t first_index;  // into DrawData::indices
  uint32_t index_count;
  int32_t clip[4];  // x0, y0, x1, y1 in pixels
};

struct DrawData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<Command> commands;
  bool empty() const { return commands.empty(); }
};

// Textures: created / updated here, uploaded by plume at the next frame. Id 0 = 1x1 white.
enum class TextureFormat { kAlpha8, kRgba8 };
struct TextureUpload {
  uint32_t id;
  uint32_t width, height;
  TextureFormat format;
  std::vector<uint8_t> data;  // width * height * (1 or 4) bytes, tightly packed
};
uint32_t CreateTexture(uint32_t width, uint32_t height, TextureFormat format, const uint8_t* data);
void UpdateTexture(uint32_t id, uint32_t width, uint32_t height, TextureFormat format, const uint8_t* data);
std::vector<TextureUpload> TakeTextureUploads();  // plume, once per frame

// Frame building (the overlay code). Coordinates in window pixels.
void BeginFrame(float width, float height);
const DrawData& EndFrame();

void Rect(float x, float y, float w, float h, uint32_t color);
void RectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
void RoundRect(float x, float y, float w, float h, float radius, uint32_t color);
void RoundRectGradient(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom);
// An outline of the rounded rectangle, thickness pixels inside it.
void RoundRectOutline(float x, float y, float w, float h, float radius, float thickness, uint32_t color);
void Image(uint32_t texture, float x, float y, float w, float h, uint32_t color = 0xFFFFFFFFu, float u0 = 0,
           float v0 = 0, float u1 = 1, float v1 = 1);
// An image cut to a rounded rectangle (uv 0..1 maps to the rectangle unless given).
void RoundImage(uint32_t texture, float x, float y, float w, float h, float radius, uint32_t color = 0xFFFFFFFFu,
                float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);

// Text: size = line height in pixels, y = top of the line. Returns the width drawn.
float Text(Font font, float size, float x, float y, std::string_view utf8, uint32_t color);
float TextWidth(Font font, float size, std::string_view utf8);
// Text cut to max_width with "..." at the end when it doesn't fit.
float TextClipped(Font font, float size, float x, float y, float max_width, std::string_view utf8, uint32_t color);
// Word-wrapped text; returns the number of lines drawn (max_lines 0 = no limit).
int TextWrapped(Font font, float size, float x, float y, float max_width, float line_gap, std::string_view utf8,
                uint32_t color, int max_lines = 0);
bool FontsReady();

void PushClip(float x, float y, float w, float h);
void PopClip();

}  // namespace spyro::ui
