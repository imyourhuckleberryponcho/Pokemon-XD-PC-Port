#include "modsafety.h"

#include "mod_layout.h"

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace engine::modcheck {

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

std::vector<std::string> Split(const std::string& path) {
  std::vector<std::string> out;
  size_t from = 0;
  while (from <= path.size()) {
    const size_t to = path.find('/', from);
    out.push_back(path.substr(from, to == std::string::npos ? std::string::npos : to - from));
    if (to == std::string::npos) {
      break;
    }
    from = to + 1;
  }
  return out;
}

std::string Extension(const std::string& lowerName) {
  const size_t dot = lowerName.rfind('.');
  return dot == std::string::npos || dot == 0 ? std::string() : lowerName.substr(dot + 1);
}

uint32_t Le16(const uint8_t* p) { return static_cast<uint32_t>(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return Le16(p) | Le16(p + 2) << 16; }
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

}

std::string LowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

std::string PathText(const fs::path& path) {
  const std::u8string u = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& out, uint64_t maxBytes) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return false;
  }
  const std::streamoff size = in.tellg();
  if (size < 0 || static_cast<uint64_t>(size) > maxBytes) {
    return false;
  }
  out.resize(static_cast<size_t>(size));
  in.seekg(0);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(out.data()), size);
  }
  return static_cast<bool>(in) || size == 0;
}

uint32_t Crc32(const uint8_t* data, size_t size, uint32_t seed) {
  uint32_t crc = static_cast<uint32_t>(::crc32(seed, data, static_cast<uInt>(std::min<size_t>(size, 0x7FFFFFFF))));
  if (size > 0x7FFFFFFF) {
    crc = Crc32(data + 0x7FFFFFFF, size - 0x7FFFFFFF, crc);
  }
  return crc;
}

std::string Sha256Hex(const uint8_t* data, size_t size) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
      0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
      0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto rotr = [](uint32_t x, int n) { return x >> n | x << (32 - n); };
  const auto block = [&](const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = Be32(p + i * 4);
    }
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  };
  size_t i = 0;
  for (; i + 64 <= size; i += 64) {
    block(data + i);
  }
  uint8_t tail[128] = {0};
  const size_t rest = size - i;
  if (rest > 0) {
    std::memcpy(tail, data + i, rest);
  }
  tail[rest] = 0x80;
  const size_t total = rest < 56 ? 64 : 128;
  const uint64_t bits = static_cast<uint64_t>(size) * 8;
  for (int k = 0; k < 8; k++) {
    tail[total - 1 - k] = static_cast<uint8_t>(bits >> (8 * k));
  }
  block(tail);
  if (total == 128) {
    block(tail + 64);
  }
  char hex[65];
  for (int k = 0; k < 8; k++) {
    std::snprintf(hex + k * 8, 9, "%08x", h[k]);
  }
  return hex;
}

std::string ExecutableNameReason(const std::string& path) {
  static const std::set<std::string> kExt = {
      "exe", "dll", "so", "dylib", "bat", "cmd", "ps1", "psm1", "vbs", "vbe", "js", "jse", "mjs", "jar", "sh", "bash", "zsh",
      "command", "app", "msi", "msp", "scr", "lnk", "com", "pif", "cpl", "reg", "hta", "wsf", "wsh", "py", "pyw", "pl", "rb", "php",
      "ocx", "sys", "drv", "gadget", "application", "appref-ms", "url", "desktop", "webloc", "scf", "inf", "apk", "appimage",
      "pkg", "dmg", "deb", "rpm", "elf", "node", "cgi"};
  static const char* const kBundles[] = {".app", ".framework", ".bundle", ".kext", ".plugin", ".xpc", ".appex", ".prefpane", ".saver"};
  const std::string lower = LowerAscii(path);
  const std::vector<std::string> segs = Split(lower);
  for (size_t i = 0; i < segs.size(); i++) {
    std::string seg = segs[i];
    while (!seg.empty() && (seg.back() == ' ' || seg.back() == '.')) {
      seg.pop_back();
    }
    for (const char* b : kBundles) {
      const size_t n = std::strlen(b);
      if (seg.size() > n && seg.compare(seg.size() - n, n, b) == 0) {
        return "part of a Mac application bundle (" + std::string(b) + ")";
      }
    }
    if (seg == "macos" && i > 0 && segs[i - 1] == "contents") {
      return "part of a Mac application bundle (Contents/MacOS)";
    }
    if (i + 1 == segs.size()) {
      const std::string ext = Extension(seg);
      if (kExt.count(ext) != 0) {
        return "a program or script type (." + ext + ")";
      }
      if (seg.find(".so.") != std::string::npos) {
        return "a Linux library (.so)";
      }
    }
  }
  return std::string();
}

std::string ExecutableContentReason(const uint8_t* d, size_t n) {
  if (n >= 4 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F') {
    return "a Linux program (ELF)";
  }
  if (n >= 2 && d[0] == 'M' && d[1] == 'Z') {
    if (n >= 0x40) {
      const uint32_t pe = Le32(d + 0x3C);
      if (pe >= 0x40 && static_cast<uint64_t>(pe) + 4 <= n && std::memcmp(d + pe, "PE\0\0", 4) == 0) {
        return "a Windows program (PE)";
      }
      for (size_t i = 0; i + 12 < std::min<size_t>(n, 0x100); i++) {
        if (std::memcmp(d + i, "This program", 12) == 0) {
          return "a DOS or Windows program";
        }
      }
    }
  }
  if (n >= 4) {
    const uint32_t m = Be32(d);
    if (m == 0xFEEDFACEu || m == 0xFEEDFACFu || m == 0xCEFAEDFEu || m == 0xCFFAEDFEu) {
      return "a Mac program (Mach-O)";
    }
    if (m == 0xCAFEBABEu) {
      return "a Mac universal program or Java class";
    }
    if (d[0] == 0 && d[1] == 'a' && d[2] == 's' && d[3] == 'm') {
      return "a WebAssembly program";
    }
  }
  if (n >= 2 && d[0] == '#' && d[1] == '!') {
    return "a script that starts with #! (the system would run it)";
  }
  static const uint8_t kOle[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
  if (n >= 8 && std::memcmp(d, kOle, 8) == 0) {
    return "an Office / installer file (OLE) that can carry programs";
  }
  static const uint8_t kLnk[8] = {0x4C, 0, 0, 0, 0x01, 0x14, 0x02, 0};
  if (n >= 8 && std::memcmp(d, kLnk, 8) == 0) {
    return "a Windows shortcut (.lnk)";
  }
  return std::string();
}

std::string ArchiveReason(const std::string& path, const uint8_t* d, size_t n) {
  static const std::set<std::string> kExt = {"zip", "7z", "rar", "tar", "gz", "tgz", "bz2", "tbz", "tbz2", "xz", "txz", "zst", "lz",
                                             "lzma", "cab", "arj", "ace", "lha", "lzh", "z", "iso", "img", "wim", "vhd", "vhdx", "cpio"};
  const std::string ext = Extension(LowerAscii(path));
  if (kExt.count(ext) != 0) {
    return "a packed archive (." + ext + ")";
  }
  if (n >= 4) {
    if (d[0] == 'P' && d[1] == 'K' && (d[2] == 3 || d[2] == 5) && (d[3] == 4 || d[3] == 6)) return "a zip archive";
    if (d[0] == '7' && d[1] == 'z' && d[2] == 0xBC && d[3] == 0xAF) return "a 7-Zip archive";
    if (std::memcmp(d, "Rar!", 4) == 0) return "a RAR archive";
    if (d[0] == 0x1F && d[1] == 0x8B) return "a gzip archive";
    if (d[0] == 'B' && d[1] == 'Z' && d[2] == 'h') return "a bzip2 archive";
    if (d[0] == 0xFD && d[1] == '7' && d[2] == 'z' && d[3] == 'X') return "an xz archive";
    if (d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD) return "a zstd archive";
    if (std::memcmp(d, "MSCF", 4) == 0) return "a cabinet (.cab) archive";
  }
  return std::string();
}

std::string TraversalReason(const std::string& path) {
  if (path.empty()) {
    return "an empty name";
  }
  if (path[0] == '/' || path[0] == '\\') {
    return "an absolute path (or a network path)";
  }
  if (path.size() >= 2 && path[1] == ':') {
    return "a path with a drive letter";
  }
  if (path.find('\\') != std::string::npos) {
    return "a path with backslashes (use / between folders)";
  }
  for (const std::string& seg : Split(path)) {
    if (seg == ".." || seg == ".") {
      return "a path that climbs out of its folder (\"..\")";
    }
  }
  return std::string();
}

std::string UnsafePathReason(const std::string& path) {
  static const char* const kDevices[] = {"con", "prn", "aux", "nul", "conin$", "conout$", "clock$"};
  for (const std::string& seg : Split(path)) {
    if (seg.empty()) {
      return "an empty folder or file name";
    }
    if (seg.size() > 255) {
      return "a name longer than 255 bytes";
    }
    for (unsigned char c : seg) {
      if (c < 0x20 || c == 0x7F) {
        return "a control character in a name";
      }
      if (std::strchr("<>:\"|?*", c) != nullptr) {
        return std::string("the character '") + static_cast<char>(c) + "', which Windows can't have in a name";
      }
    }
    if (seg.back() == '.' || seg.back() == ' ') {
      return "a name ending in a dot or space, which Windows changes";
    }
    const std::string lower = LowerAscii(seg);
    const std::string stem = lower.substr(0, lower.find('.'));
    for (const char* d : kDevices) {
      if (stem == d) {
        return "a Windows device name (\"" + seg + "\")";
      }
    }
    if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '1' && stem[3] <= '9') {
      return "a Windows device name (\"" + seg + "\")";
    }
  }
  return std::string();
}

const char* KindName(Kind kind) {
  switch (kind) {
  case Kind::ModJson: return "mod.json";
  case Kind::Json: return "JSON data";
  case Kind::Lua: return "Lua script";
  case Kind::StoryText: return "story script text";
  case Kind::Text: return "text";
  case Kind::Png: return "PNG picture";
  case Kind::Dds: return "DDS picture";
  case Kind::DiscEntry: return "game-format file";
  case Kind::DiscFile: return "game-format file";
  default: return "unknown";
  }
}

Kind KindOf(const std::string& path, const std::vector<GameRules::ExtraFolder>* extras) {
  {
    std::string folder, rest;
    if (engine::mods::SplitGameFolder(path, folder, rest) && !rest.empty()) {
      const Kind inner = KindOf(rest, extras);
      return inner == Kind::ModJson ? Kind::Unknown : inner;
    }
  }
  const std::string lower = LowerAscii(path);
  const std::vector<std::string> segs = Split(lower);
  const std::string name = segs.back();
  const std::string ext = Extension(name);
  if (lower == "mod.json") {
    return Kind::ModJson;
  }
  const bool text = ext == "txt" || ext == "md" || name.rfind("readme", 0) == 0 || name.rfind("license", 0) == 0 ||
                    name.rfind("licence", 0) == 0 || name.rfind("changes", 0) == 0 || name.rfind("changelog", 0) == 0;
  if (segs.size() == 1) {
    if (text) return Kind::Text;
    if (ext == "png") return Kind::Png;
    if (ext == "json") return Kind::Json;
    return ext == "xds" ? Kind::StoryText : Kind::Unknown;
  }
  const std::string& top = segs[0];
  if (top == "files") return Kind::DiscFile;
  if (top == "fsys") return Kind::DiscEntry;
  if (top == "data") return ext == "json" ? Kind::Json : Kind::Unknown;
  if (top == "scripts") return ext == "lua" && segs.size() == 2 ? Kind::Lua : Kind::Unknown;
  if (top == "textures") return ext == "png" ? Kind::Png : ext == "dds" ? Kind::Dds : Kind::Unknown;
  if (extras != nullptr) {
    for (const GameRules::ExtraFolder& f : *extras) {
      if (LowerAscii(f.name) != top) continue;
      if (f.kind == "entry") return Kind::DiscEntry;
      if (f.kind == "json") return ext == "json" ? Kind::Json : Kind::Unknown;
      return text ? Kind::Text : Kind::Unknown;
    }
  }
  if (ext == "xds") return Kind::StoryText;
  if (text) return Kind::Text;
  if (ext == "png" && (top == "docs" || top == "images" || top == "screenshots")) return Kind::Png;
  return Kind::Unknown;
}

bool CheckJsonLimits(const std::string& path, std::string_view text, const SafetyLimits& limits, Report& report) {
  if (text.size() > limits.maxJsonBytes) {
    report.Unsafe(Severity::Problem, "json-too-big", path, 0,
                  Fmt("this JSON file is %llu KB; the most the port reads is %llu KB", static_cast<unsigned long long>(text.size() >> 10),
                      static_cast<unsigned long long>(limits.maxJsonBytes >> 10)));
    return false;
  }
  int depth = 0, line = 1;
  bool inString = false;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (c == '\0') {
      report.Unsafe(Severity::Problem, "json-binary", path, line, "this file holds a zero byte: it is not text");
      return false;
    }
    if (c == '\n') line++;
    if (inString) {
      if (c == '\\') i++;
      else if (c == '"') inString = false;
    } else if (c == '"') {
      inString = true;
    } else if (c == '[' || c == '{') {
      if (++depth > limits.maxJsonDepth) {
        report.Unsafe(Severity::Problem, "json-too-deep", path, line, Fmt("blocks are nested more than %d deep", limits.maxJsonDepth));
        return false;
      }
    } else if (c == ']' || c == '}') {
      depth--;
    }
  }
  return true;
}

namespace {

uint64_t PngRawSize(uint32_t w, uint32_t h, uint32_t bitsPerPixel, bool interlaced) {
  const auto pass = [&](uint32_t xs, uint32_t xstep, uint32_t ys, uint32_t ystep) -> uint64_t {
    if (w <= xs || h <= ys) return 0;
    const uint64_t pw = (w - xs + xstep - 1) / xstep, ph = (h - ys + ystep - 1) / ystep;
    return ph * (1 + (pw * bitsPerPixel + 7) / 8);
  };
  if (!interlaced) return pass(0, 1, 0, 1);
  return pass(0, 8, 0, 8) + pass(4, 8, 0, 8) + pass(0, 4, 4, 8) + pass(2, 4, 0, 4) + pass(0, 2, 2, 4) + pass(1, 2, 0, 2) + pass(0, 1, 1, 2);
}

}

bool CheckPng(const std::string& path, const std::vector<uint8_t>& d, const SafetyLimits& limits, Report& report, uint32_t* width,
              uint32_t* height) {
  const auto bad = [&](const std::string& what) {
    report.Problem("png-damaged", path, 0, "this PNG picture is damaged: " + what);
    return false;
  };
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (d.size() < 8 + 25 || std::memcmp(d.data(), kSig, 8) != 0) {
    return bad("it does not start like a PNG file");
  }
  size_t p = 8;
  uint32_t w = 0, h = 0, depth = 0, colour = 0, interlace = 0;
  bool header = false, palette = false, end = false;
  std::vector<uint8_t> idat;
  while (p + 12 <= d.size()) {
    const uint32_t len = Be32(d.data() + p);
    if (len > 0x7FFFFFFF || static_cast<uint64_t>(p) + 12 + len > d.size()) {
      return bad("a chunk runs past the end of the file");
    }
    const uint8_t* type = d.data() + p + 4;
    const uint8_t* body = d.data() + p + 8;
    if (Crc32(type, static_cast<size_t>(len) + 4) != Be32(body + len)) {
      return bad(Fmt("the chunk %.4s has a wrong checksum", reinterpret_cast<const char*>(type)));
    }
    if (!header) {
      if (std::memcmp(type, "IHDR", 4) != 0 || len != 13) {
        return bad("the first chunk is not a proper header");
      }
      w = Be32(body);
      h = Be32(body + 4);
      depth = body[8];
      colour = body[9];
      interlace = body[12];
      if (w == 0 || h == 0 || w > limits.maxImageSide || h > limits.maxImageSide ||
          static_cast<uint64_t>(w) * h > limits.maxImagePixels) {
        report.Unsafe(Severity::Problem, "png-too-big", path, 0,
                      Fmt("this picture is %u x %u; the most the port accepts is %u on a side and %llu million pixels", w, h,
                          limits.maxImageSide, static_cast<unsigned long long>(limits.maxImagePixels >> 20)));
        return false;
      }
      const bool ok = (colour == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                      (colour == 2 && (depth == 8 || depth == 16)) || (colour == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                      (colour == 4 && (depth == 8 || depth == 16)) || (colour == 6 && (depth == 8 || depth == 16));
      if (!ok || body[10] != 0 || body[11] != 0 || interlace > 1) {
        return bad("the header has a colour type, bit depth or method that PNG doesn't have");
      }
      header = true;
    } else if (std::memcmp(type, "PLTE", 4) == 0) {
      palette = true;
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      if (idat.size() + len > (static_cast<size_t>(1) << 30)) return bad("too much picture data");
      idat.insert(idat.end(), body, body + len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      end = true;
      p += 12 + len;
      break;
    }
    p += 12 + static_cast<size_t>(len);
  }
  if (!header || !end) {
    return bad("it has no end chunk");
  }
  if (p != d.size()) {
    report.Warn("png-trailing", path, 0, "there are extra bytes after the end of this PNG picture");
  }
  if (colour == 3 && !palette) {
    return bad("it needs a palette and has none");
  }
  const uint32_t channels = colour == 0 || colour == 3 ? 1 : colour == 2 ? 3 : colour == 4 ? 2 : 4;
  const uint64_t expected = PngRawSize(w, h, channels * depth, interlace == 1);
  z_stream z{};
  if (inflateInit(&z) != Z_OK) {
    return bad("(the checker could not start unpacking)");
  }
  z.next_in = idat.data();
  z.avail_in = static_cast<uInt>(idat.size());
  std::vector<uint8_t> buf(65536);
  uint64_t produced = 0;
  int rc = Z_OK;
  while (rc == Z_OK) {
    z.next_out = buf.data();
    z.avail_out = static_cast<uInt>(buf.size());
    rc = inflate(&z, Z_NO_FLUSH);
    produced += buf.size() - z.avail_out;
    if (produced > expected) {
      inflateEnd(&z);
      report.Unsafe(Severity::Problem, "png-bomb", path, 0, "the picture data unpacks to more than the header says (a decompression bomb or a damaged file)");
      return false;
    }
    if (rc == Z_BUF_ERROR && z.avail_in == 0) break;
  }
  inflateEnd(&z);
  if (rc != Z_STREAM_END) {
    return bad("the picture data can't be unpacked");
  }
  if (produced != expected) {
    return bad(Fmt("the picture data is %llu bytes, a %u x %u picture needs %llu", static_cast<unsigned long long>(produced), w, h,
                   static_cast<unsigned long long>(expected)));
  }
  if (width != nullptr) *width = w;
  if (height != nullptr) *height = h;
  return true;
}

bool CheckDds(const std::string& path, const std::vector<uint8_t>& d, const SafetyLimits& limits, Report& report) {
  const auto bad = [&](const std::string& what) {
    report.Problem("dds-damaged", path, 0, "this DDS picture is damaged: " + what);
    return false;
  };
  if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) {
    return bad("it does not start like a DDS file");
  }
  if (Le32(d.data() + 4) != 124 || Le32(d.data() + 76) != 32) {
    return bad("the header has the wrong size fields");
  }
  const uint32_t h = Le32(d.data() + 12), w = Le32(d.data() + 16), mips = std::max<uint32_t>(1, Le32(d.data() + 28));
  const uint32_t pfFlags = Le32(d.data() + 80), bits = Le32(d.data() + 88), caps2 = Le32(d.data() + 112);
  if (w == 0 || h == 0 || w > limits.maxImageSide || h > limits.maxImageSide || static_cast<uint64_t>(w) * h > limits.maxImagePixels) {
    report.Unsafe(Severity::Problem, "dds-too-big", path, 0,
                  Fmt("this picture is %u x %u; the most the port accepts is %u on a side", w, h, limits.maxImageSide));
    return false;
  }
  if (mips > 16) {
    return bad("it says it has more than 16 mip levels");
  }
  uint64_t blockBytes = 0;
  if (pfFlags & 4) {
    const char* cc = reinterpret_cast<const char*>(d.data() + 84);
    if (std::memcmp(cc, "DXT1", 4) == 0) blockBytes = 8;
    else if (std::memcmp(cc, "DXT3", 4) == 0 || std::memcmp(cc, "DXT5", 4) == 0) blockBytes = 16;
    else if (std::memcmp(cc, "DX10", 4) == 0 && d.size() < 148) return bad("the extended header is cut off");
  }
  uint64_t need = 0;
  const uint64_t faces = (caps2 & 0x200) ? 6 : 1;
  if (blockBytes != 0 || ((pfFlags & 0x40) && bits >= 8 && bits <= 32 && bits % 8 == 0)) {
    for (uint32_t m = 0; m < mips; m++) {
      const uint64_t mw = std::max<uint64_t>(1, w >> m), mh = std::max<uint64_t>(1, h >> m);
      need += blockBytes != 0 ? ((mw + 3) / 4) * ((mh + 3) / 4) * blockBytes : mw * mh * (bits / 8);
    }
    need = need * faces + 128;
    if (d.size() < need) {
      return bad(Fmt("the file is %llu bytes, its header needs at least %llu", static_cast<unsigned long long>(d.size()),
                     static_cast<unsigned long long>(need)));
    }
  }
  return true;
}

namespace {

struct RawZipEntry {
  ZipEntry e;
  uint32_t flags = 0, madeBy = 0, extAttr = 0, localOffset = 0, nameLen = 0;
};

bool ReadDirectory(const std::vector<uint8_t>& z, std::vector<RawZipEntry>& out, std::string& error) {
  if (z.size() < 22) {
    error = "the file is too short to be a zip archive";
    return false;
  }
  size_t eocd = std::string::npos;
  const size_t low = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
  for (size_t p = z.size() - 22 + 1; p-- > low;) {
    if (Le32(z.data() + p) == 0x06054B50u) {
      eocd = p;
      break;
    }
  }
  if (eocd == std::string::npos) {
    error = "no zip directory found: this is not a zip archive (or it is cut off)";
    return false;
  }
  const uint32_t count = Le16(z.data() + eocd + 10), cdSize = Le32(z.data() + eocd + 12), cdOffset = Le32(z.data() + eocd + 16);
  if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
    error = "zip64 archives (over 4 GB or 65535 files) are not accepted";
    return false;
  }
  if (Le16(z.data() + eocd + 4) != 0 || Le16(z.data() + eocd + 6) != 0) {
    error = "archives split over several disks are not accepted";
    return false;
  }
  if (static_cast<uint64_t>(cdOffset) + cdSize > z.size()) {
    error = "the zip directory runs past the end of the file";
    return false;
  }
  size_t p = cdOffset;
  for (uint32_t i = 0; i < count; i++) {
    if (p + 46 > z.size() || Le32(z.data() + p) != 0x02014B50u) {
      error = "the zip directory is damaged";
      return false;
    }
    RawZipEntry r;
    const uint8_t* c = z.data() + p;
    r.madeBy = Le16(c + 4);
    r.flags = Le16(c + 8);
    r.e.method = static_cast<uint16_t>(Le16(c + 10));
    r.e.crc = Le32(c + 16);
    r.e.packed = Le32(c + 20);
    r.e.size = Le32(c + 24);
    const uint32_t nameLen = Le16(c + 28), extraLen = Le16(c + 30), commentLen = Le16(c + 32);
    r.extAttr = Le32(c + 38);
    r.e.offset = Le32(c + 42);
    if (p + 46 + nameLen + extraLen + commentLen > z.size()) {
      error = "the zip directory is damaged";
      return false;
    }
    r.e.name.assign(reinterpret_cast<const char*>(c + 46), nameLen);
    r.nameLen = nameLen;
    r.e.directory = !r.e.name.empty() && r.e.name.back() == '/';
    out.push_back(std::move(r));
    p += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

size_t LocalDataStart(const std::vector<uint8_t>& z, const RawZipEntry& r, std::string& localName) {
  const size_t o = r.e.offset;
  if (o + 30 > z.size() || Le32(z.data() + o) != 0x04034B50u) return 0;
  const size_t nameLen = Le16(z.data() + o + 26), extraLen = Le16(z.data() + o + 28);
  if (o + 30 + nameLen + extraLen > z.size()) return 0;
  localName.assign(reinterpret_cast<const char*>(z.data() + o + 30), nameLen);
  return o + 30 + nameLen + extraLen;
}

}

bool ScanZip(const std::vector<uint8_t>& z, const SafetyLimits& limits, Report& report, std::vector<ZipEntry>* entries) {
  std::vector<RawZipEntry> raw;
  std::string error;
  if (!ReadDirectory(z, raw, error)) {
    report.Unsafe(Severity::Problem, "zip-unreadable", "", 0, error);
    return false;
  }
  if (raw.size() > limits.maxFiles) {
    report.Unsafe(Severity::Problem, "zip-too-many-files", "", 0,
                  Fmt("the archive has %zu entries; the most the port accepts is %zu", raw.size(), limits.maxFiles));
    return false;
  }
  uint64_t total = 0, packedTotal = 0;
  std::map<std::string, std::string> seen;
  bool ok = true;
  for (const RawZipEntry& r : raw) {
    const std::string& n = r.e.name;
    std::string path = n;
    if (r.e.directory) path.pop_back();
    const auto fail = [&](const char* code, const std::string& text) {
      report.Unsafe(Severity::Problem, code, n, 0, text);
      ok = false;
    };
    if (r.flags & 1) fail("zip-encrypted", "this entry is password protected: it can't be checked");
    if (r.e.method != 0 && r.e.method != 8) fail("zip-method", Fmt("this entry is packed with method %u, which the port does not read", r.e.method));
    std::string why = TraversalReason(path);
    if (!why.empty()) fail("zip-bad-path", "the archive holds " + why);
    else if (!(why = UnsafePathReason(path)).empty()) fail("zip-bad-name", "the archive holds " + why);
    if (path.size() > limits.maxPathBytes) fail("zip-long-path", "the path is longer than " + std::to_string(limits.maxPathBytes) + " bytes");
    const uint32_t host = r.madeBy >> 8, mode = r.extAttr >> 16;
    if (host == 3 && mode != 0) {
      const uint32_t type = mode & 0170000;
      if (type == 0120000) fail("zip-symlink", "this entry is a symbolic link");
      else if (type != 0100000 && type != 0040000 && type != 0) fail("zip-special", "this entry is a device, pipe or other special file");
      if (mode & 06000) report.Unsafe(Severity::Problem, "zip-setuid", n, 0, "this entry has the setuid / setgid permission bit");
    }
    if ((r.extAttr & 0x400) != 0 && host != 3) fail("zip-symlink", "this entry is a link (reparse point)");
    const std::string lower = LowerAscii(path);
    const auto dup = seen.emplace(lower, n);
    if (!dup.second) fail("zip-case-duplicate", "this name is the same as \"" + dup.first->second + "\" except for letter case or a repeat");
    if (!r.e.directory) {
      if (r.e.size > limits.maxFileBytes) fail("zip-file-too-big", Fmt("this file unpacks to %llu MB; the most is %llu MB", static_cast<unsigned long long>(r.e.size >> 20), static_cast<unsigned long long>(limits.maxFileBytes >> 20)));
      if (r.e.size > (1u << 20) && r.e.packed > 0 && r.e.size / r.e.packed > limits.maxRatio)
        fail("zip-bomb", Fmt("this file unpacks to %llu times its packed size: a zip bomb", static_cast<unsigned long long>(r.e.size / r.e.packed)));
      if (r.e.packed == 0 && r.e.size > (1u << 20) && r.e.method == 8) fail("zip-bomb", "this file unpacks from nothing: a zip bomb");
      total += r.e.size;
      packedTotal += r.e.packed;
      std::string localName;
      const size_t start = LocalDataStart(z, r, localName);
      if (start == 0 || start + r.e.packed > z.size()) fail("zip-damaged", "the entry's data is cut off or its header is damaged");
      else if (localName != n) fail("zip-name-mismatch", "the name in the entry's own header (\"" + localName + "\") differs from the directory's");
      const std::string exe = ExecutableNameReason(path);
      if (!exe.empty()) fail("program-file", "this is " + exe + ": mods may not contain programs");
      const std::string arc = ArchiveReason(path, nullptr, 0);
      if (!arc.empty()) fail("nested-archive", "this is " + arc + " inside the archive; the port can't look into it");
    } else {
      const std::string exe = ExecutableNameReason(path + "/x");
      if (exe.rfind("part of a Mac", 0) == 0) fail("program-file", "this folder is " + exe);
    }
  }
  if (total > limits.maxTotalBytes) {
    report.Unsafe(Severity::Problem, "zip-too-big", "", 0,
                  Fmt("the archive unpacks to %llu MB; the most the port accepts is %llu MB", static_cast<unsigned long long>(total >> 20),
                      static_cast<unsigned long long>(limits.maxTotalBytes >> 20)));
    ok = false;
  }
  if (total > (16u << 20) && packedTotal > 0 && total / packedTotal > limits.maxTotalRatio) {
    report.Unsafe(Severity::Problem, "zip-bomb", "", 0, "the archive unpacks to far more than its packed size: a zip bomb");
    ok = false;
  }
  if (entries != nullptr) {
    entries->clear();
    for (const RawZipEntry& r : raw) entries->push_back(r.e);
  }
  return ok;
}

bool ExtractZip(const std::vector<uint8_t>& z, const fs::path& dest, const SafetyLimits& limits, std::string& error) {
  Report scan;
  std::vector<RawZipEntry> raw;
  if (!ReadDirectory(z, raw, error)) return false;
  std::vector<ZipEntry> list;
  if (!ScanZip(z, limits, scan, &list)) {
    const Finding* first = scan.findings.empty() ? nullptr : &scan.findings.front();
    error = first != nullptr ? (first->file.empty() ? first->text : first->file + ": " + first->text) : "the archive is not safe";
    return false;
  }
  std::error_code ec;
  fs::create_directories(dest, ec);
  const fs::path root = fs::weakly_canonical(dest, ec);
  uint64_t written = 0;
  for (const RawZipEntry& r : raw) {
    std::string path = r.e.name;
    if (r.e.directory) path.pop_back();
    const std::u8string u8(path.begin(), path.end());
    fs::path target = root;
    for (const std::string& seg : Split(path)) {
      target /= fs::path(std::u8string(seg.begin(), seg.end()));
    }
    const fs::path norm = target.lexically_normal();
    auto a = norm.begin();
    for (auto b = root.begin(); b != root.end(); ++b, ++a) {
      if (a == norm.end() || *a != *b) {
        error = "an entry would land outside the destination folder: " + path;
        return false;
      }
    }
    if (fs::is_symlink(fs::symlink_status(norm.parent_path(), ec))) {
      error = "a folder in the way is a link: " + path;
      return false;
    }
    if (r.e.directory) {
      fs::create_directories(norm, ec);
      continue;
    }
    fs::create_directories(norm.parent_path(), ec);
    if (fs::exists(fs::symlink_status(norm, ec))) {
      error = "the file already exists: " + path;
      return false;
    }
    std::string localName;
    const size_t start = LocalDataStart(z, r, localName);
    std::ofstream out(norm, std::ios::binary);
    if (!out) {
      error = "can't write " + path;
      return false;
    }
    uint32_t crc = 0;
    uint64_t made = 0;
    if (r.e.method == 0) {
      if (r.e.packed != r.e.size) {
        error = "damaged entry: " + path;
        return false;
      }
      out.write(reinterpret_cast<const char*>(z.data() + start), static_cast<std::streamsize>(r.e.size));
      crc = Crc32(z.data() + start, static_cast<size_t>(r.e.size));
      made = r.e.size;
    } else {
      z_stream s{};
      if (inflateInit2(&s, -15) != Z_OK) {
        error = "can't start unpacking";
        return false;
      }
      s.next_in = const_cast<uint8_t*>(z.data() + start);
      s.avail_in = static_cast<uInt>(r.e.packed);
      std::vector<uint8_t> buf(65536);
      int rc = Z_OK;
      while (rc == Z_OK) {
        s.next_out = buf.data();
        s.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&s, Z_NO_FLUSH);
        const size_t got = buf.size() - s.avail_out;
        made += got;
        if (made > r.e.size) {
          inflateEnd(&s);
          error = "the entry unpacks to more than its declared size (a zip bomb): " + path;
          return false;
        }
        out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(got));
        crc = Crc32(buf.data(), got, crc);
        if (rc == Z_BUF_ERROR && s.avail_in == 0) break;
      }
      inflateEnd(&s);
      if (rc != Z_STREAM_END || made != r.e.size) {
        error = "the entry's data is damaged: " + path;
        return false;
      }
    }
    out.close();
    written += made;
    if (crc != r.e.crc) {
      error = "the entry's checksum is wrong: " + path;
      return false;
    }
    if (written > limits.maxTotalBytes) {
      error = "the archive unpacks to more than the limit";
      return false;
    }
  }
  return true;
}

bool CheckAndExtractArchive(const std::vector<uint8_t>& zip, const fs::path& dest, const Options& options, const SafetyLimits& limits,
                            Report& report) {
  if (!ScanZip(zip, limits, report)) {
    return false;
  }
  std::string error;
  if (!ExtractZip(zip, dest, limits, error)) {
    report.Unsafe(Severity::Problem, "zip-extract", "", 0, error);
    return false;
  }
  Options inner = options;
  inner.folder = dest;
  std::error_code ec;
  if (!fs::exists(dest / "mod.json", ec)) {
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(dest, ec)) dirs.push_back(e.path());
    if (dirs.size() == 1 && fs::exists(dirs[0] / "mod.json", ec)) inner.folder = dirs[0];
  }
  const Report folder = CheckMod(inner);
  report.manifest = folder.manifest;
  report.manifestOk = folder.manifestOk;
  report.folder = inner.folder;
  report.deepDone = folder.deepDone;
  report.permissions = folder.permissions;
  for (const Finding& f : folder.findings) report.findings.push_back(f);
  return report.Count(Severity::Problem) == 0;
}

}
