// Spyro: Dawn of the Dragon Recomp - Linux launcher (SpyroRecomp, the file players start; WORK_LOG 158).
// The Linux twin of launcher.cpp, on SDL3 (message boxes, the file dialog, a small progress window).
//
// First start: finds the game - an Xbox 360 .iso or an extracted game folder (default.xex) placed
// next to the launcher is taken by itself (handy on the Steam Deck); otherwise it asks. An ISO is
// unpacked once into "game" next to the launcher (XDVDFS reader below). Checks that default.xex is
// the version the recomp was generated from, remembers the choice in launcher.ini, then starts
// bin/spyrodotd as its child and waits for it (Steam tracks the whole process tree), with saves in
// "user" and logs in "logs"; a game that cannot start or stops right away gets a message. --choose picks the game again; --game <iso | default.xex | folder> sets it without asking.
// Any other arguments are passed on to the game.

#include <SDL3/SDL.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr char kTitle[] = "Spyro: Dawn of the Dragon Recomp";
// SHA-256 of Default.xex of "Legend of Spyro, The - Dawn of the Dragon (USA, Europe)" (Xbox 360).
constexpr char kXexSha256[] = "7C9C893065E7490295230EA2DC06E67C0D90DF451F6295CA699843BCB0A51171";
constexpr int kKeptLogs = 10;

fs::path g_dir;  // the launcher's folder

void Message(const std::string& text, SDL_MessageBoxFlags kind = SDL_MESSAGEBOX_INFORMATION) {
  if (!SDL_ShowSimpleMessageBox(kind, kTitle, text.c_str(), nullptr)) std::fprintf(stderr, "%s\n", text.c_str());
}

// Yes / No question (No when there is no message box at all).
bool Ask(const std::string& text, bool default_yes = true) {
  const SDL_MessageBoxButtonData buttons[] = {
      {default_yes ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0, 1, "Yes"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | (default_yes ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT), 0, "No"},
  };
  SDL_MessageBoxData data{SDL_MESSAGEBOX_WARNING, nullptr, kTitle, text.c_str(), 2, buttons, nullptr};
  int pressed = 0;
  return SDL_ShowMessageBox(&data, &pressed) && pressed == 1;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool HasXex(const fs::path& folder) {
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return false;
  for (const auto& e : fs::directory_iterator(folder, ec))  // the disc's name is "Default.xex"
    if (Lower(e.path().filename().string()) == "default.xex" && e.is_regular_file(ec)) return true;
  return false;
}

fs::path XexIn(const fs::path& folder) {
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(folder, ec))
    if (Lower(e.path().filename().string()) == "default.xex") return e.path();
  return folder / "default.xex";
}

// ---- SHA-256 (FIPS 180-4) ---------------------------------------------------------------------
struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64];
  size_t fill = 0;
  uint64_t bits = 0;
  static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void Compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 | block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = hh + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
  }
  void Update(const uint8_t* p, size_t n) {
    bits += uint64_t(n) * 8;
    while (n--) {
      block[fill++] = *p++;
      if (fill == 64) Compress(), fill = 0;
    }
  }
  std::string Hex() {
    uint64_t total = bits;
    uint8_t one = 0x80, zero = 0;
    Update(&one, 1);
    while (fill != 56) Update(&zero, 1);
    for (int i = 7; i >= 0; --i) block[fill++] = uint8_t(total >> (i * 8));
    Compress();
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (uint32_t v : h)
      for (int i = 28; i >= 0; i -= 4) out += kHex[(v >> i) & 15];
    return out;
  }
};

std::string FileSha256(const fs::path& file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) return "";
  Sha256 s;
  std::vector<uint8_t> buf(1 << 20);
  while (f) {
    f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
    s.Update(buf.data(), size_t(f.gcount()));
  }
  return s.Hex();
}

// ---- progress window (ISO unpacking): a bar in a small window, the numbers in its title ----------
struct Progress {
  SDL_Window* window = nullptr;
  bool cancelled = false;
  uint64_t last_draw = 0;

  void Open() {
    window = SDL_CreateWindow(kTitle, 520, 90, 0);
    Update(0, 1);
  }
  void Update(uint64_t done, uint64_t total) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        if (Ask("Stop unpacking the game?", false)) cancelled = true;
    uint64_t now = SDL_GetTicks();
    if (!window || (now - last_draw < 100 && done < total)) return;
    last_draw = now;
    char title[160];
    std::snprintf(title, sizeof(title), "Unpacking the game (one time only): %.1f of %.1f GB", done / 1e9, total / 1e9);
    SDL_SetWindowTitle(window, title);
    SDL_Surface* s = SDL_GetWindowSurface(window);
    if (!s) return;
    const SDL_PixelFormatDetails* f = SDL_GetPixelFormatDetails(s->format);
    SDL_FillSurfaceRect(s, nullptr, SDL_MapRGB(f, nullptr, 30, 18, 12));
    SDL_Rect frame{20, 30, s->w - 40, 30};
    SDL_FillSurfaceRect(s, &frame, SDL_MapRGB(f, nullptr, 80, 60, 45));
    SDL_Rect bar{22, 32, int((s->w - 44) * (total ? double(done) / double(total) : 0.0)), 26};
    SDL_FillSurfaceRect(s, &bar, SDL_MapRGB(f, nullptr, 255, 154, 46));
    SDL_UpdateWindowSurface(window);
  }
  void Close() {
    if (window) SDL_DestroyWindow(window);
    window = nullptr;
  }
};

// ---- Xbox 360 disc image (XDVDFS) - as in launcher.cpp ------------------------------------------
constexpr uint64_t kSector = 2048;

struct IsoEntry {
  std::string name;
  uint32_t sector = 0, size = 0;
  bool folder = false;
};

struct Iso {
  mutable std::ifstream file;
  uint64_t base = 0;

  bool Read(uint64_t offset, void* out, uint32_t size) const {
    file.clear();
    file.seekg(std::streamoff(offset));
    file.read(static_cast<char*>(out), size);
    return uint64_t(file.gcount()) == size;
  }
  bool Open(const fs::path& path, uint32_t* root_sector, uint32_t* root_size) {
    file.open(path, std::ios::binary);
    if (!file) return false;
    for (uint64_t b : {0x0ull, 0xFD90000ull, 0x89D80000ull, 0x2080000ull}) {
      uint8_t v[28];
      if (Read(b + 32 * kSector, v, sizeof(v)) && std::memcmp(v, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        base = b;
        std::memcpy(root_sector, v + 20, 4);
        std::memcpy(root_size, v + 24, 4);
        return true;
      }
    }
    return false;
  }
  bool ReadFolder(uint32_t sector, uint32_t size, std::vector<IsoEntry>& out) const {
    if (!size) return true;
    if (size > (64u << 20)) return false;
    std::vector<uint8_t> t(size);
    if (!Read(base + sector * kSector, t.data(), size)) return false;
    auto u16 = [&](uint32_t o) { return uint16_t(t[o] | t[o + 1] << 8); };
    auto u32 = [&](uint32_t o) { return uint32_t(t[o] | t[o + 1] << 8 | t[o + 2] << 16 | uint32_t(t[o + 3]) << 24); };
    std::vector<uint32_t> todo{0};
    for (size_t steps = 0; !todo.empty() && steps < size; ++steps) {
      uint32_t o = todo.back();
      todo.pop_back();
      if (o + 14 > size) continue;
      uint16_t left = u16(o), right = u16(o + 2);
      if (left == 0xFFFF && right == 0xFFFF) continue;
      uint8_t len = t[o + 13];
      if (!len || o + 14 + len > size) continue;
      out.push_back({std::string(reinterpret_cast<const char*>(&t[o + 14]), len), u32(o + 4), u32(o + 8),
                     (t[o + 12] & 0x10) != 0});
      if (left) todo.push_back(uint32_t(left) * 4);
      if (right) todo.push_back(uint32_t(right) * 4);
    }
    return true;
  }
};

bool SafeName(const std::string& n) { return n != "." && n != ".." && n.find_first_of("/\\") == std::string::npos; }

bool CountBytes(const Iso& iso, uint32_t sector, uint32_t size, uint64_t& total, int depth = 0) {
  std::vector<IsoEntry> entries;
  if (depth > 32 || !iso.ReadFolder(sector, size, entries)) return false;
  for (const IsoEntry& e : entries) {
    if (depth == 0 && e.name == "$SystemUpdate") continue;
    if (e.folder ? !CountBytes(iso, e.sector, e.size, total, depth + 1) : false) return false;
    if (!e.folder) total += e.size;
  }
  return true;
}

bool CopyOut(const Iso& iso, uint32_t sector, uint32_t size, const fs::path& to, Progress& progress, uint64_t& done,
             uint64_t total, int depth = 0) {
  std::vector<IsoEntry> entries;
  if (depth > 32 || !iso.ReadFolder(sector, size, entries)) return false;
  std::error_code ec;
  fs::create_directories(to, ec);
  std::vector<uint8_t> buf(4 << 20);
  for (const IsoEntry& e : entries) {
    if (depth == 0 && e.name == "$SystemUpdate") continue;
    if (!SafeName(e.name)) return false;
    fs::path dest = to / e.name;
    if (e.folder) {
      if (!CopyOut(iso, e.sector, e.size, dest, progress, done, total, depth + 1)) return false;
      continue;
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    uint64_t at = iso.base + uint64_t(e.sector) * kSector;
    for (uint32_t left = e.size; left;) {
      uint32_t n = std::min<uint32_t>(left, uint32_t(buf.size()));
      if (!iso.Read(at, buf.data(), n) || !out.write(reinterpret_cast<const char*>(buf.data()), n)) return false;
      at += n;
      left -= n;
      done += n;
      progress.Update(done, total);
      if (progress.cancelled) return false;
    }
  }
  return true;
}

bool UnpackIso(const fs::path& iso_path, const fs::path& target) {
  Iso iso;
  uint32_t root_sector = 0, root_size = 0;
  if (!iso.Open(iso_path, &root_sector, &root_size)) {
    Message("This file is not an Xbox 360 game disc image:\n" + iso_path.string(), SDL_MESSAGEBOX_ERROR);
    return false;
  }
  uint64_t total = 0;
  if (!CountBytes(iso, root_sector, root_size, total)) {
    Message("The disc image could not be read (damaged or incomplete file?).", SDL_MESSAGEBOX_ERROR);
    return false;
  }
  std::error_code ec;
  fs::space_info space = fs::space(g_dir, ec);
  if (!ec && space.available < total + (256ull << 20)) {
    char text[200];
    std::snprintf(text, sizeof(text), "Not enough free disk space next to the launcher: unpacking needs %.1f GB.",
                  total / 1e9);
    Message(text, SDL_MESSAGEBOX_ERROR);
    return false;
  }
  fs::path partial = target;
  partial += ".partial";
  fs::remove_all(partial, ec);
  Progress progress;
  progress.Open();
  uint64_t done = 0;
  bool ok = CopyOut(iso, root_sector, root_size, partial, progress, done, total);
  progress.Close();
  if (ok) {
    fs::remove_all(target, ec);
    fs::rename(partial, target, ec);
    ok = !ec;
  }
  if (!ok) {
    fs::remove_all(partial, ec);
    if (!progress.cancelled) Message("Unpacking the game failed (read or write error).", SDL_MESSAGEBOX_ERROR);
    return false;
  }
  return true;
}

// ---- choosing the game ------------------------------------------------------------------------
// SDL's file dialog (desktop portal, or zenity / kdialog): waits for the answer.
fs::path AskForFile() {
  struct State {
    std::atomic<bool> done{false};
    std::string path;
  } state;
  static const SDL_DialogFileFilter filters[] = {{"Xbox 360 game (.iso or default.xex)", "iso;xex"}, {"All files", "*"}};
  SDL_ShowOpenFileDialog(
      [](void* user, const char* const* files, int) {
        auto* s = static_cast<State*>(user);
        if (files && files[0]) s->path = files[0];
        s->done = true;
      },
      &state, nullptr, filters, 2, g_dir.string().c_str(), false);
  while (!state.done) {
    SDL_Event e;
    SDL_WaitEventTimeout(&e, 100);
  }
  return state.path;
}

fs::path GameFromPick(const fs::path& pick, bool ask) {
  if (Lower(pick.extension().string()) == ".iso") {
    fs::path target = g_dir / "game";
    if (HasXex(target) &&
        (!ask || !Ask("A game is already unpacked in the \"game\" folder.\n\nUnpack this ISO again and replace it?", false)))
      return target;
    return UnpackIso(pick, target) ? target : fs::path();
  }
  if (HasXex(pick)) return pick;  // a folder
  if (HasXex(pick.parent_path())) return pick.parent_path();
  return {};
}

// A game placed next to the launcher: one .iso, or a folder holding default.xex.
fs::path FindBesideLauncher() {
  std::error_code ec;
  fs::path iso;
  int isos = 0;
  for (const auto& e : fs::directory_iterator(g_dir, ec)) {
    if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".iso") iso = e.path(), ++isos;
    if (e.is_directory(ec) && e.path().filename() != "bin" && e.path().filename() != "user" && HasXex(e.path()))
      return e.path();
  }
  return isos == 1 ? iso : fs::path();
}

fs::path ChooseGame(bool look_beside) {
  if (look_beside)
    if (fs::path found = FindBesideLauncher(); !found.empty())
      if (fs::path game = GameFromPick(found, false); !game.empty()) return game;
  Message("Please select your copy of The Legend of Spyro: Dawn of the Dragon (Xbox 360).\n\n"
          "Pick either the disc image (.iso) or the default.xex file inside an already extracted game folder. "
          "An ISO is unpacked once into the \"game\" folder next to this launcher.\n\n"
          "(Or put the .iso next to the launcher and start it again.)");
  for (;;) {
    fs::path pick = AskForFile();
    if (pick.empty()) return {};
    if (fs::path game = GameFromPick(pick, true); !game.empty()) return game;
    if (Lower(pick.extension().string()) != ".iso")
      Message("That is not the game. Pick the .iso file, or default.xex in an extracted game folder.",
              SDL_MESSAGEBOX_WARNING);
  }
}

// ---- launcher.ini (game=<path>) -------------------------------------------------------------------
fs::path SavedGame() {
  std::ifstream f(g_dir / "launcher.ini");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("game=", 0) == 0) return line.substr(5);
  return {};
}

void SaveGame(const fs::path& game) {
  std::ofstream f(g_dir / "launcher.ini", std::ios::trunc);
  f << "[launcher]\ngame=" << game.string() << "\n";
}

// ---- starting the game ------------------------------------------------------------------------
void PruneLogs(const fs::path& logs) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(logs, ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".log" && e.path().filename().string().rfind("spyro_", 0) == 0)
      files.push_back(e.path());  // only the game's own session logs, not launcher.log / game_output.log
  if (int(files.size()) < kKeptLogs) return;
  std::sort(files.begin(), files.end());
  for (size_t i = 0; i + kKeptLogs - 1 < files.size(); ++i) fs::remove(files[i], ec);
}

// Launcher notes for bug reports: logs/launcher.log (one start per line block, newest last).
void LogLine(const std::string& text) {
  std::error_code ec;
  fs::create_directories(g_dir / "logs", ec);
  if (fs::file_size(g_dir / "logs" / "launcher.log", ec) > 512 * 1024 && !ec) fs::remove(g_dir / "logs" / "launcher.log", ec);
  std::ofstream f(g_dir / "logs" / "launcher.log", std::ios::app);
  std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  f << stamp << "  " << text << "\n";
}

std::string Mode(const fs::path& p) {
  std::error_code ec;
  fs::file_status s = fs::status(p, ec);
  if (ec) return "missing (" + ec.message() + ")";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%03o", unsigned(s.permissions()) & 0777u);
  return buf;
}

[[noreturn]] void StartGame(const fs::path& game, const std::vector<std::string>& extra) {
  fs::path bin = g_dir / "bin", exe = bin / "spyrodotd", user = g_dir / "user", logs = g_dir / "logs";
  std::error_code ec;
  fs::create_directories(user, ec);
  fs::create_directories(logs, ec);
  PruneLogs(logs);
  // The Recomp Settings file lives next to SpyroRecomp, where players can find and edit it
  // (WORK_LOG 164); test builds kept it in bin/ - carried over once.
  const fs::path settings = g_dir / "recomp_settings.ini";
  if (!fs::exists(settings, ec) && fs::exists(bin / "recomp_settings.ini", ec))
    fs::rename(bin / "recomp_settings.ini", settings, ec);
  setenv("SPYRO_SETTINGS_FILE", settings.c_str(), 1);
  // The game file must be allowed to run: some ways of copying / unpacking drop that flag.
  fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  LogLine("starting " + exe.string() + " (mode " + Mode(exe) + ") with game " + game.string());
  std::time_t now = std::time(nullptr);
  char name[64];
  std::strftime(name, sizeof(name), "spyro_%Y-%m-%d_%H-%M-%S.log", std::localtime(&now));
  std::vector<std::string> args = {exe.string(), "--game_data_root", game.string(), "--user_data_root", user.string(),
                                   "--log_level", "info", "--log_file", (logs / name).string(), "--fullscreen=false"};
  args.insert(args.end(), extra.begin(), extra.end());
  std::vector<char*> argv;
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  // Fallback: through the system's program loader - works even when the file cannot be marked
  // as runnable (some drive formats).
  std::string loader = "/lib64/ld-linux-x86-64.so.2";
  std::vector<char*> argv2 = {loader.data()};
  argv2.insert(argv2.end(), argv.begin(), argv.end());  // exe, its arguments, nullptr
  const std::string out_path = (logs / "game_output.log").string();
  // Keep the last session's output too: after a freeze the player usually starts the game again
  // before sending the logs (WORK_LOG 163).
  fs::rename(logs / "game_output.log", logs / "game_output_previous.log", ec);
  SDL_Quit();  // the game opens its own SDL window

  // The game runs as a child process and the launcher waits for it: a game that cannot start
  // or stops right away gets a message saying why instead of vanishing silently. Its console
  // output goes on to the terminal (if any) and into logs/game_output.log.
  int status_pipe[2], out_pipe[2];
  std::string why;
  if (pipe2(status_pipe, O_CLOEXEC) != 0 || pipe(out_pipe) != 0) {
    why = "pipe: " + std::string(std::strerror(errno));
  } else {
    const auto started = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid == 0) {  // child: only plain system calls until exec
      close(status_pipe[0]);
      close(out_pipe[0]);
      dup2(out_pipe[1], 1);
      dup2(out_pipe[1], 2);
      close(out_pipe[1]);
      int msg[3] = {0, 0, 0};  // failed step, its error, the loader fallback's error
      if (chdir(bin.c_str()) != 0) {
        msg[0] = 1;
        msg[1] = errno;
      } else {
        execv(exe.c_str(), argv.data());
        msg[0] = 2;
        msg[1] = errno;
        execv(loader.c_str(), argv2.data());
        msg[2] = errno;
      }
      ssize_t ignored = write(status_pipe[1], msg, sizeof(msg));
      (void)ignored;
      _exit(127);
    }
    close(status_pipe[1]);
    close(out_pipe[1]);
    if (pid < 0) {
      why = "fork: " + std::string(std::strerror(errno));
    } else {
      std::signal(SIGPIPE, SIG_IGN);  // the terminal may go away; keep reading the game's output
      int msg[3] = {0, 0, 0};
      size_t got = 0;
      while (got < sizeof(msg)) {
        ssize_t n = read(status_pipe[0], reinterpret_cast<char*>(msg) + got, sizeof(msg) - got);
        if (n > 0) got += size_t(n);
        else if (n == 0 || errno != EINTR) break;
      }
      close(status_pipe[0]);
      if (got == sizeof(msg)) {  // exec failed (the pipe closes by itself when it works)
        why = msg[0] == 1 ? "cannot enter the bin folder: " + std::string(std::strerror(msg[1]))
                          : "starting it: " + std::string(std::strerror(msg[1])) + "; through " + loader +
                                ": " + std::string(std::strerror(msg[2]));
        waitpid(pid, nullptr, 0);
      } else {
        LogLine("game started (process " + std::to_string(pid) + ")");
        // Copy the game's output to the terminal and the log, keep its tail for a message.
        FILE* out = std::fopen(out_path.c_str(), "w");
        std::string tail;
        char buf[4096];
        int status = 0;
        bool exited = false;
        while (true) {
          pollfd p{out_pipe[0], POLLIN, 0};
          int r = poll(&p, 1, 500);
          if (r > 0) {
            ssize_t n = read(out_pipe[0], buf, sizeof(buf));
            if (n > 0) {
              ssize_t ignored = write(2, buf, size_t(n));
              (void)ignored;
              if (out) std::fwrite(buf, 1, size_t(n), out), std::fflush(out);
              tail.append(buf, size_t(n));
              if (tail.size() > 8192) tail.erase(0, tail.size() - 8192);
              continue;
            }
            if (n == 0 || errno != EINTR) break;  // end of output: the game (and its helpers) ended
          } else if (r < 0 && errno != EINTR) {
            break;
          }
          // A helper process the game started may keep the output open: stop when the game is gone.
          if (waitpid(pid, &status, WNOHANG) == pid) {
            exited = true;
            break;
          }
        }
        if (out) std::fclose(out);
        close(out_pipe[0]);
        if (!exited) waitpid(pid, &status, 0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::string how = WIFSIGNALED(status) ? "crashed (signal " + std::to_string(WTERMSIG(status)) + ", " +
                                                    std::string(strsignal(WTERMSIG(status))) + ")"
                                              : "ended with exit code " + std::to_string(WEXITSTATUS(status));
        LogLine("game " + how + " after " + std::to_string(int(secs)) + " s");
        const bool failed = WIFSIGNALED(status) || WEXITSTATUS(status) != 0;
        // Only right after the start: a problem when quitting should not pop up a window.
        if (failed && secs < 30.0) {
          // The last lines of its output, shortened, for the message.
          std::vector<std::string> lines;
          size_t pos = 0;
          while (pos < tail.size()) {
            size_t end = tail.find('\n', pos);
            if (end == std::string::npos) end = tail.size();
            std::string line = tail.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 150) line = line.substr(0, 150) + "...";
            if (!line.empty()) lines.push_back(line);
            pos = end + 1;
          }
          std::string last;
          for (size_t i = lines.size() > 8 ? lines.size() - 8 : 0; i < lines.size(); ++i) last += lines[i] + "\n";
          SDL_Init(SDL_INIT_VIDEO);
          Message("The game stopped right after starting: it " + how + ".\n\n" +
                      (last.empty() ? std::string() : "Its last messages:\n" + last + "\n") +
                      "Please send the files in the \"logs\" folder (game_output.log, launcher.log and the newest "
                      "spyro_ log) with your bug report.",
                  SDL_MESSAGEBOX_ERROR);
        }
        std::exit(WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status));
      }
    }
  }
  LogLine("FAILED: " + why);
  SDL_Init(SDL_INIT_VIDEO);
  Message("The game could not be started (" + exe.string() + ").\n\n" + why +
              "\n\nPlease send logs/launcher.log with your bug report.",
          SDL_MESSAGEBOX_ERROR);
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  std::error_code ec;
  g_dir = fs::read_symlink("/proc/self/exe", ec).parent_path();
  if (ec || g_dir.empty()) g_dir = fs::absolute(argv[0]).parent_path();
  SDL_Init(SDL_INIT_VIDEO);

  bool choose = false;
  fs::path given;
  std::vector<std::string> extra;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--choose") choose = true;
    else if (a == "--game" && i + 1 < argc) given = argv[++i];
    else extra.push_back(a);
  }

  // Guest memory files older builds left in /dev/shm (RAM) when the game crashed or ended quickly
  // (WORK_LOG 162; the game now drops the name at once). Removing a name never affects a game that
  // is still running - its memory stays mapped.
  {
    int removed = 0;
    for (const auto& e : fs::directory_iterator("/dev/shm", ec))
      if (e.path().filename().string().rfind("xenia_memory_", 0) == 0 && fs::remove(e.path(), ec)) ++removed;
    if (removed) LogLine("removed " + std::to_string(removed) + " leftover game memory file(s) from /dev/shm");
  }

  if (!fs::exists(g_dir / "bin" / "spyrodotd", ec)) {
    Message("bin/spyrodotd is missing. Unpack the whole download, then start SpyroRecomp again.", SDL_MESSAGEBOX_ERROR);
    return 1;
  }

  fs::path game = SavedGame();
  if (!given.empty()) {
    game = GameFromPick(given, false);
    if (game.empty()) {
      Message("--game: not the game (give the .iso, default.xex or the extracted folder):\n" + given.string(),
              SDL_MESSAGEBOX_ERROR);
      return 1;
    }
  } else if (choose || !HasXex(game)) {
    game = ChooseGame(!choose);
    if (game.empty()) return 0;
  }

  if (FileSha256(XexIn(game)) != kXexSha256 &&
      !Ask("This game's default.xex is not the version this recomp was made from (the USA/Europe disc, "
           "\"The Legend of Spyro: Dawn of the Dragon (USA, Europe)\").\n\nOther versions will most likely crash. "
           "Start anyway?",
           false))
    return 0;

  SaveGame(game);
  StartGame(game, extra);
}
