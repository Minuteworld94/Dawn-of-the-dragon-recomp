// Hooks into the recompiled game code.
//
// How this works: codegen emits every guest function twice -- the real body as
// __imp__sub_XXXXXXXX, and a WEAK alias sub_XXXXXXXX that everything calls. Defining
// sub_XXXXXXXX here (strongly) replaces the alias, so our version runs instead, and we can
// still invoke the original through __imp__sub_XXXXXXXX. No codegen changes needed.
//
// Current purpose: learn what a menu ROW widget is, so a "Recomp Settings" row can be added to
// the game's display settings menu (CKS08MenuOptionVideo360, "Menu Option Video 360").
//
// Menu framework (WORK_LOG 56):
//   menu +0x84  pointer to array of row-widget pointers      menu +0x8C  row count
//   row  +0x10  value                                         row  +0x14  state byte
//                                                               (0 normal, 2 highlighted, 4 pressed)
//   0x822C11E0  Menu::Update(dt): per frame, rows last..first: row->vt[+0x54](dt), then
//               menu->vt[57](row_index, row->state, 0)
//   0x822C2B50  Menu::SendRowEvent: menu->vt[57](..,2), row->vt[+0x58](event), menu->vt[57](..,1)
//   vt[57] of the video menu = 0x822E0C90: on (row 1, state 4, phase 1) -> Back.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "guest_memory.h"
#include "spyro_settings.h"

namespace {

// --- guest memory helpers --------------------------------------------------------------------

// True when [addr, addr+len) of guest memory is committed and readable on the host, so a debug
// dump can't fault on a bad pointer (cached: guest_memory.cpp).
bool GuestReadable(const uint8_t* base, uint32_t addr, uint32_t len) {
  return addr >= 0x00010000u && spyro::GuestReadable(base, addr, len);
}

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}

// Class records in .rdata are [display name][class name][vtable]; so the class name is the
// printable string that ends right before the vtable (after up to 3 bytes of NUL padding).
std::string ClassNameForVtable(const uint8_t* base, uint32_t vt) {
  if (vt < 0x82000000u || vt >= 0x83000000u || !GuestReadable(base, vt - 64, 64)) return "?";
  uint32_t end = vt;
  while (end > vt - 4 && base[end - 1] == 0) --end;
  uint32_t start = end;
  while (start > vt - 64 && std::isprint(base[start - 1])) --start;
  if (end - start < 3) return "?";
  return std::string(reinterpret_cast<const char*>(base + start), end - start);
}

std::string HexWords(const uint8_t* base, uint32_t addr, uint32_t count) {
  std::string s;
  for (uint32_t i = 0; i < count; ++i) {
    if (!GuestReadable(base, addr + i * 4, 4)) {
      s += " ????????";
      continue;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), " %08X", Load32(base, addr + i * 4));
    s += buf;
  }
  return s;
}

// If addr holds a printable C string of 3+ chars, return it (truncated), else "".
std::string GuestString(const uint8_t* base, uint32_t addr) {
  if (addr < 0x00010000u || !GuestReadable(base, addr, 4)) return "";
  std::string s;
  for (uint32_t i = 0; i < 96; ++i) {
    if (!GuestReadable(base, addr + i, 1)) break;
    char c = static_cast<char>(base[addr + i]);
    if (c == 0) break;
    if (!std::isprint(static_cast<unsigned char>(c))) return "";
    s += c;
  }
  return s.size() >= 3 ? s : "";
}

// Prints an object's words, and for each word that is a pointer: the class of the object it
// points at and/or the string it points at. `depth` > 0 recurses one level into pointees.
void DumpObject(const uint8_t* base, const char* tag, uint32_t obj, uint32_t words, int depth) {
  if (!GuestReadable(base, obj, words * 4)) return;
  REXLOG_INFO("[spyro/menu] {} 0x{:08X} class {}:{}", tag, obj,
              ClassNameForVtable(base, Load32(base, obj)), HexWords(base, obj, words));
  for (uint32_t i = 1; i < words; ++i) {
    uint32_t p = Load32(base, obj + i * 4);
    if (p < 0x00010000u) continue;
    std::string str = GuestString(base, p);
    if (!str.empty()) REXLOG_INFO("[spyro/menu]   {} +0x{:02X} -> \"{}\"", tag, i * 4, str);
    if (GuestReadable(base, p, 4)) {
      std::string cls = ClassNameForVtable(base, Load32(base, p));
      if (cls != "?") {
        REXLOG_INFO("[spyro/menu]   {} +0x{:02X} -> 0x{:08X} class {}", tag, i * 4, p, cls);
        if (depth > 0) {
          std::string sub = std::string(tag) + "+" + std::to_string(i * 4);
          DumpObject(base, sub.c_str(), p, 24, depth - 1);
        }
      }
      // Also strings one indirection away (e.g. a std::string / XString holding a char*).
      uint32_t pp = Load32(base, p);
      std::string str2 = GuestString(base, pp);
      if (!str2.empty()) REXLOG_INFO("[spyro/menu]   {} +0x{:02X} -> -> \"{}\"", tag, i * 4, str2);
    }
  }
}

// --- the dump --------------------------------------------------------------------------------

std::atomic<uint32_t> g_last_dumped_rows{0};

void DumpMenuRows(const uint8_t* base, uint32_t menu) {
  if (!GuestReadable(base, menu, 0xB8)) return;
  uint32_t rows = Load32(base, menu + 0x84);
  uint32_t count = Load32(base, menu + 0x8C);
  // Dump once per distinct row array, so re-entering the menu with a rebuilt list shows up.
  if (g_last_dumped_rows.exchange(rows) == rows) return;

  REXLOG_INFO("[spyro/menu] ===== video menu 0x{:08X} ({}): rows array 0x{:08X}, count {}", menu,
              ClassNameForVtable(base, Load32(base, menu)), rows, count);
  REXLOG_INFO("[spyro/menu] menu object:{}", HexWords(base, menu, 0xB8 / 4));
  if (count > 32 || !GuestReadable(base, rows, count * 4)) return;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t row = Load32(base, rows + i * 4);
    if (!GuestReadable(base, row, 0x80)) {
      REXLOG_INFO("[spyro/menu] row {}: 0x{:08X} (unreadable)", i, row);
      continue;
    }
    uint32_t vt = Load32(base, row);
    REXLOG_INFO("[spyro/menu] row {}: object 0x{:08X} class {} vtable 0x{:08X} state {} value 0x{:08X}",
                i, row, ClassNameForVtable(base, vt), vt, base[row + 0x14], Load32(base, row + 0x10));
    REXLOG_INFO("[spyro/menu] row {} bytes:{}", i, HexWords(base, row, 0x80 / 4));
    // The Flash hotspot (+0x2C) and text accessor (+0x30) decide what the row looks like:
    // dump them two levels deep to find button instance names / text ids.
    std::string tag = "row" + std::to_string(i) + ".hotspot";
    DumpObject(base, tag.c_str(), Load32(base, row + 0x2C), 16, 0);
  }
}

// --- Flash UI / player dump (WORK_LOG 65-66) ---------------------------------------------------
//
// CKFlashUI (0x23C bytes): +0x04/+0x08/+0x0C/+0x10/+0x14/+0x18 element lists with counts at
// +0x1C..+0x30; +0x48..+0xE7 40-entry slot table; +0xEC player.
// Player: +0x08 node table {base,count,?,stride}; +0x0C dictionary {base,count} of 16-byte
// entries {kind, nameIndex, a, b}; +0x1C string pool {base,?,?,stride}; +0x24 display-tree
// root; +0x48 16-byte rect (+0x50/+0x54 = width/height).
// Tree node: u32 type at +0 (1 = container), flags +8, children: descriptor at +0xAC
// {base,count}, count also at +0xB0 (low 24 bits), children inline at 0x100-byte stride.

std::string PoolString(const uint8_t* base, uint32_t player, uint32_t index) {
  uint32_t pool = Load32(base, player + 0x1C);
  if (!GuestReadable(base, pool, 16)) return "";
  uint32_t pool_base = Load32(base, pool);
  uint32_t stride = Load32(base, pool + 0xC);
  if (stride == 0 || stride > 0x1000) return "";
  return GuestString(base, pool_base + stride * index);
}

void DumpTree(const uint8_t* base, uint32_t player, uint32_t node, int depth, uint32_t& budget) {
  if (budget == 0 || depth > 8 || !GuestReadable(base, node, 0x100)) return;
  --budget;
  uint32_t type = Load32(base, node);
  uint32_t desc = Load32(base, node + 0xAC);
  uint32_t count = Load32(base, node + 0xB0) & 0xFFFFFF;
  std::string indent(depth * 2, ' ');
  REXLOG_INFO("[spyro/flash] {}node 0x{:08X} type {} flags 0x{:08X} children {}:{}", indent, node,
              type, Load32(base, node + 8), type == 1 ? count : 0, HexWords(base, node, 0x40));
  // Any word in the node that indexes a pooled name: try each small value.
  for (uint32_t off = 4; off < 0x100; off += 4) {
    uint32_t v = Load32(base, node + off);
    if (v < 4096) {
      std::string s = PoolString(base, player, v);
      if (!s.empty() && s.size() >= 3) {
        REXLOG_INFO("[spyro/flash] {}  +0x{:02X} = {} -> pool \"{}\"", indent, off, v, s);
      }
    }
  }
  if (type == 1 && GuestReadable(base, desc, 8) && count < 512) {
    uint32_t child = Load32(base, desc);
    for (uint32_t i = 0; i < count; ++i) DumpTree(base, player, child + i * 0x100, depth + 1, budget);
  }
}

std::atomic<uint32_t> g_last_dumped_ui{0};

void DumpFlashUI(const uint8_t* base, uint32_t ui) {
  if (!GuestReadable(base, ui, 0x23C)) return;
  if (g_last_dumped_ui.exchange(ui) == ui) return;
  REXLOG_INFO("[spyro/flash] ===== CKFlashUI 0x{:08X} (class {})", ui,
              ClassNameForVtable(base, Load32(base, ui)));
  static const char* kListNames[6] = {"A", "B", "C", "D", "E", "F"};
  for (int l = 0; l < 6; ++l) {
    uint32_t arr = Load32(base, ui + 0x04 + l * 4);
    uint32_t cnt = Load32(base, ui + 0x1C + l * 4);
    REXLOG_INFO("[spyro/flash] list {} @+0x{:02X}: count {}", kListNames[l], 0x04 + l * 4, cnt);
    if (cnt > 64 || !GuestReadable(base, arr, cnt * 4)) continue;
    for (uint32_t i = 0; i < cnt; ++i) {
      uint32_t e = Load32(base, arr + i * 4);
      if (!GuestReadable(base, e, 0x48)) continue;
      std::string name = GuestString(base, Load32(base, e + 4));
      REXLOG_INFO("[spyro/flash]   {}[{}] 0x{:08X} class {} name \"{}\":{}", kListNames[l], i, e,
                  ClassNameForVtable(base, Load32(base, e)), name, HexWords(base, e, 0x48 / 4));
    }
  }
  REXLOG_INFO("[spyro/flash] slot table +0x48:{}", HexWords(base, ui + 0x48, 40));
  REXLOG_INFO("[spyro/flash] +0xE8 {:08X}  +0x218 {:08X}  +0x22C {:08X}  +0x230 {:08X}  +0x234 {:08X}",
              Load32(base, ui + 0xE8), Load32(base, ui + 0x218), Load32(base, ui + 0x22C),
              Load32(base, ui + 0x230), Load32(base, ui + 0x234));

  uint32_t player = Load32(base, ui + 0xEC);
  if (!GuestReadable(base, player, 0x60)) return;
  REXLOG_INFO("[spyro/flash] player 0x{:08X}:{}", player, HexWords(base, player, 0x60 / 4));

  uint32_t dict = Load32(base, player + 0x0C);
  if (GuestReadable(base, dict, 8)) {
    uint32_t dbase = Load32(base, dict);
    uint32_t dcount = Load32(base, dict + 4);
    REXLOG_INFO("[spyro/flash] dictionary: {} entries", dcount);
    for (uint32_t i = 0; i < dcount && i < 512; ++i) {
      uint32_t e = dbase + i * 16;
      if (!GuestReadable(base, e, 16)) break;
      uint32_t kind = Load32(base, e), idx = Load32(base, e + 4);
      REXLOG_INFO("[spyro/flash]   dict[{:3}] kind {} name[{}] \"{}\"  a={:08X} b={:08X}", i, kind, idx,
                  PoolString(base, player, idx), Load32(base, e + 8), Load32(base, e + 12));
    }
  }
  uint32_t nodes = Load32(base, player + 0x08);
  if (GuestReadable(base, nodes, 16)) {
    REXLOG_INFO("[spyro/flash] node table: base 0x{:08X} count {} f2 {:08X} stride {:08X}",
                Load32(base, nodes), Load32(base, nodes + 4), Load32(base, nodes + 8),
                Load32(base, nodes + 12));
    uint32_t nb = Load32(base, nodes), nc = Load32(base, nodes + 4);
    for (uint32_t i = 0; i < nc && i < 128; ++i) {
      uint32_t n = nb + 4 + i * 0x3C;
      if (!GuestReadable(base, n, 0x3C)) break;
      REXLOG_INFO("[spyro/flash]   node[{:3}] 0x{:08X}:{}", i, n, HexWords(base, n, 0x3C / 4));
    }
  }
  uint32_t budget = 400;
  DumpTree(base, player, Load32(base, player + 0x24), 0, budget);
}

// --- experiment: drive a menu label through the game's own text setter ------------------------

void Store16BE(uint8_t* base, uint32_t addr, uint16_t v) {
  base[addr] = static_cast<uint8_t>(v >> 8);
  base[addr + 1] = static_cast<uint8_t>(v & 0xFF);
}

// Direct child of the movie root with the given node id (ids from WORK_LOG 68:
// 3 container, 5 title text, 6 "Gamma" text, 7 "Retour"/Back text, -666 hidden container).
uint32_t FindRootChild(const uint8_t* base, uint32_t player, uint32_t id) {
  uint32_t root = Load32(base, player + 0x24);
  if (!GuestReadable(base, root, 0x100)) return 0;
  uint32_t desc = Load32(base, root + 0xAC);
  uint32_t count = Load32(base, root + 0xB0) & 0xFFFFFF;
  if (!GuestReadable(base, desc, 8) || count > 256) return 0;
  uint32_t first = Load32(base, desc);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t n = first + i * 0x100;
    if (GuestReadable(base, n, 8) && Load32(base, n + 4) == id) return n;
  }
  return 0;
}

// Screenshot of the game window's client area to shots\<name>.bmp, after a delay, from a
// background thread. Skipped (and logged) if the game window isn't in front at that moment.
void ScreenshotLater(int delay_ms, std::string name) {
#if defined(_WIN32)
  std::thread([delay_ms, name] {
    Sleep(delay_ms);
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) {
      REXLOG_INFO("[spyro/shot] {} skipped: game window not in front", name);
      return;
    }
    RECT rc{};
    GetClientRect(fg, &rc);
    POINT origin{0, 0};
    ClientToScreen(fg, &origin);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY);
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int stride = (w * 3 + 3) & ~3;
    std::string pixels(static_cast<size_t>(stride) * h, '\0');
    GetDIBits(mem, bmp, 0, h, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    CreateDirectoryA("shots", nullptr);
    std::string path = "shots\\" + name + ".bmp";
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bi);
    fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
      std::fwrite(&fh, sizeof(fh), 1, f);
      std::fwrite(&bi, sizeof(bi), 1, f);
      std::fwrite(pixels.data(), 1, pixels.size(), f);
      std::fclose(f);
      REXLOG_INFO("[spyro/shot] wrote {} ({}x{})", path, w, h);
    }
  }).detach();
#else
  (void)delay_ms;
  (void)name;
#endif
}

}  // namespace

extern "C" {

REX_EXTERN(sub_823A20D8);  // text node: set UTF-16 (big-endian u16) zero-terminated text

}  // extern "C"

namespace {

// Calls the game's text setter from inside a hook. The UTF-16 string is written to the guest
// stack well below the current frame, and the stack pointer is lowered past it for the call;
// every register is restored afterwards so the hooked function continues untouched.
void GuestSetNodeText(PPCContext& ctx, uint8_t* base, uint32_t node, const char16_t* text) {
  PPCContext saved = ctx;
  uint32_t buf = (ctx.r1.u32 - 0x2000u) & ~0xFu;
  uint32_t i = 0;
  for (; text[i]; ++i) Store16BE(base, buf + i * 2, static_cast<uint16_t>(text[i]));
  Store16BE(base, buf + i * 2, 0);
  ctx.r1.u64 = buf - 0x200u;
  ctx.r3.u64 = node;
  ctx.r4.u64 = buf;
  sub_823A20D8(ctx, base);
  ctx = saved;
}

// Experiment 2 (WORK_LOG 71): which text node is which, and does setting text stick?
//   on open      : log every list-B binding's node(s) and each text node's world position;
//                  screenshot "e2_before" at +0.8 s
//   at +1.5 s    : phase 1 - set "N5"/"N6"/"N7" on text nodes 5/6/7 ONCE, mark them dirty
//                  (flags |= 2); screenshot "e2_phase1" 0.5 s later
//   from +3.0 s  : phase 2 - re-apply the markers EVERY FRAME; screenshot "e2_phase2" at +4.5 s
// Phase 1 visible = setter works; only phase 2 visible = the menu rewrites labels each frame;
// neither = these nodes aren't what's on screen (or the setter needs more).
struct LabelExperiment {
  uint32_t rows = 0;
  std::chrono::steady_clock::time_point start;
  int phase = 0;
};
LabelExperiment g_label_experiment;

float LoadF32(const uint8_t* base, uint32_t addr) {
  uint32_t v = Load32(base, addr);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

void LogTextBindings(const uint8_t* base, uint32_t ui, uint32_t player) {
  uint32_t arr = Load32(base, ui + 0x08), cnt = Load32(base, ui + 0x20);
  for (uint32_t i = 0; i < cnt && i < 16; ++i) {
    uint32_t e = Load32(base, arr + i * 4);
    if (!GuestReadable(base, e, 0x48)) continue;
    uint32_t nodes = Load32(base, e + 0x24), ncount = Load32(base, e + 0x2C);
    std::string list;
    for (uint32_t j = 0; j < ncount && j < 8 && GuestReadable(base, nodes + j * 4, 4); ++j) {
      uint32_t n = Load32(base, nodes + j * 4);
      char buf[64];
      std::snprintf(buf, sizeof(buf), " 0x%08X(id %d)", n,
                    GuestReadable(base, n, 8) ? static_cast<int>(Load32(base, n + 4)) : -9999);
      list += buf;
    }
    REXLOG_INFO("[spyro/exp] binding B[{}] \"{}\" -> {} node(s):{}  text id +0x40 = {}", i,
                GuestString(base, Load32(base, e + 4)), ncount, list, Load32(base, e + 0x40));
  }
  for (uint32_t id : {3u, 5u, 6u, 7u, 0xFFFFFD66u}) {
    uint32_t n = FindRootChild(base, player, id);
    if (!n) continue;
    REXLOG_INFO("[spyro/exp] node id {} 0x{:08X} type {} flags 0x{:X} local ({:.2f},{:.2f}) world ({:.4f},{:.4f})",
                static_cast<int32_t>(id), n, Load32(base, n), Load32(base, n + 8),
                LoadF32(base, n + 0x3C), LoadF32(base, n + 0x40), LoadF32(base, n + 0x7C),
                LoadF32(base, n + 0x80));
  }
}

void Store32(uint8_t* base, uint32_t addr, uint32_t v) {
  base[addr] = v >> 24; base[addr + 1] = v >> 16; base[addr + 2] = v >> 8; base[addr + 3] = v;
}

// Text node draw (Function_823A21F0, WORK_LOG 72) draws node+0xAC every frame with font
// node+0xBC when (flags & 0x1000004) == 4; text is 16-bit when (font->+4)->+0x10 & 2.
void LogTextNode(const uint8_t* base, uint32_t player, uint32_t id, const char* when) {
  uint32_t n = FindRootChild(base, player, id);
  if (!n) return;
  uint32_t text = Load32(base, n + 0xAC), font = Load32(base, n + 0xBC);
  uint32_t font_inner = GuestReadable(base, font, 8) ? Load32(base, font + 4) : 0;
  uint32_t font_flags = GuestReadable(base, font_inner, 0x14) ? Load32(base, font_inner + 0x10) : 0;
  std::string utf16, ascii;
  for (uint32_t i = 0; i < 48 && GuestReadable(base, text + i * 2, 2); ++i) {
    uint16_t c = static_cast<uint16_t>((base[text + i * 2] << 8) | base[text + i * 2 + 1]);
    if (c == 0) break;
    utf16 += (c < 128 && std::isprint(c)) ? static_cast<char>(c) : '?';
  }
  for (uint32_t i = 0; i < 48 && GuestReadable(base, text + i, 1) && base[text + i]; ++i)
    ascii += std::isprint(base[text + i]) ? static_cast<char>(base[text + i]) : '?';
  REXLOG_INFO("[spyro/exp] {} node {} flags 0x{:X} text@0x{:08X} cap {} font 0x{:08X} inner 0x{:08X} "
              "fontflags 0x{:X} ({}-bit) utf16\"{}\" ascii\"{}\" bytes:{}",
              when, id, Load32(base, n + 8), text, Load32(base, n + 0xB4), font, font_inner,
              font_flags, (font_flags & 2) ? 16 : 8, utf16, ascii, HexWords(base, text, 6));
}

void RunLabelExperiment(PPCContext& ctx, uint8_t* base, uint32_t menu) {
  uint32_t rows = Load32(base, menu + 0x84);
  auto now = std::chrono::steady_clock::now();
  uint32_t ui = Load32(base, menu + 0x0C);
  uint32_t player = GuestReadable(base, ui, 0x100) ? Load32(base, ui + 0xEC) : 0;
  if (!player) return;
  auto ms = [&](int v) { return std::chrono::milliseconds(v); };
  auto elapsed = now - g_label_experiment.start;
  if (g_label_experiment.rows != rows) {  // menu (re)opened
    g_label_experiment = LabelExperiment{rows, now, 0};
    LogTextBindings(base, ui, player);
    for (uint32_t id : {5u, 6u, 7u}) LogTextNode(base, player, id, "open");
    ScreenshotLater(800, "e3_before");
    return;
  }
  uint32_t title = FindRootChild(base, player, 7);
  uint32_t gamma = FindRootChild(base, player, 6);
  uint32_t ret = FindRootChild(base, player, 5);
  if (g_label_experiment.phase == 0 && elapsed >= ms(1500) && title) {
    g_label_experiment.phase = 1;
    Store32(base, title + 8, Load32(base, title + 8) & ~4u);  // hide title
    REXLOG_INFO("[spyro/exp] phase 1: title node hidden (flags 0x{:X})", Load32(base, title + 8));
    ScreenshotLater(500, "e3_phase1_title_hidden");
  } else if (g_label_experiment.phase == 1 && elapsed >= ms(3000) && title && gamma) {
    g_label_experiment.phase = 2;
    Store32(base, title + 8, Load32(base, title + 8) | 4u);  // show title again
    float x;
    uint32_t xb = Load32(base, gamma + 0x3C);
    std::memcpy(&x, &xb, 4);
    x += 100.0f;
    std::memcpy(&xb, &x, 4);
    Store32(base, gamma + 0x3C, xb);
    Store32(base, gamma + 8, Load32(base, gamma + 8) | 2u);  // dirty -> world matrix rebuilt
    REXLOG_INFO("[spyro/exp] phase 2: title shown, gamma label moved to x={:.2f}", x);
    ScreenshotLater(500, "e3_phase2_gamma_moved");
  } else if (g_label_experiment.phase == 2 && elapsed >= ms(4500) && ret) {
    g_label_experiment.phase = 3;
    GuestSetNodeText(ctx, base, ret, u"Recomp Settings");
    LogTextNode(base, player, 5, "after-set");
    ScreenshotLater(500, "e3_phase3_return_text");
  }
}

// --- self-driven dump of the display menu's row widgets (no menu visit needed) ------------------
//
// The video menu object is created at boot (array ctor 0x822E0AD8). If its rows already exist
// then, they can be dumped from the menu framework's Update while the title screen is up.
// CKInterfaceButton (WORK_LOG 74): visual pieces at +0x34 and +0x38/+0x3C/+0x40, five 0xA0-byte
// state records ending at +0x3B0, ref at +0x3B0; hotspot (+0x2C) +0x10 = array of 16-byte
// {piece, x, y, ?} entries, count at hotspot +0x18.

std::atomic<uint32_t> g_video_menu{0};
std::atomic<bool> g_rows_dumped{false};
std::chrono::steady_clock::time_point g_boot_time = std::chrono::steady_clock::now();

void DumpPiece(const uint8_t* base, const std::string& tag, uint32_t p) {
  if (!GuestReadable(base, p, 0x100)) {
    if (p) REXLOG_INFO("[spyro/rows] {} 0x{:08X} unreadable", tag, p);
    return;
  }
  REXLOG_INFO("[spyro/rows] {} 0x{:08X} vt 0x{:08X}:{}", tag, p, Load32(base, p), HexWords(base, p, 64));
  for (uint32_t off = 4; off < 0x100; off += 4) {
    uint32_t q = Load32(base, p + off);
    std::string s = GuestString(base, q);
    if (!s.empty()) REXLOG_INFO("[spyro/rows]   {} +0x{:02X} -> \"{}\"", tag, off, s);
    if (GuestReadable(base, q, 8)) {
      std::string s2 = GuestString(base, Load32(base, q));
      if (!s2.empty()) REXLOG_INFO("[spyro/rows]   {} +0x{:02X} -> -> \"{}\"", tag, off, s2);
    }
  }
}

void DumpRowWidgets(const uint8_t* base, uint32_t menu) {
  uint32_t rows = Load32(base, menu + 0x84), count = Load32(base, menu + 0x8C);
  REXLOG_INFO("[spyro/rows] video menu 0x{:08X}: rows 0x{:08X} count {}", menu, rows, count);
  if (count == 0 || count > 24 || !GuestReadable(base, rows, count * 4)) return;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t r = Load32(base, rows + i * 4);
    if (!GuestReadable(base, r, 0x3C0)) continue;
    std::string t = "row" + std::to_string(i);
    REXLOG_INFO("[spyro/rows] {} 0x{:08X} vt 0x{:08X} head:{}", t, r, Load32(base, r), HexWords(base, r, 0x50 / 4));
    REXLOG_INFO("[spyro/rows] {} tail +0x3A0:{}", t, HexWords(base, r + 0x3A0, 8));
    for (uint32_t s = 0; s < 5; ++s) {
      uint32_t rec = r + 0x3B0 - 0xA0 * (5 - s);
      REXLOG_INFO("[spyro/rows] {} state[{}] @+0x{:03X}:{}", t, s, rec - r, HexWords(base, rec, 0xA0 / 4));
    }
    DumpPiece(base, t + ".+34", Load32(base, r + 0x34));
    DumpPiece(base, t + ".+38", Load32(base, r + 0x38));
    DumpPiece(base, t + ".+3C", Load32(base, r + 0x3C));
    DumpPiece(base, t + ".+40", Load32(base, r + 0x40));
    DumpPiece(base, t + ".+3B0", Load32(base, r + 0x3B0));
    uint32_t hs = Load32(base, r + 0x2C);
    if (GuestReadable(base, hs, 0x40)) {
      uint32_t arr = Load32(base, hs + 0x10), n = Load32(base, hs + 0x18);
      REXLOG_INFO("[spyro/rows] {} hotspot 0x{:08X}: {} entries at 0x{:08X}", t, hs, n, arr);
      for (uint32_t k = 0; k < n && k < 16 && GuestReadable(base, arr + k * 16, 16); ++k) {
        uint32_t piece = Load32(base, arr + k * 16);
        REXLOG_INFO("[spyro/rows]   entry {}: piece 0x{:08X} (vt 0x{:08X}) {}", k, piece,
                    GuestReadable(base, piece, 4) ? Load32(base, piece) : 0, HexWords(base, arr + k * 16, 4));
      }
    }
  }
}

}  // namespace

extern "C" {

REX_EXTERN(__imp__sub_822E0AD8);
REX_EXTERN(__imp__sub_822C11E0);
// CText2d text layout SetText(layout = text2d + 0x28, UTF-16BE string): copies the string,
// rebuilds glyphs, measures and lays out (WORK_LOG 76).
REX_EXTERN(sub_8211B368);

}  // extern "C"

namespace {

// Calls a guest function f(r3, r4 = UTF-16 string) from inside a hook (same technique as
// GuestSetNodeText: string on the guest stack below the frame, all registers restored).
void GuestCallWithString(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*),
                         uint32_t r3, const char16_t* text) {
  PPCContext saved = ctx;
  uint32_t buf = (ctx.r1.u32 - 0x2000u) & ~0xFu;
  uint32_t i = 0;
  for (; text[i]; ++i) Store16BE(base, buf + i * 2, static_cast<uint16_t>(text[i]));
  Store16BE(base, buf + i * 2, 0);
  ctx.r1.u64 = buf - 0x200u;
  ctx.r3.u64 = r3;
  ctx.r4.u64 = buf;
  fn(ctx, base);
  ctx = saved;
}

constexpr uint32_t kText2dVtable = 0x8206A0F0;
constexpr uint32_t kButtonVtable = 0x82078720;  // CKInterfaceButton

void Store32F(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  Store32(base, addr, v);
}

}  // namespace

extern "C" {
REX_EXTERN(sub_8260DC40);  // operator new(size) -> r3
REX_EXTERN(sub_822C8DF0);  // CKInterfaceButtonDouble: show the current value's text
REX_EXTERN(sub_822C2B50);  // Menu::SendRowEvent(menu, row, event, flag)
}

namespace {

// Calls guest function fn(a0, a1, a2) from inside a hook on a fresh stack area below the
// current frame; returns r3 and restores every register.
uint32_t GuestCall(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*), uint32_t a0,
                   uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
  PPCContext saved = ctx;
  ctx.r1.u64 = (ctx.r1.u32 - 0x400u) & ~0xFu;
  ctx.r3.u64 = a0;
  ctx.r4.u64 = a1;
  ctx.r5.u64 = a2;
  ctx.r6.u64 = a3;
  fn(ctx, base);
  uint32_t ret = ctx.r3.u32;
  ctx = saved;
  return ret;
}

// ===== The native "Recomp Settings" row in the display menu (WORK_LOG 80) =====================
//
// Built once at boot (the video menu and its rows exist then), from copies of the game's own
// objects, so the game's menu code updates, navigates and draws it like any other row:
//   label   = CText2d (0x748) copied from the Gamma label, unlinked from loc/sync targets
//             (+0x740/+0x744 = 0), inserted after the Gamma label in the CScene2d draw list
//             (piece +0x08 scene, +0x0C prev, +0x10 next; scene +0x18 head, +0x1C tail,
//             +0x20 count). CText2d owns no heap memory, so a byte copy is safe.
//   hotspot = CFlashHotSpot (0x40) copied from Back's, with its own 1-entry piece list.
//   row     = CKInterfaceButton (0x3C0) copied from Back: +0x2C hotspot, +0x34 label, no
//             other pieces. It SHARES Back's per-state heap arrays, so it must never be
//             destroyed: the video menu destructor hook removes it from the row list first.
//   Appended as row 2 of the video menu (row array capacity 3).
// Each frame the label is placed under the Gamma label; its text is set each time the menu
// opens (glyphs only build correctly while the menu is active - WORK_LOG 78/79).
struct RecompRow {
  uint32_t menu = 0, row = 0, label = 0, hotspot = 0, gamma_label = 0;
  uint32_t gamma_row = 0;
  uint32_t glow[2] = {0, 0};        // our copies of Gamma's highlight glow billboards
  uint32_t gamma_glow[2] = {0, 0};  // Gamma row +0x58 / +0x5C
  uint32_t str = 0;                 // our label's UTF-16BE string buffer (kLabelChars)
  // The separate Recomp Settings menu (WORK_LOG 93): the same video menu object, re-entered
  // as game menu state kRecompMenuId with its own row list {our row, Back}.
  uint32_t mgr = 0, back_row = 0;
  uint32_t normal_rows = 0, normal_cap = 0, recomp_rows = 0;
  // The host menu's own list: row count, its Back row's index, the row selected when it shows
  // again. Display Options {Gamma, Back, Recomp}: 3 / 1 / 2. In a level the host is Sound
  // Options {Music, Voices, Sound FX, Back}: 4 / 3 / 0, with no Recomp row of its own.
  uint32_t normal_count = 3;
  uint8_t back_index = 1, normal_sel = 2;
  uint8_t back_nav[4] = {0, 0, 0, 0};
  bool recomp_mode = false, text_dirty = false;
  bool built = false, failed = false;
};
RecompRow g_recomp;
std::atomic<int> g_current_menu_id{-1};  // spyro::CurrentMenuId()
// The game manager (CKS08GameManager, menu +0x08). It lives for the whole session, while the
// menu objects are replaced with each menu set - a stale menu's +0x08 is not safe to read.
uint32_t g_manager = 0;
// Every Display Options object that got our row (a new one is built with each menu set).
std::vector<uint32_t> g_row_menus;
constexpr uint32_t kLabelChars = 0x40;
// Game manager menu states (WORK_LOG 92/93): the manager's state-object array is mgr+0x12CC
// (0x25 slots; handlers[id] for id 0x00..0x23 all forward to arr[id]). 0x21 is the video menu
// (CKS08MenuOptionVideo360); 0x22 is an EMPTY slot we point at the same object, so entering
// state 0x22 re-opens the video menu with the game's own transitions as "Recomp Settings".
constexpr uint32_t kStateArray = 0x12CC;
constexpr uint8_t kVideoMenuId = 0x21;
constexpr uint8_t kRecompMenuId = 0x22;
// Offset from the Gamma row. Normalised menu coordinates grow UPWARD; the user wants the
// Recomp row ABOVE Gamma, so positive.
constexpr float kRecompRowDy = 0.085f;
constexpr uint32_t kBillboardSize = 0x84;

// Snapshot of each Gamma glow billboard taken while GAMMA is highlighted, so our glow can look
// exactly like it when OUR row is highlighted (the glow's look/alpha lives in the billboard).
uint8_t g_glow_snapshot[2][kBillboardSize];
bool g_glow_snapshot_ok[2] = {false, false};
// ...and of Gamma's glow render-state records (+0x2C 0x1C bytes, +0x30 0x80 bytes). They hold
// which image to draw and are only filled with the glow images once the display menu is open;
// our copies taken at boot still held the Options menu's stone plates (WORK_LOG 89).
uint8_t g_rec2c_snapshot[2][0x1C];
uint8_t g_rec30_snapshot[2][0x80];

// Inserts piece p into its scene's draw list right BEFORE piece `before` (drawn earlier =
// behind). Piece +0x0C prev, +0x10 next; scene +0x18 head, +0x20 count.
void InsertPieceBefore(uint8_t* base, uint32_t scene, uint32_t p, uint32_t before) {
  uint32_t prev = Load32(base, before + 0x0C);
  Store32(base, p + 0x0C, prev);
  Store32(base, p + 0x10, before);
  Store32(base, before + 0x0C, p);
  if (prev) Store32(base, prev + 0x10, p);
  else Store32(base, scene + 0x18, p);
  Store32(base, scene + 0x20, Load32(base, scene + 0x20) + 1);
}

// ===== The Recomp Settings rows (WORK_LOG 95) =================================================
//
// Each setting is a copy of the Gamma row, a CKInterfaceButtonDouble (0x410 bytes) = a native
// left/right PICKER that draws its own label, value text, two arrows and highlight glow.
// Picker fields: +0x30 label text accessor, +0x34 label CText2d, +0x40 value CText2d,
// +0x50/+0x54 arrow billboards, +0x58/+0x5C glow billboards, +0x330 value count (byte),
// +0x334 value entries (0x2C each; entry+0 = text accessor), +0x338 current index (byte),
// +0x350 1 = left pressed, +0x40C low 5 bits = which game option it edits (3 = none; Gamma's
// writes the real gamma), +0x40D bit 0 = wrap around, bit 1 = hide the arrow at either end.
// The menu sends left/right to it as events 3/4 (Function_822C16E8); it moves +0x338 itself
// and redraws the value (Function_822C81D0 / 822C8DF0).
constexpr uint32_t kDoubleSize = 0x410;
constexpr uint32_t kText2dSize = 0x748;
// Setting names, choices and stored values live in spyro_settings.cpp.
using spyro::kNumSettings;
using spyro::SettingDef;
// Layout relative to the Gamma row (normalised, y grows UPWARD): first row this far above
// Gamma, then one row every kSettingStep downwards; arrows pushed this much further out so
// longer values ("Borderless") fit between them.
constexpr float kSettingsTop = 0.18f;
// The rows share the space 8 rows at 0.068 used to fill (down to just above the Return button).
constexpr float kSettingStep = 0.068f * 7 / (kNumSettings - 1);
constexpr float kArrowGapExtra = 0.16f;  // right arrow this much further right than Gamma's
// Value text centring (calibrated on screenshots, WORK_LOG 96): the value text is RIGHT-aligned
// and its right edge sits kTextRightOffset right of the arrows' centre when x = that centre;
// one "width unit" of text (see TextWidth) is kTextUnit wide.
constexpr float kTextRightOffset = -0.0066f;
constexpr float kTextUnit = 0.01345f;

// Rough rendered width of a value in kTextUnit units for the menu font.
float TextWidth(const char16_t* s) {
  float w = 0;
  for (; *s; ++s) {
    char16_t c = *s;
    if (c == u'W' || c == u'M') w += 1.8f;
    else if (c >= u'A' && c <= u'Z') w += 1.5f;
    else if (c >= u'0' && c <= u'9') w += 1.2f;
    else if (c == u'm' || c == u'w') w += 1.5f;
    else if (c == u'i' || c == u'l' || c == u' ' || c == u'.' || c == u'r' || c == u't' || c == u'j') w += 0.55f;
    else w += 1.0f;
  }
  return w * kTextUnit;
}

struct SettingRow {
  uint32_t row = 0;
  // our piece -> the Gamma piece it mirrors (position source); [0] label, [1] value,
  // [2]/[3] arrows, [4]/[5] glows
  uint32_t ours[6] = {}, gammas[6] = {};
  uint32_t label_str = 0;
  uint32_t values = 0;  // the picker's value entries (+0x334); entry+0 = that value's accessor
  int value = 0;
};
SettingRow g_settings[kNumSettings];
bool g_settings_built = false;
// The rows are laid out from the host's first row (Gamma). In a level the host is Sound Options,
// whose first row sits elsewhere: g_host_dy moves the list to where it is on Display Options
// (g_settings_anchor_y = the Gamma label's y).
float g_settings_anchor_y = -1.0f;
float g_host_dy = 0.0f;
// A game text source (CKLocTextAccessor) + its CKStdText, from the Gamma label: MakeGameText
// copies them for our own texts.
uint32_t g_text_template[2] = {0, 0};

// A text the game can show: CKLocTextAccessor (copy of `gsource`) -> CKStdText (copy of
// `gstd`) -> our UTF-16BE buffer (kLabelChars). Returns the accessor; *str_out = the buffer.
uint32_t MakeGameText(PPCContext& ctx, uint8_t* base, uint32_t gsource, uint32_t gstd,
                      const char16_t* text, uint32_t* str_out = nullptr) {
  uint32_t source = GuestCall(ctx, base, sub_8260DC40, 0x14);
  uint32_t stdtext = GuestCall(ctx, base, sub_8260DC40, 0x40);
  uint32_t str = GuestCall(ctx, base, sub_8260DC40, kLabelChars * 2);
  if (!source || !stdtext || !str) return 0;
  uint32_t i = 0;
  for (; text[i] && i < kLabelChars - 1; ++i) Store16BE(base, str + i * 2, static_cast<uint16_t>(text[i]));
  Store16BE(base, str + i * 2, 0);
  std::memcpy(base + stdtext, base + gstd, 0x40);
  Store32(base, stdtext + 0x18, str);
  std::memcpy(base + source, base + gsource, 0x14);
  Store32(base, source + 0x04, 0);
  Store32(base, source + 0x10, stdtext);
  if (str_out) *str_out = str;
  return source;
}

// Copy of a CText2d with its own text accessor, linked into the scene list after `src`.
uint32_t CloneText2d(PPCContext& ctx, uint8_t* base, uint32_t scene, uint32_t src, uint32_t accessor) {
  uint32_t t = GuestCall(ctx, base, sub_8260DC40, kText2dSize);
  if (!t) return 0;
  std::memcpy(base + t, base + src, kText2dSize);
  Store32(base, t + 0x73C, accessor);
  Store32(base, t + 0x740, 0);
  Store32(base, t + 0x744, 0);
  uint32_t next = Load32(base, src + 0x10);
  Store32(base, t + 0x0C, src);
  Store32(base, t + 0x10, next);
  Store32(base, src + 0x10, t);
  if (next) Store32(base, next + 0x0C, t);
  else Store32(base, scene + 0x1C, t);
  Store32(base, scene + 0x20, Load32(base, scene + 0x20) + 1);
  return t;
}

// Copy of a CBillboard2d with its own render-state records (+0x2C 0x1C bytes, +0x30 0x80 bytes;
// sharing them made two sprites fight over one look - WORK_LOG 88), linked before `before`.
uint32_t CloneBillboard(PPCContext& ctx, uint8_t* base, uint32_t scene, uint32_t src, uint32_t before) {
  uint32_t g = GuestCall(ctx, base, sub_8260DC40, kBillboardSize);
  if (!g) return 0;
  std::memcpy(base + g, base + src, kBillboardSize);
  for (auto [off, size] : {std::pair<uint32_t, uint32_t>{0x2C, 0x1C}, {0x30, 0x80}}) {
    uint32_t rec = Load32(base, g + off);
    if (!GuestReadable(base, rec, size)) continue;
    uint32_t own = GuestCall(ctx, base, sub_8260DC40, size);
    if (!own) continue;
    std::memcpy(base + own, base + rec, size);
    Store32(base, g + off, own);
  }
  InsertPieceBefore(base, scene, g, before);
  return g;
}

// Builds the kNumSettings picker rows from the Gamma row. Row i's neighbours: up i-1, down
// i+1 (the last one's down = Back, at index kNumSettings in the Recomp row list).
bool BuildSettingsRows(PPCContext& ctx, uint8_t* base, uint32_t gamma) {
  uint32_t glabel = Load32(base, gamma + 0x34), gvalue = Load32(base, gamma + 0x40);
  uint32_t ghot = Load32(base, gamma + 0x2C);
  uint32_t gbill[4] = {Load32(base, gamma + 0x50), Load32(base, gamma + 0x54), Load32(base, gamma + 0x58),
                       Load32(base, gamma + 0x5C)};
  if (!GuestReadable(base, gamma, kDoubleSize) || !GuestReadable(base, glabel, kText2dSize) ||
      !GuestReadable(base, gvalue, kText2dSize) || !GuestReadable(base, ghot, 0x40)) {
    REXLOG_INFO("[spyro/settings] Gamma row pieces not readable; no settings rows");
    return false;
  }
  for (uint32_t b : gbill) {
    if (!GuestReadable(base, b, kBillboardSize)) {
      REXLOG_INFO("[spyro/settings] Gamma billboard 0x{:08X} not readable; no settings rows", b);
      return false;
    }
  }
  uint32_t hot_entries = Load32(base, ghot + 0x10), hot_count = Load32(base, ghot + 0x18);
  if (hot_count == 0 || hot_count > 8 || !GuestReadable(base, hot_entries, hot_count * 0x10)) return false;
  uint32_t scene = Load32(base, glabel + 0x08);
  uint32_t gsource = Load32(base, glabel + 0x73C);
  if (!GuestReadable(base, scene, 0x28) || !GuestReadable(base, gsource, 0x14)) return false;
  uint32_t gstd = Load32(base, gsource + 0x04) ? Load32(base, Load32(base, gsource + 0x04))
                                                 : Load32(base, gsource + 0x10);
  if (!GuestReadable(base, gstd, 0x40)) return false;
  g_text_template[0] = gsource;  // for texts made elsewhere (Exit to Desktop on the main menu)
  g_text_template[1] = gstd;

  for (int i = 0; i < kNumSettings; ++i) {
    const SettingDef& def = spyro::Def(i);
    SettingRow& s = g_settings[i];
    s.value = spyro::GetSetting(i);  // the saved choice (recomp_settings.ini)
    uint32_t row = GuestCall(ctx, base, sub_8260DC40, kDoubleSize);
    uint32_t hot = GuestCall(ctx, base, sub_8260DC40, 0x40);
    uint32_t entries = GuestCall(ctx, base, sub_8260DC40, hot_count * 0x10);
    uint32_t values = GuestCall(ctx, base, sub_8260DC40, def.count * 0x2C);
    uint32_t label_src = MakeGameText(ctx, base, gsource, gstd, def.name, &s.label_str);
    if (!row || !hot || !entries || !values || !label_src) return false;
    std::memset(base + values, 0, def.count * 0x2C);
    for (int v = 0; v < def.count; ++v) {
      uint32_t acc = MakeGameText(ctx, base, gsource, gstd, def.values[v]);
      if (!acc) return false;
      Store32(base, values + v * 0x2C, acc);
    }
    uint32_t label = CloneText2d(ctx, base, scene, glabel, label_src);
    // The value is a copy of the LABEL text, not of Gamma's value: that one's font only has
    // digits ("30" showed, "reXglue"/"On" did not).
    uint32_t value = CloneText2d(ctx, base, scene, glabel, Load32(base, values + s.value * 0x2C));
    if (!label || !value) return false;
    // +6 = text anchor mode; 2 = right-aligned at x (calibrated, WORK_LOG 96), so
    // PlaceSettingsRows can centre the value between the arrows from its estimated width.
    base[value + 6] = 2;
    base[value + 7] = base[gvalue + 7];
    uint32_t bill[4];
    for (int b = 0; b < 4; ++b) {
      bill[b] = CloneBillboard(ctx, base, scene, gbill[b], label);
      if (!bill[b]) return false;
    }

    // Hotspot: Gamma's, entries pointing at our pieces, unlinked from Gamma's anchor.
    std::memcpy(base + hot, base + ghot, 0x40);
    Store32(base, hot + 0x04, 0);
    Store32(base, hot + 0x0C, 0);
    std::memcpy(base + entries, base + hot_entries, hot_count * 0x10);
    for (uint32_t e = 0; e < hot_count; ++e) {
      uint32_t p = Load32(base, entries + e * 0x10);
      if (p == glabel) p = label;
      else if (p == gvalue) p = value;
      for (int b = 0; b < 4; ++b)
        if (p == gbill[b]) p = bill[b];
      Store32(base, entries + e * 0x10, p);
    }
    Store32(base, hot + 0x10, entries);

    std::memcpy(base + row, base + gamma, kDoubleSize);
    Store32(base, row + 0x2C, hot);
    Store32(base, row + 0x30, label_src);
    Store32(base, row + 0x34, label);
    Store32(base, row + 0x40, value);
    Store32(base, row + 0x4C, 0);
    for (int b = 0; b < 4; ++b) Store32(base, row + 0x50 + b * 4, bill[b]);
    base[row + 0x14] = 0;
    base[row + 0x330] = static_cast<uint8_t>(def.count);
    Store32(base, row + 0x334, values);
    base[row + 0x338] = static_cast<uint8_t>(s.value);
    base[row + 0x350] = 0;
    base[row + 0x40C] = (base[row + 0x40C] & ~0x1Fu) | 3;  // edits no game option
    base[row + 0x40D] = (base[row + 0x40D] | 1u) & ~2u;   // wrap around, arrows always shown
    base[row + 4 + 0] = i == 0 ? 0xFF : static_cast<uint8_t>(i - 1);
    base[row + 4 + 1] = static_cast<uint8_t>(i + 1);
    base[row + 4 + 2] = 0xFF;
    base[row + 4 + 3] = 0xFF;

    s.row = row;
    s.values = values;
    uint32_t ours[6] = {label, value, bill[0], bill[1], bill[2], bill[3]};
    uint32_t gammas[6] = {glabel, gvalue, gbill[0], gbill[1], gbill[2], gbill[3]};
    std::memcpy(s.ours, ours, sizeof(ours));
    std::memcpy(s.gammas, gammas, sizeof(gammas));
    REXLOG_INFO("[spyro/settings] row {} '{}' = 0x{:08X} ({} values)", i,
                std::string(def.name, def.name + std::char_traits<char16_t>::length(def.name)), row, def.count);
  }
  return true;
}

// CText2d text layout (text2d + 0x28, Function_8211C170): +0x08 box width, +0x334 word count,
// +0x338 word pointers (into its own string at +0x14), +0x428 word x/y (8 bytes each),
// +0x6C0 font, +0x6C4 font size. Logged whenever a label's word layout changes - to catch the
// "squashed text" the user sometimes sees in the in-level Recomp menu.
void LogLabelLayouts(const uint8_t* base) {
  static std::string s_last[kNumSettings];
  for (int i = 0; i < kNumSettings; ++i) {
    uint32_t L = g_settings[i].ours[0] + 0x28;
    uint32_t words = Load32(base, L + 0x334);
    const SettingRow& r = g_settings[i];
    std::string s = fmt::format("x {:.3f} (host label {:.3f} value {:.3f} arrows {:.3f} {:.3f}) align {} {} box {:.3f} "
                                "font 0x{:08X} size {:.3f} words {}:",
                                LoadF32(base, r.ours[0] + 0x14), LoadF32(base, r.gammas[0] + 0x14),
                                LoadF32(base, r.gammas[1] + 0x14), LoadF32(base, r.gammas[2] + 0x14),
                                LoadF32(base, r.gammas[3] + 0x14), base[r.ours[0] + 6], base[r.ours[0] + 7],
                                LoadF32(base, L + 0x08),
                                Load32(base, L + 0x6C0), LoadF32(base, L + 0x6C4), words);
    for (uint32_t w = 0; w < words && w < 4; ++w)
      s += fmt::format(" ({:.3f},{:.3f} @{:08X})", LoadF32(base, L + 0x428 + w * 8), LoadF32(base, L + 0x42C + w * 8),
                       Load32(base, L + 0x338 + w * 4));
    if (s != s_last[i]) {
      s_last[i] = s;
      REXLOG_INFO("[spyro/settings] label {} layout (piece 0x{:08X}, text at 0x{:08X}): {}", i, g_settings[i].ours[0],
                  L + 0x14, s);
    }
  }
}

// Per frame in the Recomp menu: each setting row mirrors the Gamma row's piece positions,
// moved down to its own line; the value centred between the arrows, which keep a fixed look.
void PlaceSettingsRows(uint8_t* base) {
  if (!g_settings_built) return;
  LogLabelLayouts(base);
  // Log what each value text object really holds (inline UTF-16BE at CText2d +0x3C) whenever
  // it changes, next to the index the picker is on - the ground truth for the screen.
  static std::string s_shown[kNumSettings];
  for (int i = 0; i < kNumSettings; ++i) {
    std::string shown;
    for (uint32_t c = 0; c < 32; ++c) {
      uint16_t ch = static_cast<uint16_t>(base[g_settings[i].ours[1] + 0x3C + c * 2] << 8 |
                                          base[g_settings[i].ours[1] + 0x3C + c * 2 + 1]);
      if (!ch) break;
      shown += ch < 0x80 ? static_cast<char>(ch) : '?';
    }
    if (shown != s_shown[i]) {
      s_shown[i] = shown;
      REXLOG_INFO("[spyro/settings] row {} shows '{}' (index {})", i, shown, base[g_settings[i].row + 0x338]);
    }
  }
  // In a level the host is Sound Options: until the game has shown that screen once, its row
  // pieces sit where the scene file has them, not where the screen arranges them (label x 0.418
  // instead of 0.339, arrows 0.559 / 0.615 instead of 0.550 / 0.623) - our labels sat on the
  // left arrows, against the values ("squashed text", WORK_LOG 117). Its value x (0.587) is the
  // same either way, so label and arrows are placed from it at the arranged screen's offsets.
  constexpr float kSoundLabelDx = -0.248f, kSoundArrowDx[2] = {-0.037f, 0.036f};
  bool sound_host = g_recomp.back_index == 3;
  for (int i = 0; i < kNumSettings; ++i) {
    const SettingRow& s = g_settings[i];
    float dy = kSettingsTop - kSettingStep * i + g_host_dy;
    float vx = LoadF32(base, s.gammas[1] + 0x14);
    // Arrows: the left one stays where Gamma's is (clear of the label), the right one moves
    // out so long values fit. The value is centred between them.
    float ax[2] = {LoadF32(base, s.gammas[2] + 0x14), LoadF32(base, s.gammas[3] + 0x14)};
    if (sound_host) {
      ax[0] = vx + kSoundArrowDx[0];
      ax[1] = vx + kSoundArrowDx[1];
    }
    for (float& x : ax)
      if (x > vx) x += kArrowGapExtra;
    float centre = (ax[0] + ax[1]) * 0.5f;
    const SettingDef& def = spyro::Def(i);
    int v = base[s.row + 0x338];
    float value_x = centre - kTextRightOffset + TextWidth(def.values[v < def.count ? v : 0]) * 0.5f;
    static const bool s_calib = std::getenv("SPYRO_TEXTCALIB") != nullptr;
    if (s_calib) value_x = 0.60f + 0.03f * i;  // calibration run: known x per row
    for (int p = 0; p < 6; ++p) {
      float x = LoadF32(base, s.gammas[p] + 0x14), y = LoadF32(base, s.gammas[p] + 0x18);
      if (p == 0 && sound_host) x = vx + kSoundLabelDx;
      if (p == 1) x = value_x;
      if (p == 2 || p == 3) x = ax[p - 2];
      Store32F(base, s.ours[p] + 0x14, x);
      Store32F(base, s.ours[p] + 0x18, y + dy);
    }
  }
}

// The arrows' private render records were copied at boot, before Display Options loaded its
// arrow images: after a state change those arrows drew nothing (WORK_LOG 96). Re-copy Gamma's
// live records (filled while Display Options was open) each time the Recomp menu opens.
void RefreshSettingsArrowRecords(uint8_t* base) {
  if (!g_settings_built) return;
  for (int i = 0; i < kNumSettings; ++i) {
    for (int p = 2; p <= 3; ++p) {
      uint32_t ours = g_settings[i].ours[p], gamma = g_settings[i].gammas[p];
      for (auto [off, size] : {std::pair<uint32_t, uint32_t>{0x2C, 0x1C}, {0x30, 0x80}}) {
        uint32_t src = Load32(base, gamma + off), dst = Load32(base, ours + off);
        if (GuestReadable(base, src, size) && GuestReadable(base, dst, size) && src != dst)
          std::memcpy(base + dst, base + src, size);
      }
    }
  }
}

// Shows row i's current value (+0x338). The value CText2d's text SOURCE (+0x73C, re-read by
// every layout rebuild, Function_8211B4D8) must follow too: it stayed on the first value, so
// the screen mixed the new value with stale ones (WORK_LOG 97).
void ShowSettingValue(PPCContext& ctx, uint8_t* base, int i) {
  const SettingRow& s = g_settings[i];
  const SettingDef& def = spyro::Def(i);
  int v = base[s.row + 0x338];
  if (v >= def.count) v = 0;
  Store32(base, s.ours[1] + 0x73C, Load32(base, s.values + v * 0x2C));
  GuestCallWithString(ctx, base, sub_8211B368, s.ours[1] + 0x28, def.values[v]);
}

// Sets the labels and current values' glyphs; only valid while the menu is active.
void RefreshSettingsText(PPCContext& ctx, uint8_t* base) {
  uint8_t sel = base[g_recomp.menu + 0x6C];
  for (int i = 0; i < kNumSettings; ++i) {
    SettingRow& s = g_settings[i];
    // The picker's index is reset to 0 by the time the menu opens (the game re-reads it from
    // the option it thinks it edits): put the saved choice back first (WORK_LOG 98).
    s.value = spyro::GetSetting(i);
    base[s.row + 0x338] = static_cast<uint8_t>(s.value);
    GuestCallWithString(ctx, base, sub_8211B368, s.ours[0] + 0x28, spyro::Def(i).name);
    ShowSettingValue(ctx, base, i);
    // Put every row through a real state change (normal, or highlighted for the selected
    // one) so its arrows/glow take the game's look for that state instead of the leftovers
    // copied at boot (arrows show on the highlighted row only, like Gamma's).
    base[s.row + 0x14] = 0xFF;
    GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, i, i == sel ? 2 : 0, 0);
  }
}

bool BuildRecompRow(PPCContext& ctx, uint8_t* base, uint32_t menu) {
  uint32_t rows = Load32(base, menu + 0x84);
  uint32_t count = Load32(base, menu + 0x8C), cap = Load32(base, menu + 0x88);
  if (count != 2 || cap < 3 || !GuestReadable(base, rows, 12)) {
    REXLOG_INFO("[spyro/recomp] unexpected row list (count {} cap {}); not adding row", count, cap);
    return false;
  }
  uint32_t gamma = Load32(base, rows), back = Load32(base, rows + 4);
  if (!GuestReadable(base, back, 0x3C0) || Load32(base, back) != kButtonVtable) return false;
  uint32_t glabel = Load32(base, gamma + 0x34);
  uint32_t bhot = Load32(base, back + 0x2C);
  if (!GuestReadable(base, glabel, 0x748) || Load32(base, glabel) != kText2dVtable) return false;
  if (!GuestReadable(base, bhot, 0x40)) return false;
  uint32_t scene = Load32(base, glabel + 0x08);
  if (!GuestReadable(base, scene, 0x28)) return false;

  uint32_t gsource = Load32(base, glabel + 0x73C);  // CKLocTextAccessor (vtable 0x82065638)
  if (!GuestReadable(base, gsource, 0x14)) return false;
  // The accessor's current text object (CKStdText): **(+0x04) if set, else +0x10.
  uint32_t gstd = Load32(base, gsource + 0x04) ? Load32(base, Load32(base, gsource + 0x04))
                                                 : Load32(base, gsource + 0x10);
  if (!GuestReadable(base, gstd, 0x40)) return false;

  // Allocate everything first through the game's allocator.
  uint32_t source = GuestCall(ctx, base, sub_8260DC40, 0x14);
  uint32_t stdtext = GuestCall(ctx, base, sub_8260DC40, 0x40);
  uint32_t str = GuestCall(ctx, base, sub_8260DC40, kLabelChars * 2);
  uint32_t label = GuestCall(ctx, base, sub_8260DC40, 0x748);
  uint32_t hot = GuestCall(ctx, base, sub_8260DC40, 0x40);
  uint32_t entries = GuestCall(ctx, base, sub_8260DC40, 0x10);
  uint32_t row = GuestCall(ctx, base, sub_8260DC40, 0x3C0);
  if (!source || !stdtext || !str || !label || !hot || !entries || !row) {
    REXLOG_INFO("[spyro/recomp] allocation failed");
    return false;
  }

  // Our own localised text, in the game's format: a CKStdText (copy of Gamma's) whose string
  // pointer (+0x18, read by FUN_820C3970) is our UTF-16BE text, behind our own accessor copy.
  // Everything that (re)sets the label - the layout rebuild 0x8211B4D8 and the button's
  // SetState 0x822C51B8 - then produces "Recomp Settings" by itself (WORK_LOG 82/83).
  static const char16_t kRecompText[] = u"Recomp Settings";
  for (uint32_t i = 0; i < sizeof(kRecompText) / 2; ++i)
    Store16BE(base, str + i * 2, static_cast<uint16_t>(kRecompText[i]));
  std::memcpy(base + stdtext, base + gstd, 0x40);
  Store32(base, stdtext + 0x18, str);
  std::memcpy(base + source, base + gsource, 0x14);
  Store32(base, source + 0x04, 0);
  Store32(base, source + 0x10, stdtext);

  // Label: copy of the Gamma label, detached, then linked in after it.
  std::memcpy(base + label, base + glabel, 0x748);
  Store32(base, label + 0x73C, source);
  Store32(base, label + 0x740, 0);
  Store32(base, label + 0x744, 0);
  uint32_t next = Load32(base, glabel + 0x10);
  Store32(base, label + 0x0C, glabel);
  Store32(base, label + 0x10, next);
  Store32(base, glabel + 0x10, label);
  if (next) Store32(base, next + 0x0C, label);
  else Store32(base, scene + 0x1C, label);
  Store32(base, scene + 0x20, Load32(base, scene + 0x20) + 1);

  // Hotspot: Back's, with a single entry for our label and no secondary array.
  std::memcpy(base + hot, base + bhot, 0x40);
  Store32(base, hot + 0x04, 0);
  Store32(base, hot + 0x0C, 0);
  Store32(base, entries + 0x0, label);
  Store32(base, entries + 0x4, 0);
  Store32(base, entries + 0x8, 0);
  Store32(base, entries + 0xC, 0x00010101);
  Store32(base, hot + 0x10, entries);
  Store32(base, hot + 0x14, 1);
  Store32(base, hot + 0x18, 1);

  // Row widget: Back's button pointed at our hotspot and label.
  std::memcpy(base + row, base + back, 0x3C0);
  Store32(base, row + 0x2C, hot);
  Store32(base, row + 0x34, label);
  Store32(base, row + 0x38, 0);
  Store32(base, row + 0x3C, 0);
  Store32(base, row + 0x40, 0);
  // Each of the 5 state records (0xA0 bytes, from +0x90) names the label text for that state
  // at record +0x04; Back's say "(B) Back". Point them all at our accessor.
  for (uint32_t s = 0; s < 5; ++s) Store32(base, row + 0x90 + 0xA0 * s + 0x04, source);

  // Navigation: a row's neighbour for direction d is the byte at row+4+d (row vt+0x78 =
  // 0x8211A100). Direction codes measured in game (WORK_LOG 84): 0 = up, 1 = down,
  // 2/3 = left/right (consumed by the gamma slider). Recomp sits ABOVE Gamma:
  //   Gamma: up -> Recomp (row 2), everything else none.  Recomp: down -> Gamma (row 0).
  base[gamma + 4 + 0] = 2;
  base[gamma + 4 + 1] = 0xFF;
  base[gamma + 4 + 2] = 0xFF;
  base[gamma + 4 + 3] = 0xFF;
  base[row + 4 + 0] = 0xFF;
  base[row + 4 + 1] = 0;
  base[row + 4 + 2] = 0xFF;
  base[row + 4 + 3] = 0xFF;

  // Highlight glow: copies of Gamma's two glow billboards (Double row +0x58 / +0x5C, both at
  // x-offset 0 in its hotspot), drawn just behind our label; shown/positioned per frame by
  // PlaceRecompRow. Billboards own no heap memory, so byte copies are safe.
  uint32_t glows[2] = {0, 0}, gglows[2] = {Load32(base, gamma + 0x58), Load32(base, gamma + 0x5C)};
  for (int i = 0; i < 2; ++i) {
    if (!GuestReadable(base, gglows[i], kBillboardSize)) continue;
    uint32_t g = GuestCall(ctx, base, sub_8260DC40, kBillboardSize);
    if (!g) continue;
    std::memcpy(base + g, base + gglows[i], kBillboardSize);
    base[g + 5] &= ~2u;  // hidden until our row is highlighted
    // The draw sub-object's +0x2C (0x1C-byte record) and +0x30 (0x80-byte record) are
    // per-sprite render STATE, not shared constants: sharing them with Gamma's glows made our
    // row's per-frame scaling change Gamma's look (stone plates on both rows, WORK_LOG 88).
    // Give each copy its own records.
    for (auto [off, size] : {std::pair<uint32_t, uint32_t>{0x2C, 0x1C}, {0x30, 0x80}}) {
      uint32_t src = Load32(base, g + off);
      if (!GuestReadable(base, src, size)) continue;
      uint32_t own = GuestCall(ctx, base, sub_8260DC40, size);
      if (!own) continue;
      std::memcpy(base + own, base + src, size);
      Store32(base, g + off, own);
    }
    InsertPieceBefore(base, scene, g, label);
    glows[i] = g;
  }
  // Pieces are DRAWN BY THEIR ROW WIDGET, not by the scene list (WORK_LOG 87): a glow that no
  // widget owns is never drawn. Attach them to our button's extra piece slots (Back used
  // +0x3C for its "Return" sprite); PlaceRecompRow still controls their look and visibility.
  Store32(base, row + 0x38, glows[0]);
  Store32(base, row + 0x3C, glows[1]);

  Store32(base, rows + 8, row);
  Store32(base, menu + 0x8C, 3);

  // Recomp Settings menu: its row list {settings..., Back}, and the empty menu-state slot
  // aliased to this menu.
  uint32_t mgr = Load32(base, menu + 0x08);
  if (GuestReadable(base, mgr + 0xAC0, 0x20)) g_manager = mgr;
  g_settings_anchor_y = LoadF32(base, glabel + 0x18);
  g_host_dy = 0.0f;
  g_settings_built = BuildSettingsRows(ctx, base, gamma);
  uint32_t recomp_rows = g_settings_built ? GuestCall(ctx, base, sub_8260DC40, (kNumSettings + 1) * 4) : 0;
  if (recomp_rows) {
    for (int i = 0; i < kNumSettings; ++i) Store32(base, recomp_rows + i * 4, g_settings[i].row);
    Store32(base, recomp_rows + kNumSettings * 4, back);
  }
  uint32_t slot = mgr + kStateArray + kRecompMenuId * 4;
  // The slot is empty, or still holds an earlier Display Options object we aliased (a rebuilt
  // menu set keeps the old array contents for slots it doesn't fill).
  bool slot_free = GuestReadable(base, slot, 4) &&
                   (Load32(base, slot) == 0 ||
                    std::find(g_row_menus.begin(), g_row_menus.end(), Load32(base, slot)) != g_row_menus.end());
  bool slot_ok = slot_free && Load32(base, mgr + kStateArray + kVideoMenuId * 4) == menu && recomp_rows;
  if (slot_ok) Store32(base, slot, menu);
  g_row_menus.push_back(menu);
  REXLOG_INFO("[spyro/menus] recomp menu state 0x{:02X} -> video menu: {} (menu +0x90:{})", kRecompMenuId,
              slot_ok ? "aliased" : "NOT available", HexWords(base, menu + 0x90, 1));

  g_recomp = RecompRow{};
  g_recomp.str = str;
  g_recomp.mgr = slot_ok ? mgr : 0;
  g_recomp.back_row = back;
  g_recomp.normal_rows = rows;
  g_recomp.normal_cap = cap;
  g_recomp.recomp_rows = recomp_rows;
  g_recomp.normal_count = 3;
  g_recomp.back_index = 1;
  g_recomp.normal_sel = 2;
  g_recomp.text_dirty = true;  // set our label's glyphs the first time the menu is shown
  g_recomp.menu = menu;
  g_recomp.row = row;
  g_recomp.label = label;
  g_recomp.hotspot = hot;
  g_recomp.gamma_label = glabel;
  g_recomp.gamma_row = gamma;
  g_recomp.glow[0] = glows[0];
  g_recomp.glow[1] = glows[1];
  g_recomp.gamma_glow[0] = gglows[0];
  g_recomp.gamma_glow[1] = gglows[1];
  g_recomp.built = true;
  REXLOG_INFO("[spyro/recomp] glows 0x{:08X} 0x{:08X} (copies of Gamma's 0x{:08X} 0x{:08X})",
              glows[0], glows[1], gglows[0], gglows[1]);

  // Diagnostics for the next fixes (WORK_LOG 81): the Gamma label's text-source refs, and the
  // navigation-related fields of the Gamma and Back rows.
  REXLOG_INFO("[spyro/recomp] gamma label +0x738:{}", HexWords(base, glabel + 0x738, 4));
  for (uint32_t off : {0x73Cu, 0x740u, 0x744u}) {
    uint32_t src = Load32(base, glabel + off);
    if (GuestReadable(base, src, 0x40))
      DumpPiece(base, "gamma-label+" + std::to_string(off), src);
  }
  for (uint32_t r : {gamma, back}) {
    REXLOG_INFO("[spyro/recomp] row 0x{:08X} +0x00:{} | +0x3A0:{}", r, HexWords(base, r, 0x30 / 4),
                HexWords(base, r + 0x3A0, 8));
  }
  REXLOG_INFO("[spyro/recomp] menu +0x58:{}", HexWords(base, menu + 0x58, 0x40 / 4));

  // Menu switching diagnostics (WORK_LOG 92): game manager = video menu +0x08; its menu state
  // switcher is at +0xAC4 {owner, handler table*, ..., current id byte at +9 (= mgr+0xACD)};
  // the request stack is bytes at +0xAD0 with count at +0xAD8.
  if (GuestReadable(base, mgr + 0xAC0, 0x20)) {
    uint32_t table = Load32(base, mgr + 0xAC4 + 4);
    REXLOG_INFO("[spyro/menus] manager 0x{:08X} switcher +0xAC4:{} | current id 0x{:02X} | stack {} [{}]",
                mgr, HexWords(base, mgr + 0xAC4, 4), base[mgr + 0xACD], Load32(base, mgr + 0xAD8),
                HexWords(base, mgr + 0xAD0, 2));
    if (GuestReadable(base, table, 0x100)) {
      for (uint32_t i = 0; i < 64; ++i) {
        uint32_t h = Load32(base, table + i * 4);
        bool code = h >= 0x820B0000u && h < 0x826DF1BCu;
        REXLOG_INFO("[spyro/menus]   handler[0x{:02X}] = 0x{:08X}{}", i, h, code ? "" : "  (not code)");
        if (!code) break;
      }
    }
    // State objects: the generic handler does mgr->vt[0x364]() = mgr+0x12CC, obj = arr[id].
    for (uint32_t i = 0; i < 0x30; ++i) {
      uint32_t obj = Load32(base, mgr + 0x12CC + i * 4);
      uint32_t vt = GuestReadable(base, obj, 4) ? Load32(base, obj) : 0;
      REXLOG_INFO("[spyro/menus]   state[0x{:02X}] = 0x{:08X} vt 0x{:08X} {}{}", i, obj, vt,
                  ClassNameForVtable(base, vt), obj == menu ? "  <== VIDEO MENU" : "");
    }
  }
  REXLOG_INFO("[spyro/recomp] row built: row 0x{:08X} label 0x{:08X} hotspot 0x{:08X} scene 0x{:08X} "
              "(scene count now {})", row, label, hot, scene, Load32(base, scene + 0x20));
  return true;
}

// Per frame while the video menu updates: keep our label (and glows) offset from Gamma's
// (position +0x14/+0x18), and show our glow only while our row is highlighted/pressed.
// Row state byte = row +0x14 (0 normal, 2 highlighted, 4 pressed); piece flags byte +5 bit 0x02
// = shown (it is set on every piece of an active menu).
void PlaceRecompRow(uint8_t* base) {
  const RecompRow& r = g_recomp;
  if (!r.built || !GuestReadable(base, r.gamma_label, 0x30)) return;
  if (r.recomp_mode) {  // Recomp Settings menu: the picker rows; our button row is not shown
    PlaceSettingsRows(base);
    for (uint32_t g : r.glow)
      if (g) base[g + 5] &= ~2u;
    return;
  }
  if (!r.row) return;  // hosted on Sound Options (in a level): no Recomp row of its own
  float dy = kRecompRowDy;
  float gx = LoadF32(base, r.gamma_label + 0x14), gy = LoadF32(base, r.gamma_label + 0x18);
  Store32F(base, r.label + 0x14, gx);
  Store32F(base, r.label + 0x18, gy + dy);

  uint8_t gamma_state = GuestReadable(base, r.gamma_row, 0x18) ? base[r.gamma_row + 0x14] : 0;
  uint8_t our_state = base[r.row + 0x14];
  bool menu_shown = (base[r.label + 5] & 2) != 0;

  for (int i = 0; i < 2; ++i) {
    uint32_t g = r.glow[i], gg = r.gamma_glow[i];
    if (!g || !GuestReadable(base, gg, kBillboardSize)) continue;
    if (gamma_state == 2) {  // Gamma highlighted: remember what its glow looks like
      uint32_t r2c = Load32(base, gg + 0x2C), r30 = Load32(base, gg + 0x30);
      if (GuestReadable(base, r2c, 0x1C) && GuestReadable(base, r30, 0x80)) {
        std::memcpy(g_glow_snapshot[i], base + gg, kBillboardSize);
        std::memcpy(g_rec2c_snapshot[i], base + r2c, 0x1C);
        std::memcpy(g_rec30_snapshot[i], base + r30, 0x80);
        g_glow_snapshot_ok[i] = true;
      }
    }
    bool show = menu_shown && (our_state == 2 || our_state == 4) && g_glow_snapshot_ok[i];
    if (show) {
      // Look from the snapshot; keep our own vtable/scene/links (+0x00..+0x13) and our own
      // render-state records (+0x2C/+0x30).
      uint32_t own2c = Load32(base, g + 0x2C), own30 = Load32(base, g + 0x30);
      std::memcpy(base + g + 0x14, g_glow_snapshot[i] + 0x14, kBillboardSize - 0x14);
      Store32(base, g + 0x2C, own2c);
      Store32(base, g + 0x30, own30);
      // Our own records get Gamma's live contents (the right glow images).
      if (GuestReadable(base, own2c, 0x1C)) std::memcpy(base + own2c, g_rec2c_snapshot[i], 0x1C);
      if (GuestReadable(base, own30, 0x80)) std::memcpy(base + own30, g_rec30_snapshot[i], 0x80);
      float x, y;
      uint32_t xb, yb;
      std::memcpy(&xb, g_glow_snapshot[i] + 0x14, 4);
      std::memcpy(&yb, g_glow_snapshot[i] + 0x18, 4);
      xb = __builtin_bswap32(xb);
      yb = __builtin_bswap32(yb);
      std::memcpy(&x, &xb, 4);
      std::memcpy(&y, &yb, 4);
      Store32F(base, g + 0x14, x);
      Store32F(base, g + 0x18, y + dy);
      base[g + 4] = g_glow_snapshot[i][4];
      base[g + 5] = g_glow_snapshot[i][5] | 2;
    } else {
      base[g + 5] &= ~2u;
    }
  }
}

// ===== "Recomp Settings" on the pause menu's Options screen (WORK_LOG 114) ====================
//
// The in-game Options screen (state 0x17, vt 0x8207A110, 13 rows) has no Display Options button
// on screen: its row 8 IS that button, parked off screen (x ~1.04) with no neighbours and no
// label. We reuse it: a label cloned from Sound Options' (row 9, same plank style) reading
// "Recomp Settings", Sound Options' two highlight sprites cloned, all placed on the Tutorial line
// to the LEFT of Tutorial (where the user asked), reachable with LEFT from Tutorial. Pressing it
// opens the Recomp Settings menu; its Back returns here.
constexpr uint32_t kInGameOptionsVtable = 0x8207A110;
constexpr uint8_t kInGameOptionsId = 0x17;
constexpr uint32_t kIgoRows = 13, kIgoTutorial = 1, kIgoHidden = 8, kIgoSound = 9;
constexpr float kIgoGapX = 0.255f;  // our plank's centre this far left of Tutorial's

struct InGameOptionsRow {
  uint32_t menu = 0, tried = 0;          // the Options object we changed / last one we looked at
  uint32_t row = 0, tutorial = 0;        // row 8 (ours now) and the Tutorial row
  uint32_t pieces[4] = {};               // label, our two sprite clones, our plank clone
  uint32_t sources[4] = {};              // the Sound Options pieces they copy
  uint32_t visible = 0xFFFFFFFF;         // label piece flags last time we set its text
};
InGameOptionsRow g_igo;
// Where the Recomp Settings menu's Back goes: Display Options, or the in-game Options screen.
uint8_t g_recomp_return = kVideoMenuId;

bool BuildInGameOptionsRow(PPCContext& ctx, uint8_t* base, uint32_t menu) {
  uint32_t rows = Load32(base, menu + 0x84);
  if (Load32(base, menu + 0x8C) != kIgoRows || !GuestReadable(base, rows, kIgoRows * 4)) return false;
  uint32_t hidden = Load32(base, rows + kIgoHidden * 4), sound = Load32(base, rows + kIgoSound * 4);
  uint32_t tutorial = Load32(base, rows + kIgoTutorial * 4);
  if (!GuestReadable(base, hidden, 0x3C0) || !GuestReadable(base, sound, 0x3C0) || !GuestReadable(base, tutorial, 0x44))
    return false;
  // Row 8 must still be the parked button (no neighbours, only its plank) and row 9 a labelled one.
  if (Load32(base, hidden) != kButtonVtable || Load32(base, sound) != kButtonVtable ||
      Load32(base, hidden + 4) != 0xFFFFFFFFu || Load32(base, hidden + 0x34) != 0)
    return false;
  uint32_t slabel = Load32(base, sound + 0x34), splank = Load32(base, sound + 0x3C);
  uint32_t sbill[2] = {Load32(base, sound + 0x38), Load32(base, sound + 0x40)};
  if (!GuestReadable(base, slabel, kText2dSize) || Load32(base, slabel) != kText2dVtable ||
      !GuestReadable(base, splank, kBillboardSize) || !GuestReadable(base, Load32(base, tutorial + 0x3C), 0x20))
    return false;
  uint32_t shot = Load32(base, sound + 0x2C);
  uint32_t sentries = Load32(base, shot + 0x10), scount = Load32(base, shot + 0x18);
  if (!GuestReadable(base, shot, 0x40) || scount == 0 || scount > 8 || !GuestReadable(base, sentries, scount * 0x10))
    return false;
  uint32_t scene = Load32(base, slabel + 0x08), gsource = Load32(base, slabel + 0x73C);
  if (!GuestReadable(base, scene, 0x28) || !GuestReadable(base, gsource, 0x14)) return false;
  uint32_t gstd = Load32(base, gsource + 0x04) ? Load32(base, Load32(base, gsource + 0x04)) : Load32(base, gsource + 0x10);
  if (!GuestReadable(base, gstd, 0x40)) return false;

  // All pieces are our own copies of Sound Options' (row 8's own hotspot is anchored to the
  // parked spot and kept pulling its plank there), with our own hotspot unlinked from any anchor.
  uint32_t accessor = MakeGameText(ctx, base, gsource, gstd, u"Recomp Settings");
  uint32_t label = accessor ? CloneText2d(ctx, base, scene, slabel, accessor) : 0;
  uint32_t plank = label ? CloneBillboard(ctx, base, scene, splank, label) : 0;
  uint32_t hot = GuestCall(ctx, base, sub_8260DC40, 0x40);
  uint32_t entries = GuestCall(ctx, base, sub_8260DC40, scount * 0x10);
  if (!label || !plank || !hot || !entries) return false;
  uint32_t bill[2] = {0, 0};
  for (int b = 0; b < 2; ++b)
    if (GuestReadable(base, sbill[b], kBillboardSize)) bill[b] = CloneBillboard(ctx, base, scene, sbill[b], label);

  std::memcpy(base + hot, base + shot, 0x40);
  Store32(base, hot + 0x04, 0);
  Store32(base, hot + 0x0C, 0);
  std::memcpy(base + entries, base + sentries, scount * 0x10);
  for (uint32_t e = 0; e < scount; ++e) {
    uint32_t p = Load32(base, entries + e * 0x10);
    if (p == slabel) p = label;
    else if (p == splank) p = plank;
    else if (p == sbill[0]) p = bill[0];
    else if (p == sbill[1]) p = bill[1];
    Store32(base, entries + e * 0x10, p);
  }
  Store32(base, hot + 0x10, entries);
  Store32(base, hidden + 0x2C, hot);
  Store32(base, hidden + 0x34, label);
  Store32(base, hidden + 0x38, bill[0]);
  Store32(base, hidden + 0x3C, plank);
  Store32(base, hidden + 0x40, bill[1]);
  // Every state record names the label text for that state (+0x04): all ours.
  for (uint32_t s = 0; s < 5; ++s) Store32(base, hidden + 0x90 + 0xA0 * s + 0x04, accessor);
  // Neighbours (up, down, left, right): ours sits left of Tutorial.
  base[hidden + 4 + 0] = 0;             // up -> Subtitles
  base[hidden + 4 + 1] = 2;             // down -> Vibration P1
  base[hidden + 4 + 2] = 0xFF;
  base[hidden + 4 + 3] = kIgoTutorial;  // right -> Tutorial
  // (Tutorial is an on/off switch that takes LEFT itself; the direction hook moves to ours.)

  g_igo = InGameOptionsRow{};
  g_igo.menu = g_igo.tried = menu;
  g_igo.row = hidden;
  g_igo.tutorial = tutorial;
  uint32_t pieces[4] = {label, bill[0], bill[1], plank}, sources[4] = {slabel, sbill[0], sbill[1], splank};
  std::memcpy(g_igo.pieces, pieces, sizeof(pieces));
  std::memcpy(g_igo.sources, sources, sizeof(sources));
  REXLOG_INFO("[spyro/recomp] pause Options 0x{:08X}: row 8 is now Recomp Settings (label 0x{:08X})", menu, label);
  return true;
}

// In a level the manager's list has no Display Options (state 0x21 empty, the main menu's object
// is freed - opening the Recomp menu through it crashed). Host the Recomp Settings menu on the
// level's Sound Options instead (state 0x1D, CKS08MenuOptionSound360): same make-up as Display
// Options (picker rows like Gamma + a Back button), so the settings rows are built from its first
// row and its row list is swapped while state 0x22 (aliased to it) is open.
constexpr uint8_t kSoundMenuId = 0x1D;
constexpr uint32_t kSoundMenuVtable = 0x8207A238, kDoubleVtable = 0x820787D0;

bool BuildRecompHostOnSound(PPCContext& ctx, uint8_t* base, uint32_t sound) {
  uint32_t rows = Load32(base, sound + 0x84), count = Load32(base, sound + 0x8C);
  if (!GuestReadable(base, sound, 0xB8) || Load32(base, sound) != kSoundMenuVtable || count != 4 ||
      !GuestReadable(base, rows, count * 4))
    return false;
  uint32_t first = Load32(base, rows), back = Load32(base, rows + 3 * 4);
  if (!GuestReadable(base, first, kDoubleSize) || Load32(base, first) != kDoubleVtable ||
      !GuestReadable(base, back, 0x3C0) || Load32(base, back) != kButtonVtable)
    return false;
  uint32_t flabel = Load32(base, first + 0x34);
  if (!GuestReadable(base, flabel, kText2dSize)) return false;
  uint32_t mgr = g_manager, slot = mgr + kStateArray + kRecompMenuId * 4;
  bool slot_free = GuestReadable(base, slot, 4) &&
                   (Load32(base, slot) == 0 ||
                    std::find(g_row_menus.begin(), g_row_menus.end(), Load32(base, slot)) != g_row_menus.end());
  if (!slot_free || Load32(base, mgr + kStateArray + kSoundMenuId * 4) != sound) return false;

  if (!BuildSettingsRows(ctx, base, first)) return false;
  uint32_t recomp_rows = GuestCall(ctx, base, sub_8260DC40, (kNumSettings + 1) * 4);
  if (!recomp_rows) return false;
  for (int i = 0; i < kNumSettings; ++i) Store32(base, recomp_rows + i * 4, g_settings[i].row);
  Store32(base, recomp_rows + kNumSettings * 4, back);
  g_settings_built = true;
  g_host_dy = g_settings_anchor_y >= 0.0f ? g_settings_anchor_y - LoadF32(base, flabel + 0x18) : 0.0f;

  Store32(base, slot, sound);
  g_row_menus.push_back(sound);
  g_recomp = RecompRow{};
  g_recomp.menu = sound;
  g_recomp.mgr = mgr;
  g_recomp.back_row = back;
  g_recomp.normal_rows = rows;
  g_recomp.normal_cap = Load32(base, sound + 0x88);
  g_recomp.normal_count = 4;
  g_recomp.back_index = 3;
  g_recomp.normal_sel = 0;
  g_recomp.recomp_rows = recomp_rows;
  g_recomp.gamma_label = flabel;
  g_recomp.gamma_row = first;
  g_recomp.built = true;
  REXLOG_INFO("[spyro/recomp] Recomp Settings hosted on the level's Sound Options 0x{:08X} (dy {:.3f})", sound,
              g_host_dy);
  return true;
}

// Per frame while the in-game Options updates: our pieces on the Tutorial line, left of it, and
// the label's glyphs set whenever it becomes visible (text only builds while the menu is live).
void PlaceInGameOptionsRow(PPCContext& ctx, uint8_t* base) {
  uint32_t tplank = Load32(base, g_igo.tutorial + 0x3C);
  if (!GuestReadable(base, tplank, 0x20)) return;
  float x = LoadF32(base, tplank + 0x14) - kIgoGapX, y = LoadF32(base, tplank + 0x18);
  for (uint32_t p : g_igo.pieces) {
    if (!p) continue;
    Store32F(base, p + 0x14, x);
    Store32F(base, p + 0x18, y);
  }
  // The sprites' render records (+0x2C 0x1C bytes, +0x30 0x80 bytes: which image) were copied
  // before the game filled them - the plank drew as a plain yellow box. The plank follows Sound
  // Options' live records (same idea as RefreshSettingsArrowRecords).
  auto copy_records = [&](uint32_t ours, const uint8_t* rec2c, const uint8_t* rec30) {
    uint32_t d2c = Load32(base, ours + 0x2C), d30 = Load32(base, ours + 0x30);
    if (GuestReadable(base, d2c, 0x1C)) std::memcpy(base + d2c, rec2c, 0x1C);
    if (GuestReadable(base, d30, 0x80)) std::memcpy(base + d30, rec30, 0x80);
  };
  uint32_t splank = g_igo.sources[3];
  if (g_igo.pieces[3] && GuestReadable(base, splank, kBillboardSize)) {
    uint32_t s2c = Load32(base, splank + 0x2C), s30 = Load32(base, splank + 0x30);
    if (GuestReadable(base, s2c, 0x1C) && GuestReadable(base, s30, 0x80))
      copy_records(g_igo.pieces[3], base + s2c, base + s30);
  }
  // Glows stay hidden: their copied image was never reliable (drawn as a big yellow box, also
  // with records copied from a highlighted Sound Options). Selected = brighter plank and text.
  for (int b = 0; b < 2; ++b)
    if (g_igo.pieces[1 + b]) base[g_igo.pieces[1 + b] + 5] &= ~2u;
  uint32_t visible = base[g_igo.pieces[0] + 5];
  if (visible != g_igo.visible) {
    g_igo.visible = visible;
    GuestCallWithString(ctx, base, sub_8211B368, g_igo.pieces[0] + 0x28, u"Recomp Settings");
  }
}

// ===== "Exit to Desktop" on the main menu and the in-level pause menu (WORK_LOG 125) ==========
//
// Both screens are an arc of stone buttons (CKInterfaceButton): Single Player, Moves, Armor,
// Elements, Options, Quit level (row 5). We append a 7th stone after Quit level: a copy of that
// row (0x3C0) with copies of its three sprites (+0x38 / +0x3C / +0x40), its own hotspot and a
// CText2d label "Exit to Desktop" (the pause menu's rows have labels at +0x34 - Quit level's is
// copied; the main menu's are drawn by the menu movie, so its first row's label is copied). Each
// frame the copies sit one arc step past Quit level (the step from Elements... Options -> Quit
// level), following the menu's slide-in. Pressing it closes the game window (the normal exit).
// Like the Display Options row, the copy SHARES Quit level's per-state heap arrays: the menu
// destructor hooks put the original row list back first, so it is never destroyed.
constexpr uint32_t kMainMenuVtable = 0x82079920, kPauseMenuVtable = 0x8207A4A0;
constexpr uint32_t kQuitRow = 5;
constexpr uint32_t kButtonSize = 0x3C0;

struct ExitRow {
  uint32_t menu = 0, tried = 0, row = 0, index = 0;
  uint32_t src = 0, prev = 0;  // Quit level and the row before it (one arc step)
  uint32_t pieces[4] = {};     // our label, our copies of the three sprites
  uint32_t sources[4] = {};    // what each copies: Quit level's label (pause menu only), sprites
  uint32_t prevs[4] = {};      // the same pieces of the row before Quit level
  uint32_t visible = 0xFFFFFFFF;
};
ExitRow g_exit[2];  // [0] main menu, [1] in-level pause menu
struct ExitRestore {
  uint32_t menu, rows, cap, count;
};
std::vector<ExitRestore> g_exit_restore;  // every menu object we changed: its original row list

void ExitToDesktop() {
  REXLOG_INFO("[spyro/menu] Exit to Desktop");
#if defined(_WIN32)
  // The game window (any visible top-level window of this process): WM_CLOSE = the user closing it.
  HWND found = nullptr;
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
        *reinterpret_cast<HWND*>(p) = h;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&found));
  if (found) PostMessageW(found, WM_CLOSE, 0, 0);
  else std::exit(0);
#else
  std::exit(0);
#endif
}

bool BuildExitRow(PPCContext& ctx, uint8_t* base, uint32_t menu, ExitRow& e, bool pause) {
  auto fail = [&](const char* why) {
    REXLOG_INFO("[spyro/menu] menu 0x{:08X}: no Exit to Desktop yet ({})", menu, why);
    return false;
  };
  uint32_t rows = Load32(base, menu + 0x84), count = Load32(base, menu + 0x8C);
  uint32_t want = pause ? 7 : 6;
  if (count != want || !GuestReadable(base, rows, count * 4)) return fail("row count");
  uint32_t src = Load32(base, rows + kQuitRow * 4), prev = Load32(base, rows + (kQuitRow - 1) * 4);
  uint32_t first = Load32(base, rows);
  if (!GuestReadable(base, src, kButtonSize) || !GuestReadable(base, prev, kButtonSize) ||
      !GuestReadable(base, first, 0x44) || Load32(base, src) != kButtonVtable || Load32(base, prev) != kButtonVtable)
    return fail("Quit level row");
  uint32_t slabel = pause ? Load32(base, src + 0x34) : Load32(base, first + 0x34);
  if (!GuestReadable(base, slabel, kText2dSize) || Load32(base, slabel) != kText2dVtable) return fail("label");
  uint32_t scene = Load32(base, slabel + 0x08), gsource = Load32(base, slabel + 0x73C);
  if (!GuestReadable(base, scene, 0x28)) return fail("label scene");
  uint32_t gstd = 0;
  if (GuestReadable(base, gsource, 0x14)) {
    uint32_t ind = Load32(base, gsource + 0x04);
    gstd = ind ? (GuestReadable(base, ind, 4) ? Load32(base, ind) : 0) : Load32(base, gsource + 0x10);
  }
  if (!GuestReadable(base, gstd, 0x40)) {  // the main menu's label: use the Gamma label's kind
    gsource = g_text_template[0];
    gstd = g_text_template[1];
    if (!GuestReadable(base, gsource, 0x14) || !GuestReadable(base, gstd, 0x40)) return fail("label text");
  }
  uint32_t shot = Load32(base, src + 0x2C);
  uint32_t sentries = Load32(base, shot + 0x10), scount = Load32(base, shot + 0x18);
  if (!GuestReadable(base, shot, 0x40) || scount == 0 || scount > 8 || !GuestReadable(base, sentries, scount * 0x10))
    return fail("hotspot");

  uint32_t accessor = MakeGameText(ctx, base, gsource, gstd, u"Exit to Desktop");
  uint32_t label = accessor ? CloneText2d(ctx, base, scene, slabel, accessor) : 0;
  uint32_t row = GuestCall(ctx, base, sub_8260DC40, kButtonSize);
  uint32_t hot = GuestCall(ctx, base, sub_8260DC40, 0x40);
  uint32_t entries = GuestCall(ctx, base, sub_8260DC40, scount * 0x10);
  uint32_t list = GuestCall(ctx, base, sub_8260DC40, (count + 1) * 4);
  if (!label || !row || !hot || !entries || !list) return false;
  uint32_t sb[3], pb[3], bill[3] = {0, 0, 0};
  for (int b = 0; b < 3; ++b) {
    sb[b] = Load32(base, src + 0x38 + 4 * b);
    pb[b] = Load32(base, prev + 0x38 + 4 * b);
    if (GuestReadable(base, sb[b], kBillboardSize))
      bill[b] = CloneBillboard(ctx, base, Load32(base, sb[b] + 0x08), sb[b], sb[b]);
  }

  std::memcpy(base + row, base + src, kButtonSize);
  std::memcpy(base + hot, base + shot, 0x40);
  Store32(base, hot + 0x04, 0);
  Store32(base, hot + 0x0C, 0);
  std::memcpy(base + entries, base + sentries, scount * 0x10);
  uint32_t src_label = Load32(base, src + 0x34);
  for (uint32_t i = 0; i < scount; ++i) {
    uint32_t p = Load32(base, entries + i * 0x10);
    if (p && p == src_label) p = label;
    for (int b = 0; b < 3; ++b)
      if (p && p == sb[b]) p = bill[b];
    Store32(base, entries + i * 0x10, p);
  }
  Store32(base, hot + 0x10, entries);
  Store32(base, row + 0x2C, hot);
  Store32(base, row + 0x34, label);
  for (int b = 0; b < 3; ++b) Store32(base, row + 0x38 + 4 * b, bill[b]);
  for (uint32_t s = 0; s < 5; ++s) Store32(base, row + 0x90 + 0xA0 * s + 0x04, accessor);

  // Neighbours (+4 up, +5 down, +6 left, +7 right): ours between Quit level and what came after it.
  uint32_t index = count;
  uint8_t after = base[src + 5];
  base[row + 4] = base[row + 6] = uint8_t(kQuitRow);
  base[row + 5] = base[row + 7] = after;
  base[src + 5] = base[src + 7] = uint8_t(index);
  if (after < count) {
    uint32_t a = Load32(base, rows + after * 4);
    if (GuestReadable(base, a, 8)) {
      if (base[a + 4] == kQuitRow) base[a + 4] = uint8_t(index);
      if (base[a + 6] == kQuitRow) base[a + 6] = uint8_t(index);
    }
  }
  std::memcpy(base + list, base + rows, count * 4);
  Store32(base, list + count * 4, row);
  g_exit_restore.push_back({menu, rows, Load32(base, menu + 0x88), count});
  Store32(base, menu + 0x84, list);
  Store32(base, menu + 0x88, count + 1);
  Store32(base, menu + 0x8C, count + 1);

  e = ExitRow{};
  e.menu = e.tried = menu;
  e.row = row;
  e.index = index;
  e.src = src;
  e.prev = prev;
  uint32_t pieces[4] = {label, bill[0], bill[1], bill[2]};
  uint32_t sources[4] = {pause ? src_label : 0, sb[0], sb[1], sb[2]};
  uint32_t prevs[4] = {pause ? Load32(base, prev + 0x34) : 0, pb[0], pb[1], pb[2]};
  std::memcpy(e.pieces, pieces, sizeof(pieces));
  std::memcpy(e.sources, sources, sizeof(sources));
  std::memcpy(e.prevs, prevs, sizeof(prevs));
  REXLOG_INFO("[spyro/menu] {} 0x{:08X}: Exit to Desktop is row {} (label 0x{:08X})", pause ? "pause menu" : "main menu",
              menu, index, label);
  return true;
}

// Per frame while the menu updates: our pieces one arc step past Quit level, its look copied.
void PlaceExitRow(PPCContext& ctx, uint8_t* base, ExitRow& e) {
  auto pos = [&](uint32_t p, float& x, float& y) {
    if (!p || !GuestReadable(base, p, 0x20)) return false;
    x = LoadF32(base, p + 0x14);
    y = LoadF32(base, p + 0x18);
    return true;
  };
  float ax = 0, ay = 0;  // where the main sprite (+0x3C, the stone) goes: the label follows it
  for (int i = 1; i < 4; ++i) {
    float sx, sy, px, py;
    uint32_t ours = e.pieces[i], src = e.sources[i];
    if (!ours || !pos(src, sx, sy) || !pos(e.prevs[i], px, py)) continue;
    float x = sx + (sx - px), y = sy + (sy - py);
    Store32F(base, ours + 0x14, x);
    Store32F(base, ours + 0x18, y);
    if (i == 2) ax = x, ay = y;
    // Same look as Quit level's sprite (its render records are filled late - WORK_LOG 114).
    uint32_t s2c = Load32(base, src + 0x2C), s30 = Load32(base, src + 0x30);
    uint32_t d2c = Load32(base, ours + 0x2C), d30 = Load32(base, ours + 0x30);
    if (GuestReadable(base, s2c, 0x1C) && GuestReadable(base, d2c, 0x1C)) std::memcpy(base + d2c, base + s2c, 0x1C);
    if (GuestReadable(base, s30, 0x80) && GuestReadable(base, d30, 0x80)) std::memcpy(base + d30, base + s30, 0x80);
  }
  uint32_t label = e.pieces[0];
  float lx, ly, px, py;
  if (e.sources[0] && pos(e.sources[0], lx, ly) && pos(e.prevs[0], px, py)) {
    Store32F(base, label + 0x14, lx + (lx - px));
    Store32F(base, label + 0x18, ly + (ly - py));
  } else if (ax != 0 || ay != 0) {
    Store32F(base, label + 0x14, ax);
    Store32F(base, label + 0x18, ay);
  }
  uint32_t visible = base[label + 5];
  if (visible != e.visible) {
    e.visible = visible;
    GuestCallWithString(ctx, base, sub_8211B368, label + 0x28, u"Exit to Desktop");
  }
}

// Menu destructors: put the original row list back (our row shares Quit level's arrays).
void RestoreExitRows(uint8_t* base, uint32_t menu) {
  for (auto it = g_exit_restore.begin(); it != g_exit_restore.end(); ++it) {
    if (it->menu != menu) continue;
    Store32(base, menu + 0x84, it->rows);
    Store32(base, menu + 0x88, it->cap);
    Store32(base, menu + 0x8C, it->count);
    g_exit_restore.erase(it);
    for (ExitRow& e : g_exit)
      if (e.menu == menu) e = ExitRow{};
    return;
  }
}

// ===== "Quit to Desktop" plank on the Pause and Game Menu screens (WORK_LOG 125) ==============
//
// Not a row of the menu (adding one broke loading a save): a copy of the pause Options' stone
// plank and its label (the Recomp Settings look), linked into the screen's own scene at the spot
// the user marked (bottom, left of "A Accept"). DOWN on the bottom stone selects it (the stone
// dims, the plank grows), UP goes back, A closes the game.
constexpr uint8_t kQuitScreenIds[2] = {0x06, 0x0F};  // Game Menu, in-level Pause
constexpr float kQuitPlankPos[2][2] = {{0.55f, 0.125f}, {0.67f, 0.16f}};  // x right, y up (0..1)
constexpr float kQuitPlankScale = 1.0f;  // of the pause Options plank (user: fine as it is)
constexpr float kQuitSelectedScale = 1.12f;

struct QuitPlank {
  uint32_t menu = 0, tried = 0, label = 0, plank = 0, src_plank = 0, ref = 0;  // ref: a piece of the screen
  uint32_t bottom = 0xFF;  // the stone it was selected from
  bool selected = false;
  uint32_t visible = 0xFFFFFFFF;
  float scale[2] = {1, 1};
};
QuitPlank g_quit[2];

// The pause Options object (state 0x17) of the current menu set, if the manager has one.
uint32_t InGameOptionsObject(uint8_t* base) {
  if (!g_manager || !GuestReadable(base, g_manager + kStateArray, 0x25 * 4)) return 0;
  uint32_t o = Load32(base, g_manager + kStateArray + kInGameOptionsId * 4);
  return GuestReadable(base, o, 0xB8) && Load32(base, o) == kInGameOptionsVtable ? o : 0;
}

bool BuildQuitPlank(PPCContext& ctx, uint8_t* base, uint32_t menu, QuitPlank& q) {
  auto fail = [&](const char* why) {
    REXLOG_INFO("[spyro/menu] menu 0x{:08X}: no Quit to Desktop yet ({})", menu, why);
    return false;
  };
  uint32_t rows = Load32(base, menu + 0x84), count = Load32(base, menu + 0x8C);
  if (count < 6 || !GuestReadable(base, rows, count * 4)) return fail("rows");
  // A piece of this screen (its scene; our pieces are linked next to it): row 5's main sprite.
  uint32_t r5 = Load32(base, rows + 5 * 4);
  uint32_t ref = GuestReadable(base, r5, 0x44) ? Load32(base, r5 + 0x3C) : 0;
  if (!GuestReadable(base, ref, kBillboardSize)) return fail("screen piece");
  uint32_t scene = Load32(base, ref + 0x08);
  if (!GuestReadable(base, scene, 0x28)) return fail("scene");
  // The plank and label to copy: pause Options' Sound Options row (as for Recomp Settings).
  uint32_t opt = InGameOptionsObject(base);
  if (!opt || Load32(base, opt + 0x8C) < kIgoSound + 1) return fail("pause Options not loaded");
  uint32_t sound = Load32(base, Load32(base, opt + 0x84) + kIgoSound * 4);
  if (!GuestReadable(base, sound, kButtonSize)) return fail("Sound Options row");
  uint32_t slabel = Load32(base, sound + 0x34), splank = Load32(base, sound + 0x3C);
  if (!GuestReadable(base, slabel, kText2dSize) || Load32(base, slabel) != kText2dVtable ||
      !GuestReadable(base, splank, kBillboardSize))
    return fail("plank / label");
  uint32_t gsource = Load32(base, slabel + 0x73C);
  uint32_t gstd = 0;
  if (GuestReadable(base, gsource, 0x14)) {
    uint32_t ind = Load32(base, gsource + 0x04);
    gstd = ind ? (GuestReadable(base, ind, 4) ? Load32(base, ind) : 0) : Load32(base, gsource + 0x10);
  }
  if (!GuestReadable(base, gstd, 0x40)) {
    gsource = g_text_template[0];
    gstd = g_text_template[1];
    if (!GuestReadable(base, gsource, 0x14) || !GuestReadable(base, gstd, 0x40)) return fail("text");
  }
  uint32_t accessor = MakeGameText(ctx, base, gsource, gstd, u"Quit to Desktop");
  uint32_t plank = accessor ? CloneBillboard(ctx, base, scene, splank, ref) : 0;
  uint32_t label = plank ? GuestCall(ctx, base, sub_8260DC40, kText2dSize) : 0;
  if (!label) return fail("allocation");
  // The label copy goes into THIS screen's scene, drawn right after the plank.
  std::memcpy(base + label, base + slabel, kText2dSize);
  Store32(base, label + 0x73C, accessor);
  Store32(base, label + 0x740, 0);
  Store32(base, label + 0x744, 0);
  InsertPieceBefore(base, scene, label, ref);  // draw order: plank, label, the screen's sprite
  Store32(base, plank + 0x08, scene);
  Store32(base, label + 0x08, scene);

  q = QuitPlank{};
  q.menu = q.tried = menu;
  q.label = label;
  q.plank = plank;
  q.src_plank = splank;
  q.ref = ref;
  q.scale[0] = LoadF32(base, plank + 0x20);
  q.scale[1] = LoadF32(base, plank + 0x24);
  REXLOG_INFO("[spyro/menu] menu 0x{:08X}: Quit to Desktop plank 0x{:08X} label 0x{:08X}", menu, plank, label);
  return true;
}

void PlaceQuitPlank(PPCContext& ctx, uint8_t* base, QuitPlank& q, int k) {
  float x = kQuitPlankPos[k][0], y = kQuitPlankPos[k][1];
  for (uint32_t p : {q.plank, q.label}) {
    Store32F(base, p + 0x14, x);
    Store32F(base, p + 0x18, y);
    // Shown exactly when the screen's own pieces are (flag bit 1).
    base[p + 5] = uint8_t((base[p + 5] & ~2u) | (base[q.ref + 5] & 2u));
  }
  float grow = kQuitPlankScale * (q.selected ? kQuitSelectedScale : 1.0f);
  Store32F(base, q.plank + 0x20, q.scale[0] * grow);
  Store32F(base, q.plank + 0x24, q.scale[1] * grow);
  // The plank's look follows the source plank's live render records (filled late, WORK_LOG 114).
  uint32_t s2c = Load32(base, q.src_plank + 0x2C), s30 = Load32(base, q.src_plank + 0x30);
  uint32_t d2c = Load32(base, q.plank + 0x2C), d30 = Load32(base, q.plank + 0x30);
  if (GuestReadable(base, s2c, 0x1C) && GuestReadable(base, d2c, 0x1C)) std::memcpy(base + d2c, base + s2c, 0x1C);
  if (GuestReadable(base, s30, 0x80) && GuestReadable(base, d30, 0x80)) std::memcpy(base + d30, base + s30, 0x80);
  uint32_t visible = base[q.label + 5];
  if (visible != q.visible) {
    q.visible = visible;
    GuestCallWithString(ctx, base, sub_8211B368, q.label + 0x28, u"Quit to Desktop");
  }
}

// Every game frame: a plank whose screen is not the current menu is hidden (its screen stops
// updating when another menu opens, so PlaceQuitPlank can't hide it - the user saw it stay).
void HideQuitPlanks(uint8_t* base) {
  if (!g_manager || !GuestReadable(base, g_manager + 0xAC0, 0x20)) return;
  uint8_t id = base[g_manager + 0xACD];
  for (int k = 0; k < 2; ++k) {
    QuitPlank& q = g_quit[k];
    if (!q.menu || id == kQuitScreenIds[k]) continue;
    q.selected = false;
    for (uint32_t p : {q.plank, q.label})
      if (GuestReadable(base, p, 8)) base[p + 5] &= ~2u;
  }
}

void RequestMenu(uint8_t* base, uint8_t id);

// The host screen's title (a CText2d near the top of its scene reading "Display Options" or
// "Sound Options") reads "Recomp Settings" while the Recomp menu is showing: its text source
// (+0x73C, re-read by layout rebuilds) is swapped for ours and put back when the host screen
// shows as itself again (WORK_LOG 114).
struct HostTitle {
  uint32_t piece = 0, own_source = 0, ours = 0;
  std::u16string text;
  bool changed = false;
} g_title;

// A CText2d's laid-out text (inline UTF-16BE at +0x3C). The game stores each space as a 0, so
// words are joined back with spaces; two 0s in a row end the text.
std::u16string Text2dString(const uint8_t* base, uint32_t t) {
  std::u16string s;
  auto at = [&](uint32_t c) { return static_cast<char16_t>(base[t + 0x3C + c * 2] << 8 | base[t + 0x3C + c * 2 + 1]); };
  for (uint32_t c = 0; c < 48; ++c) {
    char16_t ch = at(c);
    if (!ch) {
      if (c + 1 >= 48 || !at(c + 1)) break;
      ch = u' ';
    }
    s += ch;
  }
  return s;
}

uint32_t FindHostTitle(const uint8_t* base) {
  uint32_t scene = GuestReadable(base, g_recomp.gamma_label, 0x10) ? Load32(base, g_recomp.gamma_label + 0x08) : 0;
  if (!GuestReadable(base, scene, 0x28)) return 0;
  uint32_t p = Load32(base, scene + 0x18);
  for (int n = 0; p && n < 4000 && GuestReadable(base, p, kText2dSize); ++n, p = Load32(base, p + 0x10)) {
    if (Load32(base, p) != kText2dVtable || LoadF32(base, p + 0x18) < 0.8f) continue;  // titles sit at the top
    std::u16string s = Text2dString(base, p);
    if (s == u"Display Options" || s == u"Sound Options") return p;
  }
  return 0;
}

void SetHostTitle(PPCContext& ctx, uint8_t* base, bool recomp) {
  // Only on the level's Sound Options host (back_index 3). The main menu's Display Options title
  // is drawn by its UI movie, not by a scene text: the "Display Options" text found there was
  // another one, so that title stays as it was.
  if (recomp && !g_title.changed && g_recomp.back_index == 3) {
    // The title found the first time is reused while it belongs to this host's scene: once the
    // game had shown Sound Options itself, the search no longer found it and the Recomp menu
    // kept the "Sound Options" title (WORK_LOG 117).
    uint32_t scene = GuestReadable(base, g_recomp.gamma_label, 0x10) ? Load32(base, g_recomp.gamma_label + 0x08) : 0;
    uint32_t t = g_title.piece && GuestReadable(base, g_title.piece, kText2dSize) &&
                         Load32(base, g_title.piece) == kText2dVtable && Load32(base, g_title.piece + 0x08) == scene
                     ? g_title.piece
                     : FindHostTitle(base);
    if (!t) return;
    if (t != g_title.piece) {  // new host / menu set: our own text source for this title
      uint32_t src = Load32(base, t + 0x73C);
      uint32_t std_text = GuestReadable(base, src, 0x14)
                              ? (Load32(base, src + 0x04) ? Load32(base, Load32(base, src + 0x04)) : Load32(base, src + 0x10))
                              : 0;
      if (!GuestReadable(base, std_text, 0x40)) return;
      g_title = HostTitle{};
      g_title.piece = t;
      g_title.own_source = src;
      g_title.ours = MakeGameText(ctx, base, src, std_text, u"Recomp Settings");
      if (!g_title.ours) return;
      g_title.text = Text2dString(base, t);  // the host's own title, put back on the way out
    }
    Store32(base, t + 0x73C, g_title.ours);
    GuestCallWithString(ctx, base, sub_8211B368, t + 0x28, u"Recomp Settings");
    g_title.changed = true;
  } else if (!recomp && g_title.changed && GuestReadable(base, g_title.piece, kText2dSize)) {
    Store32(base, g_title.piece + 0x73C, g_title.own_source);
    GuestCallWithString(ctx, base, sub_8211B368, g_title.piece + 0x28, g_title.text.c_str());
    g_title.changed = false;
  }
}

// The Recomp Settings menu's per-row callback (the host menu's vtable slot 57: Display Options
// in the main menu, Sound Options in a level). The host's own handler must not run: it treats
// its rows as its own (row 1 = Display Options' Back). Left/right on a setting row = the
// picker's own events 3/4 -> state 4; the picker has already moved its index (+0x338) when
// phase 1 arrives.
void RecompMenuRowEvent(PPCContext& ctx, uint8_t* base, uint32_t row, uint32_t state, uint32_t phase) {
  bool pressed = state == 4 && phase == 1;
  if (row == 0 && phase == 0 && g_recomp.text_dirty) {
    g_recomp.text_dirty = false;
    RefreshSettingsText(ctx, base);
    SetHostTitle(ctx, base, true);
    REXLOG_INFO("[spyro/settings] texts set (title piece 0x{:08X})", g_title.changed ? g_title.piece : 0);
  }
  // A row going highlighted -> normal shrinks its arrows to dots, while a fresh change to
  // normal keeps them visible (measured, WORK_LOG 96): redo every "became normal" as fresh.
  static bool s_refreshing = false;
  if (phase == 1 && state == 0 && row < kNumSettings && !s_refreshing) {
    s_refreshing = true;
    base[g_settings[row].row + 0x14] = 0xFF;
    GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, row, 0, 0);
    s_refreshing = false;
  }
  if (pressed && row < kNumSettings) {
    SettingRow& s = g_settings[row];
    s.value = base[s.row + 0x338];
    ShowSettingValue(ctx, base, row);
    spyro::SetSetting(row, s.value);  // apply (live settings) and save
    REXLOG_INFO("[spyro/settings] {} -> {} ({})", row, s.value, base[s.row + 0x350] ? "left" : "right");
  }
  if (pressed && row == kNumSettings) RequestMenu(base, g_recomp_return);  // Back
}

// Sets the Recomp Settings row's live glyphs (only valid while the menu is active).
void SetRecompLabel(PPCContext& ctx, uint8_t* base) {
  GuestCallWithString(ctx, base, sub_8211B368, g_recomp.label + 0x28, u"Recomp Settings");
}

// Asks the game manager to switch menu state, the way the game's Back handlers do:
// request stack bytes at mgr+0xAD0, count at mgr+0xAD8.
void RequestMenu(uint8_t* base, uint8_t id) {
  uint32_t mgr = g_recomp.mgr;
  Store32(base, mgr + 0xAD8, 0);
  base[mgr + 0xAD0] = id;
  Store32(base, mgr + 0xAD8, 1);
  REXLOG_INFO("[spyro/menus] requested menu state 0x{:02X}", id);
}

// Switches the video menu between its own row list {Gamma, Back, Recomp} and the Recomp
// Settings list {settings rows..., Back}. Row neighbours (row+4+dir, 0 up / 1 down) follow
// the list; the settings rows' own neighbours are fixed at build time.
void SetRecompLayout(uint8_t* base, bool recomp) {
  RecompRow& r = g_recomp;
  if (r.recomp_mode == recomp) return;
  uint32_t menu = r.menu;
  if (recomp) {
    std::memcpy(r.back_nav, base + r.back_row + 4, 4);
    Store32(base, menu + 0x84, r.recomp_rows);
    Store32(base, menu + 0x88, kNumSettings + 1);
    Store32(base, menu + 0x8C, kNumSettings + 1);
    base[r.back_row + 4 + 0] = kNumSettings - 1;  // Back: up -> last setting
    base[menu + 0x6C] = 0;
    RefreshSettingsArrowRecords(base);
  } else {
    Store32(base, menu + 0x84, r.normal_rows);
    Store32(base, menu + 0x88, r.normal_cap);
    Store32(base, menu + 0x8C, r.normal_count);
    std::memcpy(base + r.back_row + 4, r.back_nav, 4);
    base[menu + 0x6C] = r.normal_sel;
  }
  // (The menu's reported state id follows recomp_mode: see the sub_82120D08 hook.)
  r.recomp_mode = recomp;
  r.text_dirty = true;
  REXLOG_INFO("[spyro/menus] layout -> {}", recomp ? "Recomp Settings" : "Display Options");
}

// Are pieces kept in a draw list? Every piece shares +0x08 (a layer?) and has unique +0x0C /
// +0x10 (next/prev links?). Dump those for each piece of the video menu's rows, the shared
// object, and walk +0x0C and +0x10 chains printing each step's vtable (WORK_LOG 79).
void DumpPieceLinks(const uint8_t* base, uint32_t menu) {
  uint32_t rows = Load32(base, menu + 0x84), count = Load32(base, menu + 0x8C);
  if (count > 8 || !GuestReadable(base, rows, count * 4)) return;
  bool layer_done = false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t r = Load32(base, rows + i * 4);
    for (uint32_t off : {0x34u, 0x3Cu, 0x40u}) {
      uint32_t p = GuestReadable(base, r, 0x44) ? Load32(base, r + off) : 0;
      if (!GuestReadable(base, p, 0x20)) continue;
      uint32_t layer = Load32(base, p + 8), a = Load32(base, p + 0xC), b = Load32(base, p + 0x10);
      REXLOG_INFO("[spyro/links] row{} +0x{:02X} piece 0x{:08X} vt 0x{:08X}: +08 0x{:08X} +0C 0x{:08X} +10 0x{:08X}",
                  i, off, p, Load32(base, p), layer, a, b);
      for (uint32_t q : {a, b}) {
        if (GuestReadable(base, q, 0x30))
          REXLOG_INFO("[spyro/links]     0x{:08X}:{}", q, HexWords(base, q, 12));
      }
      if (!layer_done && GuestReadable(base, layer, 0x80)) {
        layer_done = true;
        REXLOG_INFO("[spyro/links] shared +08 object 0x{:08X}:{}", layer, HexWords(base, layer, 32));
      }
      for (uint32_t link : {0x0Cu, 0x10u}) {
        std::string chain;
        uint32_t cur = p;
        for (int step = 0; step < 24; ++step) {
          uint32_t nxt = GuestReadable(base, cur + link, 4) ? Load32(base, cur + link) : 0;
          if (!GuestReadable(base, nxt, 8)) break;
          char buf[40];
          std::snprintf(buf, sizeof(buf), " %08X(vt %08X)", nxt, Load32(base, nxt));
          chain += buf;
          if (nxt == p) break;
          cur = nxt;
        }
        REXLOG_INFO("[spyro/links]   follow +0x{:02X}:{}", link, chain);
      }
    }
  }
}

}  // namespace

extern "C" {

// Video menu array constructor (runs at boot): remember the object.
REX_FUNC(sub_822E0AD8) {
  __imp__sub_822E0AD8(ctx, base);
  g_video_menu.store(ctx.r3.u32);
  REXLOG_INFO("[spyro/rows] video menu object 0x{:08X}", ctx.r3.u32);
}

// Menu::Update(dt) for every live menu (r3 = menu). Builds the Recomp row from whichever menu
// is updating (the title screen's, at boot) once the video menu's rows exist - the video menu's
// own Update only runs while it is open, which would be too late. Placement runs while the
// video menu itself is updating.
REX_FUNC(sub_822C11E0) {
  uint32_t menu = ctx.r3.u32;
  uint32_t vm = g_video_menu.load();
  // The game builds a NEW set of menus when gameplay starts (and again after Save and quit): a
  // new Display Options object (array ctor hook above) that has no Recomp row. Forget the old
  // build so the new object gets its own (WORK_LOG 114). The old menu stays in g_row_menus so its
  // destructor hook still takes our row out before it frees the row list.
  // Only a Display Options object that is in the manager's list counts (in a level the list has
  // none and g_video_menu still names the freed main-menu one; the host is then Sound Options).
  bool vm_live = vm && g_manager && GuestReadable(base, g_manager + kStateArray, 0x25 * 4) &&
                 Load32(base, g_manager + kStateArray + kVideoMenuId * 4) == vm;
  if ((g_recomp.built || g_recomp.failed) && vm_live && vm != g_recomp.menu && !g_recomp.recomp_mode) {
    REXLOG_INFO("[spyro/recomp] new Display Options object 0x{:08X} (was 0x{:08X}): rebuilding the Recomp row",
                vm, g_recomp.menu);
    g_recomp = RecompRow{};
    g_glow_snapshot_ok[0] = g_glow_snapshot_ok[1] = false;
  }
  if (!g_recomp.built && !g_recomp.failed && vm && GuestReadable(base, vm, 0xB8) &&
      Load32(base, vm + 0x8C) == 2) {
    // Build only once the manager's state array points at this object (the Recomp menu state is
    // an alias of that slot).
    uint32_t mgr = Load32(base, vm + 0x08);
    bool in_array = GuestReadable(base, mgr + kStateArray, 0x25 * 4) &&
                    Load32(base, mgr + kStateArray + kVideoMenuId * 4) == vm;
    if (in_array && !BuildRecompRow(ctx, base, vm)) g_recomp.failed = true;
  }
  __imp__sub_822C11E0(ctx, base);
  if (g_recomp.built && menu == g_recomp.menu) PlaceRecompRow(base);
  // The pause menu's Options screen: turn its parked row 8 into Recomp Settings once per object
  // (a new one comes with each menu set), then keep it in place.
  if (Load32(base, menu) == kInGameOptionsVtable) {
    if (menu != g_igo.menu && menu != g_igo.tried && Load32(base, menu + 0x8C) == kIgoRows) {
      g_igo.tried = menu;
      if (!BuildInGameOptionsRow(ctx, base, menu))
        REXLOG_INFO("[spyro/recomp] pause Options 0x{:08X}: unexpected layout, no Recomp Settings button", menu);
    }
    if (menu == g_igo.menu) PlaceInGameOptionsRow(ctx, base);
  }
  // Quit to Desktop plank on the Game Menu / Pause screens: built once per menu object (tried for
  // a while: the pause Options it copies from may load later), placed every frame.
  for (int k = 0; k < 2; ++k) {
    if (Load32(base, menu) != (k ? kPauseMenuVtable : kMainMenuVtable)) continue;
    QuitPlank& q = g_quit[k];
    static uint32_t s_tries[2] = {0, 0};
    if (menu != q.tried) {
      q.tried = menu;
      s_tries[k] = 0;
    }
    if (menu != q.menu && s_tries[k] < 600 && (s_tries[k]++ % 60) == 0) BuildQuitPlank(ctx, base, menu, q);
    if (menu == q.menu) {
      bool current = g_manager && GuestReadable(base, g_manager + 0xAC0, 0x20) && base[g_manager + 0xACD] == kQuitScreenIds[k];
      if (!current) q.selected = false;
      PlaceQuitPlank(ctx, base, q, k);
    }
  }
  // Exit to Desktop as a 7th arc stone: OFF - with the extra row the game crashed loading a save
  // (guest read of 0x3FFFD7B5 leaving state 0x23), and the user wants a plank button instead.
  static const bool s_exit_arc = std::getenv("SPYRO_EXITARC") != nullptr;
  for (int k = 0; k < 2 && s_exit_arc; ++k) {
    if (Load32(base, menu) != (k ? kPauseMenuVtable : kMainMenuVtable)) continue;
    ExitRow& e = g_exit[k];
    // Tried on each of the first frames (the rows may still be coming), then given up.
    static uint32_t s_tries[2] = {0, 0};
    if (menu != e.tried) {
      e.tried = menu;
      s_tries[k] = 0;
    }
    if (menu != e.menu && s_tries[k] < 30) {
      ++s_tries[k];
      bool ok = BuildExitRow(ctx, base, menu, e, k == 1);
      if (ok) e.tried = menu;
    }
    if (menu == e.menu) PlaceExitRow(ctx, base, e);
  }
  // Log every change of the game manager's current menu id (WORK_LOG 92).
  if (g_manager) {
    uint32_t mgr = g_manager;
    if (GuestReadable(base, mgr + 0xAC0, 0x20)) {
      static int s_last_id = -1;
      int id = base[mgr + 0xACD];
      if (id != s_last_id) {
        REXLOG_INFO("[spyro/menus] current menu id 0x{:02X} -> 0x{:02X} (stack {} [{}])", s_last_id & 0xFF, id,
                    Load32(base, mgr + 0xAD8), HexWords(base, mgr + 0xAD0, 2));
        s_last_id = id;
      }
    }
  }
  // Debug (env SPYRO_LOGMENUS=1): each menu object the first time it updates - which object,
  // class, manager and row count is live (WORK_LOG 114: the in-game pause screens).
  static const bool s_log_menus = std::getenv("SPYRO_LOGMENUS") != nullptr;
  if (s_log_menus) {
    static std::vector<uint32_t> s_seen;
    if (std::find(s_seen.begin(), s_seen.end(), menu) == s_seen.end()) {
      s_seen.push_back(menu);
      uint32_t vt = Load32(base, menu), mgr = Load32(base, menu + 8);
      REXLOG_INFO("[spyro/menus] update: menu 0x{:08X} vt 0x{:08X} {} mgr 0x{:08X} rows {} | mgr id 0x{:02X}", menu, vt,
                  ClassNameForVtable(base, vt), mgr, Load32(base, menu + 0x8C),
                  GuestReadable(base, mgr + 0xAC0, 0x20) ? base[mgr + 0xACD] : 0xFF);
      if (Load32(base, menu + 0x8C) >= 10) DumpRowWidgets(base, menu);
    }
  }
  // Debug (env SPYRO_DUMPMENU=<hex state id>): dump the rows of that menu state's object once per
  // object, when it becomes the current menu (WORK_LOG 114: the pause menu's layout).
  static const uint32_t s_dump_id = std::getenv("SPYRO_DUMPMENU") ? uint32_t(std::strtoul(std::getenv("SPYRO_DUMPMENU"), nullptr, 16)) : 0xFFFFFFFFu;
  if (s_dump_id != 0xFFFFFFFFu && g_manager) {
    uint32_t mgr = g_manager;
    static uint32_t s_dumped = 0;
    if (GuestReadable(base, mgr + 0xAC0, 0x20) && base[mgr + 0xACD] == s_dump_id) {
      uint32_t obj = Load32(base, mgr + kStateArray + s_dump_id * 4);
      if (obj && obj != s_dumped && GuestReadable(base, obj, 0xB8) && Load32(base, obj + 0x8C) > 0) {
        s_dumped = obj;
        REXLOG_INFO("[spyro/rows] ===== menu state 0x{:02X} object 0x{:08X} vt 0x{:08X}: +0x58:{}", s_dump_id, obj,
                    Load32(base, obj), HexWords(base, obj + 0x58, 0x40 / 4));
        uint32_t vt = Load32(base, obj);
        REXLOG_INFO("[spyro/rows] vtable slots: +0xD4 0x{:08X} +0xDC 0x{:08X} +0xE0 0x{:08X} +0xE4 0x{:08X} "
                    "+0xEC 0x{:08X} +0xF0 0x{:08X} +0xF4 0x{:08X}", Load32(base, vt + 0xD4), Load32(base, vt + 0xDC),
                    Load32(base, vt + 0xE0), Load32(base, vt + 0xE4), Load32(base, vt + 0xEC), Load32(base, vt + 0xF0),
                    Load32(base, vt + 0xF4));
        DumpRowWidgets(base, obj);
      }
    }
  }
  // Debug (env SPYRO_OPENMENU=<hex state id>): ask the menu manager for that state ~12 s after the
  // menus exist, so a screen can be looked at without scripting the pad through the game.
  // Format "<id>@<seconds>", e.g. "1A@28" (default 12 s).
  static const uint32_t s_open_id = std::getenv("SPYRO_OPENMENU") ? uint32_t(std::strtoul(std::getenv("SPYRO_OPENMENU"), nullptr, 16)) : 0xFFFFFFFFu;
  static const double s_open_at = [] {
    const char* v = std::getenv("SPYRO_OPENMENU");
    const char* at = v ? std::strchr(v, '@') : nullptr;
    return at ? std::atof(at + 1) : 12.0;
  }();
  if (s_open_id != 0xFFFFFFFFu && g_recomp.built && g_recomp.mgr) {
    static const auto t0 = std::chrono::steady_clock::now();
    static bool s_done = false;
    if (!s_done && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > s_open_at) {
      s_done = true;
      RequestMenu(base, static_cast<uint8_t>(s_open_id));
    }
  }
  // Settings test (env SPYRO_FPSTEST=1): frame rate 30 / 60 / 120 / Unlimited for 10 s each
  // (the [spyro/fps] lines show the result), FXAA on and off, borderless and back.
  static const bool s_fpstest = std::getenv("SPYRO_FPSTEST") != nullptr;
  if (s_fpstest) {
    static const auto t0 = std::chrono::steady_clock::now();
    static int step = 0;
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    struct Step { double at; int id, value; };
    static const Step kSteps[] = {
        {1, spyro::kVSync, 0},         {2, spyro::kFrameRate, 1},     {5, spyro::kFrameRate, 0},
        {15, spyro::kFrameRate, 1},    {25, spyro::kFrameRate, 2},    {35, spyro::kFrameRate, 3},
        {45, spyro::kVSync, 1},        {55, spyro::kFrameRate, 2},    {65, spyro::kFrameRate, 1},
        {66, spyro::kVSync, 0},        {67, spyro::kAntiAliasing, 1}, {70, spyro::kAntiAliasing, 0},
        {71, spyro::kDisplayMode, 1},  {75, spyro::kDisplayMode, 0}};
    if (step < int(std::size(kSteps)) && t > kSteps[step].at) {
      REXLOG_INFO("[spyro/fpstest] t={:.0f}s set {} = {}", t, spyro::Def(kSteps[step].id).key, kSteps[step].value);
      spyro::SetSetting(kSteps[step].id, kSteps[step].value);
      ++step;
    }
  }
  // Test mode (env SPYRO_AUTOTEST=1, for self-runs without a player): opens Display Options,
  // then the Recomp menu, moves the selection, and screenshots each step (project\run\shots).
  static const bool s_autotest = std::getenv("SPYRO_AUTOTEST") != nullptr;
  if (s_autotest && g_recomp.built && g_recomp.mgr) {
    static const auto t0 = std::chrono::steady_clock::now();
    static int step = 0;
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (step == 0 && t > 15) {
      RequestMenu(base, kVideoMenuId);
      ScreenshotLater(3000, "auto_display");
      step = 1;
    } else if (step == 1 && t > 20) {
      RequestMenu(base, kRecompMenuId);
      ScreenshotLater(3000, "auto_recomp");
      step = 2;
    } else if (step == 2 && t > 25 && g_recomp.recomp_mode) {
      // Move the highlight from row 0 to row 3 the way the menu does (SendRowEvent 0 =
      // normal, 2 = highlighted).
      base[g_recomp.menu + 0x6C] = 3;
      GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, 0, 0, 0);
      GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, 3, 2, 0);
      ScreenshotLater(1500, "auto_recomp_row3");
      step = 3;
    } else if (step == 3 && t > 30 && g_recomp.recomp_mode) {
      // Press RIGHT on Online co-op (harmless; Display mode would resize the window). With
      // SPYRO_AUTOTEST_RES set, on Resolution instead (cycles 720p -> 1080p -> ...).
      static const int s_row = std::getenv("SPYRO_AUTOTEST_RES") ? spyro::kResolution : spyro::kOnlineCoop;
      GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, s_row, 4, 0);
      step = 4;
    } else if (step == 4 && t > 31 && g_recomp.recomp_mode) {
      static const int s_row = std::getenv("SPYRO_AUTOTEST_RES") ? spyro::kResolution : spyro::kOnlineCoop;
      GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, s_row, 0, 0);  // release
      step = 5;
    } else if (step == 5 && t > 32 && g_recomp.recomp_mode) {
      // Six RIGHT presses on Anti-aliasing (3 values -> wraps), press + release each.
      static int s_presses = 0;
      static double s_next = 0;
      if (t > s_next) {
        if (s_presses % 2 == 0) GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, spyro::kAntiAliasing, 4, 0);
        else GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, spyro::kAntiAliasing, 0, 0);
        s_next = t + 0.4;
        if (++s_presses == 12) step = 6;
      }
    } else if (step == 6 && t > 60 && g_recomp.recomp_mode) {
      // What B does once the cancel row is redirected: press the Back row.
      GuestCall(ctx, base, sub_822C2B50, g_recomp.menu, kNumSettings, 1, 1);
      step = 7;
    }
  }
}

REX_EXTERN(__imp__sub_822E0C90);
REX_EXTERN(__imp__sub_822E0BE0);
REX_EXTERN(__imp__sub_822DD210);  // main menu destructor (vt 0x82079920 slot 0)
REX_EXTERN(__imp__sub_822E0F38);  // in-level pause menu destructor (vt 0x8207A4A0 slot 0)
REX_EXTERN(__imp__sub_822C2DC0);
REX_EXTERN(__imp__sub_822ACFC0);
REX_EXTERN(__imp__sub_822AAF70);
REX_EXTERN(__imp__sub_822E92A0);

// CKBlurNodeFx start (vtable 0x8207B220 slot 24): switches on the screen-blur render effect
// 0x38D (its flags |= 0x100, +0x20 = this node's data) that the node's update then animates.
// Recomp Settings "Motion blur: Off": do only the node's own bookkeeping, never enable the
// effect; the update then sees the effect isn't ours and ends the node (WORK_LOG 98).
REX_FUNC(sub_822E92A0) {
  if (spyro::MotionBlurEnabled()) {
    __imp__sub_822E92A0(ctx, base);
    return;
  }
  uint32_t self = ctx.r3.u32;
  base[self + 0x0C] &= 0xFEu;
  Store32(base, self + 0x14, Load32(base, 0x82087CA8));  // the float the original stores (0)
}

// Game manager per-frame update (Function_822AAF70, once per game frame): runs the Recomp
// Settings frame limiter, and logs frame statistics every 5 s (WORK_LOG 98): frames per second
// and the game's own frame time (*(*(*(0x828354F8)+0x48)+4)+0x20, the dt every update reads).
REX_FUNC(sub_822AAF70) {
  __imp__sub_822AAF70(ctx, base);
  HideQuitPlanks(base);
  spyro::OnGameFrame();  // Recomp Settings: frame-rate limiter, plugin settings on frame 1
  spyro::LockOnFrame(base);
  if (g_manager && GuestReadable(base, g_manager + 0xAC0, 0x20)) g_current_menu_id.store(base[g_manager + 0xACD]);
  static auto s_t0 = std::chrono::steady_clock::now();
  static int s_frames = 0;
  static float s_dt_min = 1e9f, s_dt_max = 0, s_dt_sum = 0;
  uint32_t g = Load32(base, 0x828354F8);
  uint32_t clk = GuestReadable(base, g + 0x48, 4) ? Load32(base, g + 0x48) : 0;
  uint32_t tm = GuestReadable(base, clk + 4, 4) ? Load32(base, clk + 4) : 0;
  float dt = GuestReadable(base, tm + 0x20, 4) ? LoadF32(base, tm + 0x20) : -1.0f;
  ++s_frames;
  s_dt_min = std::min(s_dt_min, dt);
  s_dt_max = std::max(s_dt_max, dt);
  s_dt_sum += dt;
  double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_t0).count();
  if (el >= 5.0) {
    REXLOG_INFO("[spyro/fps] {:.1f} fps | game dt avg {:.4f} min {:.4f} max {:.4f} | timer +0x00:{}", s_frames / el,
                s_dt_sum / s_frames, s_dt_min, s_dt_max, HexWords(base, tm, 12));
    s_t0 = std::chrono::steady_clock::now();
    s_frames = 0;
    s_dt_min = 1e9f;
    s_dt_max = 0;
    s_dt_sum = 0;
  }
}
REX_EXTERN(__imp__sub_820B0E58);
REX_EXTERN(__imp__sub_82120D08);
REX_EXTERN(__imp__sub_822C3918);
REX_EXTERN(__imp__sub_822C2B50);

// Menu::SendRowEvent(menu, row, event, flag) - event 1 = press. Pressing our Recomp Settings
// button on the pause Options screen (its row 8) opens the Recomp Settings menu; the request is
// made AFTER the original so it wins over whatever the game's own row-8 handler asks for.
REX_FUNC(sub_822C2B50) {
  uint32_t menu = ctx.r3.u32, row = ctx.r4.u32 & 0xFF, event = ctx.r5.u32 & 0xFF, lr = uint32_t(ctx.lr);
  // A on the Quit to Desktop plank (the menu still has the bottom stone as its selection).
  for (const QuitPlank& q : g_quit)
    if (q.menu && menu == q.menu && q.selected && event == 1) {
      ExitToDesktop();
      return;
    }
  // Exit to Desktop pressed: close the game (the copied Quit level row must not quit the level).
  for (const ExitRow& e : g_exit)
    if (e.menu && menu == e.menu && row == e.index && event == 1) {
      ExitToDesktop();
      return;
    }
  __imp__sub_822C2B50(ctx, base);
  if (g_igo.menu && menu == g_igo.menu && row == kIgoHidden) {
    REXLOG_INFO("[spyro/recomp] pause Options: Recomp Settings button event {} (from LR 0x{:08X}, selection {} +0x60 {})",
                event, lr, base[menu + 0x6C], base[menu + 0x60]);
    if (event == 1 && g_recomp.built && g_recomp.mgr) {
      // Which menu objects the manager holds right now (the Recomp menu borrows the Display
      // Options object: in a level it may not be loaded - opening it crashed, WORK_LOG 114).
      uint32_t mgr = g_recomp.mgr;
      for (uint32_t i = 0; i < 0x25; ++i) {
        uint32_t obj = Load32(base, mgr + kStateArray + i * 4);
        uint32_t vt = GuestReadable(base, obj, 4) ? Load32(base, obj) : 0;
        uint32_t ui = GuestReadable(base, obj, 0x10) ? Load32(base, obj + 0x0C) : 0;
        if (obj)
          REXLOG_INFO("[spyro/menus]   in-level state[0x{:02X}] = 0x{:08X} vt 0x{:08X} {} ui 0x{:08X}{}", i, obj, vt,
                      ClassNameForVtable(base, vt), ui, obj == g_recomp.menu ? "  <== Recomp/Display menu" : "");
      }
      // The host: Display Options if this menu set has one, else the level's Sound Options.
      uint32_t slot = mgr + kStateArray + kRecompMenuId * 4;
      uint32_t video = Load32(base, mgr + kStateArray + kVideoMenuId * 4);
      uint32_t sound = Load32(base, mgr + kStateArray + kSoundMenuId * 4);
      bool ready = video && video == g_recomp.menu && Load32(base, slot) == video;
      if (!ready && !video && sound) {
        ready = (g_recomp.menu == sound && Load32(base, slot) == sound) || BuildRecompHostOnSound(ctx, base, sound);
      }
      if (ready) {
        g_recomp_return = kInGameOptionsId;
        RequestMenu(base, kRecompMenuId);
      } else {
        REXLOG_INFO("[spyro/recomp] Recomp Settings not available here (0x21 0x{:08X} 0x1D 0x{:08X} 0x22 0x{:08X})",
                    video, sound, Load32(base, slot));
      }
    }
  }
}

// Menu vt+0xF4 (Function_822C3918, r3 = menu, r4 = cancel row), called every frame: if B is
// held it presses that row (SendRowEvent(menu, row, 1, 1)). The row comes from vt+0xF0, which
// for the video menu is 0x822A7160 = "return 1" - shared with vt+0xEC "has a cancel row"
// (must be exactly 1), so it can't be changed there. In the Recomp menu row 1 is a setting and
// Back is the last row: redirect here (WORK_LOG 96).
REX_FUNC(sub_822C3918) {
  if (g_recomp.recomp_mode && ctx.r3.u32 == g_recomp.menu && (ctx.r4.u32 & 0xFF) == g_recomp.back_index)
    ctx.r4.u64 = kNumSettings;
  __imp__sub_822C3918(ctx, base);
}

// Sound Options vtable slots used while it hosts the Recomp Settings menu (in a level):
// slot 57 = the per-row callback (0x822E0948), vt+0xDC = "my state id" (0x82187E80, 0x1D).
REX_EXTERN(__imp__sub_822E0948);
REX_EXTERN(__imp__sub_82187E80);
REX_FUNC(sub_822E0948) {
  if (g_recomp.built && g_recomp.recomp_mode && ctx.r3.u32 == g_recomp.menu) {
    RecompMenuRowEvent(ctx, base, ctx.r4.u32 & 0xFF, ctx.r5.u32 & 0xFF, ctx.r6.u32);
    return;
  }
  // Sound Options showing as itself again: its own title back.
  if (g_title.changed && ctx.r3.u32 == g_recomp.menu && (ctx.r4.u32 & 0xFF) == 0 && ctx.r6.u32 == 0)
    SetHostTitle(ctx, base, false);
  __imp__sub_822E0948(ctx, base);
}
REX_FUNC(sub_82187E80) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_82187E80(ctx, base);
  if (g_recomp.recomp_mode && self == g_recomp.menu) ctx.r3.u64 = kRecompMenuId;
}

// Video menu vt+0xDC "my state id" (returns constant 0x21). The per-frame state check
// (Function_820B0EE8) switches to whatever the active menu returns here, so while the video
// menu is showing as Recomp Settings it must answer 0x22, or the game flips straight back to
// Display Options every frame (WORK_LOG 94).
REX_FUNC(sub_82120D08) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_82120D08(ctx, base);
  if (g_recomp.recomp_mode && self == g_recomp.menu) ctx.r3.u64 = kRecompMenuId;
}

// Menu state switcher (r3 = switcher = mgr+0xAC4, r4 = new id). Logs who asks for each switch
// (return address), to find what pulls the Recomp menu back to Display Options (WORK_LOG 94).
REX_FUNC(sub_820B0E58) {
  uint32_t sw = ctx.r3.u32;
  if (g_recomp.mgr && sw == g_recomp.mgr + 0xAC4) {
    static int s_logged = 0;
    if (s_logged < 40) {
      ++s_logged;
      uint32_t mgr = g_recomp.mgr;
      REXLOG_INFO("[spyro/switch] 0x{:02X} -> 0x{:02X} from LR 0x{:08X} (stack {} [{}], prev 0x{:02X})",
                  base[mgr + 0xACD], ctx.r4.u32 & 0xFF, uint32_t(ctx.lr), Load32(base, mgr + 0xAD8),
                  HexWords(base, mgr + 0xAD0, 2), base[mgr + 0xADC]);
    }
  }
  __imp__sub_820B0E58(ctx, base);
}

// Generic menu-state handler (states 0x00..0x23): r3 = manager, r4 = phase (1 enter, 2 exit,
// 0 = every frame), r5 = the other state; acts on arr[current id]. Entering our Recomp state
// swaps the video menu to the Recomp row list BEFORE it opens; entering any other state
// swaps it back.
REX_FUNC(sub_822ACFC0) {
  uint32_t mgr = ctx.r3.u32, phase = ctx.r4.u32;
  bool ours = g_recomp.built && g_recomp.mgr && mgr == g_recomp.mgr;
  uint8_t id = ours ? base[mgr + 0xACD] : 0;
  if (ours) {
    static uint32_t s_last = 0xFFFFFFFF;
    uint32_t key = (uint32_t(id) << 16) | (phase << 8) | (ctx.r5.u32 & 0xFF);
    if (key != s_last) {
      s_last = key;
      REXLOG_INFO("[spyro/menus] handler: state 0x{:02X} phase {} other 0x{:02X}", id, phase, ctx.r5.u32 & 0xFF);
    }
  }
  if (ours && phase == 1) SetRecompLayout(base, id == kRecompMenuId);
  __imp__sub_822ACFC0(ctx, base);
}

// Menu input -> direction code (0..3, 0xFF = none), r3 = menu. Logs each code produced while
// the display menu is up, to learn which code is up/down/left/right (WORK_LOG 83).
REX_FUNC(sub_822C2DC0) {
  uint32_t menu = ctx.r3.u32;
  __imp__sub_822C2DC0(ctx, base);
  uint32_t dir = ctx.r3.u32 & 0xFF;
  // Quit to Desktop plank (Game Menu / Pause): DOWN on the bottom stone (whose down neighbour
  // wraps back up) selects the plank; while it is selected UP goes back and nothing else moves.
  // Direction codes follow the neighbour bytes: 0 up, 1 down, 2 left, 3 right.
  for (QuitPlank& q : g_quit) {
    if (!q.menu || menu != q.menu) continue;
    uint32_t sel = base[menu + 0x6C], count = Load32(base, menu + 0x8C), rows = Load32(base, menu + 0x84);
    if (q.selected) {
      if (dir == 0) {
        q.selected = false;
        GuestCall(ctx, base, sub_822C2B50, menu, q.bottom, 2, 0);  // the stone highlighted again
      }
      if (dir != 0xFF) {
        REXLOG_INFO("[spyro/menu] Quit to Desktop: direction {} -> {}", dir, q.selected ? "stays" : "back to the stone");
        ctx.r3.u64 = 0xFF;
      }
      return;
    }
    if (dir == 1 && sel < count && GuestReadable(base, rows, count * 4)) {
      uint32_t row = Load32(base, rows + sel * 4);
      if (GuestReadable(base, row, 8) && base[row + 5] <= sel) {
        q.selected = true;
        q.bottom = sel;
        GuestCall(ctx, base, sub_822C2B50, menu, sel, 0, 0);  // the stone back to normal
        REXLOG_INFO("[spyro/menu] Quit to Desktop selected (from row {})", sel);
        ctx.r3.u64 = 0xFF;
        return;
      }
    }
  }
  // Pause Options: LEFT on the Tutorial switch moves to our Recomp Settings button (to its left)
  // instead of flipping the switch; RIGHT and A still flip it.
  if (g_igo.menu && menu == g_igo.menu && dir == 2 && base[menu + 0x6C] == kIgoTutorial) {
    GuestCall(ctx, base, sub_822C2B50, menu, kIgoTutorial, 0, 0);  // Tutorial -> normal
    base[menu + 0x6C] = kIgoHidden;
    GuestCall(ctx, base, sub_822C2B50, menu, kIgoHidden, 2, 0);    // ours -> highlighted
    ctx.r3.u64 = 0xFF;
    return;
  }
  if (g_recomp.built && menu == g_recomp.menu && dir != 0xFF) {
    REXLOG_INFO("[spyro/recomp] input direction code {} (selected row {})", dir,
                base[g_recomp.menu + 0x6C]);
  }
}

// Main menu / pause menu destructors: their original row list back first (Exit to Desktop).
REX_FUNC(sub_822DD210) {
  RestoreExitRows(base, ctx.r3.u32);
  for (QuitPlank& q : g_quit)  // the plank's pieces go with the screen's scene
    if (q.menu == ctx.r3.u32) q = QuitPlank{};
  __imp__sub_822DD210(ctx, base);
}

REX_FUNC(sub_822E0F38) {
  RestoreExitRows(base, ctx.r3.u32);
  for (QuitPlank& q : g_quit)
    if (q.menu == ctx.r3.u32) q = QuitPlank{};
  __imp__sub_822E0F38(ctx, base);
}

// Video menu destructor: take our row out of the row list first, so the menu never destroys
// it (it shares Back's per-state heap arrays, which Back's own destructor frees).
REX_FUNC(sub_822E0BE0) {
  uint32_t self = ctx.r3.u32;
  if (g_recomp.built && self == g_recomp.menu) SetRecompLayout(base, false);
  auto it = std::find(g_row_menus.begin(), g_row_menus.end(), self);
  if (it != g_row_menus.end()) {  // any Display Options object we added a row to, old or current
    if (GuestReadable(base, self, 0xB8) && Load32(base, self + 0x8C) == 3) {
      Store32(base, self + 0x8C, 2);
      REXLOG_INFO("[spyro/recomp] video menu 0x{:08X} destroyed: Recomp row detached first", self);
    }
    g_row_menus.erase(it);
  }
  __imp__sub_822E0BE0(ctx, base);
}

// CKS08MenuOptionVideo360 vtable[57]: called per row per frame while the menu is live.
REX_FUNC(sub_822E0C90) {
  // r4 = row index, r5 = row state (0 normal, 2 highlighted, 4 pressed), r6 = phase
  // (2 before an event, 1 after it, 0 per-frame update).
  uint32_t row = ctx.r4.u32 & 0xFF, state = ctx.r5.u32 & 0xFF, phase = ctx.r6.u32;
  if (g_recomp.built && ctx.r3.u32 == g_recomp.menu) {
    bool pressed = state == 4 && phase == 1;
    if (g_recomp.recomp_mode) {
      RecompMenuRowEvent(ctx, base, row, state, phase);
      return;
    }
    // Display Options showing as itself again: its own title back.
    if (g_title.changed && row == 0 && phase == 0) SetHostTitle(ctx, base, false);
    // Display Options: (re)set our row's text whenever it becomes visible, while glyphs can build.
    static uint32_t s_label_state = 0xFFFFFFFF;
    uint32_t visible = GuestReadable(base, g_recomp.label, 8) ? base[g_recomp.label + 5] : 0;
    uint32_t our_index = 2;
    if (row == our_index && phase == 0 && (visible != s_label_state || g_recomp.text_dirty)) {
      s_label_state = visible;
      g_recomp.text_dirty = false;
      SetRecompLabel(ctx, base);
      REXLOG_INFO("[spyro/recomp] label text set (piece flags byte 0x{:02X})", visible);
    }
    if (pressed && row == 2) {
      REXLOG_INFO("[spyro/recomp] *** Recomp Settings PRESSED ***");
      g_recomp_return = kVideoMenuId;
      if (g_recomp.mgr) RequestMenu(base, kRecompMenuId);
    }
    static uint32_t s_last_sel = 0xFFFFFFFF;
    uint32_t sel = base[g_recomp.menu + 0x6C];
    if (sel != s_last_sel) {
      s_last_sel = sel;
      REXLOG_INFO("[spyro/recomp] menu selection -> row {}", sel);
    }
    static uint32_t s_last_state = 0xFFFFFFFF;
    if (row == our_index && state != s_last_state) {
      s_last_state = state;
      REXLOG_INFO("[spyro/recomp] Recomp row state -> {} (phase {})", state, phase);
    }
  }
  __imp__sub_822E0C90(ctx, base);
}

}  // extern "C"

namespace spyro {
int CurrentMenuId() { return g_current_menu_id.load(); }
}  // namespace spyro
