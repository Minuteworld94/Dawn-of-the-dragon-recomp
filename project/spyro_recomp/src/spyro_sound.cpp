// The fire whoosh for the achievement popup (spyro_sound.h, WORK_LOG 146).
//
// The game's final audio mix goes to the audio driver in sub_8269E428: it fills a frame of 256
// samples x 6 channels (big-endian float, one channel after the other: front L, R, centre, LFE,
// surround L, R) and hands it to XAudioSubmitRenderDriverFrame. The frame pointer is the local
// at r1+88 of that function's frame (r1 - 112 + 88 of the caller's r1), still there after it
// returns. 48 kHz.
//
// Recording (research dump of the mix, WORK_LOG 146): when the main menu's Game Menu (state 0x06)
// comes in, the music has stopped (the mix is silent, or only the menu's quiet fire ambience at
// ~0.012) and the swoosh's whoosh starts around the menu change - two bursts (fire in, fire out)
// up to ~0.35 level, decaying over ~2 s. It is recorded from 0.6 s before the menu changes until
// 2.8 s after, or until the menu changes again or a button is pressed (that menu sound must not
// get in), then trimmed to the whoosh and faded out at the end. If something loud played just
// before it, or it came out too short, it is dropped and the next Game Menu is tried.
//
// Research: env SPYRO_AUDIODUMP=<file.wav>[,<from s>,<to s>] (default 20..60 s after the first
// frame) writes the mix as a 48 kHz stereo WAV; env SPYRO_WHOOSHDUMP=<file.wav> writes the
// recorded whoosh.
#include "spyro_sound.h"

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "spyro_platform.h"
#include "spyro_settings.h"

extern "C" {
REX_EXTERN(__imp__sub_8269E428);
}

namespace spyro::sound {
namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

float LoadF32(const uint8_t* base, uint32_t a) {
  uint32_t v = Load32(base, a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

constexpr uint32_t kSamples = 256, kRate = 48000;

// A WAV file (16-bit stereo, 48 kHz) around the samples.
std::vector<uint8_t> MakeWav(const std::vector<int16_t>& pcm) {
  std::vector<uint8_t> out(44 + pcm.size() * 2);
  uint8_t* p = out.data();
  auto put = [&](const void* d, size_t n) {
    std::memcpy(p, d, n);
    p += n;
  };
  auto u32 = [&](uint32_t v) { put(&v, 4); };
  auto u16 = [&](uint16_t v) { put(&v, 2); };
  uint32_t bytes = uint32_t(pcm.size() * 2);
  put("RIFF", 4);
  u32(36 + bytes);
  put("WAVEfmt ", 8);
  u32(16);
  u16(1);  // PCM
  u16(2);
  u32(kRate);
  u32(kRate * 4);
  u16(4);
  u16(16);
  put("data", 4);
  u32(bytes);
  put(pcm.data(), bytes);
  return out;
}

int16_t ToPcm(float v) {
  float s = v * 32767.0f;
  return int16_t(s > 32767.0f ? 32767.0f : s < -32768.0f ? -32768.0f : s);
}

// ---- research dump of the mix ----
struct Dump {
  std::string path;
  double from = 20.0, to = 60.0;
  std::vector<int16_t> pcm;  // stereo
  bool on = false, started = false, written = false;
} g_dump;

void DumpFrame(const float* stereo, double t) {
  static bool s_init = false;
  if (!s_init) {
    s_init = true;
    if (const char* v = std::getenv("SPYRO_AUDIODUMP"); v && *v) {
      std::string s = v;
      size_t c1 = s.find(',');
      g_dump.path = s.substr(0, c1);
      if (c1 != std::string::npos) {
        g_dump.from = std::atof(s.c_str() + c1 + 1);
        size_t c2 = s.find(',', c1 + 1);
        if (c2 != std::string::npos) g_dump.to = std::atof(s.c_str() + c2 + 1);
      }
      g_dump.on = true;
    }
  }
  if (!g_dump.on || g_dump.written || t < g_dump.from) return;
  if (!g_dump.started) {
    g_dump.started = true;
    REXLOG_INFO("[spyro/sound] audio dump: recording the mix ({:.0f}..{:.0f} s)", g_dump.from, g_dump.to);
  }
  if (t > g_dump.to) {
    std::vector<uint8_t> wav = MakeWav(g_dump.pcm);
    std::ofstream(g_dump.path, std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), wav.size());
    g_dump.written = true;
    REXLOG_INFO("[spyro/sound] audio dump: {:.1f} s written to {}", g_dump.pcm.size() / 2.0 / kRate, g_dump.path);
    return;
  }
  for (uint32_t i = 0; i < kSamples * 2; ++i) g_dump.pcm.push_back(ToPcm(stereo[i]));
}

// ---- the whoosh ----
// 0.6 s kept from before the Game Menu: pressing A on the main menu starts the whoosh ~0.25 s
// before the menu state changes (via state 0x12 it starts with the change, ~0.7 s after the press).
constexpr uint32_t kPreRoll = kRate * 6 / 10;
constexpr uint32_t kQuietPart = kRate * 15 / 100;     // its first 0.15 s: what played before
constexpr uint32_t kMaxLength = kRate * 28 / 10;     // 2.8 s after it
constexpr uint32_t kMinLength = kRate * 9 / 10;      // shorter (cut by a press): try again
constexpr uint32_t kCutTrim = kRate / 20;            // a cut recording loses its last 50 ms
std::deque<float> g_preroll;  // stereo samples, audio thread
std::vector<float> g_rec;     // stereo samples being recorded
bool g_recording = false;
int g_prev_menu = -1;
uint32_t g_rec_after = 0;  // samples recorded since the menu changed
std::atomic<bool> g_pad_pressed{false};
uint16_t g_prev_buttons = 0;
bool g_prev_stick = false;
std::vector<uint8_t> g_wav;  // the recorded whoosh as a WAV file (set once, then read only)
std::atomic<bool> g_ready{false};

void FinishRecording(const char* why, bool cut) {
  g_recording = false;
  const std::vector<float>& r = g_rec;
  size_t frames = r.size() / 2;
  // Cut short by a press / menu change: its sound may have begun in the last few ms.
  if (cut) frames = frames > kCutTrim ? frames - kCutTrim : 0;
  // Level of what played before the Game Menu (the pre-roll): silence, or the main menu's quiet
  // fire ambience (~0.012) - music or anything loud there means a bad recording.
  auto rms = [&](size_t from, size_t count) {
    double sum = 0;
    for (size_t i = from; i < from + count && i < frames; ++i) sum += r[i * 2] * r[i * 2] + r[i * 2 + 1] * r[i * 2 + 1];
    return count ? float(std::sqrt(sum / (2.0 * count))) : 0.0f;
  };
  float before_rms = rms(0, std::min<size_t>(kQuietPart, frames));
  // The whoosh starts at the first 5 ms with 4x that level (at least 0.04); 10 ms lead-in kept.
  const size_t win = kRate / 200;
  float threshold = std::max(0.04f, before_rms * 4.0f);
  size_t attack = frames;
  for (size_t i = 0; i + win <= frames; i += win)
    if (rms(i, win) > threshold) {
      attack = i;
      break;
    }
  size_t start = attack > kRate / 100 ? attack - kRate / 100 : 0;
  size_t length = attack < frames ? frames - start : 0;
  if (attack >= frames || before_rms > 0.05f || length < kMinLength) {
    REXLOG_INFO("[spyro/sound] whoosh recording dropped ({}): {}, level before {:.3f}, {:.2f} s - next Game Menu",
                why, attack >= frames ? "no sound" : "ok start", before_rms, length / double(kRate));
    return;
  }
  // Never louder than 0.9 (the mix can pass 1.0 - seen 1.05 when the menus were pressed through
  // fast - which would crackle as 16-bit), and a fade-out over the last 30% (a cosine) so a cut
  // never clicks.
  float peak = 0;
  for (size_t i = start * 2; i < (start + length) * 2; ++i) peak = std::max(peak, std::fabs(r[i]));
  float gain = peak > 0.9f ? 0.9f / peak : 1.0f;
  size_t fade = length * 3 / 10;
  std::vector<int16_t> pcm(length * 2);
  for (size_t i = 0; i < length; ++i) {
    float g = gain;
    if (i >= length - fade) g *= 0.5f + 0.5f * std::cos(3.14159265f * float(i - (length - fade)) / float(fade));
    for (int c = 0; c < 2; ++c) pcm[i * 2 + c] = ToPcm(r[(start + i) * 2 + c] * g);
  }
  g_wav = MakeWav(pcm);
  g_ready.store(true, std::memory_order_release);
  REXLOG_INFO("[spyro/sound] whoosh recorded ({}): {:.2f} s, peak {:.2f} (gain {:.2f}), level before {:.3f}", why,
              length / double(kRate), peak, gain, before_rms);
  if (const char* path = std::getenv("SPYRO_WHOOSHDUMP"); path && *path)
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(g_wav.data()), g_wav.size());
}

void WhooshFrame(const float* stereo) {
  if (g_ready.load(std::memory_order_relaxed)) return;
  int menu = spyro::CurrentMenuId();
  bool entered = menu == 0x06 && g_prev_menu != 0x06 && g_prev_menu != -1;
  bool left = g_recording && menu != 0x06;
  g_prev_menu = menu;
  if (g_recording) {
    g_rec.insert(g_rec.end(), stereo, stereo + kSamples * 2);
    g_rec_after += kSamples;
    if (left) return FinishRecording("menu changed", true);
    if (g_pad_pressed.load()) return FinishRecording("button pressed", true);
    if (g_rec_after >= kMaxLength) return FinishRecording("full length", false);
    return;
  }
  if (entered) {
    g_recording = true;
    g_rec.assign(g_preroll.begin(), g_preroll.end());
    g_rec.insert(g_rec.end(), stereo, stereo + kSamples * 2);
    g_rec_after = kSamples;
    g_pad_pressed = false;
    return;
  }
  g_preroll.insert(g_preroll.end(), stereo, stereo + kSamples * 2);
  while (g_preroll.size() > kPreRoll * 2) g_preroll.pop_front();
}

void OnMixFrame(const uint8_t* base, uint32_t frame) {
  if (!frame) return;
  static const auto s_t0 = std::chrono::steady_clock::now();
  double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_t0).count();
  // Stereo: front left / right with the centre and the surrounds mixed in.
  float stereo[kSamples * 2];
  auto ch = [&](uint32_t c, uint32_t i) { return LoadF32(base, frame + (c * kSamples + i) * 4); };
  for (uint32_t i = 0; i < kSamples; ++i) {
    float c = ch(2, i) * 0.707f;
    stereo[i * 2 + 0] = ch(0, i) + c + ch(4, i) * 0.5f;
    stereo[i * 2 + 1] = ch(1, i) + c + ch(5, i) * 0.5f;
  }
  DumpFrame(stereo, t);
  WhooshFrame(stereo);
}

}  // namespace

void PadActivity(uint16_t buttons, int16_t lx, int16_t ly) {
  bool stick = std::abs(int(lx)) > 16000 || std::abs(int(ly)) > 16000;
  if ((buttons & ~g_prev_buttons) || (stick && !g_prev_stick)) g_pad_pressed = true;
  g_prev_buttons = buttons;
  g_prev_stick = stick;
}

void KeyPressed() { g_pad_pressed = true; }

bool WhooshReady() { return g_ready.load(std::memory_order_acquire); }

void PlayWhoosh() {
  if (!WhooshReady()) return;
  // Asynchronous, from memory (g_wav never changes once ready); a new call cuts the last one.
  bool ok = platform::PlayWav(g_wav);
  REXLOG_INFO("[spyro/sound] whoosh played{}", ok ? "" : " - FAILED");
}

}  // namespace spyro::sound

// The game's audio frame submit (see the top of the file). extern "C": the override must replace
// the generated weak symbol of the same name.
extern "C" REX_FUNC(sub_8269E428) {
  uint32_t sp = ctx.r1.u32;
  __imp__sub_8269E428(ctx, base);
  spyro::sound::OnMixFrame(base, spyro::sound::Load32(base, sp - 112 + 88));
}
