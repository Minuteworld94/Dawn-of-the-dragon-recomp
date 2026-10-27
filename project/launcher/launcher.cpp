// Spyro: Dawn of the Dragon Recomp - launcher (SpyroRecomp.exe, the file players double-click).
//
// First start: asks for the game - the Xbox 360 .iso, or default.xex inside an extracted game
// folder. An ISO is unpacked once into "game" next to the launcher (XDVDFS reader below). Checks
// that default.xex is the version the recomp was generated from, remembers the choice in
// launcher.ini and starts bin\spyrodotd.exe with saves in "user" and logs in "logs".
// Hold Shift while starting (or pass --choose) to pick the game again; --game <iso | default.xex |
// folder> sets it without the picker. Any other arguments are passed on to the game.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTitle[] = L"Spyro: Dawn of the Dragon Recomp";
// SHA-256 of Default.xex of "Legend of Spyro, The - Dawn of the Dragon (USA, Europe)" (Xbox 360),
// the executable this recomp was generated from.
constexpr wchar_t kXexSha256[] = L"7C9C893065E7490295230EA2DC06E67C0D90DF451F6295CA699843BCB0A51171";
constexpr int kKeptLogs = 10;

fs::path g_dir;  // the launcher's folder

void Message(const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
  MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | icon);
}

bool HasXex(const fs::path& folder) {
  std::error_code ec;
  return !folder.empty() && fs::is_regular_file(folder / L"default.xex", ec);
}

// Upper-case hex SHA-256 of a file ("" if it can't be read).
std::wstring Sha256(const fs::path& file) {
  std::wstring hex;
  HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return hex;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<uint8_t> buf(1 << 20);
    DWORD got = 0;
    bool ok = true;
    while ((ok = ReadFile(f, buf.data(), DWORD(buf.size()), &got, nullptr) != 0) && got)
      BCryptHashData(hash, buf.data(), got, 0);
    uint8_t digest[32];
    if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      static const wchar_t kHex[] = L"0123456789ABCDEF";
      for (uint8_t b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return hex;
}

// ---- progress window (ISO unpacking) ---------------------------------------------------------
struct Progress {
  HWND window = nullptr, label = nullptr, bar = nullptr;
  bool cancelled = false;

  static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CLOSE) {
      auto* self = reinterpret_cast<Progress*>(GetWindowLongPtrW(h, GWLP_USERDATA));
      if (self && MessageBoxW(h, L"Stop unpacking the game?", kTitle, MB_YESNO | MB_ICONQUESTION) == IDYES)
        self->cancelled = true;
      return 0;
    }
    return DefWindowProcW(h, msg, w, l);
  }

  void Open() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SpyroRecompProgress";
    RegisterClassW(&wc);
    RECT r{0, 0, 460, 110};
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    window = CreateWindowW(wc.lpszClassName, kTitle, WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                           nullptr, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    label = CreateWindowW(L"STATIC", L"Unpacking the game...", WS_CHILD | WS_VISIBLE, 20, 18, 420, 40, window,
                          nullptr, wc.hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    bar = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 20, 66, 420, 22, window, nullptr,
                        wc.hInstance, nullptr);
    SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
  }

  void Update(uint64_t done, uint64_t total) {
    int permille = total ? int(done * 1000 / total) : 0;
    SendMessageW(bar, PBM_SETPOS, permille, 0);
    wchar_t text[160];
    swprintf(text, 160, L"Unpacking the game (one time only)...\n%.1f of %.1f GB", done / 1e9, total / 1e9);
    SetWindowTextW(label, text);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  void Close() {
    if (window) DestroyWindow(window);
    window = nullptr;
  }
};

// ---- Xbox 360 disc image (XDVDFS) -----------------------------------------------------------
// The game partition starts at one of these offsets (0 = an "xiso" already trimmed to it); its
// sector 32 holds "MICROSOFT*XBOX*MEDIA", the root directory's sector and size. A directory is a
// binary tree of entries {u16 left, u16 right (in 4-byte units), u32 sector, u32 size, u8 attributes
// (0x10 = folder), u8 name length, name}; all values little-endian, sectors 2048 bytes.
constexpr uint64_t kSector = 2048;

struct IsoEntry {
  std::string name;
  uint32_t sector = 0, size = 0;
  bool folder = false;
};

struct Iso {
  HANDLE file = INVALID_HANDLE_VALUE;
  uint64_t base = 0;

  ~Iso() {
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  }
  bool Read(uint64_t offset, void* out, uint32_t size) const {
    LARGE_INTEGER at;
    at.QuadPart = LONGLONG(offset);
    DWORD got = 0;
    return SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && ReadFile(file, out, size, &got, nullptr) && got == size;
  }
  bool Open(const fs::path& path, uint32_t* root_sector, uint32_t* root_size) {
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                       nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
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
      if (left == 0xFFFF && right == 0xFFFF) continue;  // padding: empty folder
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

bool SafeName(const std::string& n) {
  return n != "." && n != ".." && n.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

// Total bytes of the files under a folder (for the progress bar); false if the image is broken.
bool CountBytes(const Iso& iso, uint32_t sector, uint32_t size, uint64_t& total, int depth = 0) {
  std::vector<IsoEntry> entries;
  if (depth > 32 || !iso.ReadFolder(sector, size, entries)) return false;
  for (const IsoEntry& e : entries) {
    if (depth == 0 && e.name == "$SystemUpdate") continue;  // console system update: not needed
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
    fs::path dest = to / fs::path(std::wstring(e.name.begin(), e.name.end()));
    if (e.folder) {
      if (!CopyOut(iso, e.sector, e.size, dest, progress, done, total, depth + 1)) return false;
      continue;
    }
    HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE) return false;
    uint64_t at = iso.base + uint64_t(e.sector) * kSector;
    for (uint32_t left = e.size; left;) {
      uint32_t n = std::min<uint32_t>(left, uint32_t(buf.size()));
      DWORD wrote = 0;
      if (!iso.Read(at, buf.data(), n) || !WriteFile(out, buf.data(), n, &wrote, nullptr) || wrote != n) {
        CloseHandle(out);
        return false;
      }
      at += n;
      left -= n;
      done += n;
      progress.Update(done, total);
      if (progress.cancelled) {
        CloseHandle(out);
        return false;
      }
    }
    CloseHandle(out);
  }
  return true;
}

// Unpacks the ISO into <launcher>\game (via game.partial, renamed when complete).
bool UnpackIso(const fs::path& iso_path, const fs::path& target) {
  Iso iso;
  uint32_t root_sector = 0, root_size = 0;
  if (!iso.Open(iso_path, &root_sector, &root_size)) {
    Message(L"This file is not an Xbox 360 game disc image:\n" + iso_path.wstring(), MB_ICONERROR);
    return false;
  }
  uint64_t total = 0;
  if (!CountBytes(iso, root_sector, root_size, total)) {
    Message(L"The disc image could not be read (damaged or incomplete file?).", MB_ICONERROR);
    return false;
  }
  ULARGE_INTEGER free_bytes{};
  if (GetDiskFreeSpaceExW(g_dir.c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < total + (256ull << 20)) {
    wchar_t text[200];
    swprintf(text, 200, L"Not enough free disk space next to the launcher: unpacking needs %.1f GB.", total / 1e9);
    Message(text, MB_ICONERROR);
    return false;
  }
  fs::path partial = target;
  partial += L".partial";
  std::error_code ec;
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
    if (!progress.cancelled) Message(L"Unpacking the game failed (read or write error).", MB_ICONERROR);
    return false;
  }
  return true;
}

// ---- choosing the game ------------------------------------------------------------------------
fs::path AskForFile() {
  fs::path result;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return result;
  COMDLG_FILTERSPEC types[] = {{L"Xbox 360 game (.iso or default.xex)", L"*.iso;default.xex"},
                               {L"All files", L"*.*"}};
  dialog->SetFileTypes(2, types);
  dialog->SetTitle(L"Select your Spyro: Dawn of the Dragon - the .iso, or default.xex in an extracted game");
  IShellItem* item = nullptr;
  if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item))) {
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
      result = path;
      CoTaskMemFree(path);
    }
    item->Release();
  }
  dialog->Release();
  return result;
}

// The game folder for a picked file or folder: an ISO is unpacked into <launcher>\game (asking
// first if one is already there, unless `ask` is false); default.xex -> its folder. Empty if the
// pick is not the game or unpacking failed.
fs::path GameFromPick(const fs::path& pick, bool ask) {
  std::wstring ext = pick.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  if (ext == L".iso") {
    fs::path target = g_dir / L"game";
    if (HasXex(target) &&
        (!ask || MessageBoxW(nullptr,
                             L"A game is already unpacked in the \"game\" folder.\n\nUnpack this ISO again and replace it?",
                             kTitle, MB_YESNO | MB_ICONQUESTION) == IDNO))
      return target;
    return UnpackIso(pick, target) ? target : fs::path();
  }
  if (HasXex(pick)) return pick;  // a folder
  if (HasXex(pick.parent_path())) return pick.parent_path();
  return {};
}

// The extracted game folder the player picks (unpacking an ISO first); empty if they cancel.
fs::path ChooseGame() {
  Message(L"Please select your copy of The Legend of Spyro: Dawn of the Dragon (Xbox 360).\n\n"
          L"Pick either the disc image (.iso) or the default.xex file inside an already extracted game "
          L"folder. An ISO is unpacked once into the \"game\" folder next to this launcher.");
  for (;;) {
    fs::path pick = AskForFile();
    if (pick.empty()) return {};
    if (fs::path game = GameFromPick(pick, true); !game.empty()) return game;
    std::wstring ext = pick.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext != L".iso")
      Message(L"That is not the game. Pick the .iso file, or default.xex in an extracted game folder.",
              MB_ICONWARNING);
  }
}

// ---- starting the game ------------------------------------------------------------------------
std::wstring Quoted(fs::path p) {
  std::wstring s = p.wstring();
  if (!s.empty() && s.back() == L'\\') s += L'.';  // "E:\" would escape the closing quote
  return L"\"" + s + L"\"";
}

void PruneLogs(const fs::path& logs) {
  std::vector<fs::directory_entry> files;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(logs, ec))
    if (e.is_regular_file(ec) && e.path().extension() == L".log") files.push_back(e);
  if (int(files.size()) < kKeptLogs) return;
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
  for (size_t i = 0; i + kKeptLogs - 1 < files.size(); ++i) fs::remove(files[i].path(), ec);
}

bool StartGame(const fs::path& game, const std::wstring& extra) {
  fs::path bin = g_dir / L"bin", exe = bin / L"spyrodotd.exe", user = g_dir / L"user", logs = g_dir / L"logs";
  std::error_code ec;
  fs::create_directories(user, ec);
  fs::create_directories(logs, ec);
  PruneLogs(logs);
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[64];
  swprintf(name, 64, L"spyro_%04d-%02d-%02d_%02d-%02d-%02d.log", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
           t.wSecond);
  std::wstring cmd = Quoted(exe) + L" --gpu_plugin xenos --game_data_root " + Quoted(game) + L" --user_data_root " +
                     Quoted(user) + L" --log_level info --log_file " + Quoted(logs / name) + L" --fullscreen=false" +
                     extra;
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, bin.c_str(), &si, &pi)) {
    Message(L"The game could not be started (" + exe.wstring() + L").", MB_ICONERROR);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  std::wstring self(32768, L'\0');
  self.resize(GetModuleFileNameW(nullptr, self.data(), DWORD(self.size())));
  g_dir = fs::path(self).parent_path();

  bool choose = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  fs::path given;  // --game <iso | default.xex | folder>: no picker
  std::wstring extra;
  int argc = 0;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    for (int i = 1; i < argc; ++i) {
      std::wstring a = argv[i];
      if (a == L"--choose") choose = true;
      else if (a == L"--game" && i + 1 < argc) given = argv[++i];
      else extra += L" \"" + a + L"\"";
    }
    LocalFree(argv);
  }

  if (!fs::exists(g_dir / L"bin" / L"spyrodotd.exe")) {
    Message(L"bin\\spyrodotd.exe is missing. Unzip the whole download, then start SpyroRecomp.exe again.",
            MB_ICONERROR);
    return 1;
  }

  fs::path ini = g_dir / L"launcher.ini";
  std::wstring saved(32768, L'\0');
  saved.resize(GetPrivateProfileStringW(L"launcher", L"game", L"", saved.data(), DWORD(saved.size()), ini.c_str()));
  fs::path game = saved;
  if (!given.empty()) {
    game = GameFromPick(given, false);
    if (game.empty()) {
      Message(L"--game: not the game (give the .iso, default.xex or the extracted folder):\n" + given.wstring(),
              MB_ICONERROR);
      return 1;
    }
  } else if (choose || !HasXex(game)) {
    game = ChooseGame();
    if (game.empty()) return 0;
  }

  std::wstring hash = Sha256(game / L"default.xex");
  if (hash != kXexSha256 &&
      MessageBoxW(nullptr,
                  L"This game's default.xex is not the version this recomp was made from (the USA/Europe disc, "
                  L"\"The Legend of Spyro: Dawn of the Dragon (USA, Europe)\").\n\n"
                  L"Other versions will most likely crash. Start anyway?",
                  kTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    return 0;

  WritePrivateProfileStringW(L"launcher", L"game", game.wstring().c_str(), ini.c_str());
  return StartGame(game, extra) ? 0 : 1;
}
