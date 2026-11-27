#include "disc_textures.h"

#include "gs_formats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace engine::hdtex {

namespace {
constexpr uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull,
                   P4 = 9650029242287828579ull, P5 = 2870177450012600261ull;
inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t Rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t Round(uint64_t acc, uint64_t in) { return Rotl(acc + in * P2, 31) * P1; }
inline uint64_t Merge(uint64_t acc, uint64_t v) { return (acc ^ Round(0, v)) * P1 + P4; }
}

uint64_t Xxh64(const uint8_t* p, size_t len) {
  const uint8_t* end = p + len;
  uint64_t h;
  if (len >= 32) {
    const uint8_t* limit = end - 32;
    uint64_t v1 = P1 + P2, v2 = P2, v3 = 0, v4 = 0 - P1;
    do {
      v1 = Round(v1, Rd64(p)); v2 = Round(v2, Rd64(p + 8)); v3 = Round(v3, Rd64(p + 16)); v4 = Round(v4, Rd64(p + 24));
      p += 32;
    } while (p <= limit);
    h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
    h = Merge(h, v1); h = Merge(h, v2); h = Merge(h, v3); h = Merge(h, v4);
  } else {
    h = P5;
  }
  h += len;
  while (p + 8 <= end) { h ^= Round(0, Rd64(p)); h = Rotl(h, 27) * P1 + P4; p += 8; }
  if (p + 4 <= end) { h ^= (uint64_t)Rd32(p) * P1; h = Rotl(h, 23) * P2 + P3; p += 4; }
  while (p < end) { h ^= (*p++) * P5; h = Rotl(h, 11) * P1; }
  h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
  return h;
}

const char* SourceName(TexSource s) {
  switch (s) {
    case TexSource::Map: return "map";
    case TexSource::Object: return "object";
    case TexSource::Camera: return "camera";
    case TexSource::Model: return "model";
    case TexSource::Menu: return "menu";
    case TexSource::Flash: return "flash";
    case TexSource::Face: return "face";
    case TexSource::Particle: return "particle";
    case TexSource::MoveEffect: return "move";
    case TexSource::LensFlare: return "lensflare";
  }
  return "?";
}

const char* ClassName(TexClass c) {
  switch (c) {
    case TexClass::Surface3D: return "surface3d";
    case TexClass::Interface: return "interface";
    case TexClass::Effect: return "effect";
    case TexClass::TextOrMask: return "text_or_mask";
    case TexClass::Character: return "character";
    case TexClass::AlphaDither: return "alpha_dither";
  }
  return "?";
}

namespace {
bool BlockOf(uint32_t fmt, uint32_t& bw, uint32_t& bh, uint32_t& bytes) {
  switch (fmt) {
    case 0: case 8: case 14: bw = 8; bh = 8; bytes = 32; return true;
    case 1: case 2: case 9: bw = 8; bh = 4; bytes = 32; return true;
    case 3: case 4: case 5: case 10: bw = 4; bh = 4; bytes = 32; return true;
    case 6: bw = 4; bh = 4; bytes = 64; return true;
  }
  return false;
}
size_t LevelBytes(uint32_t w, uint32_t h, uint32_t fmt) {
  uint32_t bw, bh, bs;
  if (!BlockOf(fmt, bw, bh, bs)) return 0;
  return (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bs;
}
inline uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline uint32_t Be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
std::string Hex16(uint64_t v) { char b[20]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }

struct Raw {
  uint32_t w, h, fmt;
  bool mip;
  const uint8_t* data;
  const uint8_t* tlut;
  size_t tlutBytes;
  uint32_t tlutFormat;
};

std::string NameOf(const Raw& r) {
  const size_t n = LevelBytes(r.w, r.h, r.fmt);
  if (n == 0) return {};
  std::string s = "tex1_" + std::to_string(r.w) + "x" + std::to_string(r.h) + (r.mip ? "_m_" : "_") +
                  Hex16(Xxh64(r.data, n));
  if (r.fmt == 8 || r.fmt == 9 || r.fmt == 10) {
    if (!r.tlut) return {};
    uint32_t lo = 0xFFFF, hi = 0;
    if (r.fmt == 8) {
      for (size_t i = 0; i < n; i++) {
        const uint32_t a = r.data[i] >> 4, b = r.data[i] & 15;
        lo = std::min({lo, a, b}); hi = std::max({hi, a, b});
      }
    } else if (r.fmt == 9) {
      for (size_t i = 0; i < n; i++) { lo = std::min<uint32_t>(lo, r.data[i]); hi = std::max<uint32_t>(hi, r.data[i]); }
    } else {
      for (size_t i = 0; i + 1 < n; i += 2) {
        const uint32_t v = Be16(r.data + i) & 0x3fff;
        lo = std::min(lo, v); hi = std::max(hi, v);
      }
    }
    if (2 * (size_t)(hi + 1) > r.tlutBytes) return {};
    s += "_" + Hex16(Xxh64(r.tlut + 2 * lo, 2 * (size_t)(hi + 1 - lo)));
  }
  return s + "_" + std::to_string(r.fmt);
}
}

namespace {
inline void Put(std::vector<uint8_t>& o, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
  if (x >= w || y >= h) return;
  uint8_t* p = &o[((size_t)y * w + x) * 4];
  p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}
inline void C565(uint32_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
  const uint32_t R = (v >> 11) & 31, G = (v >> 5) & 63, B = v & 31;
  r = (uint8_t)((R << 3) | (R >> 2)); g = (uint8_t)((G << 2) | (G >> 4)); b = (uint8_t)((B << 3) | (B >> 2));
}
void C5A3(uint32_t v, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
  if (v & 0x8000) {
    const uint32_t R = (v >> 10) & 31, G = (v >> 5) & 31, B = v & 31;
    r = (uint8_t)((R << 3) | (R >> 2)); g = (uint8_t)((G << 3) | (G >> 2)); b = (uint8_t)((B << 3) | (B >> 2)); a = 255;
  } else {
    const uint32_t A = (v >> 12) & 7;
    r = (uint8_t)(((v >> 8) & 15) * 17); g = (uint8_t)(((v >> 4) & 15) * 17); b = (uint8_t)((v & 15) * 17);
    a = (uint8_t)((A << 5) | (A << 2) | (A >> 1));
  }
}
void PalColour(const std::vector<uint8_t>& tl, uint32_t fmt, uint32_t idx, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
  r = g = b = 0; a = 255;
  if (2 * (size_t)idx + 1 >= tl.size()) return;
  const uint32_t v = Be16(&tl[2 * idx]);
  if (fmt == 0) { a = (uint8_t)(v >> 8); r = g = b = (uint8_t)v; }
  else if (fmt == 1) C565(v, r, g, b);
  else C5A3(v, r, g, b, a);
}
}

bool DecodeRgba(const DiscTexture& t, std::vector<uint8_t>& out) {
  const uint32_t w = t.width, h = t.height, fmt = t.format;
  uint32_t bw, bh, bs;
  if (!BlockOf(fmt, bw, bh, bs) || t.level0.size() < LevelBytes(w, h, fmt)) return false;
  out.assign((size_t)w * h * 4, 0);
  const uint8_t* d = t.level0.data();
  const uint32_t tx = (w + bw - 1) / bw, ty = (h + bh - 1) / bh;
  for (uint32_t by = 0; by < ty; by++) {
    for (uint32_t bx = 0; bx < tx; bx++) {
      const uint8_t* b = d + ((size_t)by * tx + bx) * bs;
      const uint32_t X = bx * bw, Y = by * bh;
      switch (fmt) {
        case 0:
          for (uint32_t y = 0; y < 8; y++) for (uint32_t x = 0; x < 8; x++) {
            const uint8_t v = (uint8_t)(((b[y * 4 + x / 2] >> ((x & 1) ? 0 : 4)) & 15) * 17);
            Put(out, w, h, X + x, Y + y, v, v, v, v);
          }
          break;
        case 1:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 8; x++) { const uint8_t v = b[y * 8 + x]; Put(out, w, h, X + x, Y + y, v, v, v, v); }
          break;
        case 2:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 8; x++) {
            const uint8_t v = b[y * 8 + x];
            const uint8_t i = (uint8_t)((v & 15) * 17), a = (uint8_t)((v >> 4) * 17);
            Put(out, w, h, X + x, Y + y, i, i, i, a);
          }
          break;
        case 3:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) { const uint8_t* p = b + (y * 4 + x) * 2; Put(out, w, h, X + x, Y + y, p[1], p[1], p[1], p[0]); }
          break;
        case 4:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
            uint8_t r, g, bl; C565(Be16(b + (y * 4 + x) * 2), r, g, bl); Put(out, w, h, X + x, Y + y, r, g, bl, 255);
          }
          break;
        case 5:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
            uint8_t r, g, bl, a; C5A3(Be16(b + (y * 4 + x) * 2), r, g, bl, a); Put(out, w, h, X + x, Y + y, r, g, bl, a);
          }
          break;
        case 6:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
            const uint32_t i = y * 4 + x;
            Put(out, w, h, X + x, Y + y, b[i * 2 + 1], b[32 + i * 2], b[32 + i * 2 + 1], b[i * 2]);
          }
          break;
        case 8:
          for (uint32_t y = 0; y < 8; y++) for (uint32_t x = 0; x < 8; x++) {
            const uint32_t idx = (b[y * 4 + x / 2] >> ((x & 1) ? 0 : 4)) & 15;
            uint8_t r, g, bl, a; PalColour(t.tlut, t.tlutFormat, idx, r, g, bl, a); Put(out, w, h, X + x, Y + y, r, g, bl, a);
          }
          break;
        case 9:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 8; x++) {
            uint8_t r, g, bl, a; PalColour(t.tlut, t.tlutFormat, b[y * 8 + x], r, g, bl, a); Put(out, w, h, X + x, Y + y, r, g, bl, a);
          }
          break;
        case 10:
          for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
            uint8_t r, g, bl, a; PalColour(t.tlut, t.tlutFormat, Be16(b + (y * 4 + x) * 2) & 0x3fff, r, g, bl, a); Put(out, w, h, X + x, Y + y, r, g, bl, a);
          }
          break;
        case 14:
          for (uint32_t s = 0; s < 4; s++) {
            const uint8_t* sb = b + s * 8;
            const uint32_t c0 = Be16(sb), c1 = Be16(sb + 2);
            uint8_t col[4][4];
            C565(c0, col[0][0], col[0][1], col[0][2]); col[0][3] = 255;
            C565(c1, col[1][0], col[1][1], col[1][2]); col[1][3] = 255;
            if (c0 > c1) {
              for (int k = 0; k < 3; k++) {
                col[2][k] = (uint8_t)((2 * col[0][k] + col[1][k]) / 3);
                col[3][k] = (uint8_t)((col[0][k] + 2 * col[1][k]) / 3);
              }
              col[2][3] = col[3][3] = 255;
            } else {
              for (int k = 0; k < 3; k++) { col[2][k] = (uint8_t)((col[0][k] + col[1][k]) / 2); col[3][k] = 0; }
              col[2][3] = 255; col[3][3] = 0;
            }
            for (uint32_t y = 0; y < 4; y++) for (uint32_t x = 0; x < 4; x++) {
              const uint32_t i = (sb[4 + y] >> (6 - 2 * x)) & 3;
              Put(out, w, h, X + (s & 1) * 4 + x, Y + (s >> 1) * 4 + y, col[i][0], col[i][1], col[i][2], col[i][3]);
            }
          }
          break;
      }
    }
  }
  return true;
}

namespace {
bool Starts(const std::string& s, const char* p) {
  const size_t n = std::strlen(p);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++) if (std::tolower((unsigned char)s[i]) != p[i]) return false;
  return true;
}
}

TexClass ClassifyByOrigin(const DiscTexture& t) {
  switch (t.source) {
    case TexSource::Menu: case TexSource::Flash: case TexSource::Face: return TexClass::Interface;
    case TexSource::Particle: case TexSource::MoveEffect: case TexSource::LensFlare: return TexClass::Effect;
    default: break;
  }
  if (Starts(t.fsys, "people") || Starts(t.fsys, "chara")) return TexClass::Character;
  if (t.format <= 3) return TexClass::TextOrMask;
  return TexClass::Surface3D;
}

bool HasAlphaDither(const uint8_t* rgba, uint32_t w, uint32_t h) {
  if (w < 4 || h < 4) return false;
  uint64_t pairs = 0, jumps = 0;
  bool anyPartialOrKeyed = false;
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x + 1 < w; x++) {
      const int a = rgba[((size_t)y * w + x) * 4 + 3], b = rgba[((size_t)y * w + x + 1) * 4 + 3];
      pairs++;
      if (std::abs(a - b) >= 128) jumps++;
      if (a != 255) anyPartialOrKeyed = true;
    }
  }
  return anyPartialOrKeyed && pairs && (double)jumps / (double)pairs > 0.12;
}

namespace {
bool GsFormat(int32_t f, uint32_t& gx) {
  switch (f) {
    case 0x00: gx = 8; return true;
    case 0x01: gx = 9; return true;
    case 0x30: gx = 10; return true;
    case 0x40: gx = 0; return true;
    case 0x41: gx = 2; return true;
    case 0x42: gx = 1; return true;
    case 0x43: gx = 3; return true;
    case 0x44: gx = 4; return true;
    case 0x45: gx = 6; return true;
    case 0x90: gx = 5; return true;
    case 0xB0: gx = 14; return true;
  }
  return false;
}

bool GsRecord(const uint8_t* b, size_t n, Raw& r) {
  if (n < 0x80) return false;
  const uint32_t w = Be16(b), h = Be16(b + 2);
  if (w < 1 || w > 1024 || h < 1 || h > 1024) return false;
  uint32_t gx;
  if (!GsFormat((int32_t)Be32(b + 8), gx)) return false;
  if (b[5] < 1 || b[5] > 8) return false;
  const uint32_t m0 = Be32(b + 0x28), t = Be32(b + 0x48);
  const size_t len = LevelBytes(w, h, gx);
  if (m0 == 0 || m0 + len > n) return false;
  r.w = w; r.h = h; r.fmt = gx; r.mip = b[5] > 1; r.data = b + m0; r.tlut = nullptr; r.tlutBytes = 0; r.tlutFormat = 2;
  if (gx == 8 || gx == 9 || gx == 10) {
    if (!t) return false;
    const size_t ent = gx == 8 ? 16 : gx == 9 ? 256 : 1024;
    if (t + ent * 2 > n) return false;
    r.tlut = b + t; r.tlutBytes = ent * 2;
    const uint32_t tf = Be32(b + 0xC);
    r.tlutFormat = tf == 1 ? 0 : tf == 2 ? 1 : 2;
  }
  return true;
}

using Sink = std::function<void(const Raw&)>;

void GsScan(const uint8_t* b, size_t n, const Sink& sink) {
  for (size_t off = 0; off + 0x80 <= n; off += 32) {
    if (Be32(b + off + 0x28) != 0x80) continue;
    Raw r;
    if (GsRecord(b + off, n - off, r)) sink(r);
  }
}

void HsdImages(const uint8_t* a, size_t n, const Sink& sink) {
  if (n < 0x20) return;
  const uint32_t ds = Be32(a + 4), nr = Be32(a + 8);
  if (ds == 0 || nr > 0x100000 || 0x20 + (size_t)ds + (size_t)nr * 4 > n) return;
  const uint8_t* d = a + 0x20;
  std::unordered_map<uint32_t, uint32_t> slot;
  std::unordered_map<uint32_t, std::vector<uint32_t>> targets;
  slot.reserve(nr * 2);
  for (uint32_t i = 0; i < nr; i++) {
    const uint32_t r = Be32(a + 0x20 + ds + i * 4);
    if (r + 4 > ds) continue;
    const uint32_t v = Be32(d + r);
    slot[r] = v;
    targets[v].push_back(r);
  }
  std::set<uint32_t> seen;
  for (const auto& kv : slot) {
    const uint32_t o = kv.first, p = kv.second;
    if (p == 0 || o + 24 > ds || p + 32 > ds) continue;
    const uint32_t w = Be16(d + o + 4), h = Be16(d + o + 6), fmt = Be32(d + o + 8), mip = Be32(d + o + 12);
    uint32_t bw, bh, bs;
    if (w < 1 || w > 1024 || h < 1 || h > 1024 || mip > 1 || !BlockOf(fmt, bw, bh, bs)) continue;
    if (!targets.count(o)) continue;
    const size_t len = LevelBytes(w, h, fmt);
    if (p + len > ds) continue;
    float mn, mx;
    uint32_t u = Be32(d + o + 16); std::memcpy(&mn, &u, 4);
    u = Be32(d + o + 20); std::memcpy(&mx, &u, 4);
    if (!(mn >= 0.f && mn <= 16.f && mx >= 0.f && mx <= 16.f)) continue;
    if (!seen.insert(o).second) continue;
    Raw r{w, h, fmt, mip != 0, d + p, nullptr, 0, 2};
    for (uint32_t s : targets[o]) {
      if (s < 0x4C) continue;
      const uint32_t t = s - 0x4C;
      auto it = slot.find(t + 0x50);
      if (it == slot.end() || it->second == 0 || it->second + 14 > ds) continue;
      const uint32_t tp = it->second, cnt = Be16(d + tp + 12);
      auto dt = slot.find(tp);
      if (dt == slot.end() || dt->second == 0 || cnt == 0 || cnt > 0x4000 || dt->second + cnt * 2 > ds) continue;
      r.tlut = d + dt->second; r.tlutBytes = cnt * 2; r.tlutFormat = Be32(d + tp + 4) & 3;
      break;
    }
    sink(r);
  }
}

bool PkxArchive(const uint8_t* b, size_t n, const uint8_t*& a, size_t& an) {
  if (n < 0x84) return false;
  const uint32_t size = Be32(b), psize = Be32(b + 8), count = Be32(b + 16);
  if (Be16(b + 0x1A) != 0xC || size == 0 || count > 1000) return false;
  size_t at = (0x84 + (size_t)count * 0xD0 + 31) & ~(size_t)31;
  if (psize) at += (psize + 31) & ~(size_t)31;
  if (at + size > n) return false;
  a = b + at; an = size;
  return true;
}
}

bool ForEachDiscTexture(const FileSource& source, const std::function<void(DiscTexture&)>& each,
                        const std::function<bool(uint32_t, uint32_t)>& progress, ExportStats* stats) {
  std::vector<std::string> files;
  for (const auto& f : source.list()) {
    if (f.size() > 5 && (f.compare(f.size() - 5, 5, ".fsys") == 0 || f.compare(f.size() - 5, 5, ".FSYS") == 0)) files.push_back(f);
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) return false;
  ExportStats local;
  ExportStats& st = stats ? *stats : local;
  std::unordered_set<std::string> seen;
  std::vector<uint8_t> data;
  for (size_t fi = 0; fi < files.size(); fi++) {
    if (progress && !progress((uint32_t)fi, (uint32_t)files.size())) return false;
    data.clear();
    if (!source.read(files[fi], data)) continue;
    FsysArchive arc;
    if (!Fsys_Open(data.data(), data.size(), &arc)) continue;
    std::string base = files[fi];
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    for (uint32_t i = 0; i < arc.count; i++) {
      FsysEntry e;
      if (!Fsys_Entry(&arc, i, &e)) continue;
      TexSource src;
      enum { None, Hsd, Gs, Scan, Pkx } how = None;
      switch (e.type) {
        case 1: src = TexSource::Map; how = Hsd; break;
        case 2: src = TexSource::Object; how = Hsd; break;
        case 12: src = TexSource::Camera; how = Hsd; break;
        case 15: src = TexSource::Model; how = Pkx; break;
        case 9: src = TexSource::Menu; how = Gs; break;
        case 24: src = TexSource::Flash; how = Scan; break;
        case 25: src = TexSource::Face; how = Scan; break;
        case 10: case 18: src = TexSource::Particle; how = Scan; break;
        case 16: src = TexSource::MoveEffect; how = Scan; break;
        case 17: src = TexSource::LensFlare; how = Scan; break;
        default: continue;
      }
      uint32_t sz = 0;
      uint8_t* body = Fsys_Extract(&arc, i, &sz);
      if (!body) continue;
      st.members++;
      auto sink = [&](const Raw& r) {
        st.found++;
        std::string stem = NameOf(r);
        if (stem.empty() || !seen.insert(stem).second) return;
        DiscTexture t;
        t.stem = stem; t.width = r.w; t.height = r.h; t.format = r.fmt; t.mipmapped = r.mip;
        t.source = src; t.fsys = base; t.member = e.name ? e.name : "";
        const size_t len = LevelBytes(r.w, r.h, r.fmt);
        t.level0.assign(r.data, r.data + len);
        if (r.tlut) { t.tlut.assign(r.tlut, r.tlut + r.tlutBytes); t.tlutFormat = r.tlutFormat; }
        t.cls = ClassifyByOrigin(t);
        if (t.cls == TexClass::Surface3D) {
          std::vector<uint8_t> px;
          if (DecodeRgba(t, px) && HasAlphaDither(px.data(), t.width, t.height)) t.cls = TexClass::AlphaDither;
        }
        st.unique++; st.byClass[(int)t.cls]++; if (r.fmt < 16) st.byFormat[r.fmt]++; st.level0Bytes += len;
        each(t);
      };
      if (how == Hsd) HsdImages(body, sz, sink);
      else if (how == Pkx) { const uint8_t* a; size_t an; if (PkxArchive(body, sz, a, an)) HsdImages(a, an, sink); }
      else if (how == Gs) { Raw r; if (GsRecord(body, sz, r)) sink(r); }
      else GsScan(body, sz, sink);
      std::free(body);
    }
  }
  if (progress) progress((uint32_t)files.size(), (uint32_t)files.size());
  return true;
}

}
