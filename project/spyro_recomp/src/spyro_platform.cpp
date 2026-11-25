// What our code needs from the operating system (spyro_platform.h, WORK_LOG 156).
#include "spyro_platform.h"

#include <rex/logging.h>

#include <atomic>
#include <cstdlib>
#include <mutex>

#if defined(_WIN32)
#include <mmsystem.h>  // PlaySoundW (winmm)
#pragma comment(lib, "winmm.lib")
#else
#include <SDL3/SDL.h>
#include <execinfo.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#endif

namespace spyro::platform {
namespace {
std::atomic<void*> g_window{nullptr};
}  // namespace

void SetGameWindow(void* native) { g_window = native; }
void* GameWindow() { return g_window.load(); }

std::filesystem::path FindNoCase(std::filesystem::path root, std::initializer_list<const char*> parts) {
  auto lower = [](std::string s) {
    for (char& c : s) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
  };
  for (const char* part : parts) {
    std::filesystem::path next = root / part;
    std::error_code ec;
    if (!std::filesystem::exists(next, ec)) {
      const std::string want = lower(part);
      for (const auto& e : std::filesystem::directory_iterator(root, ec))
        if (lower(e.path().filename().string()) == want) {
          next = e.path();
          break;
        }
    }
    root = next;
  }
  return root;
}

#if defined(_WIN32)
// ================================================================================================
// Windows

bool GameWindowFocused() {
  HWND h = static_cast<HWND>(g_window.load());
  return h && GetForegroundWindow() == h;
}

bool CloseGameWindow() {
  HWND h = static_cast<HWND>(g_window.load());
  return h && PostMessageW(h, WM_CLOSE, 0, 0);
}

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

uint32_t KeyChar(int vk) { return MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_CHAR) & 0x7FFF; }

bool MousePosition(int& x, int& y) {
  HWND h = static_cast<HWND>(g_window.load());
  POINT p;
  if (!h || !GetCursorPos(&p) || !ScreenToClient(h, &p)) return false;
  x = p.x;
  y = p.y;
  return true;
}

bool PlayWav(const std::vector<uint8_t>& wav) {
  // The buffer must stay alive while it plays (the caller keeps it).
  return PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

std::vector<std::filesystem::path> FontFiles(int weight) {
  std::filesystem::path dir = L"C:\\Windows\\Fonts";
  wchar_t win[MAX_PATH];
  if (UINT n = GetWindowsDirectoryW(win, MAX_PATH); n && n < MAX_PATH) dir = std::filesystem::path(win) / L"Fonts";
  static const wchar_t* kFiles[3] = {L"segoeui.ttf", L"seguisb.ttf", L"segoeuib.ttf"};
  return {dir / kFiles[weight < 0 || weight > 2 ? 0 : weight]};
}

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  return n && n < MAX_PATH ? std::filesystem::path(path).parent_path() : std::filesystem::current_path();
}

int CaptureStack(void** frames, int count) { return RtlCaptureStackBackTrace(2, DWORD(count), frames, nullptr); }
uintptr_t ExeBase() { return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)); }

#else
// ================================================================================================
// Linux (SDL3)

bool GameWindowFocused() {
  SDL_Window* w = static_cast<SDL_Window*>(g_window.load());
  return w && SDL_GetKeyboardFocus() == w;
}

bool CloseGameWindow() {
  SDL_Window* w = static_cast<SDL_Window*>(g_window.load());
  if (!w) return false;
  SDL_Event e{};
  e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  e.window.windowID = SDL_GetWindowID(w);
  return SDL_PushEvent(&e);
}

namespace {
SDL_Scancode Scancode(int vk) {
  if (vk >= 'A' && vk <= 'Z') return SDL_Scancode(SDL_SCANCODE_A + (vk - 'A'));
  if (vk >= '1' && vk <= '9') return SDL_Scancode(SDL_SCANCODE_1 + (vk - '1'));
  if (vk == '0') return SDL_SCANCODE_0;
  if (vk >= VK_F1 && vk <= VK_F1 + 11) return SDL_Scancode(SDL_SCANCODE_F1 + (vk - VK_F1));
  if (vk >= VK_F1 + 12 && vk <= VK_F24) return SDL_Scancode(SDL_SCANCODE_F13 + (vk - VK_F1 - 12));
  if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return SDL_Scancode(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD1));
  switch (vk) {
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_RETURN: return SDL_SCANCODE_RETURN;
    case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_PAUSE: return SDL_SCANCODE_PAUSE;
    case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_END: return SDL_SCANCODE_END;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_SNAPSHOT: return SDL_SCANCODE_PRINTSCREEN;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_LWIN: return SDL_SCANCODE_LGUI;
    case VK_RWIN: return SDL_SCANCODE_RGUI;
    case VK_APPS: return SDL_SCANCODE_APPLICATION;
    case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
    case VK_ADD: return SDL_SCANCODE_KP_PLUS;
    case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
    case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
    case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
    case VK_NUMLOCK: return SDL_SCANCODE_NUMLOCKCLEAR;
    case VK_SCROLL: return SDL_SCANCODE_SCROLLLOCK;
    case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
    case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
    case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
    case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
    case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
    case VK_OEM_2: return SDL_SCANCODE_SLASH;
    case VK_OEM_3: return SDL_SCANCODE_GRAVE;
    case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
    case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
    case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
    case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
    case VK_OEM_102: return SDL_SCANCODE_NONUSBACKSLASH;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}
}  // namespace

bool KeyDown(int vk) {
  // Mouse buttons.
  SDL_MouseButtonFlags mask = 0;
  switch (vk) {
    case VK_LBUTTON: mask = SDL_BUTTON_LMASK; break;
    case VK_RBUTTON: mask = SDL_BUTTON_RMASK; break;
    case VK_MBUTTON: mask = SDL_BUTTON_MMASK; break;
    case VK_XBUTTON1: mask = SDL_BUTTON_X1MASK; break;
    case VK_XBUTTON2: mask = SDL_BUTTON_X2MASK; break;
    default: break;
  }
  if (mask) return (SDL_GetMouseState(nullptr, nullptr) & mask) != 0;
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  if (!keys) return false;
  auto down = [&](SDL_Scancode s) { return s != SDL_SCANCODE_UNKNOWN && int(s) < count && keys[s]; };
  // Either side for the plain modifier codes.
  if (vk == VK_SHIFT) return down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
  if (vk == VK_CONTROL) return down(SDL_SCANCODE_LCTRL) || down(SDL_SCANCODE_RCTRL);
  if (vk == VK_MENU) return down(SDL_SCANCODE_LALT) || down(SDL_SCANCODE_RALT);
  return down(Scancode(vk));
}

uint32_t KeyChar(int vk) {
  SDL_Scancode s = Scancode(vk);
  if (s == SDL_SCANCODE_UNKNOWN) return 0;
  SDL_Keycode k = SDL_GetKeyFromScancode(s, SDL_KMOD_NONE, false);
  return k > 32 && k < 127 ? uint32_t(k) : 0;
}

bool MousePosition(int& x, int& y) {
  SDL_Window* w = static_cast<SDL_Window*>(g_window.load());
  if (!w || SDL_GetMouseFocus() != w) return false;
  float fx = 0, fy = 0;
  SDL_GetMouseState(&fx, &fy);
  float density = SDL_GetWindowPixelDensity(w);
  if (density <= 0) density = 1;
  x = int(fx * density);
  y = int(fy * density);
  return true;
}

bool PlayWav(const std::vector<uint8_t>& wav) {
  static std::mutex s_mutex;
  static SDL_AudioStream* s_stream = nullptr;
  static SDL_AudioSpec s_spec{};
  std::lock_guard<std::mutex> lock(s_mutex);
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXLOG_WARN("[spyro/platform] SDL audio: {}", SDL_GetError());
    return false;
  }
  SDL_AudioSpec spec{};
  Uint8* data = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(wav.data(), wav.size()), true, &spec, &data, &len)) {
    REXLOG_WARN("[spyro/platform] SDL_LoadWAV: {}", SDL_GetError());
    return false;
  }
  if (s_stream && (spec.format != s_spec.format || spec.channels != s_spec.channels || spec.freq != s_spec.freq)) {
    SDL_DestroyAudioStream(s_stream);
    s_stream = nullptr;
  }
  if (!s_stream) {
    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    s_spec = spec;
  }
  bool ok = s_stream != nullptr;
  if (ok) {
    SDL_ClearAudioStream(s_stream);  // one sound at a time, like PlaySound
    ok = SDL_PutAudioStreamData(s_stream, data, int(len)) && SDL_ResumeAudioStreamDevice(s_stream);
  }
  SDL_free(data);
  return ok;
}

std::vector<std::filesystem::path> FontFiles(int weight) {
  if (weight < 0 || weight > 2) weight = 0;
  std::filesystem::path fonts = ExeDir() / "fonts";
  static const char* kOwn[3][2] = {{"segoeui.ttf", "selawk.ttf"}, {"seguisb.ttf", "selawksb.ttf"},
                                   {"segoeuib.ttf", "selawkb.ttf"}};
  static const char* kNoto[3] = {"NotoSans-Regular.ttf", "NotoSans-SemiBold.ttf", "NotoSans-Bold.ttf"};
  static const char* kDejaVu[3] = {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"};
  static const char* kLiberation[3] = {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
                                       "LiberationSans-Bold.ttf"};
  std::vector<std::filesystem::path> out = {fonts / kOwn[weight][0], fonts / kOwn[weight][1]};
  for (const char* dir : {"/usr/share/fonts/noto", "/usr/share/fonts/truetype/noto", "/usr/share/fonts/google-noto",
                          "/usr/share/fonts/noto-fonts", "/usr/share/fonts/TTF"})
    out.push_back(std::filesystem::path(dir) / kNoto[weight]);
  for (const char* dir : {"/usr/share/fonts/TTF", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu"})
    out.push_back(std::filesystem::path(dir) / kDejaVu[weight]);
  for (const char* dir : {"/usr/share/fonts/liberation", "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/TTF"})
    out.push_back(std::filesystem::path(dir) / kLiberation[weight]);
  return out;
}

std::filesystem::path ExeDir() {
  std::error_code ec;
  std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
  // Started through the system's program loader (the launcher's fallback): /proc/self/exe is the
  // loader then - the launcher always starts the game inside bin/, so use that.
  if (ec || p.filename().string().rfind("ld-", 0) == 0) return std::filesystem::current_path();
  return p.parent_path();
}

int CaptureStack(void** frames, int count) {
  void* all[64];
  int n = backtrace(all, std::min(count + 2, 64));
  int keep = std::max(0, n - 2);  // without this function and its caller's frame
  for (int i = 0; i < keep; ++i) frames[i] = all[i + 2];
  return keep;
}
uintptr_t ExeBase() { return 0; }

void EndProcess(int code) {
  REXLOG_INFO("[spyro] ending the process (code {})", code);
  rex::FlushLogging();
  std::fflush(nullptr);
  _exit(code);
}

namespace {
void OnAbort(int sig) {
  static const char kHead[] = "\n[spyro] crash (abort) - stack, offsets inside each file:\n";
  ssize_t ignored = write(2, kHead, sizeof(kHead) - 1);
  void* frames[64];
  int n = backtrace(frames, 64);
  backtrace_symbols_fd(frames, n, 2);
  ignored = write(2, "[spyro] end of stack\n", 21);
  (void)ignored;
  rex::FlushLogging();
  signal(sig, SIG_DFL);
  raise(sig);
}
}  // namespace

void InstallCrashReport() {
  void* warm[2];
  backtrace(warm, 2);  // loads the unwinder now, not inside the signal handler
  signal(SIGABRT, OnAbort);
}

#endif

}  // namespace spyro::platform
