// Achievements on screen (spyro_achievements.h, WORK_LOG 144).
#include "spyro_achievements.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "spyro_controls.h"
#include "spyro_sound.h"
#include "spyro_ui.h"

// A private copy of stb_image (PNG only) for the achievement icons in the game's XDBF data.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace spyro::achievements {
namespace {

using Clock = std::chrono::steady_clock;
double Now() {
  static const Clock::time_point s_t0 = Clock::now();
  return std::chrono::duration<double>(Clock::now() - s_t0).count();
}

// ---- data --------------------------------------------------------------------------------------
struct Entry {
  uint32_t id = 0, gamerscore = 0, image_id = 0;
  uint32_t texture = 0;  // spyro_ui texture of the icon (0 = not loaded / none)
  bool icon_tried = false;
  std::string name, description, locked_description;
  bool unlocked = false;
  uint64_t unlock_time = 0;  // FILETIME
};
std::vector<Entry> g_entries;  // render thread
uint32_t g_total_gamerscore = 0;
bool g_loaded = false;
std::atomic<bool> g_off{false};  // achievements turned off with Delete (WORK_LOG 152)

std::string Clean(std::string s) {  // the game's strings carry stray CR / LF
  for (char& c : s)
    if (c == '\r' || c == '\n') c = ' ';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

void LoadCatalog() {
  rex::system::KernelState* ks = rex::system::kernel_state();
  if (!ks) return;
  auto list = ks->achievements().ListAchievements();
  if (list.empty()) return;
  std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  g_entries.clear();
  g_total_gamerscore = 0;
  for (const auto& a : list) {
    Entry e;
    e.id = a.id;
    e.gamerscore = a.gamerscore;
    e.image_id = a.image_id;
    e.name = Clean(a.label);
    e.description = Clean(a.description);
    e.locked_description = Clean(a.unachieved_description);
    g_total_gamerscore += a.gamerscore;
    g_entries.push_back(std::move(e));
  }
  g_loaded = true;
  REXLOG_INFO("[spyro/achievements] {} achievements, {} G in total", g_entries.size(), g_total_gamerscore);
}

void RefreshUnlocks() {
  rex::system::KernelState* ks = rex::system::kernel_state();
  if (!ks) return;
  for (Entry& e : g_entries) {
    e.unlocked = ks->achievements().IsUnlocked(e.id);
    e.unlock_time = e.unlocked ? ks->achievements().GetUnlockTime(e.id) : 0;
  }
}

Entry* Find(uint32_t id) {
  for (Entry& e : g_entries)
    if (e.id == id) return &e;
  return nullptr;
}

// The icon: a PNG in the game's own XDBF data (read from the player's XEX, never shipped).
uint32_t Icon(Entry& e) {
  if (e.icon_tried) return e.texture;
  e.icon_tried = true;
  rex::system::KernelState* ks = rex::system::kernel_state();
  if (!ks) return 0;
  rex::system::util::XdbfGameData db = ks->title_xdbf();
  if (!db.is_valid()) return 0;
  auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, e.image_id);
  if (!block.buffer || !block.size) return 0;
  int w = 0, h = 0, n = 0;
  uint8_t* rgba = stbi_load_from_memory(block.buffer, int(block.size), &w, &h, &n, 4);
  if (!rgba) return 0;
  e.texture = ui::CreateTexture(uint32_t(w), uint32_t(h), ui::TextureFormat::kRgba8, rgba);
  stbi_image_free(rgba);
  return e.texture;
}

// ---- popup -------------------------------------------------------------------------------------
std::mutex g_queue_mutex;
std::deque<uint32_t> g_queue;  // achievement ids to show, oldest first
uint32_t g_popup_id = 0;
double g_popup_start = 0;
constexpr double kPopupSeconds = 6.0;

// The main menu swoosh's fire (plume, WORK_LOG 146), render thread.
struct Fire {
  std::vector<uint32_t> textures;  // spyro_ui textures, in playing order
  uint32_t settle = 0;             // first steady frame (the ones before are the bright flash)
  float fps = 45.0f;               // the movie's speed
  float aspect = 1.0f;             // width / height of a frame
} g_fire;

// The fire over the bar (t = seconds since the popup began): the swoosh's flash plays once at
// the movie's speed as the badge comes in, then the steady fire swings forward and back in slow
// motion (frames crossfaded), and the visible slice drifts slowly across the captured band.
void DrawFire(float x, float y, float w, float h, float r, float alpha, float t) {
  const uint32_t n = uint32_t(g_fire.textures.size());
  const float settle = float(g_fire.settle), last = float(n - 1);
  // Middle of the steady part: the flash runs on into it at full speed, then the swing takes over
  // there at its fastest (no visible stop).
  const float centre = (settle + last) * 0.5f, amplitude = (last - settle) * 0.5f;
  const float intro = centre / g_fire.fps;
  float p;
  if (t < intro) {
    p = t * g_fire.fps;
  } else {
    // Peak speed 18 frames/s (the movie runs at ~45): a slow, rolling fire.
    float omega = amplitude > 0.5f ? 18.0f / amplitude : 1.0f;
    p = centre + amplitude * std::sin(omega * (t - intro));
  }
  p = std::clamp(p, 0.0f, last);
  uint32_t a = uint32_t(p), b = std::min(a + 1, n - 1);
  float blend = p - float(a);
  // Slice: as wide as the bar's shape allows at 80% of the band's width (the round badge at the
  // start shows a square), drifting with two slow waves.
  float bar_aspect = std::max(w / std::max(h, 1.0f), 0.1f);
  float uw = 0.8f, vh = uw * g_fire.aspect / bar_aspect;
  if (vh > 1.0f) {
    vh = 1.0f;
    uw = bar_aspect / g_fire.aspect;
  }
  float uc = 0.5f + (1.0f - uw) * 0.5f * std::sin(t * 0.55f);
  float vc = 0.5f + (1.0f - vh) * 0.5f * std::sin(t * 0.8f + 1.3f);
  float u0 = uc - uw * 0.5f, v0 = vc - vh * 0.5f;
  ui::RoundImage(g_fire.textures[a], x, y, w, h, r, ui::Rgba(0xFFFFFFFF, alpha), u0, v0, u0 + uw, v0 + vh);
  if (blend > 0.02f && b != a)
    ui::RoundImage(g_fire.textures[b], x, y, w, h, r, ui::Rgba(0xFFFFFFFF, alpha * blend), u0, v0, u0 + uw, v0 + vh);
}

void Enqueue(uint32_t id) {
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  g_queue.push_back(id);
}

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void DrawPopup(float gx, float gy, float gw, float gh) {
  double now = Now();
  if (!g_popup_id || now - g_popup_start > kPopupSeconds) {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_popup_id = 0;
    if (g_queue.empty()) return;
    g_popup_id = g_queue.front();
    g_queue.pop_front();
    g_popup_start = now;
    // Not the 360's chime: the main menu swoosh's fire whoosh (spyro_sound.cpp, WORK_LOG 146).
    spyro::sound::PlayWhoosh();
  }
  Entry* e = Find(g_popup_id);
  if (!e) return;
  float t = float(now - g_popup_start);
  // Xbox 360 style: a round badge rises in, opens into the bar, closes again and fades out.
  float in = Smooth(t / 0.3f), open = Smooth((t - 0.3f) / 0.35f);
  float close = Smooth((t - float(kPopupSeconds) + 0.75f) / 0.35f), out = Smooth((t - float(kPopupSeconds) + 0.4f) / 0.4f);
  float alpha = in * (1.0f - out);
  float width_t = open * (1.0f - close);
  if (alpha <= 0.0f) return;

  float h = gh * 0.105f, full_w = gw * 0.42f;
  float w = h + (full_w - h) * width_t;
  float x = gx + (gw - w) * 0.5f;
  float y = gy + gh - h - gh * 0.07f + (1.0f - in) * gh * 0.04f;
  float r = h * 0.5f;

  // Background: the main menu swoosh's fire - until the Game Menu has been seen, embers.
  if (!g_fire.textures.empty()) {
    DrawFire(x, y, w, h, r, alpha, t);
    // A little darker towards the bottom so the white text reads on the bright fire (the text
    // also has a shadow).
    ui::RoundRectGradient(x, y, w, h, r, ui::Rgba(0xFF000000, alpha * 0.08f), ui::Rgba(0xFF000000, alpha * 0.42f));
  } else {
    ui::RoundRectGradient(x, y, w, h, r, ui::Rgba(0xFF5A1600, alpha), ui::Rgba(0xFF1A0500, alpha));
  }
  ui::RoundRectOutline(x, y, w, h, r, std::max(1.5f, h * 0.035f), ui::Rgba(0xFFFFA43A, alpha));

  // The icon in a round badge at the left end.
  float badge = h * 0.80f, bx = x + (h - badge) * 0.5f, by = y + (h - badge) * 0.5f;
  ui::RoundRect(bx - 2, by - 2, badge + 4, badge + 4, (badge + 4) * 0.5f, ui::Rgba(0xFF1A0A02, alpha));
  if (uint32_t icon = Icon(*e))
    ui::RoundImage(icon, bx, by, badge, badge, badge * 0.5f, ui::Rgba(0xFFFFFFFF, alpha));

  // Text, revealed as the bar opens.
  if (width_t > 0.05f && ui::FontsReady()) {
    float tx = x + h * 1.05f, max_w = x + w - tx - r * 0.6f;
    if (max_w > 4.0f) {
      ui::PushClip(tx, y, max_w, h);
      float s1 = h * 0.25f, s2 = h * 0.34f;
      float so = std::max(1.0f, h * 0.014f);  // drop shadow offset
      uint32_t shadow = ui::Rgba(0xFF1A0600, alpha * 0.75f);
      std::string line = std::to_string(e->gamerscore) + "G - " + e->name;
      float line_w = full_w - (tx - x) - r * 0.6f;
      ui::Text(ui::kSemibold, s1, tx + so, y + h * 0.14f + so, "Achievement unlocked", shadow);
      ui::Text(ui::kSemibold, s1, tx, y + h * 0.14f, "Achievement unlocked", ui::Rgba(0xFFFFE2B8, alpha));
      ui::TextClipped(ui::kBold, s2, tx + so, y + h * 0.45f + so, line_w, line, shadow);
      ui::TextClipped(ui::kBold, s2, tx, y + h * 0.45f, line_w, line, ui::Rgba(0xFFFFFFFF, alpha));
      ui::PopClip();
    }
  }
}

// ---- list --------------------------------------------------------------------------------------
std::atomic<bool> g_open{false};
std::atomic<bool> g_wait_release{false};  // closed, but a button is still held
std::atomic<uint32_t> g_pad_buttons{0};
std::atomic<int32_t> g_pad_ly{0};
int g_selected = 0;
float g_scroll = 0;  // first visible row (smoothed)
uint32_t g_prev_buttons = 0;
int g_prev_dir = 0;
double g_repeat_at = 0;
bool g_prev_key1 = false, g_prev_up = false, g_prev_down = false, g_prev_esc = false;

std::string DateOf(uint64_t filetime) {
  if (!filetime) return "";
  std::time_t t = std::time_t((filetime - 116444736000000000ull) / 10000000ull);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%d %b %Y", &tm);
  return buf;
}

void HandleListInput(bool focused) {
  double now = Now();
  // Controller (while the list is open the game gets a neutral pad - test_input.cpp).
  uint32_t buttons = g_pad_buttons.load();
  int32_t ly = g_pad_ly.load();
  uint32_t pressed = buttons & ~g_prev_buttons;
  g_prev_buttons = buttons;
  int dir = 0;
  if ((buttons & 0x0001) || ly > 16000) dir = -1;  // D-pad up / stick up
  if ((buttons & 0x0002) || ly < -16000) dir = 1;   // D-pad down / stick down
  // Keyboard: arrows, Page Up / Down, Esc.
  bool up = focused && (GetAsyncKeyState(VK_UP) & 0x8000), down = focused && (GetAsyncKeyState(VK_DOWN) & 0x8000);
  bool esc = focused && (GetAsyncKeyState(VK_ESCAPE) & 0x8000);
  if (up) dir = -1;
  if (down) dir = 1;
  if (dir != 0 && (dir != g_prev_dir || now >= g_repeat_at)) {
    g_selected += dir;
    g_repeat_at = now + (dir != g_prev_dir ? 0.40 : 0.075);
  }
  g_prev_dir = dir;
  if (pressed & 0x0100) g_selected -= 5;  // LB: page up
  if (pressed & 0x0200) g_selected += 5;  // RB: page down
  if ((pressed & 0x2000) || (pressed & 0x0020) || (esc && !g_prev_esc)) {  // B / Back / Esc
    g_wait_release = true;
    g_open = false;
  }
  g_prev_esc = esc;
  g_prev_up = up;
  g_prev_down = down;
  g_selected = std::clamp(g_selected, 0, std::max(0, int(g_entries.size()) - 1));
}

void DrawList(float ww, float wh, float gx, float gy, float gw, float gh) {
  ui::Rect(0, 0, ww, wh, 0xC8000000);
  float px = gx + gw * 0.10f, pw = gw * 0.80f, py = gy + gh * 0.06f, ph = gh * 0.88f;
  float pad = gh * 0.03f;
  ui::RoundRectGradient(px, py, pw, ph, gh * 0.025f, 0xF52A1208, 0xF5100604);
  ui::RoundRectOutline(px, py, pw, ph, gh * 0.025f, std::max(1.5f, gh * 0.003f), 0xFFFF9A2E);
  if (!ui::FontsReady()) return;

  // Header: title and progress.
  float title = gh * 0.055f;
  ui::Text(ui::kBold, title, px + pad, py + pad * 0.8f, "Achievements", 0xFFFFFFFF);
  if (g_off) {  // turned off with Delete (WORK_LOG 152)
    float os = gh * 0.03f;
    ui::Text(ui::kSemibold, os, px + pad + ui::TextWidth(ui::kBold, title, "Achievements") + gh * 0.02f,
             py + pad * 0.8f + (title - os) * 0.6f, "(turned off)", 0xFFFF7A5A);
  }
  uint32_t unlocked = 0, score = 0;
  for (const Entry& e : g_entries)
    if (e.unlocked) ++unlocked, score += e.gamerscore;
  std::string progress = std::to_string(unlocked) + " of " + std::to_string(g_entries.size()) + " unlocked     " +
                         std::to_string(score) + " / " + std::to_string(g_total_gamerscore) + " G";
  float ps = gh * 0.03f;
  ui::Text(ui::kSemibold, ps, px + pw - pad - ui::TextWidth(ui::kSemibold, ps, progress), py + pad * 0.8f + (title - ps) * 0.6f,
           progress, 0xFFFFD39A);
  // Progress bar.
  float bar_y = py + pad * 0.8f + title * 1.25f, bar_h = gh * 0.008f;
  ui::RoundRect(px + pad, bar_y, pw - 2 * pad, bar_h, bar_h * 0.5f, 0x40FFFFFF);
  if (!g_entries.empty())
    ui::RoundRect(px + pad, bar_y, (pw - 2 * pad) * float(unlocked) / float(g_entries.size()), bar_h, bar_h * 0.5f,
                  0xFFFF9A2E);

  // Rows.
  float list_y = bar_y + bar_h + pad * 0.7f, footer = gh * 0.05f;
  float list_h = py + ph - footer - list_y, row_h = gh * 0.095f, gap = gh * 0.008f;
  int visible = std::max(1, int(list_h / (row_h + gap)));
  float target = float(std::clamp(g_selected - visible / 2, 0, std::max(0, int(g_entries.size()) - visible)));
  g_scroll += (target - g_scroll) * 0.25f;
  if (std::fabs(target - g_scroll) < 0.01f) g_scroll = target;
  ui::PushClip(px, list_y, pw, list_h);
  for (int i = 0; i < int(g_entries.size()); ++i) {
    float ry = list_y + (float(i) - g_scroll) * (row_h + gap);
    if (ry + row_h < list_y || ry > list_y + list_h) continue;
    Entry& e = g_entries[i];
    float rx = px + pad, rw = pw - 2 * pad;
    bool sel = i == g_selected;
    ui::RoundRect(rx, ry, rw, row_h, row_h * 0.18f, sel ? 0x50FF8A20 : (i & 1) ? 0x22FFFFFF : 0x14FFFFFF);
    if (sel) ui::RoundRectOutline(rx, ry, rw, row_h, row_h * 0.18f, std::max(1.5f, gh * 0.0025f), 0xFFFFA43A);
    float icon = row_h * 0.80f, ix = rx + (row_h - icon) * 0.5f, iy = ry + (row_h - icon) * 0.5f;
    if (uint32_t tex = Icon(e))
      ui::RoundImage(tex, ix, iy, icon, icon, icon * 0.12f, e.unlocked ? 0xFFFFFFFF : 0xFF4A4A4A);
    else
      ui::RoundRect(ix, iy, icon, icon, icon * 0.12f, 0xFF302018);
    float tx = ix + icon + row_h * 0.22f, right_w = gw * 0.13f, text_w = rx + rw - right_w - tx;
    float s_name = row_h * 0.30f, s_desc = row_h * 0.235f;
    ui::TextClipped(ui::kBold, s_name, tx, ry + row_h * 0.12f, text_w, e.name, e.unlocked ? 0xFFFFFFFF : 0xFFB0A8A0);
    const std::string& desc = e.unlocked || e.locked_description.empty() ? e.description : e.locked_description;
    ui::TextClipped(ui::kRegular, s_desc, tx, ry + row_h * 0.52f, text_w, desc, e.unlocked ? 0xFFE8D8C8 : 0xFF8A827A);
    std::string gs = std::to_string(e.gamerscore) + " G";
    float rxr = rx + rw - row_h * 0.25f;
    ui::Text(ui::kBold, s_name, rxr - ui::TextWidth(ui::kBold, s_name, gs), ry + row_h * 0.12f, gs,
             e.unlocked ? 0xFFFFC870 : 0xFF8A827A);
    std::string when = e.unlocked ? DateOf(e.unlock_time) : "Locked";
    ui::Text(ui::kRegular, s_desc, rxr - ui::TextWidth(ui::kRegular, s_desc, when), ry + row_h * 0.52f, when,
             e.unlocked ? 0xFFE8D8C8 : 0xFF6E6862);
  }
  ui::PopClip();

  // Footer.
  float fs = gh * 0.026f;
  std::string hint = std::string("Up / Down: scroll     LB / RB: page     ") +
                     (g_off ? "Delete: turn back on" : "Delete: turn off") + "     Insert: reset all     B or 1: close";
  ui::Text(ui::kRegular, fs, px + (pw - ui::TextWidth(ui::kRegular, fs, hint)) * 0.5f, py + ph - footer + (footer - fs) * 0.35f,
           hint, 0xFFBFA890);
}

// ---- files -------------------------------------------------------------------------------------
// <user>\achievements, as kernel_state.cpp builds it (Runtime::user_data_root()); empty = unknown.
std::filesystem::path AchievementsDir() {
  rex::system::KernelState* ks = rex::system::kernel_state();
  if (!ks || !ks->emulator() || ks->emulator()->user_data_root().empty()) return {};
  return ks->emulator()->user_data_root() / "achievements";
}

std::filesystem::path UnlockFile() {  // the SDK's unlock save, <title id>.toml
  std::filesystem::path dir = AchievementsDir();
  if (dir.empty()) return {};
  char name[32];
  std::snprintf(name, sizeof(name), "%08X.toml", rex::system::kernel_state()->title_id());
  return dir / name;
}

std::filesystem::path OffFile() { return AchievementsDir().empty() ? std::filesystem::path() : AchievementsDir() / "achievements_off.txt"; }

// ---- achievements off (Delete key, WORK_LOG 152) ----------------------------------------------------
// While off, an unlock the game reports is undone at once. The SDK has no switch for it, but its
// unlock callbacks run right after it saved the unlock and before the popup callbacks: ours writes
// the save again without it (same format as AchievementManager::SaveUnlockState) and reads it back
// (SetUnlockSavePath clears the unlocks in memory, LoadUnlockState loads the file). The popup
// callback skips it. Remembered across sessions: <user>\achievements\achievements_off.txt (g_off).
void SetOff(bool off, const char* why) {
  g_off = off;
  std::filesystem::path flag = OffFile();
  std::error_code ec;
  if (!flag.empty()) {
    if (off) {
      std::filesystem::create_directories(flag.parent_path(), ec);
      std::ofstream file(flag, std::ios::binary);
      file << "Achievements are turned off: nothing unlocks while this file is here.\n"
              "Press Delete in the game to turn them back on (or delete this file).\n";
    } else {
      std::filesystem::remove(flag, ec);
    }
  }
  REXLOG_INFO("[spyro/achievements] achievements turned {} ({})", off ? "OFF" : "back on", why);
}

void UndoUnlock(uint32_t id, const std::string& label) {  // game thread (the XGI message)
  rex::system::KernelState* ks = rex::system::kernel_state();
  std::filesystem::path path = UnlockFile();
  if (!ks || path.empty()) {
    REXLOG_WARN("[spyro/achievements] achievements are off, but {} could not be undone (no save path)", id);
    return;
  }
  rex::system::AchievementManager& am = ks->achievements();
  std::string content = "# Achievement unlock state - managed by ReXGlue runtime\n\n";
  for (const auto& a : am.ListAchievements())
    if (a.id != id && am.IsUnlocked(a.id))
      content += "[unlocked." + std::to_string(a.id) + "]\nfiletime = " + std::to_string(am.GetUnlockTime(a.id)) + "\n\n";
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary);
    if (!file) {
      REXLOG_WARN("[spyro/achievements] achievements are off, but {} could not be undone (cannot write)", id);
      return;
    }
    file << content;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::rename(temporary, path, ec);
  am.SetUnlockSavePath(path);  // forgets every unlock in memory
  am.LoadUnlockState();        // and reads the file back (all but this one)
  REXLOG_INFO("[spyro/achievements] achievements are off: {} \"{}\" not unlocked (undone, {})", id, label,
              am.IsUnlocked(id) ? "STILL UNLOCKED" : "ok");
}

// ---- questions and notices (Delete = off, Insert = reset all; WORK_LOG 148, 152) ----------------
// Turning achievements off and resetting them ask first (Yes / No, No picked when it opens).
// Turning them back on needs no question (Delete while off), only a notice.
enum Question { kNoQuestion = 0, kAskReset = 1, kAskOff = 2 };
std::atomic<int> g_question{kNoQuestion};
std::atomic<int32_t> g_pad_lx{0};
bool g_yes = false;  // the picked button (No when it opens)
uint32_t g_q_prev_buttons = 0;
int g_q_prev_dir = 0;
bool g_q_prev_left = false, g_q_prev_right = false, g_q_prev_enter = false, g_q_prev_esc = false;
bool g_prev_delete = false, g_prev_insert = false;
std::atomic<bool> g_reset_request{false};   // test switch SPYRO_ACHRESET (game thread -> render thread)
std::atomic<bool> g_toggle_request{false};  // test switch SPYRO_ACHTOGGLE

// A short notice near the top of the picture that fades out (render thread).
std::string g_notice;
double g_notice_at = -100;
constexpr double kNoticeSeconds = 2.6;

void Notice(const char* text) {
  g_notice = text;
  g_notice_at = Now();
}

bool NoticeActive() { return !g_notice.empty() && Now() - g_notice_at < kNoticeSeconds; }

void OpenQuestion(Question q, const char* why) {
  g_yes = false;
  // Only presses from now on count (a button or key still held does nothing).
  g_q_prev_buttons = 0xFFFF;
  g_q_prev_dir = 0;
  g_q_prev_left = g_q_prev_right = g_q_prev_enter = g_q_prev_esc = true;
  RefreshUnlocks();
  g_question = q;
  REXLOG_INFO("[spyro/achievements] {}: asking ({})", q == kAskReset ? "reset" : "turn off", why);
}

void ResetAll() {
  rex::system::KernelState* ks = rex::system::kernel_state();
  if (!ks) return;
  // The SDK has no reset call: SetUnlockSavePath (same file) forgets every unlock in memory,
  // SaveUnlockState then writes the file with none. A copy of the old file is kept next to it as
  // 565507F9.before_reset.toml (only the latest reset's).
  std::filesystem::path path = UnlockFile();
  std::error_code ec;
  if (!path.empty() && std::filesystem::exists(path, ec)) {
    std::filesystem::path backup = path;
    backup.replace_extension(".before_reset.toml");
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
  }
  ks->achievements().SetUnlockSavePath(path);  // forgets every unlock (the file stays the same)
  ks->achievements().SaveUnlockState();         // and writes it with none
  RefreshUnlocks();
  REXLOG_INFO("[spyro/achievements] reset: all achievements locked again ({})", path.string());
}

void CloseQuestion() {
  g_question = kNoQuestion;
  g_wait_release = true;    // the A / B that closed it must not reach the game
  g_prev_buttons = 0xFFFF;  // nor close the list behind it while still held
  g_prev_esc = true;
}

void HandleQuestionInput(bool focused) {
  uint32_t buttons = g_pad_buttons.load();
  int32_t lx = g_pad_lx.load();
  uint32_t pressed = buttons & ~g_q_prev_buttons;
  g_q_prev_buttons = buttons;
  int dir = 0;
  if ((buttons & 0x0004) || lx < -16000) dir = -1;  // D-pad left / stick left
  if ((buttons & 0x0008) || lx > 16000) dir = 1;    // D-pad right / stick right
  bool left = focused && (GetAsyncKeyState(VK_LEFT) & 0x8000), right = focused && (GetAsyncKeyState(VK_RIGHT) & 0x8000);
  bool enter = focused && ((GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000));
  bool esc = focused && ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) || (GetAsyncKeyState('N') & 0x8000));
  if (dir != 0 && dir != g_q_prev_dir) g_yes = dir < 0;  // Yes is on the left
  g_q_prev_dir = dir;
  if (left && !g_q_prev_left) g_yes = true;
  if (right && !g_q_prev_right) g_yes = false;
  bool confirm = (pressed & 0x1000) || (enter && !g_q_prev_enter);                       // A / Enter
  bool cancel = (pressed & 0x2000) || (pressed & 0x0020) || (esc && !g_q_prev_esc);      // B / Back / Esc
  g_q_prev_left = left;
  g_q_prev_right = right;
  g_q_prev_enter = enter;
  g_q_prev_esc = esc;
  bool reset = g_question == kAskReset;
  if (cancel || (confirm && !g_yes)) {
    REXLOG_INFO("[spyro/achievements] {}: cancelled", reset ? "reset" : "turn off");
    CloseQuestion();
  } else if (confirm) {
    if (reset) {
      ResetAll();
      Notice("All achievements have been reset");
    } else {
      SetOff(true, "Yes picked");
      Notice("Achievements are off - press Delete to turn them back on");
    }
    CloseQuestion();
  }
}

void DrawQuestionButton(float x, float y, float w, float h, const char* label, bool selected, float gh) {
  ui::RoundRect(x, y, w, h, h * 0.3f, selected ? 0xFFFF9A2E : 0x26FFFFFF);
  ui::RoundRectOutline(x, y, w, h, h * 0.3f, std::max(1.5f, gh * 0.0025f), selected ? 0xFFFFD39A : 0x60FFFFFF);
  float s = h * 0.52f;
  ui::Text(ui::kBold, s, x + (w - ui::TextWidth(ui::kBold, s, label)) * 0.5f, y + (h - s) * 0.42f, label,
           selected ? 0xFF2A1208 : 0xFFE8D8C8);
}

void DrawNotice(float gx, float gy, float gw, float gh) {
  if (!ui::FontsReady() || !NoticeActive()) return;
  float t = float(Now() - g_notice_at);
  float a = Smooth(t / 0.15f) * (1.0f - Smooth((t - float(kNoticeSeconds) + 0.6f) / 0.6f));
  float s = gh * 0.034f, w = ui::TextWidth(ui::kSemibold, s, g_notice) + gh * 0.08f, h = gh * 0.075f;
  float x = gx + (gw - w) * 0.5f, y = gy + gh * 0.08f;
  ui::RoundRectGradient(x, y, w, h, h * 0.5f, ui::Rgba(0xF52A1208, a), ui::Rgba(0xF5100604, a));
  ui::RoundRectOutline(x, y, w, h, h * 0.5f, std::max(1.5f, gh * 0.003f), ui::Rgba(0xFFFF9A2E, a));
  ui::Text(ui::kSemibold, s, x + gh * 0.04f, y + (h - s) * 0.45f, g_notice, ui::Rgba(0xFFFFFFFF, a));
}

void DrawQuestion(float ww, float wh, float gx, float gy, float gw, float gh) {
  if (!ui::FontsReady()) return;
  ui::Rect(0, 0, ww, wh, 0xB4000000);
  float pw = gw * 0.50f, ph = gh * 0.40f, px = gx + (gw - pw) * 0.5f, py = gy + (gh - ph) * 0.5f;
  float pad = gh * 0.035f;
  ui::RoundRectGradient(px, py, pw, ph, gh * 0.025f, 0xFA2A1208, 0xFA100604);
  ui::RoundRectOutline(px, py, pw, ph, gh * 0.025f, std::max(1.5f, gh * 0.003f), 0xFFFF9A2E);
  float title = gh * 0.05f;
  bool reset = g_question == kAskReset;
  ui::Text(ui::kBold, title, px + pad, py + pad, reset ? "Reset all achievements?" : "Turn off achievements?",
           0xFFFFFFFF);
  uint32_t unlocked = 0, score = 0;
  for (const Entry& e : g_entries)
    if (e.unlocked) ++unlocked, score += e.gamerscore;
  std::string body;
  if (reset) {
    body = "This will lock all " + std::to_string(g_entries.size()) +
           " achievements again and erase their unlock dates (you have " + std::to_string(unlocked) + " unlocked, " +
           std::to_string(score) + " G). You will have to earn them again by playing. This can't be undone. Are you sure?";
  } else {
    body = "While they are off, nothing you do in the game unlocks an achievement and no popups appear. ";
    if (unlocked == 1)
      body += "The 1 you already have stays unlocked. ";
    else if (unlocked > 1)
      body += "The " + std::to_string(unlocked) + " you already have stay unlocked. ";
    body += "Press Delete again any time to turn them back on.";
  }
  float bs = gh * 0.03f;
  ui::TextWrapped(ui::kRegular, bs, px + pad, py + pad + title * 1.35f, pw - 2 * pad, bs * 0.25f, body, 0xFFE8D8C8);
  // Yes / No.
  float bw = pw * 0.26f, bh = gh * 0.065f, by = py + ph - pad - bh - gh * 0.04f, gap = pw * 0.06f;
  float bx = px + (pw - (2 * bw + gap)) * 0.5f;
  DrawQuestionButton(bx, by, bw, bh, "Yes", g_yes, gh);
  DrawQuestionButton(bx + bw + gap, by, bw, bh, "No", !g_yes, gh);
  float fs = gh * 0.024f;
  std::string hint = "Left / Right: choose      A or Enter: confirm      B or Esc: cancel";
  ui::Text(ui::kRegular, fs, px + (pw - ui::TextWidth(ui::kRegular, fs, hint)) * 0.5f, py + ph - pad * 0.5f - fs, hint,
           0xFFBFA890);
}

std::deque<double> TimesFromEnv(const char* name) {  // "<s>[,<s>...]"
  std::deque<double> times;
  for (const char* p = std::getenv(name); p && *p;) {
    times.push_back(std::atof(p));
    p = std::strchr(p, ',');
    if (p) ++p;
  }
  return times;
}

// Test switches: SPYRO_ACHTEST=<id>[,<seconds>[,<every seconds>]] shows that popup (no unlock),
// default once 30 s after start, or again every so often (TEST-ACHIEVEMENT-POPUP.bat);
// SPYRO_ACHLIST=<seconds> opens the list then.
double g_test_popup_at = -1, g_test_list_at = -1, g_test_popup_every = 0;
uint32_t g_test_popup_id = 0;

}  // namespace

void OnGameFrame() {
  static bool s_registered = false;
  if (!s_registered) {
    rex::system::KernelState* ks = rex::system::kernel_state();
    if (ks) {
      s_registered = true;
      // Achievements off (WORK_LOG 152): remembered from the last session.
      std::error_code ec;
      std::filesystem::path off_file = OffFile();
      if (!off_file.empty() && std::filesystem::exists(off_file, ec)) {
        g_off = true;
        REXLOG_INFO("[spyro/achievements] achievements are OFF ({} - Delete turns them back on)", off_file.string());
      }
      // Unlock callbacks run before the popup callbacks: while off, the unlock is undone here.
      ks->achievements().RegisterUnlockCallback([](const rex::system::AchievementEvent& ev) {
        if (g_off) UndoUnlock(ev.achievement.id, ev.achievement.label);
      });
      ks->achievements().RegisterNotificationCallback([](const rex::system::AchievementEvent& ev) {
        if (g_off) return;  // undone just before (no popup)
        REXLOG_INFO("[spyro/achievements] unlocked {} \"{}\" ({} G)", ev.achievement.id, ev.achievement.label,
                    ev.achievement.gamerscore);
        Enqueue(ev.achievement.id);
      });
      if (const char* v = std::getenv("SPYRO_ACHTEST")) {
        g_test_popup_id = uint32_t(std::atoi(v));
        const char* comma = std::strchr(v, ',');
        g_test_popup_at = comma ? std::atof(comma + 1) : 30.0;
        const char* comma2 = comma ? std::strchr(comma + 1, ',') : nullptr;
        g_test_popup_every = comma2 ? std::atof(comma2 + 1) : 0.0;
      }
      if (const char* v = std::getenv("SPYRO_ACHLIST")) g_test_list_at = std::atof(v);
    }
  }
  double now = Now();
  if (g_test_popup_at >= 0 && now >= g_test_popup_at) {
    g_test_popup_at = g_test_popup_every > 0 ? now + g_test_popup_every : -1;
    REXLOG_INFO("[spyro/achievements] test popup for {}", g_test_popup_id);
    Enqueue(g_test_popup_id);
  }
  if (g_test_list_at >= 0 && now >= g_test_list_at) {
    g_test_list_at = -1;
    g_open = true;
  }
  // SPYRO_ACHRESET=<seconds>[,<seconds>...]: the reset question opens then, as if Insert was pressed;
  // SPYRO_ACHTOGGLE=<seconds>[,...]: as if Delete was pressed (off question / back on).
  static std::deque<double> s_reset_at = TimesFromEnv("SPYRO_ACHRESET");
  static std::deque<double> s_toggle_at = TimesFromEnv("SPYRO_ACHTOGGLE");
  if (!s_reset_at.empty() && now >= s_reset_at.front()) {
    REXLOG_INFO("[spyro/achievements] test switch: reset question at {:.1f} s (asked for {:.0f} s)", now,
                s_reset_at.front());
    s_reset_at.pop_front();
    g_reset_request = true;
  }
  if (!s_toggle_at.empty() && now >= s_toggle_at.front()) {
    REXLOG_INFO("[spyro/achievements] test switch: Delete at {:.1f} s (asked for {:.0f} s)", now, s_toggle_at.front());
    s_toggle_at.pop_front();
    g_toggle_request = true;
  }
  // SPYRO_ACHUNLOCK=<id>[+<id>...],<seconds>[;<id>...,<seconds>...]: REAL unlocks through the SDK at
  // those times - the same call the game's XGIUserWriteAchievements ends in (xgi_app.cpp ->
  // KernelState::UnlockAchievement), so the whole popup path runs. Test runs only: they use the
  // throwaway project\run\testuser.
  struct UnlockGroup {
    double at;
    std::vector<uint32_t> ids;
  };
  static std::deque<UnlockGroup> s_unlocks = [] {
    std::deque<UnlockGroup> groups;
    for (const char* p = std::getenv("SPYRO_ACHUNLOCK"); p && *p;) {
      UnlockGroup g;
      while (*p && *p != ',' && *p != ';') {
        char* end = nullptr;
        uint32_t id = uint32_t(std::strtoul(p, &end, 10));
        if (end == p) break;  // not a number
        g.ids.push_back(id);
        p = end;
        if (*p == '+') ++p;
      }
      g.at = *p == ',' ? std::atof(p + 1) : 30.0;
      groups.push_back(std::move(g));
      p = std::strchr(p, ';');
      if (p) ++p;
    }
    return groups;
  }();
  if (!s_unlocks.empty() && now >= s_unlocks.front().at) {
    UnlockGroup g = std::move(s_unlocks.front());
    s_unlocks.pop_front();
    if (rex::system::KernelState* ks = rex::system::kernel_state())
      for (uint32_t id : g.ids) {
        auto result = ks->achievements().UnlockAchievement(id, rex::system::AchievementNotification::kShow);
        REXLOG_INFO("[spyro/achievements] test unlock {}: {}{}", id,
                    result == rex::system::AchievementUnlockResult::kUnlocked ? "unlocked" : "not unlocked (already / unknown)",
                    ks->achievements().IsUnlocked(id) ? "" : " - not in the save now");
      }
  }
}

void ToggleList() {
  if (g_open) g_wait_release = true;
  g_open = !g_open;
}

void OpenList() {
  if (!g_open) REXLOG_INFO("[spyro/achievements] list opened (Recomp Settings)");
  g_open = true;
}

bool ListOpen() { return g_open; }

bool InputCaptured() { return g_open || g_wait_release || g_question != kNoQuestion; }

void PadInput(uint16_t buttons, int16_t lx, int16_t ly) {
  g_pad_buttons = buttons;
  g_pad_lx = lx;
  g_pad_ly = ly;
  if (!g_open && g_question == kNoQuestion && g_wait_release && buttons == 0 && ly > -8000 && ly < 8000 &&
      lx > -8000 && lx < 8000)
    g_wait_release = false;
}

void SetFireFrames(const std::vector<std::vector<uint8_t>>& frames, uint32_t width, uint32_t height,
                   uint32_t settle, float fps) {
  if (!g_fire.textures.empty() || frames.empty() || !width || !height) return;  // once per session
  for (const std::vector<uint8_t>& f : frames)
    g_fire.textures.push_back(ui::CreateTexture(width, height, ui::TextureFormat::kRgba8, f.data()));
  g_fire.settle = std::min<uint32_t>(settle, uint32_t(frames.size()) - 1);
  g_fire.fps = fps;
  g_fire.aspect = float(width) / float(height);
  REXLOG_INFO("[spyro/achievements] popup fire: {} frames {}x{} ({} bright), {:.0f} fps", frames.size(), width,
              height, g_fire.settle, fps);
}

void BuildOverlay(float window_w, float window_h, float game_x, float game_y, float game_w, float game_h,
                  bool window_focused) {
  if (!g_loaded) LoadCatalog();
  bool asking = g_question != kNoQuestion;
  // The hotkeys give way to the controls screens, and to keys the keyboard layout uses for playing
  // (spyro_controls, WORK_LOG 150).
  bool controls = spyro::controls::MenuOpen();
  // 1 (top row or numpad) toggles the list (not while a question is up).
  bool key1 = window_focused && !controls &&
              (((GetAsyncKeyState('1') & 0x8000) && !spyro::controls::KeyIsBound('1')) ||
               ((GetAsyncKeyState(VK_NUMPAD1) & 0x8000) && !spyro::controls::KeyIsBound(VK_NUMPAD1)));
  if (key1 && !g_prev_key1 && !asking) {
    g_open = !g_open;
    REXLOG_INFO("[spyro/achievements] list {}", g_open ? "opened (1 key)" : "closed (1 key)");
  }
  g_prev_key1 = key1;
  // Delete turns achievements off (asks first) or back on (WORK_LOG 152); Insert asks whether to
  // reset them all (WORK_LOG 148).
  bool del = window_focused && !controls && (GetAsyncKeyState(VK_DELETE) & 0x8000) &&
             !spyro::controls::KeyIsBound(VK_DELETE);
  bool ins = window_focused && !controls && (GetAsyncKeyState(VK_INSERT) & 0x8000) &&
             !spyro::controls::KeyIsBound(VK_INSERT);
  bool toggle_request = g_toggle_request.exchange(false), reset_request = g_reset_request.exchange(false);
  if (!asking && g_loaded) {
    if ((del && !g_prev_delete) || toggle_request) {
      const char* why = toggle_request ? "test switch" : "Delete key";
      if (g_off) {
        SetOff(false, why);
        Notice("cheevos are back!");
      } else {
        OpenQuestion(kAskOff, why);
        asking = true;
      }
    } else if ((ins && !g_prev_insert) || reset_request) {
      OpenQuestion(kAskReset, reset_request ? "test switch" : "Insert key");
      asking = true;
    }
  }
  g_prev_delete = del;
  g_prev_insert = ins;
  bool open = g_open && g_loaded;
  bool popup;
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    popup = g_popup_id || !g_queue.empty();
  }
  ui::BeginFrame(window_w, window_h);
  static bool s_was_open = false;
  if (open && !s_was_open) RefreshUnlocks();  // each time the list opens
  s_was_open = open;
  if (!open && !popup && !asking && !NoticeActive()) return;
  if (open) {
    if (!asking) HandleListInput(window_focused);  // the question takes the input while it is up
    if (g_open) DrawList(window_w, window_h, game_x, game_y, game_w, game_h);
  }
  if (popup) {
    if (!g_loaded) LoadCatalog();
    DrawPopup(game_x, game_y, game_w, game_h);
  }
  if (asking) {
    HandleQuestionInput(window_focused);
    if (g_question != kNoQuestion) DrawQuestion(window_w, window_h, game_x, game_y, game_w, game_h);
  }
  DrawNotice(game_x, game_y, game_w, game_h);
}

}  // namespace spyro::achievements
