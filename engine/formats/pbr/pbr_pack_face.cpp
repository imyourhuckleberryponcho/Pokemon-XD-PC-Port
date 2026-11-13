#include <algorithm>
#include <cmath>
#include <cstring>

#include "pbr_pack.h"

namespace pbr {
namespace {

constexpr int kXdW = 42, kXdH = 84;

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
  if (o + 4 > d.size()) throw Fail("face: file too short");
  return ((uint32_t)d[o] << 24) | ((uint32_t)d[o + 1] << 16) | ((uint32_t)d[o + 2] << 8) | d[o + 3];
}
uint32_t be16(const std::vector<uint8_t>& d, size_t o) {
  if (o + 2 > d.size()) throw Fail("face: file too short");
  return ((uint32_t)d[o] << 8) | d[o + 1];
}

struct FRgba { uint8_t r, g, b, a; };
struct FImage {
  int w = 0, h = 0;
  std::vector<FRgba> px;
  FImage() = default;
  FImage(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, FRgba{0, 0, 0, 0}) {}
  FRgba& at(int x, int y) { return px[(size_t)y * w + x]; }
  const FRgba& at(int x, int y) const { return px[(size_t)y * w + x]; }
};

FRgba rgb5a3(unsigned v) {
  if (v & 0x8000) {
    unsigned r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    return {(uint8_t)(r * 255 / 31), (uint8_t)(g * 255 / 31), (uint8_t)(b * 255 / 31), 255};
  }
  unsigned a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
  return {(uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17), (uint8_t)(a * 255 / 7)};
}
unsigned to_rgb5a3(const FRgba& c) {
  if (c.a >= 0xE0) return 0x8000u | (unsigned)(c.r >> 3) << 10 | (unsigned)(c.g >> 3) << 5 | (unsigned)(c.b >> 3);
  return (unsigned)(c.a >> 5) << 12 | (unsigned)(c.r >> 4) << 8 | (unsigned)(c.g >> 4) << 4 | (unsigned)(c.b >> 4);
}

FImage decode_c8(const std::vector<uint8_t>& d) {
  const int w = (int)be16(d, 0), h = (int)be16(d, 2);
  const size_t off = be32(d, 0x28), poff = be32(d, 0x48);
  FRgba pal[256];
  for (int i = 0; i < 256; i++) pal[i] = rgb5a3(be16(d, poff + 2 * i));
  const int pw = (w + 7) / 8 * 8, ph = (h + 3) / 4 * 4;
  FImage im(w, h);
  size_t p = off;
  for (int ty = 0; ty < ph; ty += 4)
    for (int tx = 0; tx < pw; tx += 8)
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 8; x++) {
          if (p >= d.size()) throw Fail("face: texture data too short");
          const int idx = d[p++];
          if (tx + x < w && ty + y < h) im.at(tx + x, ty + y) = pal[idx];
        }
  return im;
}

FImage crop(const FImage& in, int x0, int y0, int x1, int y1) {
  FImage o(x1 - x0, y1 - y0);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) o.at(x - x0, y - y0) = in.at(x, y);
  return o;
}

double sinc_filter(double x) {
  if (x == 0.0) return 1.0;
  x = x * 3.14159265358979323846;
  return std::sin(x) / x;
}
double lanczos_filter(double x) {
  if (-3.0 <= x && x < 3.0) return sinc_filter(x) * sinc_filter(x / 3);
  return 0.0;
}
constexpr int kPrecisionBits = 32 - 8 - 2;

struct Coeffs {
  int ksize = 0;
  std::vector<int> bounds;
  std::vector<int32_t> kk;
};
Coeffs precompute(int in_size, float in0, float in1, int out_size) {
  double filterscale, scale;
  filterscale = scale = (double)(in1 - in0) / out_size;
  if (filterscale < 1.0) filterscale = 1.0;
  const double support = 3.0 * filterscale;
  Coeffs c;
  c.ksize = (int)std::ceil(support) * 2 + 1;
  std::vector<double> kk((size_t)out_size * c.ksize);
  c.bounds.resize((size_t)out_size * 2);
  for (int xx = 0; xx < out_size; xx++) {
    const double center = in0 + (xx + 0.5) * scale;
    double ww = 0.0;
    const double ss = 1.0 / filterscale;
    int xmin = (int)(center - support + 0.5);
    if (xmin < 0) xmin = 0;
    int xmax = (int)(center + support + 0.5);
    if (xmax > in_size) xmax = in_size;
    xmax -= xmin;
    double* k = &kk[(size_t)xx * c.ksize];
    int x;
    for (x = 0; x < xmax; x++) {
      const double w = lanczos_filter((x + xmin - center + 0.5) * ss);
      k[x] = w;
      ww += w;
    }
    for (x = 0; x < xmax; x++)
      if (ww != 0.0) k[x] /= ww;
    for (; x < c.ksize; x++) k[x] = 0;
    c.bounds[(size_t)xx * 2] = xmin;
    c.bounds[(size_t)xx * 2 + 1] = xmax;
  }
  c.kk.resize(kk.size());
  for (size_t x = 0; x < kk.size(); x++) {
    if (kk[x] < 0) c.kk[x] = (int32_t)(-0.5 + kk[x] * (1 << kPrecisionBits));
    else c.kk[x] = (int32_t)(0.5 + kk[x] * (1 << kPrecisionBits));
  }
  return c;
}
inline uint8_t clip8(int32_t v) {
  const int32_t s = v >> kPrecisionBits;
  return (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);
}

FImage resample_rgba(const FImage& in, int xsize, int ysize) {
  const Coeffs h = precompute(in.w, 0.f, (float)in.w, xsize);
  const Coeffs v = precompute(in.h, 0.f, (float)in.h, ysize);
  FImage tmp(xsize, in.h);
  for (int yy = 0; yy < in.h; yy++)
    for (int xx = 0; xx < xsize; xx++) {
      const int xmin = h.bounds[(size_t)xx * 2], xmax = h.bounds[(size_t)xx * 2 + 1];
      const int32_t* k = &h.kk[(size_t)xx * h.ksize];
      int ss[4];
      for (int c = 0; c < 4; c++) ss[c] = 1 << (kPrecisionBits - 1);
      for (int x = 0; x < xmax; x++) {
        const FRgba& p = in.at(x + xmin, yy);
        ss[0] += p.r * k[x]; ss[1] += p.g * k[x]; ss[2] += p.b * k[x]; ss[3] += p.a * k[x];
      }
      tmp.at(xx, yy) = {clip8(ss[0]), clip8(ss[1]), clip8(ss[2]), clip8(ss[3])};
    }
  FImage out(xsize, ysize);
  for (int yy = 0; yy < ysize; yy++) {
    const int ymin = v.bounds[(size_t)yy * 2], ymax = v.bounds[(size_t)yy * 2 + 1];
    const int32_t* k = &v.kk[(size_t)yy * v.ksize];
    for (int xx = 0; xx < xsize; xx++) {
      int ss[4];
      for (int c = 0; c < 4; c++) ss[c] = 1 << (kPrecisionBits - 1);
      for (int y = 0; y < ymax; y++) {
        const FRgba& p = tmp.at(xx, y + ymin);
        ss[0] += p.r * k[y]; ss[1] += p.g * k[y]; ss[2] += p.b * k[y]; ss[3] += p.a * k[y];
      }
      out.at(xx, yy) = {clip8(ss[0]), clip8(ss[1]), clip8(ss[2]), clip8(ss[3])};
    }
  }
  return out;
}

inline unsigned muldiv255(unsigned a, unsigned b) {
  unsigned t = a * b + 128;
  return ((t >> 8) + t) >> 8;
}
FImage resize_lanczos(const FImage& in, int xsize, int ysize) {
  FImage pre = in;
  for (FRgba& p : pre.px) {
    const unsigned a = p.a;
    p.r = (uint8_t)muldiv255(p.r, a);
    p.g = (uint8_t)muldiv255(p.g, a);
    p.b = (uint8_t)muldiv255(p.b, a);
  }
  FImage out = resample_rgba(pre, xsize, ysize);
  for (FRgba& p : out.px) {
    const unsigned a = p.a;
    if (a != 255 && a != 0) {
      auto c = [&](unsigned v) { const unsigned r = 255 * v / a; return (uint8_t)(r > 255 ? 255 : r); };
      p.r = c(p.r); p.g = c(p.g); p.b = c(p.b);
    }
  }
  return out;
}

template <class T, class Cmp>
void shortsort(T* lo, T* hi, Cmp cmp) {
  while (hi > lo) {
    T* max = lo;
    for (T* p = lo + 1; p <= hi; p++)
      if (cmp(*p, *max) > 0) max = p;
    std::swap(*max, *hi);
    hi--;
  }
}
template <class T, class Cmp>
void qsort_ms(T* base, size_t num, Cmp cmp) {
  if (num < 2) return;
  T *lostk[64], *histk[64];
  int stkptr = 0;
  T* lo = base;
  T* hi = base + (num - 1);
  for (;;) {
    const size_t size = (size_t)(hi - lo) + 1;
    if (size <= 8) {
      shortsort(lo, hi, cmp);
    } else {
      T* mid = lo + (size / 2);
      if (cmp(*lo, *mid) > 0) std::swap(*lo, *mid);
      if (cmp(*lo, *hi) > 0) std::swap(*lo, *hi);
      if (cmp(*mid, *hi) > 0) std::swap(*mid, *hi);
      T* loguy = lo;
      T* higuy = hi;
      for (;;) {
        if (mid > loguy) {
          do { loguy++; } while (loguy < mid && cmp(*loguy, *mid) <= 0);
        }
        if (mid <= loguy) {
          do { loguy++; } while (loguy <= hi && cmp(*loguy, *mid) <= 0);
        }
        do { higuy--; } while (higuy > mid && cmp(*higuy, *mid) > 0);
        if (higuy < loguy) break;
        std::swap(*loguy, *higuy);
        if (mid == higuy) mid = loguy;
      }
      higuy++;
      if (mid < higuy) {
        do { higuy--; } while (higuy > mid && cmp(*higuy, *mid) == 0);
      }
      if (mid >= higuy) {
        do { higuy--; } while (higuy > lo && cmp(*higuy, *mid) == 0);
      }
      if (higuy - lo >= hi - loguy) {
        if (lo < higuy) { lostk[stkptr] = lo; histk[stkptr] = higuy; ++stkptr; }
        if (loguy < hi) { lo = loguy; continue; }
      } else {
        if (loguy < hi) { lostk[stkptr] = loguy; histk[stkptr] = hi; ++stkptr; }
        if (lo < higuy) { hi = higuy; continue; }
      }
    }
    if (--stkptr < 0) return;
    lo = lostk[stkptr];
    hi = histk[stkptr];
  }
}

struct Bucket { uint32_t count = 0; uint64_t r = 0, g = 0, b = 0, a = 0; };
struct Cube {
  unsigned rBits, gBits, bBits, aBits, rWidth, gWidth, bWidth, aWidth, rOffset, gOffset, bOffset, aOffset;
  std::vector<Bucket> buckets;
  Cube(unsigned r, unsigned g, unsigned b, unsigned a) : rBits(r), gBits(g), bBits(b), aBits(a) {
    rWidth = 1u << rBits; gWidth = 1u << gBits; bWidth = 1u << bBits; aWidth = 1u << aBits;
    rOffset = gBits + bBits + aBits; gOffset = bBits + aBits; bOffset = aBits; aOffset = 0;
    buckets.assign((size_t)rWidth * gWidth * bWidth * aWidth, Bucket());
  }
  long pos(unsigned r, unsigned g, unsigned b, unsigned a) const { return (long)(r << rOffset | g << gOffset | b << bOffset | a << aOffset); }
  Bucket& of(const FRgba& p) {
    return buckets[(size_t)pos(p.r >> (8 - rBits), p.g >> (8 - gBits), p.b >> (8 - bBits), p.a >> (8 - aBits))];
  }
};
size_t count_used(const Cube& c) {
  size_t n = 0;
  for (const Bucket& b : c.buckets)
    if (b.count > 0) n++;
  return n;
}
FRgba avg_color(const Bucket& b) {
  float count = (float)b.count;
  if (count != 0) {
    auto cl = [](int v) { return (uint8_t)(v <= 0 ? 0 : v < 256 ? v : 255); };
    return {cl((int)((float)b.r / count)), cl((int)((float)b.g / count)), cl((int)((float)b.b / count)), cl((int)((float)b.a / count))};
  }
  return {0, 0, 0, 0};
}
void add_values(const Bucket& s, Bucket& d) {
  d.count += s.count; d.r += s.r; d.g += s.g; d.b += s.b; d.a += s.a;
}
Cube copy_cube(const Cube& cube, unsigned rB, unsigned gB, unsigned bB, unsigned aB) {
  Cube result(rB, gB, bB, aB);
  unsigned src_reduce[4] = {0, 0, 0, 0}, dst_reduce[4] = {0, 0, 0, 0}, width[4];
  const unsigned cb[4] = {cube.rBits, cube.gBits, cube.bBits, cube.aBits};
  const unsigned wb[4] = {rB, gB, bB, aB};
  const unsigned cw[4] = {cube.rWidth, cube.gWidth, cube.bWidth, cube.aWidth};
  const unsigned rw[4] = {result.rWidth, result.gWidth, result.bWidth, result.aWidth};
  for (int i = 0; i < 4; i++) {
    if (cb[i] > wb[i]) { dst_reduce[i] = cb[i] - wb[i]; width[i] = cw[i]; }
    else { src_reduce[i] = wb[i] - cb[i]; width[i] = rw[i]; }
  }
  for (unsigned r = 0; r < width[0]; r++)
    for (unsigned g = 0; g < width[1]; g++)
      for (unsigned b = 0; b < width[2]; b++)
        for (unsigned a = 0; a < width[3]; a++) {
          const long sp = cube.pos(r >> src_reduce[0], g >> src_reduce[1], b >> src_reduce[2], a >> src_reduce[3]);
          const long dp = result.pos(r >> dst_reduce[0], g >> dst_reduce[1], b >> dst_reduce[2], a >> dst_reduce[3]);
          add_values(cube.buckets[(size_t)sp], result.buckets[(size_t)dp]);
        }
  return result;
}
void subtract_buckets(Cube& cube, const Bucket* buckets, long n) {
  for (long i = 0; i < n; i++) {
    const Bucket& s = buckets[i];
    if (s.count == 0) continue;
    const FRgba p = avg_color(s);
    Bucket& m = cube.of(p);
    m.count -= s.count; m.r -= s.r; m.g -= s.g; m.b -= s.b; m.a -= s.a;
  }
}
std::vector<Bucket> sorted_palette(const Cube& cube) {
  std::vector<Bucket> b = cube.buckets;
  qsort_ms(b.data(), b.size(), [](const Bucket& x, const Bucket& y) { return (int)(uint32_t)(y.count - x.count); });
  return b;
}
void add_lookup(Cube& cube, const std::vector<Bucket>& palette, long n, long offset) {
  for (long i = offset + n - 1; i >= offset; i--) {
    const FRgba p = avg_color(palette[(size_t)i]);
    cube.of(p).count = (uint32_t)i;
  }
}

void quantize_octree(const std::vector<FRgba>& pixels, unsigned nQuant, std::vector<FRgba>& palette, std::vector<uint32_t>& out) {
  static const unsigned L[8] = {3, 4, 3, 3, 2, 2, 2, 2};
  Cube fine(L[0], L[1], L[2], L[3]);
  for (const FRgba& p : pixels) {
    Bucket& b = fine.of(p);
    b.count += 1; b.r += p.r; b.g += p.g; b.b += p.b; b.a += p.a;
  }
  Cube coarse = copy_cube(fine, L[4], L[5], L[6], L[7]);
  size_t nCoarse = count_used(coarse);
  if (nCoarse > nQuant) nCoarse = nQuant;
  size_t nFine = nQuant - nCoarse;
  std::vector<Bucket> fineSorted = sorted_palette(fine);
  subtract_buckets(coarse, fineSorted.data(), (long)nFine);
  while (nCoarse > count_used(coarse)) {
    const size_t already = nFine;
    nCoarse = count_used(coarse);
    nFine = nQuant - nCoarse;
    subtract_buckets(coarse, &fineSorted[already], (long)(nFine - already));
  }
  std::vector<Bucket> coarseSorted = sorted_palette(coarse);
  std::vector<Bucket> all(coarseSorted.begin(), coarseSorted.begin() + (long)nCoarse);
  all.insert(all.end(), fineSorted.begin(), fineSorted.begin() + (long)nFine);
  Cube coarseLookup(L[4], L[5], L[6], L[7]);
  add_lookup(coarseLookup, all, (long)nCoarse, 0);
  Cube lookup = copy_cube(coarseLookup, L[0], L[1], L[2], L[3]);
  add_lookup(lookup, all, (long)nFine, (long)nCoarse);
  out.resize(pixels.size());
  for (size_t i = 0; i < pixels.size(); i++) out[i] = lookup.of(pixels[i]).count;
  palette.assign(nQuant, FRgba{0, 0, 0, 0});
  for (unsigned i = 0; i < nQuant; i++) palette[i] = avg_color(all[i]);
}

std::vector<uint8_t> encode_c8(const FImage& im, const std::vector<uint8_t>& tpl) {
  const int w = (int)be16(tpl, 0), h = (int)be16(tpl, 2);
  if (im.w != w || im.h != h) throw Fail("face: picture and template differ in size");
  const size_t off = be32(tpl, 0x28), poff = be32(tpl, 0x48);
  std::vector<FRgba> p = im.px;
  bool seen = false;
  uint8_t tr = 0, tg = 0, tb = 0;
  for (FRgba& q : p) {
    if (q.a == 0) {
      if (!seen) { seen = true; tr = q.r; tg = q.g; tb = q.b; }
      else { q.r = tr; q.g = tg; q.b = tb; }
    }
  }
  std::vector<FRgba> pal;
  std::vector<uint32_t> qp;
  quantize_octree(p, 255, pal, qp);
  FRgba colours[256];
  colours[0] = {0, 0, 0, 0};
  for (int i = 0; i < 255; i++) colours[i + 1] = {pal[(size_t)i].r, pal[(size_t)i].g, pal[(size_t)i].b, 255};
  const int pw = (w + 7) / 8 * 8, ph = (h + 3) / 4 * 4;
  std::vector<uint8_t> out = tpl;
  size_t o = off;
  for (int ty = 0; ty < ph; ty += 4)
    for (int tx = 0; tx < pw; tx += 8)
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 8; x++) {
          unsigned idx = 0;
          if (tx + x < w && ty + y < h) {
            const size_t k = (size_t)(ty + y) * w + (tx + x);
            if (im.px[k].a >= 16) idx = qp[k] + 1;
          }
          if (o >= out.size()) throw Fail("face: template too small");
          out[o++] = (uint8_t)idx;
        }
  for (int i = 0; i < 256; i++) {
    const unsigned v = to_rgb5a3(colours[i]);
    out.at(poff + 2 * i) = (uint8_t)(v >> 8);
    out.at(poff + 2 * i + 1) = (uint8_t)v;
  }
  return out;
}

}

std::vector<FaceOut> gen4_faces(const DiscSource& pbr, const DiscSource& xd) {
  std::vector<uint8_t> tpl;
  {
    const std::vector<uint8_t> pf = xd.read("poke_face.fsys");
    bool found = false;
    for (const FsysEntry& e : fsys_entries(pf))
      if (e.name == "face025") {
        tpl = fsys_extract(pf, e);
        found = true;
        break;
      }
    if (!found) throw Fail("XD's poke_face.fsys has no face025.", 2);
  }
  std::vector<uint8_t> m0, m21;
  {
    const std::vector<uint8_t> common = pbr.read("common.fsys");
    for (const FsysEntry& e : fsys_entries(common)) {
      if (e.index == 0) m0 = fsys_extract(common, e);
      if (e.index == 21) m21 = fsys_extract(common, e);
    }
    if (m0.empty() || m21.empty()) throw Fail("The Battle Revolution disc's picture tables are not as expected.", 2);
  }
  const std::vector<uint8_t> face_data = pbr.read("menu_face.fsys");
  std::map<uint32_t, FsysEntry> face;
  for (const FsysEntry& e : fsys_entries(face_data)) face[e.id] = e;

  std::vector<FaceOut> out;
  for (int dex = 387; dex <= 493; dex++) {
    const size_t r21 = 0x30 + (size_t)dex * 12;
    if (be16(m21, r21 + 2) != (uint32_t)dex) throw Fail("Battle Revolution's picture table row " + std::to_string(dex) + " is not this species.", 2);
    const uint32_t fid = be16(m0, 0x30 + (size_t)be16(m21, r21 + 4) * 16);
    auto it = face.find(fid);
    if (it == face.end()) throw Fail("Battle Revolution has no menu picture " + std::to_string(fid) + ".", 2);
    FImage im = decode_c8(fsys_extract(face_data, it->second));
    if (im.w == 96) im = crop(im, 0, 0, 48, im.h);
    FImage sheet(kXdW, kXdH);
    for (int k = 0; k < 2; k++) {
      const FImage part = crop(im, 0, 48 * k, 48, 48 * k + 48);
      const FImage small = resize_lanczos(part, kXdW, kXdW);
      for (int y = 0; y < kXdW; y++)
        for (int x = 0; x < kXdW; x++) sheet.at(x, k * kXdW + y) = small.at(x, y);
    }
    FaceOut f;
    f.dex = dex;
    f.data = encode_c8(sheet, tpl);
    out.push_back(std::move(f));
  }
  return out;
}

std::vector<IconOut> gen4_icons(const DiscSource& pbr, const DiscSource& xd) {
  constexpr size_t kTexOff = 0xA0;
  constexpr int kCell = 64, kPic = 54;
  constexpr int kTops[3] = {10, 8, 6};
  std::vector<uint8_t> tpl;
  {
    const std::vector<uint8_t> pd = xd.read("poke_dance.fsys");
    for (const FsysEntry& e : fsys_entries(pd))
      if (e.name == "unknown_a") {
        tpl = fsys_extract(pd, e);
        break;
      }
    if (tpl.size() <= kTexOff) throw Fail("XD's poke_dance.fsys has no unknown_a.", 2);
  }
  const std::vector<uint8_t> tex(tpl.begin() + kTexOff, tpl.end());
  std::vector<uint8_t> m0, m21;
  {
    const std::vector<uint8_t> common = pbr.read("common.fsys");
    for (const FsysEntry& e : fsys_entries(common)) {
      if (e.index == 0) m0 = fsys_extract(common, e);
      if (e.index == 21) m21 = fsys_extract(common, e);
    }
    if (m0.empty() || m21.empty()) throw Fail("The Battle Revolution disc's picture tables are not as expected.", 2);
  }
  const std::vector<uint8_t> pic_data = pbr.read("menu_pokemon.fsys");
  std::map<uint32_t, FsysEntry> pics;
  for (const FsysEntry& e : fsys_entries(pic_data)) pics[e.id] = e;
  std::vector<IconOut> out;
  for (int dex = 387; dex <= 493; dex++) {
    if (dex == 438) continue;
    const size_t r21 = 0x30 + (size_t)dex * 12;
    if (be16(m21, r21 + 2) != (uint32_t)dex) throw Fail("Battle Revolution's picture table row " + std::to_string(dex) + " is not this species.", 2);
    const uint32_t pid = be16(m0, 0x30 + (size_t)be16(m21, r21 + 4) * 16 + 4);
    auto it = pics.find(pid);
    if (it == pics.end()) throw Fail("Battle Revolution has no menu picture " + std::to_string(pid) + ".", 2);
    const FImage im = decode_c8(fsys_extract(pic_data, it->second));
    if (im.w != kPic || im.h != 2 * kPic) throw Fail("Battle Revolution's menu picture " + std::to_string(pid) + " has another size.", 2);
    IconOut o;
    o.dex = dex;
    for (int shiny = 0; shiny < 2; shiny++) {
      const FImage pic = crop(im, 0, shiny * kPic, kPic, shiny * kPic + kPic);
      FImage sheet(kCell, 3 * kCell);
      const int x0 = (kCell - kPic) / 2;
      for (int f = 0; f < 3; f++)
        for (int y = 0; y < kPic; y++)
          for (int x = 0; x < kPic; x++) sheet.at(x0 + x, f * kCell + kTops[f] + y) = pic.at(x, y);
      std::vector<uint8_t> ant(tpl.begin(), tpl.begin() + kTexOff);
      const std::vector<uint8_t> t = encode_c8(sheet, tex);
      ant.insert(ant.end(), t.begin(), t.end());
      (shiny ? o.shiny : o.normal) = std::move(ant);
    }
    out.push_back(std::move(o));
  }
  return out;
}

}
