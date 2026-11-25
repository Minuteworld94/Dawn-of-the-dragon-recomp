// A minimal pkg-config for cross-compiling the Linux build on Windows (WORK_LOG 158): CMake's
// FindPkgConfig (SDK, SDL3) runs it to find X11 / Wayland / ... in the Linux sysroot. Reads .pc files
// from PKG_CONFIG_LIBDIR (';' or ':'-separated after a drive letter is allowed) and prefixes
// -I / -L paths with PKG_CONFIG_SYSROOT_DIR. Supports what FindPkgConfig asks: --version, --exists
// (with version constraints), --modversion, --variable=NAME, --cflags[-only-I|-only-other],
// --libs[-only-L|-only-l|-only-other], --static, --print-errors / --short-errors / --silence-errors.
// Paths with spaces are written with backslash-escaped spaces (as pkg-config does).
// Build: TOOLS\linux\pkgconfig\build.ps1
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Pc {
  std::map<std::string, std::string> vars, fields;
  std::string dir;
};

std::vector<std::string> g_dirs;
std::string g_sysroot;
std::map<std::string, Pc> g_cache;
bool g_errors = true;

std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

std::string Trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Expand(const std::string& s, const Pc& pc, int depth = 0) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '$') {
      out += '$';
      ++i;
    } else if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
      size_t end = s.find('}', i);
      if (end == std::string::npos) break;
      std::string name = s.substr(i + 2, end - i - 2);
      auto it = pc.vars.find(name);
      std::string v = it != pc.vars.end() ? it->second : name == "pcfiledir" ? pc.dir : "";
      out += depth < 16 ? Expand(v, pc, depth + 1) : v;
      i = end;
    } else {
      out += s[i];
    }
  }
  return out;
}

bool Load(const std::string& name, Pc& out) {
  if (auto it = g_cache.find(name); it != g_cache.end()) {
    out = it->second;
    return true;
  }
  for (const std::string& d : g_dirs) {
    std::ifstream f(d + "/" + name + ".pc");
    if (!f) continue;
    Pc pc;
    pc.dir = d;
    std::string line;
    while (std::getline(f, line)) {
      if (size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
      size_t colon = line.find(':'), eq = line.find('=');
      if (eq != std::string::npos && (colon == std::string::npos || eq < colon)) {
        pc.vars[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
      } else if (colon != std::string::npos) {
        pc.fields[Trim(line.substr(0, colon))] = Trim(line.substr(colon + 1));
      }
    }
    g_cache[name] = pc;
    out = pc;
    return true;
  }
  return false;
}

std::string Field(const Pc& pc, const char* key) {
  auto it = pc.fields.find(key);
  return it == pc.fields.end() ? "" : Expand(it->second, pc);
}

// "a >= 1.2, b c" -> {name, op, version}
struct Req {
  std::string name, op, version;
};
std::vector<Req> ParseReqs(const std::string& s) {
  std::vector<std::string> tok;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == ',') {
      if (!cur.empty()) tok.push_back(cur), cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) tok.push_back(cur);
  std::vector<Req> out;
  for (size_t i = 0; i < tok.size(); ++i) {
    Req r{tok[i], "", ""};
    if (i + 2 < tok.size() && (tok[i + 1] == ">=" || tok[i + 1] == "<=" || tok[i + 1] == "=" || tok[i + 1] == ">" ||
                               tok[i + 1] == "<" || tok[i + 1] == "!=")) {
      r.op = tok[i + 1];
      r.version = tok[i + 2];
      i += 2;
    }
    out.push_back(r);
  }
  return out;
}

int CompareVersions(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    while (i < a.size() && !std::isalnum(static_cast<unsigned char>(a[i]))) ++i;
    while (j < b.size() && !std::isalnum(static_cast<unsigned char>(b[j]))) ++j;
    std::string x, y;
    bool num = i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]));
    while (i < a.size() && std::isalnum(static_cast<unsigned char>(a[i])) &&
           (std::isdigit(static_cast<unsigned char>(a[i])) != 0) == num)
      x += a[i++];
    while (j < b.size() && std::isalnum(static_cast<unsigned char>(b[j])) &&
           (std::isdigit(static_cast<unsigned char>(b[j])) != 0) == num)
      y += b[j++];
    if (x.empty() && y.empty()) break;
    if (x.empty()) return -1;
    if (y.empty()) return 1;
    if (num) {
      unsigned long long p = std::strtoull(x.c_str(), nullptr, 10), q = std::strtoull(y.c_str(), nullptr, 10);
      if (p != q) return p < q ? -1 : 1;
    } else if (x != y) {
      return x < y ? -1 : 1;
    }
  }
  return 0;
}

bool Satisfies(const std::string& have, const Req& r) {
  if (r.op.empty()) return true;
  int c = CompareVersions(have, r.version);
  if (r.op == ">=") return c >= 0;
  if (r.op == "<=") return c <= 0;
  if (r.op == ">") return c > 0;
  if (r.op == "<") return c < 0;
  if (r.op == "=") return c == 0;
  return c != 0;
}

std::vector<std::string> Split(const std::string& s) {  // shell-like (quotes, backslash escapes)
  std::vector<std::string> out;
  std::string cur;
  bool any = false;
  char quote = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == quote) quote = 0;
      else cur += c;
    } else if (c == '"' || c == '\'') {
      quote = c;
      any = true;
    } else if (c == '\\' && i + 1 < s.size()) {
      cur += s[++i];
      any = true;
    } else if (c == ' ' || c == '\t') {
      if (any || !cur.empty()) out.push_back(cur), cur.clear(), any = false;
    } else {
      cur += c;
    }
  }
  if (any || !cur.empty()) out.push_back(cur);
  return out;
}

std::string Escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == ' ') out += '\\';
    out += c;
  }
  return out;
}

// Collects flags of the module and what it requires (depth-first, each module once).
void Collect(const std::string& name, bool cflags, bool statik, std::vector<std::string>& out,
             std::set<std::string>& seen) {
  if (!seen.insert(name).second) return;
  Pc pc;
  if (!Load(name, pc)) return;
  for (const std::string& f : Split(Field(pc, cflags ? "Cflags" : "Libs"))) out.push_back(f);
  if (!cflags && statik)
    for (const std::string& f : Split(Field(pc, "Libs.private"))) out.push_back(f);
  for (const Req& r : ParseReqs(Field(pc, "Requires"))) Collect(r.name, cflags, statik, out, seen);
  if (cflags || statik)
    for (const Req& r : ParseReqs(Field(pc, "Requires.private"))) Collect(r.name, cflags, statik, out, seen);
}

std::string WithSysroot(const std::string& flag) {
  if (g_sysroot.empty()) return flag;
  for (const char* p : {"-I", "-L"})
    if (flag.rfind(p, 0) == 0 && flag.size() > 2 && flag[2] == '/') return std::string(p) + g_sysroot + flag.substr(2);
  if (flag.rfind("-isystem", 0) == 0 && flag.size() > 8 && flag[8] == '/') return "-isystem" + g_sysroot + flag.substr(8);
  return flag;
}

}  // namespace

int main(int argc, char** argv) {
  std::string libdir = Env("PKG_CONFIG_LIBDIR");
  if (libdir.empty()) libdir = Env("PKG_CONFIG_PATH");
  // ';' separates; ':' too, except right after a drive letter ("E:/...").
  std::string cur;
  for (size_t i = 0; i <= libdir.size(); ++i) {
    char c = i < libdir.size() ? libdir[i] : ';';
    bool drive = c == ':' && cur.size() == 1 && std::isalpha(static_cast<unsigned char>(cur[0]));
    if ((c == ';' || c == ':') && !drive) {
      if (!cur.empty()) g_dirs.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  g_sysroot = Env("PKG_CONFIG_SYSROOT_DIR");
  while (!g_sysroot.empty() && (g_sysroot.back() == '/' || g_sysroot.back() == '\\')) g_sysroot.pop_back();

  enum { kNone, kExists, kModversion, kVariable, kFlags } mode = kNone;
  bool cflags = false, libs = false, only_I = false, only_cother = false, only_L = false, only_l = false,
       only_lother = false, statik = false;
  std::string variable, spec;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--version") return std::printf("0.29.2\n"), 0;
    if (a == "--exists") mode = kExists;
    else if (a == "--modversion") mode = kModversion;
    else if (a.rfind("--variable=", 0) == 0) mode = kVariable, variable = a.substr(11);
    else if (a == "--variable" && i + 1 < argc) mode = kVariable, variable = argv[++i];
    else if (a == "--cflags") mode = kFlags, cflags = true;
    else if (a == "--cflags-only-I") mode = kFlags, cflags = only_I = true;
    else if (a == "--cflags-only-other") mode = kFlags, cflags = only_cother = true;
    else if (a == "--libs") mode = kFlags, libs = true;
    else if (a == "--libs-only-L") mode = kFlags, libs = only_L = true;
    else if (a == "--libs-only-l") mode = kFlags, libs = only_l = true;
    else if (a == "--libs-only-other") mode = kFlags, libs = only_lother = true;
    else if (a == "--static") statik = true;
    else if (a == "--silence-errors") g_errors = false;
    else if (a == "--print-errors" || a == "--short-errors") g_errors = true;
    else if (a.rfind("--", 0) == 0) continue;  // other options: ignored
    else spec += (spec.empty() ? "" : " ") + a;
  }
  std::vector<Req> reqs = ParseReqs(spec);
  if (reqs.empty()) {
    if (g_errors) std::fprintf(stderr, "Must specify package names on the command line\n");
    return 1;
  }
  for (const Req& r : reqs) {
    Pc pc;
    if (!Load(r.name, pc)) {
      if (g_errors) std::fprintf(stderr, "Package %s was not found in the pkg-config search path.\n", r.name.c_str());
      return 1;
    }
    std::string have = Field(pc, "Version");
    if (!Satisfies(have, r)) {
      if (g_errors)
        std::fprintf(stderr, "Requested '%s %s %s' but version of %s is %s\n", r.name.c_str(), r.op.c_str(),
                     r.version.c_str(), r.name.c_str(), have.c_str());
      return 1;
    }
  }
  if (mode == kExists || mode == kNone) return 0;
  if (mode == kModversion) {
    for (const Req& r : reqs) {
      Pc pc;
      Load(r.name, pc);
      std::printf("%s\n", Field(pc, "Version").c_str());
    }
    return 0;
  }
  if (mode == kVariable) {
    std::string line;
    for (const Req& r : reqs) {
      Pc pc;
      Load(r.name, pc);
      auto it = pc.vars.find(variable);
      std::string v = it != pc.vars.end() ? Expand(it->second, pc) : variable == "pcfiledir" ? pc.dir : "";
      if (!line.empty() && !v.empty()) line += ' ';
      line += v;
    }
    std::printf("%s\n", line.c_str());
    return 0;
  }
  std::vector<std::string> all, seen_flags, result;
  std::set<std::string> seen;
  for (const Req& r : reqs) Collect(r.name, cflags, statik, all, seen);
  std::set<std::string> dedup;
  for (const std::string& raw : all) {
    std::string f = WithSysroot(raw);
    bool is_I = f.rfind("-I", 0) == 0 || f.rfind("-isystem", 0) == 0, is_L = f.rfind("-L", 0) == 0,
         is_l = f.rfind("-l", 0) == 0;
    bool keep = cflags ? (only_I ? is_I : only_cother ? !is_I : true)
                       : (only_L ? is_L : only_l ? is_l : only_lother ? (!is_L && !is_l) : true);
    if (!keep) continue;
    if ((is_I || is_L) && !dedup.insert(f).second) continue;  // repeated paths: once
    result.push_back(Escape(f));
  }
  std::string line;
  for (const std::string& f : result) line += (line.empty() ? "" : " ") + f;
  std::printf("%s\n", line.c_str());
  return 0;
}
