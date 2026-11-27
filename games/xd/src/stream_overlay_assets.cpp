#include "stream_overlay_assets.h"

#include "dol_tables.h"
#include "ui_kit2.h"

#include <gs_formats.h>

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace xd::stream::assets {

namespace {
namespace fs = std::filesystem;

constexpr int kPiecesVersion = 1;
constexpr int kFontVersion = 1;
constexpr const char* kPiecesMarker = "xd-overlay-pieces";
constexpr const char* kFontMarker = "xd-overlay-font";

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

bool WriteFile(const fs::path& file, const std::vector<uint8_t>& bytes) {
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) return false;
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  if (ec) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
  }
  return true;
}

bool MarkerCurrent(const fs::path& file, const char* word, int version) {
  std::ifstream f(file);
  std::string a;
  int v = 0;
  return f && (f >> a >> v) && a == word && v == version;
}

void WriteMarker(const fs::path& file, const char* word, int version) {
  const std::string t = std::string(word) + " " + std::to_string(version) + "\n";
  WriteFile(file, std::vector<uint8_t>(t.begin(), t.end()));
}

void PutBe32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(uint8_t(v >> 24)), o.push_back(uint8_t(v >> 16)), o.push_back(uint8_t(v >> 8)), o.push_back(uint8_t(v));
}
void Chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  PutBe32(o, static_cast<uint32_t>(data.size()));
  const size_t at = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), data.begin(), data.end());
  PutBe32(o, static_cast<uint32_t>(crc32(0, o.data() + at, static_cast<uInt>(o.size() - at))));
}

}

std::vector<uint8_t> EncodePng(int w, int h, const std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 4 + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + static_cast<size_t>(y) * w * 4, rgba.begin() + static_cast<size_t>(y + 1) * w * 4);
  }
  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> z(bound);
  if (compress2(z.data(), &bound, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return {};
  z.resize(bound);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  PutBe32(ihdr, static_cast<uint32_t>(w));
  PutBe32(ihdr, static_cast<uint32_t>(h));
  ihdr.push_back(8), ihdr.push_back(6), ihdr.push_back(0), ihdr.push_back(0), ihdr.push_back(0);
  Chunk(out, "IHDR", ihdr);
  Chunk(out, "IDAT", z);
  Chunk(out, "IEND", {});
  return out;
}

namespace {

struct Img {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
};

bool Crop(const Img& s, int x0, int y0, int x1, int y1, Img& out) {
  if (x0 < 0 || y0 < 0 || x1 > s.w || y1 > s.h || x1 <= x0 || y1 <= y0) return false;
  out.w = x1 - x0, out.h = y1 - y0;
  out.px.resize(static_cast<size_t>(out.w) * out.h * 4);
  for (int y = 0; y < out.h; y++) std::memcpy(&out.px[static_cast<size_t>(y) * out.w * 4], &s.px[(static_cast<size_t>(y0 + y) * s.w + x0) * 4], static_cast<size_t>(out.w) * 4);
  return true;
}

float Cubic(float x) {
  x = std::fabs(x);
  if (x < 1) return (1.5f * x - 2.5f) * x * x + 1.0f;
  if (x < 2) return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;
  return 0;
}

Img Up(const Img& s, int k) {
  const int W = s.w * k, H = s.h * k;
  std::vector<float> pl(static_cast<size_t>(s.w) * s.h * 4);
  for (size_t i = 0; i < static_cast<size_t>(s.w) * s.h; i++) {
    const float a = s.px[i * 4 + 3];
    for (int c = 0; c < 3; c++) pl[i * 4 + c] = s.px[i * 4 + c] * a / 255.0f;
    pl[i * 4 + 3] = a;
  }
  auto axis = [&](int srcN, int dstN, std::vector<int>& idx, std::vector<float>& wt) {
    idx.resize(static_cast<size_t>(dstN) * 4), wt.resize(static_cast<size_t>(dstN) * 4);
    for (int j = 0; j < dstN; j++) {
      const float src = (j + 0.5f) / k - 0.5f;
      const int i0 = static_cast<int>(std::floor(src));
      const float t = src - i0;
      float sum = 0;
      for (int m = 0; m < 4; m++) {
        const int i = i0 - 1 + m;
        const float w = Cubic(t - (m - 1));
        idx[static_cast<size_t>(j) * 4 + m] = std::min(std::max(i, 0), srcN - 1);
        wt[static_cast<size_t>(j) * 4 + m] = w;
        sum += w;
      }
      for (int m = 0; m < 4; m++) wt[static_cast<size_t>(j) * 4 + m] /= sum;
    }
  };
  std::vector<int> ix, iy;
  std::vector<float> wx, wy;
  axis(s.w, W, ix, wx);
  axis(s.h, H, iy, wy);
  std::vector<float> tmp(static_cast<size_t>(W) * s.h * 4);
  for (int y = 0; y < s.h; y++)
    for (int x = 0; x < W; x++)
      for (int c = 0; c < 4; c++) {
        float v = 0;
        for (int m = 0; m < 4; m++) v += pl[(static_cast<size_t>(y) * s.w + ix[static_cast<size_t>(x) * 4 + m]) * 4 + c] * wx[static_cast<size_t>(x) * 4 + m];
        tmp[(static_cast<size_t>(y) * W + x) * 4 + c] = v;
      }
  Img o;
  o.w = W, o.h = H;
  o.px.resize(static_cast<size_t>(W) * H * 4);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v[4];
      for (int c = 0; c < 4; c++) {
        v[c] = 0;
        for (int m = 0; m < 4; m++) v[c] += tmp[(static_cast<size_t>(iy[static_cast<size_t>(y) * 4 + m]) * W + x) * 4 + c] * wy[static_cast<size_t>(y) * 4 + m];
      }
      const float a = std::min(std::max(v[3], 0.0f), 255.0f);
      uint8_t* d = &o.px[(static_cast<size_t>(y) * W + x) * 4];
      for (int c = 0; c < 3; c++) {
        const float rgb = a > 0.5f ? v[c] * 255.0f / std::max(a, 0.5f) : 0.0f;
        d[c] = static_cast<uint8_t>(std::lround(std::min(std::max(rgb, 0.0f), 255.0f)));
      }
      d[3] = static_cast<uint8_t>(std::lround(a));
    }
  return o;
}

struct Archive {
  std::vector<uint8_t> bytes;
};

bool Texture(const Archive& a, uint32_t id, Img& out) {
  FsysArchive fsys{};
  if (a.bytes.empty() || !Fsys_Open(a.bytes.data(), a.bytes.size(), &fsys)) return false;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e) || e.type != 9 || e.id != id) continue;
    uint32_t size = 0;
    uint8_t* data = Fsys_Extract(&fsys, i, &size);
    if (data == nullptr) return false;
    const bool ok = xd::kit2::DecodeTexture(data, size, out.px, out.w, out.h);
    std::free(data);
    return ok;
  }
  return false;
}

struct PieceSpec {
  const char* name;
  uint32_t tex;
  int x0, y0, x1, y1;
};

const PieceSpec kPieces[] = {
    {"small_shadow", 0x18A1, 0, 110, 350, 174},
    {"small_faint", 0x17C7, 0, 110, 350, 174},
    {"hud_silver", 0x16CD, 0, 0, 220, 52},
    {"lip_silver", 0x16CD, 0, 52, 180, 74},
    {"hud_shadow", 0x16CF, 0, 0, 220, 52},
    {"lip_shadow", 0x16CF, 0, 52, 180, 74},
    {"hud_faint", 0x17BD, 0, 0, 220, 52},
    {"lip_faint", 0x17BD, 0, 52, 180, 74},
    {"hplabel", 0x16CC, 1, 1, 27, 13},
    {"explabel", 0x16CC, 1, 13, 25, 23},
    {"ball_none", 0x16CC, 1, 25, 17, 41},
    {"ball_ok", 0x16CC, 17, 25, 33, 41},
    {"ball_bad", 0x16CC, 33, 25, 49, 41},
    {"ball_dead", 0x16CC, 49, 25, 65, 41},
    {"ball_shadow", 0x16CC, 139, 28, 155, 44},
    {"hp2", 0x1717, 1, 1, 29, 19},
    {"st_sleep", 0x1847, 2, 1, 50, 15},
    {"st_poison", 0x1847, 2, 29, 50, 43},
    {"st_freeze", 0x1847, 2, 43, 50, 57},
    {"st_burn", 0x1847, 2, 57, 50, 71},
    {"st_paralysis", 0x1847, 2, 71, 50, 85},
    {"st_fainted", 0x1847, 2, 99, 50, 113},
};

bool SheetFits(uint32_t tex, const Img& s) {
  switch (tex) {
    case 0x1718: case 0x18A1: case 0x17C7: return s.w == 352 && s.h == 176;
    case 0x16CD: case 0x16CF: case 0x17BD: return s.w == 220 && s.h == 74;
    case 0x16CC: return s.w == 244 && s.h == 58;
    case 0x1717: return s.w == 258 && s.h == 20;
    case 0x1847: return s.w == 52 && s.h == 128;
    default: return false;
  }
}

bool RailStrip(const Img& capsule, Img& out) {
  const int x = 55;
  if (capsule.w < 60 || capsule.h < 110) return false;
  const int y0 = 2, y1 = 109;
  out.w = 100, out.h = 62;
  out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
  for (int r = 0; r < out.h; r++) {
    const float a = y0 + (y1 - y0) * (static_cast<float>(r) / out.h), b = y0 + (y1 - y0) * (static_cast<float>(r + 1) / out.h);
    float acc[4] = {0, 0, 0, 0}, wsum = 0;
    for (int y = static_cast<int>(std::floor(a)); y < static_cast<int>(std::ceil(b)) && y < capsule.h; y++) {
      const float w = std::min(b, static_cast<float>(y + 1)) - std::max(a, static_cast<float>(y));
      if (w <= 0) continue;
      const uint8_t* p = &capsule.px[(static_cast<size_t>(y) * capsule.w + x) * 4];
      const float al = p[3];
      for (int c = 0; c < 3; c++) acc[c] += p[c] * al * w;
      acc[3] += al * w;
      wsum += w;
    }
    for (int c = 0; c < out.w; c++) {
      uint8_t* d = &out.px[(static_cast<size_t>(r) * out.w + c) * 4];
      if (wsum <= 0 || acc[3] <= 0) continue;
      for (int k = 0; k < 3; k++) d[k] = static_cast<uint8_t>(std::lround(acc[k] / acc[3]));
      d[3] = static_cast<uint8_t>(std::lround(acc[3] / wsum));
    }
  }
  return true;
}

int MakePieces(const fs::path& folder, const Archive& orre, const Archive& fight) {
  const fs::path dir = folder / "ui";
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::map<uint32_t, Img> sheets;
  auto sheet = [&](uint32_t id) -> const Img* {
    auto it = sheets.find(id);
    if (it == sheets.end()) {
      Img s;
      const bool orreSheet = id == 0x1718 || id == 0x18A1 || id == 0x17C7 || id == 0x1717;
      bool ok = Texture(orreSheet ? orre : fight, id, s);
      if (!ok) ok = Texture(orreSheet ? fight : orre, id, s);
      if (!ok || !SheetFits(id, s)) s = Img{};
      it = sheets.emplace(id, std::move(s)).first;
    }
    return it->second.w > 0 ? &it->second : nullptr;
  };
  int made = 0;
  auto save = [&](const char* name, const Img& native) {
    const Img big = Up(native, 3);
    const std::vector<uint8_t> png = EncodePng(big.w, big.h, big.px);
    if (!png.empty() && WriteFile(dir / (std::string(name) + ".png"), png)) made++;
  };
  for (const PieceSpec& p : kPieces) {
    const Img* s = sheet(p.tex);
    Img c;
    if (s == nullptr || !Crop(*s, p.x0, p.y0, p.x1, p.y1, c)) continue;
    save(p.name, c);
  }
  const std::pair<const char*, uint32_t> rails[] = {{"railmid_shadow", 0x18A1}, {"railmid_faint", 0x17C7}, {"railmid_silver", 0x1718}};
  for (const auto& r : rails) {
    const Img* s = sheet(r.second);
    Img strip;
    if (s != nullptr && RailStrip(*s, strip)) save(r.first, strip);
  }
  if (const Img* s = sheet(0x1718)) {
    Img c;
    if (Crop(*s, 0, 110, 350, 174, c)) save("small_silver", c);
  }
  return made;
}

struct FontBlock {
  const uint8_t* b;
  size_t size;
  uint32_t info;
};
struct FontDef {
  int width = 0;
  std::vector<FontBlock> blocks;
};

std::map<int, FontDef> FontsOf(const std::vector<FontTable>& tables) {
  std::map<int, FontDef> out;
  for (const FontTable& t : tables) {
    const uint8_t* b = t.bytes.data();
    size_t p = 0;
    for (int guard = 0; guard < 64 && p + 8 <= t.bytes.size(); guard++) {
      const uint16_t fid = Be16(b + p);
      if (fid == 0xFFFF) break;
      FontDef& d = out[fid];
      if (d.blocks.empty()) d.width = b[p + 2];
      d.blocks.push_back({b, t.bytes.size(), static_cast<uint32_t>(p + 8)});
      const uint32_t nxt = Be32(b + p + 4);
      if (nxt == 0) break;
      p += nxt;
    }
  }
  return out;
}

struct Glyph {
  int w = 0, h = 0, yofs = 0;
  std::vector<uint8_t> a;
};

bool GlyphOf(const FontDef& f, uint32_t code, Glyph& g) {
  for (const FontBlock& fb : f.blocks) {
    if (fb.info + 16 > fb.size) continue;
    const uint8_t* b = fb.b;
    const uint32_t count = Be16(b + fb.info), data = Be32(b + fb.info + 4);
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      const size_t e = fb.info + 16 + static_cast<size_t>(mid) * 8;
      if (e + 8 > fb.size) break;
      const uint16_t c = Be16(b + e);
      if (c == code) {
        g.w = b[e + 2], g.h = b[e + 3];
        const uint32_t bits = Be32(b + e + 4);
        g.yofs = static_cast<int8_t>(bits >> 24);
        const size_t src = fb.info + data + (bits & 0xFFFFFF);
        const int rb = ((g.w + 1) & ~1) / 2;
        if (g.w <= 0 || g.h <= 0 || src + static_cast<size_t>(rb) * g.h > fb.size) return false;
        g.a.assign(static_cast<size_t>(g.w) * g.h, 0);
        for (int y = 0; y < g.h; y++)
          for (int x = 0; x < g.w; x++) {
            const uint8_t v = b[src + static_cast<size_t>(y) * rb + x / 2];
            g.a[static_cast<size_t>(y) * g.w + x] = static_cast<uint8_t>(((x & 1) ? (v & 15) : (v >> 4)) * 17);
          }
        return true;
      }
      if (c < code) lo = mid + 1;
      else hi = mid;
    }
  }
  return false;
}

struct Pt {
  double x, y;
};

void Rdp(const std::vector<Pt>& q, size_t a, size_t b, double eps, std::vector<char>& keep) {
  if (b <= a + 1) return;
  const Pt s = q[a], t = q[b];
  const double dx = t.x - s.x, dy = t.y - s.y, n = std::hypot(dx, dy);
  double best = -1;
  size_t at = a;
  for (size_t i = a + 1; i < b; i++) {
    const double d = n > 1e-9 ? std::fabs(dx * (q[i].y - s.y) - dy * (q[i].x - s.x)) / n : std::hypot(q[i].x - s.x, q[i].y - s.y);
    if (d > best) best = d, at = i;
  }
  if (best < eps) return;
  keep[at] = 1;
  Rdp(q, a, at, eps, keep);
  Rdp(q, at, b, eps, keep);
}

std::vector<Pt> Simplify(const std::vector<Pt>& ring, double eps) {
  const size_t h = ring.size() / 2;
  std::vector<Pt> out;
  for (int half = 0; half < 2; half++) {
    const size_t a = half == 0 ? 0 : h, b = half == 0 ? h : ring.size() - 1;
    std::vector<Pt> seg(ring.begin() + a, ring.begin() + b + 1);
    std::vector<char> keep(seg.size(), 0);
    keep.front() = keep.back() = 1;
    Rdp(seg, 0, seg.size() - 1, eps, keep);
    for (size_t i = 0; i + 1 < seg.size(); i++)
      if (keep[i]) out.push_back(seg[i]);
  }
  return out;
}

constexpr int kUp = 6;
constexpr double kBlur = kUp * 0.22;

std::vector<std::vector<Pt>> Trace(const Glyph& g) {
  const int pad = 2, W0 = g.w + 2 * pad, H0 = g.h + 2 * pad, W = W0 * kUp, H = H0 * kUp;
  std::vector<float> src(static_cast<size_t>(W0) * H0, 0.0f);
  for (int y = 0; y < g.h; y++)
    for (int x = 0; x < g.w; x++) src[static_cast<size_t>(y + pad) * W0 + x + pad] = g.a[static_cast<size_t>(y) * g.w + x] / 255.0f;
  auto axisW = [&](int srcN, int dstN, std::vector<int>& idx, std::vector<float>& wt) {
    idx.resize(static_cast<size_t>(dstN) * 4), wt.resize(static_cast<size_t>(dstN) * 4);
    for (int j = 0; j < dstN; j++) {
      const float s = (j + 0.5f) / kUp - 0.5f;
      const int i0 = static_cast<int>(std::floor(s));
      const float t = s - i0;
      float sum = 0;
      for (int m = 0; m < 4; m++) {
        const int i = i0 - 1 + m;
        const float w = Cubic(t - (m - 1));
        idx[static_cast<size_t>(j) * 4 + m] = (i < 0 || i >= srcN) ? -1 : i;
        wt[static_cast<size_t>(j) * 4 + m] = w;
        sum += w;
      }
      for (int m = 0; m < 4; m++) wt[static_cast<size_t>(j) * 4 + m] /= sum;
    }
  };
  std::vector<int> ix, iy;
  std::vector<float> wx, wy;
  axisW(W0, W, ix, wx);
  axisW(H0, H, iy, wy);
  std::vector<float> tmp(static_cast<size_t>(W) * H0);
  for (int y = 0; y < H0; y++)
    for (int x = 0; x < W; x++) {
      float v = 0;
      for (int m = 0; m < 4; m++) {
        const int i = ix[static_cast<size_t>(x) * 4 + m];
        if (i >= 0) v += src[static_cast<size_t>(y) * W0 + i] * wx[static_cast<size_t>(x) * 4 + m];
      }
      tmp[static_cast<size_t>(y) * W + x] = v;
    }
  std::vector<float> big(static_cast<size_t>(W) * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v = 0;
      for (int m = 0; m < 4; m++) {
        const int i = iy[static_cast<size_t>(y) * 4 + m];
        if (i >= 0) v += tmp[static_cast<size_t>(i) * W + x] * wy[static_cast<size_t>(y) * 4 + m];
      }
      big[static_cast<size_t>(y) * W + x] = v;
    }
  const int R = static_cast<int>(std::ceil(4 * kBlur));
  std::vector<float> k(static_cast<size_t>(2 * R + 1));
  float ks = 0;
  for (int i = -R; i <= R; i++) ks += (k[static_cast<size_t>(i + R)] = static_cast<float>(std::exp(-0.5 * i * i / (kBlur * kBlur))));
  for (float& v : k) v /= ks;
  auto refl = [](int i, int n) {
    while (i < 0 || i >= n) i = i < 0 ? -i - 1 : 2 * n - i - 1;
    return i;
  };
  std::vector<float> bt(static_cast<size_t>(W) * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v = 0;
      for (int i = -R; i <= R; i++) v += big[static_cast<size_t>(y) * W + refl(x + i, W)] * k[static_cast<size_t>(i + R)];
      bt[static_cast<size_t>(y) * W + x] = v;
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v = 0;
      for (int i = -R; i <= R; i++) v += bt[static_cast<size_t>(refl(y + i, H)) * W + x] * k[static_cast<size_t>(i + R)];
      big[static_cast<size_t>(y) * W + x] = v;
    }
  const float iso = 0.5f;
  auto V = [&](int x, int y) { return big[static_cast<size_t>(y) * W + x]; };
  struct Seg {
    long a, b;
  };
  std::unordered_map<long, Pt> pos;
  std::unordered_map<long, long> next;
  auto edgeId = [&](int kind, int x, int y) { return (static_cast<long>(y) * W + x) * 2 + kind; };
  auto edgePt = [&](long id) {
    auto it = pos.find(id);
    if (it != pos.end()) return it->second;
    const int kind = static_cast<int>(id & 1);
    const long cell = id >> 1;
    const int x = static_cast<int>(cell % W), y = static_cast<int>(cell / W);
    Pt p;
    if (kind == 0) {
      const float a = V(x, y), b = V(x + 1, y);
      p = {x + (a == b ? 0.5 : (iso - a) / (b - a)), static_cast<double>(y)};
    } else {
      const float a = V(x, y), b = V(x, y + 1);
      p = {static_cast<double>(x), y + (a == b ? 0.5 : (iso - a) / (b - a))};
    }
    pos[id] = p;
    return p;
  };
  for (int y = 0; y + 1 < H; y++) {
    for (int x = 0; x + 1 < W; x++) {
      const bool c00 = V(x, y) >= iso, c10 = V(x + 1, y) >= iso, c11 = V(x + 1, y + 1) >= iso, c01 = V(x, y + 1) >= iso;
      const int cs = int(c00) | int(c10) << 1 | int(c11) << 2 | int(c01) << 3;
      if (cs == 0 || cs == 15) continue;
      const long T = edgeId(0, x, y), Bm = edgeId(0, x, y + 1), L = edgeId(1, x, y), Rr = edgeId(1, x + 1, y);
      struct S {
        long a, b;
        double cx, cy;
      };
      S segs[2];
      int n = 0;
      auto add = [&](long a, long b, double cx, double cy) { segs[n++] = {a, b, cx, cy}; };
      switch (cs) {
        case 1: add(L, T, x, y); break;
        case 2: add(T, Rr, x + 1, y); break;
        case 3: add(L, Rr, x, y); break;
        case 4: add(Rr, Bm, x + 1, y + 1); break;
        case 5: {
          const bool joined = (V(x, y) + V(x + 1, y) + V(x + 1, y + 1) + V(x, y + 1)) / 4 >= iso;
          if (joined) add(T, Rr, x, y), add(L, Bm, x, y);
          else add(L, T, x, y), add(Rr, Bm, x + 1, y + 1);
          break;
        }
        case 6: add(T, Bm, x + 1, y); break;
        case 7: add(L, Bm, x, y); break;
        case 8: add(Bm, L, x, y + 1); break;
        case 9: add(T, Bm, x, y); break;
        case 10: {
          const bool joined = (V(x, y) + V(x + 1, y) + V(x + 1, y + 1) + V(x, y + 1)) / 4 >= iso;
          if (joined) add(L, T, x + 1, y), add(Rr, Bm, x + 1, y);
          else add(T, Rr, x + 1, y), add(Bm, L, x, y + 1);
          break;
        }
        case 11: add(Rr, Bm, x, y); break;
        case 12: add(L, Rr, x + 1, y + 1); break;
        case 13: add(T, Rr, x, y); break;
        case 14: add(L, T, x + 1, y); break;
        default: break;
      }
      for (int i = 0; i < n; i++) {
        const Pt pa = edgePt(segs[i].a), pb = edgePt(segs[i].b);
        const double cross = (pb.x - pa.x) * (segs[i].cy - pa.y) - (pb.y - pa.y) * (segs[i].cx - pa.x);
        if (cross < 0) next[segs[i].a] = segs[i].b;
        else next[segs[i].b] = segs[i].a;
      }
    }
  }
  std::vector<std::vector<Pt>> out;
  std::set<long> done;
  for (const auto& kv : next) {
    if (done.count(kv.first)) continue;
    std::vector<Pt> ring;
    long cur = kv.first;
    bool closed = false;
    for (size_t guard = 0; guard < next.size() + 2; guard++) {
      if (done.count(cur)) { closed = ring.size() > 2; break; }
      done.insert(cur);
      const Pt p = edgePt(cur);
      ring.push_back({(p.x + 0.5) / kUp - pad, (p.y + 0.5) / kUp - pad});
      auto it = next.find(cur);
      if (it == next.end()) break;
      cur = it->second;
    }
    if (!closed || ring.size() < 6) continue;
    std::vector<Pt> r = Simplify(ring, 0.045);
    if (r.size() >= 3) out.push_back(std::move(r));
  }
  return out;
}

struct Out {
  std::vector<uint8_t> v;
  void u8(uint32_t x) { v.push_back(static_cast<uint8_t>(x)); }
  void u16(uint32_t x) { u8(x >> 8), u8(x); }
  void u32(uint32_t x) { u16(x >> 16), u16(x & 0xFFFF); }
  void i16(int x) { u16(static_cast<uint16_t>(x)); }
  void pad4() { while (v.size() & 3) v.push_back(0); }
};

uint32_t Sum(const std::vector<uint8_t>& t) {
  uint32_t s = 0;
  for (size_t i = 0; i < t.size(); i += 4) {
    uint32_t w = 0;
    for (size_t k = 0; k < 4; k++) w = w << 8 | (i + k < t.size() ? t[i + k] : 0);
    s += w;
  }
  return s;
}

struct GlyphOut {
  uint32_t code = 0;
  int advance = 0;
  int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
  std::vector<std::vector<std::pair<int, int>>> contours;
};

std::vector<uint8_t> WriteTtf(int fid, int width, const std::vector<GlyphOut>& glyphs, int unit, int asc) {
  const int upem = unit * width;
  const int nGlyphs = static_cast<int>(glyphs.size()) + 2;
  Out glyf, loca;
  int maxPoints = 0, maxContours = 0, xMin = 0, yMin = 0, xMax = 0, yMax = 0, advMax = 0, minLsb = 0x7FFF;
  auto addGlyph = [&](const GlyphOut* g) {
    loca.u32(static_cast<uint32_t>(glyf.v.size()));
    if (g == nullptr || g->contours.empty()) return;
    int pts = 0;
    for (const auto& c : g->contours) pts += static_cast<int>(c.size());
    maxPoints = std::max(maxPoints, pts), maxContours = std::max(maxContours, static_cast<int>(g->contours.size()));
    glyf.i16(static_cast<int>(g->contours.size()));
    glyf.i16(g->xMin), glyf.i16(g->yMin), glyf.i16(g->xMax), glyf.i16(g->yMax);
    int end = -1;
    for (const auto& c : g->contours) glyf.u16(static_cast<uint32_t>(end += static_cast<int>(c.size())));
    glyf.u16(0);
    for (int i = 0; i < pts; i++) glyf.u8(1);
    int px = 0, py = 0;
    for (const auto& c : g->contours)
      for (const auto& p : c) glyf.i16(p.first - px), px = p.first;
    for (const auto& c : g->contours)
      for (const auto& p : c) glyf.i16(p.second - py), py = p.second;
    glyf.pad4();
    xMin = std::min(xMin, g->xMin), yMin = std::min(yMin, g->yMin), xMax = std::max(xMax, g->xMax), yMax = std::max(yMax, g->yMax);
  };
  addGlyph(nullptr), addGlyph(nullptr);
  for (const GlyphOut& g : glyphs) addGlyph(&g);
  loca.u32(static_cast<uint32_t>(glyf.v.size()));
  Out hmtx;
  hmtx.u16(static_cast<uint32_t>(unit * width / 2)), hmtx.i16(0);
  hmtx.u16(static_cast<uint32_t>(unit * width / 2)), hmtx.i16(0);
  advMax = unit * width / 2;
  for (const GlyphOut& g : glyphs) {
    hmtx.u16(static_cast<uint32_t>(g.advance)), hmtx.i16(g.contours.empty() ? 0 : g.xMin);
    advMax = std::max(advMax, g.advance);
    if (!g.contours.empty()) minLsb = std::min(minLsb, g.xMin);
  }
  if (minLsb == 0x7FFF) minLsb = 0;
  struct Seg {
    uint32_t a, b;
    int gid;
  };
  std::vector<Seg> segs;
  segs.push_back({32, 32, 1});
  segs.push_back({0xA0, 0xA0, 1});
  for (size_t i = 0; i < glyphs.size(); i++) {
    const uint32_t c = glyphs[i].code;
    if (c > 0xFFFE || c == 32 || c == 0xA0) continue;
    const int gid = static_cast<int>(i) + 2;
    if (!segs.empty() && segs.back().b + 1 == c && segs.back().gid + static_cast<int>(segs.back().b - segs.back().a) + 1 == gid && segs.back().a != 32 && segs.back().a != 0xA0) segs.back().b = c;
    else segs.push_back({c, c, gid});
  }
  std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) { return a.a < b.a; });
  segs.push_back({0xFFFF, 0xFFFF, 0});
  Out cmap;
  cmap.u16(0), cmap.u16(1), cmap.u16(3), cmap.u16(1), cmap.u32(12);
  const int segN = static_cast<int>(segs.size());
  cmap.u16(4), cmap.u16(static_cast<uint32_t>(16 + segN * 8)), cmap.u16(0), cmap.u16(static_cast<uint32_t>(segN * 2));
  int sr = 1, es = 0;
  while (sr * 2 <= segN) sr *= 2, es++;
  cmap.u16(static_cast<uint32_t>(sr * 2)), cmap.u16(static_cast<uint32_t>(es)), cmap.u16(static_cast<uint32_t>(segN * 2 - sr * 2));
  for (const Seg& s : segs) cmap.u16(s.b);
  cmap.u16(0);
  for (const Seg& s : segs) cmap.u16(s.a);
  for (const Seg& s : segs) cmap.u16(s.a == 0xFFFF ? 1 : static_cast<uint32_t>((s.gid - static_cast<int>(s.a)) & 0xFFFF));
  for (size_t i = 0; i < segs.size(); i++) cmap.u16(0);
  Out head;
  head.u32(0x00010000), head.u32(0x00010000), head.u32(0), head.u32(0x5F0F3CF5), head.u16(1), head.u16(static_cast<uint32_t>(upem));
  head.u32(0), head.u32(0), head.u32(0), head.u32(0);
  head.i16(xMin), head.i16(yMin), head.i16(xMax), head.i16(yMax);
  head.u16(0), head.u16(8), head.i16(2), head.i16(1), head.i16(0);
  const int ascent = unit * (asc - 1), descent = -unit * 4;
  Out hhea;
  hhea.u32(0x00010000), hhea.i16(ascent), hhea.i16(descent), hhea.i16(0), hhea.u16(static_cast<uint32_t>(advMax)), hhea.i16(minLsb), hhea.i16(0),
      hhea.i16(xMax), hhea.i16(1), hhea.i16(0), hhea.i16(0);
  for (int i = 0; i < 4; i++) hhea.i16(0);
  hhea.i16(0), hhea.u16(static_cast<uint32_t>(nGlyphs));
  Out maxp;
  maxp.u32(0x00010000), maxp.u16(static_cast<uint32_t>(nGlyphs)), maxp.u16(static_cast<uint32_t>(maxPoints)), maxp.u16(static_cast<uint32_t>(maxContours)), maxp.u16(0), maxp.u16(0);
  maxp.u16(1), maxp.u16(0), maxp.u16(0), maxp.u16(0), maxp.u16(0), maxp.u16(0), maxp.u16(0), maxp.u16(0), maxp.u16(0);
  Out os2;
  os2.u16(2), os2.i16(upem / 2), os2.u16(400), os2.u16(5), os2.u16(0);
  for (int i = 0; i < 10; i++) os2.i16(0);
  os2.i16(0);
  for (int i = 0; i < 10; i++) os2.u8(0);
  for (int i = 0; i < 4; i++) os2.u32(0);
  os2.u8('X'), os2.u8('D'), os2.u8(' '), os2.u8(' ');
  os2.u16(0x40), os2.u16(32), os2.u16(0xFFFF);
  os2.i16(ascent), os2.i16(descent), os2.i16(0), os2.u16(static_cast<uint32_t>(ascent)), os2.u16(static_cast<uint32_t>(-descent));
  os2.u32(0), os2.u32(0);
  os2.i16(unit * (asc - 6)), os2.i16(unit * (asc - 5)), os2.u16(0), os2.u16(32), os2.u16(0);
  const std::string fam = "XD" + std::to_string(fid);
  const std::string strs[4] = {fam, "Regular", fam, fam + "-Regular"};
  const int ids[4] = {1, 2, 4, 6};
  Out name;
  name.u16(0), name.u16(4), name.u16(6 + 4 * 12);
  uint32_t off = 0;
  for (int i = 0; i < 4; i++) {
    name.u16(3), name.u16(1), name.u16(0x409), name.u16(static_cast<uint32_t>(ids[i])), name.u16(static_cast<uint32_t>(strs[i].size() * 2)), name.u16(off);
    off += static_cast<uint32_t>(strs[i].size() * 2);
  }
  for (int i = 0; i < 4; i++)
    for (char c : strs[i]) name.u8(0), name.u8(static_cast<uint8_t>(c));
  Out post;
  post.u32(0x00030000), post.u32(0), post.i16(-unit), post.i16(unit / 2 + 1), post.u32(0), post.u32(0), post.u32(0), post.u32(0), post.u32(0);
  struct T {
    const char* tag;
    std::vector<uint8_t>* d;
  };
  std::vector<T> tabs = {{"OS/2", &os2.v}, {"cmap", &cmap.v}, {"glyf", &glyf.v}, {"head", &head.v}, {"hhea", &hhea.v}, {"hmtx", &hmtx.v},
                         {"loca", &loca.v}, {"maxp", &maxp.v}, {"name", &name.v}, {"post", &post.v}};
  Out f;
  const int nt = static_cast<int>(tabs.size());
  int srt = 1, est = 0;
  while (srt * 2 <= nt) srt *= 2, est++;
  f.u32(0x00010000), f.u16(static_cast<uint32_t>(nt)), f.u16(static_cast<uint32_t>(srt * 16)), f.u16(static_cast<uint32_t>(est)), f.u16(static_cast<uint32_t>(nt * 16 - srt * 16));
  uint32_t at = 12 + 16 * nt;
  std::vector<uint32_t> offs;
  for (T& t : tabs) {
    f.u8(t.tag[0]), f.u8(t.tag[1]), f.u8(t.tag[2]), f.u8(t.tag[3]);
    f.u32(Sum(*t.d)), f.u32(at), f.u32(static_cast<uint32_t>(t.d->size()));
    offs.push_back(at);
    at += static_cast<uint32_t>((t.d->size() + 3) & ~size_t(3));
  }
  size_t headAt = 0;
  for (size_t i = 0; i < tabs.size(); i++) {
    if (std::strcmp(tabs[i].tag, "head") == 0) headAt = f.v.size();
    f.v.insert(f.v.end(), tabs[i].d->begin(), tabs[i].d->end());
    f.pad4();
  }
  const uint32_t adj = 0xB1B0AFBAu - Sum(f.v);
  f.v[headAt + 8] = uint8_t(adj >> 24), f.v[headAt + 9] = uint8_t(adj >> 16), f.v[headAt + 10] = uint8_t(adj >> 8), f.v[headAt + 11] = uint8_t(adj);
  return f.v;
}

}

std::vector<std::pair<int, std::vector<uint8_t>>> BuildFonts(const std::vector<FontTable>& tables) {
  std::vector<std::pair<int, std::vector<uint8_t>>> result;
  const std::map<int, FontDef> fonts = FontsOf(tables);
  constexpr int kUnit = 64;
  for (const auto& kv : fonts) {
    const int fid = kv.first;
    if (fid < 0 || fid > 3) continue;
    const FontDef& f = kv.second;
    const int asc = f.width + 2;
    std::set<uint32_t> codes;
    for (const FontBlock& fb : f.blocks) {
      if (fb.info + 16 > fb.size) continue;
      const uint32_t count = Be16(fb.b + fb.info);
      for (uint32_t k = 0; k < count && fb.info + 16 + (k + 1) * 8 <= fb.size; k++) codes.insert(Be16(fb.b + fb.info + 16 + k * 8));
    }
    std::vector<GlyphOut> outs;
    for (uint32_t code : codes) {
      if (code == 32) continue;
      Glyph g;
      if (!GlyphOf(f, code, g)) continue;
      GlyphOut o;
      o.code = code;
      o.advance = kUnit * g.w;
      bool first = true;
      for (const auto& ring : Trace(g)) {
        std::vector<std::pair<int, int>> c;
        for (const Pt& p : ring) {
          const std::pair<int, int> q{static_cast<int>(std::lround(p.x * kUnit)), static_cast<int>(std::lround((asc - g.yofs - p.y) * kUnit))};
          if (!c.empty() && c.back() == q) continue;
          c.push_back(q);
        }
        if (c.size() > 1 && c.front() == c.back()) c.pop_back();
        if (c.size() < 3) continue;
        for (const auto& q : c) {
          if (first) o.xMin = o.xMax = q.first, o.yMin = o.yMax = q.second, first = false;
          o.xMin = std::min(o.xMin, q.first), o.xMax = std::max(o.xMax, q.first), o.yMin = std::min(o.yMin, q.second), o.yMax = std::max(o.yMax, q.second);
        }
        o.contours.push_back(std::move(c));
      }
      outs.push_back(std::move(o));
    }
    result.emplace_back(fid, WriteTtf(fid, f.width, outs, kUnit, asc));
  }
  return result;
}

namespace {

enum class Step { Check, ReadOrre, ReadFight, CopyFont, Run, Done };
Step sStep = Step::Check;
fs::path sFolder;
bool sNeedPieces = false, sNeedFont = false;
Archive sOrre, sFight;
std::vector<FontTable> sTables;
std::thread sThread;

void Worker() {
  using C = std::chrono::steady_clock;
  const fs::path folder = sFolder;
  if (sNeedPieces) {
    const auto t0 = C::now();
    const int made = MakePieces(folder, sOrre, sFight);
    sOrre = Archive{}, sFight = Archive{};
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(C::now() - t0).count());
    std::fprintf(stderr, "[stream overlay] pieces: %d pictures made from the disc in %ld ms\n", made, ms);
    if (made >= 25) WriteMarker(folder / "ui" / "assets.txt", kPiecesMarker, kPiecesVersion);
  }
  if (sNeedFont) {
    const auto t0 = C::now();
    const auto fonts = BuildFonts(sTables);
    sTables.clear();
    std::error_code ec;
    fs::create_directories(folder / "font", ec);
    int n = 0;
    for (const auto& f : fonts)
      if (!f.second.empty() && WriteFile(folder / "font" / ("xd" + std::to_string(f.first) + ".ttf"), f.second)) n++;
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(C::now() - t0).count());
    std::fprintf(stderr, "[stream overlay] font: %d files traced from the game's font tables in %ld ms\n", n, ms);
    if (n == 4) WriteMarker(folder / "font" / "assets.txt", kFontMarker, kFontVersion);
  }
}

}

void Pump(const fs::path& folder) {
  switch (sStep) {
    case Step::Done:
    case Step::Run:
      return;
    case Step::Check: {
      sFolder = folder;
      sNeedPieces = !MarkerCurrent(folder / "ui" / "assets.txt", kPiecesMarker, kPiecesVersion);
      sNeedFont = !MarkerCurrent(folder / "font" / "assets.txt", kFontMarker, kFontVersion);
      sStep = sNeedPieces ? Step::ReadOrre : sNeedFont ? Step::CopyFont : Step::Done;
      return;
    }
    case Step::ReadOrre:
      sOrre.bytes = xd::kit2::DiscFile("orre_menu.fsys");
      sStep = Step::ReadFight;
      return;
    case Step::ReadFight:
      sFight.bytes = xd::kit2::DiscFile("fight_common.fsys");
      sStep = sNeedFont ? Step::CopyFont : Step::Run;
      if (sStep == Step::Run) sThread = std::thread(Worker);
      return;
    case Step::CopyFont: {
      for (const char* name : {"resident_fnt", "common_fnt"}) {
        const uint8_t* t = PortXD_GetDolTable(name);
        uint32_t size = 0;
        for (int i = 0; i < PortXD_DolTableCount(); i++)
          if (std::strcmp(PortXD_DolTableName(i), name) == 0) size = PortXD_DolTableSize(i);
        if (t != nullptr && size > 8) sTables.push_back({std::vector<uint8_t>(t, t + size)});
      }
      if (sTables.empty()) {
        std::fprintf(stderr, "[stream overlay] font: the game's font tables are not available; the pages use system fonts\n");
        sNeedFont = false;
      }
      sStep = Step::Run;
      if (sNeedPieces || sNeedFont) sThread = std::thread(Worker);
      else sStep = Step::Done;
      return;
    }
  }
}

void Join() {
  if (sThread.joinable()) sThread.join();
}

}
