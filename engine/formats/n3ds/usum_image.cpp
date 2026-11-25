#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "n3ds_image.h"

namespace n3ds {

namespace {
struct Huff {
  uint16_t count[16];
  uint16_t symbol[320];
};
struct InBits {
  const uint8_t* p;
  size_t n, pos = 0;
  uint32_t buf = 0;
  int cnt = 0;
  int bits(int need) {
    uint32_t v = buf;
    while (cnt < need) {
      if (pos >= n) throw std::runtime_error("inflate: data ends too early");
      v |= (uint32_t)p[pos++] << cnt;
      cnt += 8;
    }
    buf = v >> need;
    cnt -= need;
    return (int)(v & ((1u << need) - 1));
  }
};
int decode_sym(InBits& s, const Huff& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++) {
    code |= s.bits(1);
    int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  throw std::runtime_error("inflate: bad code");
}
void construct(Huff& h, const short* length, int n) {
  for (int l = 0; l <= 15; l++) h.count[l] = 0;
  for (int i = 0; i < n; i++) h.count[length[i]]++;
  uint16_t offs[16];
  offs[1] = 0;
  for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h.count[l]);
  for (int i = 0; i < n; i++) if (length[i] != 0) h.symbol[offs[length[i]]++] = (uint16_t)i;
}
const short kLBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLExt[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDExt[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void codes(InBits& s, std::vector<uint8_t>& out, const Huff& lc, const Huff& dc) {
  for (;;) {
    int sym = decode_sym(s, lc);
    if (sym < 256) out.push_back((uint8_t)sym);
    else if (sym == 256) return;
    else {
      sym -= 257;
      if (sym >= 29) throw std::runtime_error("inflate: bad length");
      int len = kLBase[sym] + s.bits(kLExt[sym]);
      int ds = decode_sym(s, dc);
      if (ds >= 30) throw std::runtime_error("inflate: bad distance");
      size_t dist = (size_t)kDBase[ds] + (size_t)s.bits(kDExt[ds]);
      if (dist > out.size()) throw std::runtime_error("inflate: distance too far");
      size_t from = out.size() - dist;
      for (int i = 0; i < len; i++) out.push_back(out[from + (size_t)i]);
    }
  }
}
}

std::vector<uint8_t> zlib_inflate(const uint8_t* d, size_t n) {
  if (n < 6) throw std::runtime_error("zlib: too short");
  InBits s{d + 2, n - 2};
  std::vector<uint8_t> out;
  int last;
  do {
    last = s.bits(1);
    int type = s.bits(2);
    if (type == 0) {
      s.buf = 0;
      s.cnt = 0;
      if (s.pos + 4 > s.n) throw std::runtime_error("inflate: stored block cut");
      unsigned len = s.p[s.pos] | (s.p[s.pos + 1] << 8);
      s.pos += 4;
      if (s.pos + len > s.n) throw std::runtime_error("inflate: stored block cut");
      out.insert(out.end(), s.p + s.pos, s.p + s.pos + len);
      s.pos += len;
    } else if (type == 1) {
      static Huff lc, dc;
      static bool made = false;
      if (!made) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        construct(lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        construct(dc, l, 30);
        made = true;
      }
      codes(s, out, lc, dc);
    } else if (type == 2) {
      static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
      short lengths[320];
      int i = 0;
      for (; i < ncode; i++) lengths[order[i]] = (short)s.bits(3);
      for (; i < 19; i++) lengths[order[i]] = 0;
      Huff lencode, lc, dc;
      construct(lencode, lengths, 19);
      i = 0;
      while (i < nlen + ndist) {
        int sym = decode_sym(s, lencode);
        if (sym < 16) lengths[i++] = (short)sym;
        else {
          int len = 0, rep;
          if (sym == 16) { if (i == 0) throw std::runtime_error("inflate: bad repeat"); len = lengths[i - 1]; rep = 3 + s.bits(2); }
          else if (sym == 17) rep = 3 + s.bits(3);
          else rep = 11 + s.bits(7);
          if (i + rep > nlen + ndist) throw std::runtime_error("inflate: too many lengths");
          while (rep--) lengths[i++] = (short)len;
        }
      }
      construct(lc, lengths, nlen);
      construct(dc, lengths + nlen, ndist);
      codes(s, out, lc, dc);
    } else throw std::runtime_error("inflate: bad block type");
  } while (!last);
  return out;
}

namespace {
struct BitW {
  std::vector<uint8_t>& o;
  uint32_t buf = 0;
  int cnt = 0;
  void put(uint32_t v, int n) {
    buf |= v << cnt;
    cnt += n;
    while (cnt >= 8) { o.push_back((uint8_t)buf); buf >>= 8; cnt -= 8; }
  }
  void huff(uint32_t code, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) r |= ((code >> i) & 1u) << (n - 1 - i);
    put(r, n);
  }
  void flush() { if (cnt) { o.push_back((uint8_t)buf); buf = 0; cnt = 0; } }
};
void lit(BitW& w, int v) {
  if (v < 144) w.huff(0x30 + (uint32_t)v, 8);
  else if (v < 256) w.huff(0x190 + (uint32_t)(v - 144), 9);
  else if (v < 280) w.huff((uint32_t)(v - 256), 7);
  else w.huff(0xC0 + (uint32_t)(v - 280), 8);
}
uint32_t adler32(const std::vector<uint8_t>& d) {
  uint32_t a = 1, b = 0;
  for (uint8_t c : d) { a = (a + c) % 65521; b = (b + a) % 65521; }
  return (b << 16) | a;
}
uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
  static uint32_t t[256];
  static bool made = false;
  if (!made) {
    for (uint32_t i = 0; i < 256; i++) { uint32_t x = i; for (int k = 0; k < 8; k++) x = (x & 1) ? 0xEDB88320u ^ (x >> 1) : x >> 1; t[i] = x; }
    made = true;
  }
  c = ~c;
  for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
  return ~c;
}
}

std::vector<uint8_t> zlib_deflate(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out = {0x78, 0x01};
  BitW w{out};
  w.put(1, 1);
  w.put(1, 2);
  const size_t n = raw.size();
  const int HB = 15;
  std::vector<int> head(1 << HB, -1), prev(n, -1);
  auto hash = [&](size_t i) { return (((uint32_t)raw[i] << 10) ^ ((uint32_t)raw[i + 1] << 5) ^ raw[i + 2]) & ((1u << HB) - 1); };
  size_t i = 0;
  while (i < n) {
    int best = 0;
    size_t bd = 0;
    if (i + 3 <= n) {
      uint32_t h = hash(i);
      int cand = head[h], tries = 24;
      while (cand >= 0 && tries-- > 0 && i - (size_t)cand <= 32768) {
        size_t l = 0, mx = std::min<size_t>(258, n - i);
        while (l < mx && raw[(size_t)cand + l] == raw[i + l]) l++;
        if ((int)l > best) { best = (int)l; bd = i - (size_t)cand; if (l == mx) break; }
        cand = prev[(size_t)cand];
      }
    }
    auto insert = [&](size_t k) { if (k + 3 <= n) { uint32_t h = hash(k); prev[k] = head[h]; head[h] = (int)k; } };
    if (best >= 3) {
      int ls = 28;
      while (kLBase[ls] > best) ls--;
      lit(w, 257 + ls);
      w.put((uint32_t)(best - kLBase[ls]), kLExt[ls]);
      int ds = 29;
      while (kDBase[ds] > (int)bd) ds--;
      w.huff((uint32_t)ds, 5);
      w.put((uint32_t)((int)bd - kDBase[ds]), kDExt[ds]);
      for (int k = 0; k < best; k++) insert(i + (size_t)k);
      i += (size_t)best;
    } else {
      lit(w, raw[i]);
      insert(i);
      i++;
    }
  }
  lit(w, 256);
  w.flush();
  uint32_t a = adler32(raw);
  for (int k = 3; k >= 0; k--) out.push_back((uint8_t)(a >> (8 * k)));
  return out;
}

Image8 png_decode(const std::vector<uint8_t>& f) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  if (f.size() < 33 || memcmp(f.data(), sig, 8) != 0) throw std::runtime_error("not a PNG");
  auto be = [&](size_t o) { return (uint32_t)f[o] << 24 | (uint32_t)f[o + 1] << 16 | (uint32_t)f[o + 2] << 8 | f[o + 3]; };
  size_t pos = 8;
  int w = 0, h = 0, depth = 0, ctype = 0, inter = 0;
  std::vector<uint8_t> idat, plte, trns;
  while (pos + 12 <= f.size()) {
    uint32_t len = be(pos);
    std::string type((const char*)&f[pos + 4], 4);
    if (pos + 12 + len > f.size()) throw std::runtime_error("PNG chunk cut");
    const uint8_t* d = &f[pos + 8];
    if (type == "IHDR") { w = (int)be(pos + 8); h = (int)be(pos + 12); depth = d[8]; ctype = d[9]; inter = d[12]; }
    else if (type == "IDAT") idat.insert(idat.end(), d, d + len);
    else if (type == "PLTE") plte.assign(d, d + len);
    else if (type == "tRNS") trns.assign(d, d + len);
    else if (type == "IEND") break;
    pos += 12 + len;
  }
  if (w <= 0 || h <= 0 || depth != 8 || inter != 0) throw std::runtime_error("PNG kind not supported (8 bit, not interlaced only)");
  int bpp = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
  if (!bpp) throw std::runtime_error("PNG colour type not supported");
  std::vector<uint8_t> raw = zlib_inflate(idat.data(), idat.size());
  size_t stride = (size_t)w * bpp;
  if (raw.size() < (stride + 1) * (size_t)h) throw std::runtime_error("PNG data short");
  std::vector<uint8_t> cur(stride), up(stride, 0);
  Image8 im;
  im.w = w; im.h = h; im.ch = 4;
  im.px.resize((size_t)w * h * 4);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = &raw[(size_t)y * (stride + 1)];
    int ft = row[0];
    for (size_t x = 0; x < stride; x++) {
      int a = x >= (size_t)bpp ? cur[x - bpp] : 0, b = up[x], c = x >= (size_t)bpp ? up[x - bpp] : 0, v = row[1 + x];
      switch (ft) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
        default: throw std::runtime_error("PNG filter");
      }
      cur[x] = (uint8_t)v;
    }
    uint8_t* o = &im.px[(size_t)y * w * 4];
    for (int x = 0; x < w; x++) {
      const uint8_t* s = &cur[(size_t)x * bpp];
      uint8_t r, g, b, a = 255;
      if (ctype == 0) r = g = b = s[0];
      else if (ctype == 2) { r = s[0]; g = s[1]; b = s[2]; }
      else if (ctype == 3) { size_t k = s[0]; r = k * 3 + 2 < plte.size() ? plte[k * 3] : 0; g = k * 3 + 2 < plte.size() ? plte[k * 3 + 1] : 0; b = k * 3 + 2 < plte.size() ? plte[k * 3 + 2] : 0; a = k < trns.size() ? trns[k] : 255; }
      else if (ctype == 4) { r = g = b = s[0]; a = s[1]; }
      else { r = s[0]; g = s[1]; b = s[2]; a = s[3]; }
      o[x * 4] = r; o[x * 4 + 1] = g; o[x * 4 + 2] = b; o[x * 4 + 3] = a;
    }
    up = cur;
  }
  return im;
}

std::vector<uint8_t> png_encode(const Image8& im) {
  const size_t stride = (size_t)im.w * im.ch;
  std::vector<uint8_t> raw;
  raw.reserve((stride + 1) * (size_t)im.h);
  for (int y = 0; y < im.h; y++) {
    raw.push_back(1);
    const uint8_t* r = &im.px[(size_t)y * stride];
    for (size_t x = 0; x < stride; x++) raw.push_back((uint8_t)(r[x] - (x >= (size_t)im.ch ? r[x - im.ch] : 0)));
  }
  std::vector<uint8_t> z = zlib_deflate(raw), o = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    for (int k = 3; k >= 0; k--) o.push_back((uint8_t)((uint32_t)d.size() >> (8 * k)));
    size_t at = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), d.begin(), d.end());
    uint32_t c = crc32(&o[at], o.size() - at);
    for (int k = 3; k >= 0; k--) o.push_back((uint8_t)(c >> (8 * k)));
  };
  std::vector<uint8_t> hd(13);
  for (int k = 0; k < 4; k++) { hd[(size_t)k] = (uint8_t)((uint32_t)im.w >> (24 - 8 * k)); hd[4 + (size_t)k] = (uint8_t)((uint32_t)im.h >> (24 - 8 * k)); }
  hd[8] = 8; hd[9] = im.ch == 4 ? 6 : 2;
  chunk("IHDR", hd);
  chunk("IDAT", z);
  chunk("IEND", {});
  return o;
}

namespace {
constexpr int PREC = 22;
double sinc_filter(double x) {
  if (x == 0.0) return 1.0;
  x = x * 3.14159265358979323846;
  return std::sin(x) / x;
}
double lanczos_filter(double x) {
  if (-3.0 <= x && x < 3.0) return sinc_filter(x) * sinc_filter(x / 3);
  return 0.0;
}
struct Coeffs {
  int ksize = 0;
  std::vector<int> bounds;
  std::vector<int32_t> kk;
};
Coeffs precompute(int in_size, int out_size) {
  const double support0 = 3.0;
  double scale = (double)in_size / out_size, filterscale = scale < 1.0 ? 1.0 : scale;
  double support = support0 * filterscale;
  Coeffs c;
  c.ksize = (int)std::ceil(support) * 2 + 1;
  c.bounds.resize((size_t)out_size * 2);
  std::vector<double> pre((size_t)out_size * c.ksize, 0.0);
  for (int xx = 0; xx < out_size; xx++) {
    double center = (xx + 0.5) * scale, ww = 0.0, ss = 1.0 / filterscale;
    int xmin = (int)(center - support + 0.5);
    if (xmin < 0) xmin = 0;
    int xmax = (int)(center + support + 0.5);
    if (xmax > in_size) xmax = in_size;
    xmax -= xmin;
    double* k = &pre[(size_t)xx * c.ksize];
    for (int x = 0; x < xmax; x++) {
      double wv = lanczos_filter((x + xmin - center + 0.5) * ss);
      k[x] = wv;
      ww += wv;
    }
    for (int x = 0; x < xmax; x++) if (ww != 0.0) k[x] /= ww;
    c.bounds[(size_t)xx * 2] = xmin;
    c.bounds[(size_t)xx * 2 + 1] = xmax;
  }
  c.kk.resize(pre.size());
  for (size_t i = 0; i < pre.size(); i++) c.kk[i] = pre[i] < 0 ? (int32_t)(-0.5 + pre[i] * (1 << PREC)) : (int32_t)(0.5 + pre[i] * (1 << PREC));
  return c;
}
uint8_t clip8(int32_t v) {
  int32_t s = v >> PREC;
  return (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);
}
Image8 resample(const Image8& in, int nw, int nh) {
  const int ch = in.ch;
  Coeffs ch_x = precompute(in.w, nw), ch_y = precompute(in.h, nh);
  Image8 tmp;
  tmp.w = nw; tmp.h = in.h; tmp.ch = ch;
  tmp.px.assign((size_t)nw * in.h * ch, 0);
  for (int y = 0; y < in.h; y++)
    for (int x = 0; x < nw; x++) {
      int xmin = ch_x.bounds[(size_t)x * 2], xmax = ch_x.bounds[(size_t)x * 2 + 1];
      const int32_t* k = &ch_x.kk[(size_t)x * ch_x.ksize];
      for (int c = 0; c < ch; c++) {
        int32_t ss = 1 << (PREC - 1);
        for (int i = 0; i < xmax; i++) ss += (int32_t)in.px[((size_t)y * in.w + (size_t)(i + xmin)) * ch + c] * k[i];
        tmp.px[((size_t)y * nw + x) * ch + c] = clip8(ss);
      }
    }
  Image8 out;
  out.w = nw; out.h = nh; out.ch = ch;
  out.px.assign((size_t)nw * nh * ch, 0);
  for (int y = 0; y < nh; y++) {
    int ymin = ch_y.bounds[(size_t)y * 2], ymax = ch_y.bounds[(size_t)y * 2 + 1];
    const int32_t* k = &ch_y.kk[(size_t)y * ch_y.ksize];
    for (int x = 0; x < nw; x++)
      for (int c = 0; c < ch; c++) {
        int32_t ss = 1 << (PREC - 1);
        for (int i = 0; i < ymax; i++) ss += (int32_t)tmp.px[((size_t)(i + ymin) * nw + x) * ch + c] * k[i];
        out.px[((size_t)y * nw + x) * ch + c] = clip8(ss);
      }
  }
  return out;
}
}

Image8 pil_resize_lanczos(const Image8& in, int nw, int nh) {
  if (in.ch == 3) return resample(in, nw, nh);
  Image8 p = in;
  for (size_t i = 0; i < p.px.size(); i += 4) {
    unsigned a = p.px[i + 3];
    for (int c = 0; c < 3; c++) {
      unsigned tmp = (unsigned)p.px[i + (size_t)c] * a + 128;
      p.px[i + (size_t)c] = (uint8_t)(((tmp >> 8) + tmp) >> 8);
    }
  }
  Image8 r = resample(p, nw, nh);
  for (size_t i = 0; i < r.px.size(); i += 4) {
    unsigned a = r.px[i + 3];
    if (a == 255 || a == 0) continue;
    for (int c = 0; c < 3; c++) {
      unsigned v = (255u * r.px[i + (size_t)c]) / a;
      r.px[i + (size_t)c] = (uint8_t)(v > 255 ? 255 : v);
    }
  }
  return r;
}

Image8 round_face(const Image8& im, int size) {
  Image8 big;
  big.w = im.w; big.h = im.h; big.ch = 3;
  big.px.resize((size_t)im.w * im.h * 3);
  for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
    const float a = (float)im.px[i * 4 + 3] / 255.0f;
    for (int c = 0; c < 3; c++) {
      float v = ((float)im.px[i * 4 + (size_t)c] / 255.0f) * a;
      float t = v * 255.0f;
      t = t + 0.5f;
      if (t < 0.0f) t = 0.0f;
      if (t > 255.0f) t = 255.0f;
      big.px[i * 3 + (size_t)c] = (uint8_t)t;
    }
  }
  Image8 small = pil_resize_lanczos(big, size, size);
  Image8 out;
  out.w = size; out.h = size; out.ch = 4;
  out.px.resize((size_t)size * size * 4);
  const int ss = 8;
  const double half = size / 2.0, r2 = half * half;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int inside = 0;
      for (int sy = 0; sy < ss; sy++)
        for (int sx = 0; sx < ss; sx++) {
          double dx = (x * ss + sx + 0.5) / ss - half, dy = (y * ss + sy + 0.5) / ss - half;
          if (dx * dx + dy * dy <= r2) inside++;
        }
      double m = (double)inside / 64.0 * 255.0 + 0.5;
      if (m > 255.0) m = 255.0;
      uint8_t* o = &out.px[((size_t)y * size + x) * 4];
      o[0] = small.px[((size_t)y * size + x) * 3]; o[1] = small.px[((size_t)y * size + x) * 3 + 1]; o[2] = small.px[((size_t)y * size + x) * 3 + 2];
      o[3] = (uint8_t)m;
    }
  return out;
}

bool face_ok(const Image8& im, double lo, double hi) {
  if (im.w <= 0 || im.h <= 0) return false;
  size_t inside = 0, on = 0;
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      double dx = (x + 0.5) / im.w - 0.5, dy = (y + 0.5) / im.h - 0.5;
      if (std::hypot(dx, dy) <= 0.5) {
        inside++;
        if (im.px[((size_t)y * im.w + x) * 4 + 3] > 128) on++;
      }
    }
  if (!inside) return false;
  double cov = (double)on / (double)inside;
  return lo <= cov && cov <= hi;
}

bool box_icon_face(const Image8& icon, Image8* out) {
  int x0 = icon.w, y0 = icon.h, x1 = 0, y1 = 0;
  for (int y = 0; y < icon.h; y++)
    for (int x = 0; x < icon.w; x++)
      if (icon.px[((size_t)y * icon.w + x) * 4 + 3]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1); }
  if (x1 <= x0 || y1 <= y0) return false;
  const int cw = x1 - x0, chh = y1 - y0, side = std::max(cw, chh) + 2;
  Image8 sq;
  sq.w = side; sq.h = side; sq.ch = 4;
  sq.px.assign((size_t)side * side * 4, 0);
  const int ox = (side - cw) / 2, oy = (side - chh) / 2;
  for (int y = 0; y < chh; y++)
    for (int x = 0; x < cw; x++) memcpy(&sq.px[((size_t)(y + oy) * side + (x + ox)) * 4], &icon.px[((size_t)(y + y0) * icon.w + (x + x0)) * 4], 4);
  *out = pil_resize_lanczos(sq, 84, 84);
  return true;
}

bool box_icon_pair(const Image8& normal, const Image8* shiny, Image8* out) {
  const Image8* src[2] = {&normal, shiny ? shiny : &normal};
  int x0 = normal.w, y0 = normal.h, x1 = 0, y1 = 0;
  for (const Image8* im : src) {
    if (im->w != normal.w || im->h != normal.h) return false;
    for (int y = 0; y < im->h; y++)
      for (int x = 0; x < im->w; x++)
        if (im->px[((size_t)y * im->w + x) * 4 + 3] >= 24) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1); }
  }
  if (x1 <= x0 || y1 <= y0) return false;
  const int cw = x1 - x0, chh = y1 - y0, longer = std::max(cw, chh);
  const int tw = std::max(1, (cw * 52 + longer / 2) / longer), th = std::max(1, (chh * 52 + longer / 2) / longer);
  out->w = 54; out->h = 108; out->ch = 4;
  out->px.assign((size_t)54 * 108 * 4, 0);
  for (int k = 0; k < 2; k++) {
    Image8 crop;
    crop.w = cw; crop.h = chh; crop.ch = 4;
    crop.px.resize((size_t)cw * chh * 4);
    for (int y = 0; y < chh; y++) memcpy(&crop.px[(size_t)y * cw * 4], &src[k]->px[((size_t)(y + y0) * src[k]->w + x0) * 4], (size_t)cw * 4);
    const Image8 small = pil_resize_lanczos(crop, tw, th);
    std::vector<uint8_t> pl((size_t)54 * 54 * 4, 0);
    const int ox = (54 - tw) / 2, oy = 54 - 1 - th;
    for (int y = 0; y < th; y++)
      for (int x = 0; x < tw; x++) memcpy(&pl[((size_t)(y + oy) * 54 + x + ox) * 4], &small.px[((size_t)y * tw + x) * 4], 4);
    for (int y = 0; y < 54; y++)
      for (int x = 0; x < 54; x++) {
        const uint8_t* p = &pl[((size_t)y * 54 + x) * 4];
        int near_a = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= 54 || yy >= 54) continue;
            near_a = std::max<int>(near_a, pl[((size_t)yy * 54 + xx) * 4 + 3]);
          }
        const int a = p[3], oa = near_a;
        const int ra = a + oa * (255 - a) / 255;
        uint8_t* o = &out->px[((size_t)(k * 54 + y) * 54 + x) * 4];
        if (ra <= 0) continue;
        const int dark[3] = {16, 16, 24};
        for (int c = 0; c < 3; c++) o[c] = (uint8_t)std::min(255, (p[c] * a * 255 + dark[c] * oa * (255 - a)) / (ra * 255));
        o[3] = (uint8_t)std::min(255, ra);
      }
  }
  return true;
}

}
