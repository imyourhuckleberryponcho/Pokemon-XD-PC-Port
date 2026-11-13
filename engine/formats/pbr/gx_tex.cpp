#include "pbr_gx_tex.h"

#include <algorithm>
#include <cmath>

namespace pbr {

bool gx_tile(int fmt, int* bw, int* bh) {
  switch (fmt) {
    case 0: *bw = 8; *bh = 8; return true;
    case 1: *bw = 8; *bh = 4; return true;
    case 2: *bw = 8; *bh = 4; return true;
    case 3: case 4: case 5: case 6: *bw = 4; *bh = 4; return true;
    case 14: *bw = 8; *bh = 8; return true;
  }
  return false;
}

static void rgb565(int v, int out[3]) {
  int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  out[0] = (r << 3) | (r >> 2);
  out[1] = (g << 2) | (g >> 4);
  out[2] = (b << 3) | (b >> 2);
}

Rgba gx_decode(int fmt, int w, int h, const uint8_t* d, size_t size) {
  int bw, bh;
  if (!gx_tile(fmt, &bw, &bh)) throw std::runtime_error("texture format " + std::to_string(fmt) + ": no decoder");
  int tx = (w + bw - 1) / bw, ty = (h + bh - 1) / bh;
  size_t nt = (size_t)tx * ty;
  size_t tb = fmt == 6 ? 64 : 32;
  if (size < nt * tb) throw std::runtime_error("texture data too short");
  Rgba out(w, h);
  for (size_t t = 0; t < nt; t++) {
    int x0 = (int)(t % tx) * bw, y0 = (int)(t / tx) * bh;
    const uint8_t* td = d + t * tb;
    for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++) {
        int X = x0 + x, Y = y0 + y;
        if (X >= w || Y >= h) continue;
        uint8_t* o = &out.px[((size_t)Y * w + X) * 4];
        switch (fmt) {
          case 0: {
            int b = td[y * 4 + x / 2];
            int v = ((x & 1) ? (b & 15) : (b >> 4)) * 17;
            o[0] = o[1] = o[2] = o[3] = (uint8_t)v;
            break;
          }
          case 1: o[0] = o[1] = o[2] = o[3] = td[y * 8 + x]; break;
          case 2: {
            int v = td[y * 8 + x];
            o[3] = (uint8_t)((v >> 4) * 17);
            o[0] = o[1] = o[2] = (uint8_t)((v & 15) * 17);
            break;
          }
          case 3: {
            const uint8_t* p = td + (y * 4 + x) * 2;
            o[3] = p[0];
            o[0] = o[1] = o[2] = p[1];
            break;
          }
          case 4: {
            const uint8_t* p = td + (y * 4 + x) * 2;
            int c[3];
            rgb565(p[0] << 8 | p[1], c);
            o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2]; o[3] = 255;
            break;
          }
          case 5: {
            const uint8_t* p = td + (y * 4 + x) * 2;
            int v = p[0] << 8 | p[1];
            if (v & 0x8000) {
              int r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31;
              o[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
              o[1] = (uint8_t)((g5 << 3) | (g5 >> 2));
              o[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
              o[3] = 255;
            } else {
              int a3 = (v >> 12) & 7, r4 = (v >> 8) & 15, g4 = (v >> 4) & 15, b4 = v & 15;
              o[0] = (uint8_t)(r4 * 17); o[1] = (uint8_t)(g4 * 17); o[2] = (uint8_t)(b4 * 17);
              o[3] = (uint8_t)((a3 << 5) | (a3 << 2) | (a3 >> 1));
            }
            break;
          }
          case 6: {
            int i = y * 4 + x;
            o[3] = td[i * 2];
            o[0] = td[i * 2 + 1];
            o[1] = td[32 + i * 2];
            o[2] = td[32 + i * 2 + 1];
            break;
          }
          case 14: {
            int sub = (y >> 2) * 2 + (x >> 2), sy = y & 3, sx = x & 3;
            const uint8_t* sb = td + sub * 8;
            int c0 = sb[0] << 8 | sb[1], c1 = sb[2] << 8 | sb[3];
            int p0[3], p1[3];
            rgb565(c0, p0);
            rgb565(c1, p1);
            int idx = (sb[4 + sy] >> (6 - 2 * sx)) & 3;
            bool four = c0 > c1;
            int col[3] = {0, 0, 0}, al = 255;
            for (int k = 0; k < 3; k++) {
              if (idx == 0) col[k] = p0[k];
              else if (idx == 1) col[k] = p1[k];
              else if (idx == 2) col[k] = four ? (2 * p0[k] + p1[k]) / 3 : (p0[k] + p1[k]) / 2;
              else col[k] = four ? (p0[k] + 2 * p1[k]) / 3 : 0;
            }
            if (idx == 3 && !four) al = 0;
            o[0] = (uint8_t)col[0]; o[1] = (uint8_t)col[1]; o[2] = (uint8_t)col[2]; o[3] = (uint8_t)al;
            break;
          }
        }
      }
  }
  return out;
}

static inline const uint8_t* tile_px(const Rgba& img, int tx, int bw, int bh, size_t t, int y, int x) {
  int X = std::min((int)(t % tx) * bw + x, img.w - 1), Y = std::min((int)(t / tx) * bh + y, img.h - 1);
  return &img.px[((size_t)Y * img.w + X) * 4];
}

static int to565(const double* rgb) {
  int r = (int)rgb[0], g = (int)rgb[1], b = (int)rgb[2];
  return (((r * 31 + 127) / 255) << 11) | (((g * 63 + 127) / 255) << 5) | ((b * 31 + 127) / 255);
}

std::vector<uint8_t> gx_encode_cmpr(const Rgba& img) {
  int tx = (img.w + 7) / 8, ty = (img.h + 7) / 8;
  size_t nt = (size_t)tx * ty;
  std::vector<uint8_t> out(nt * 32);
  for (size_t t = 0; t < nt; t++)
    for (int s = 0; s < 4; s++) {
      int oy = (s >> 1) * 4, ox = (s & 1) * 4;
      double rgb[16][3];
      bool clear[16];
      int ncl = 0;
      for (int p = 0; p < 16; p++) {
        const uint8_t* q = tile_px(img, tx, 8, 8, t, oy + p / 4, ox + p % 4);
        rgb[p][0] = q[0]; rgb[p][1] = q[1]; rgb[p][2] = q[2];
        clear[p] = q[3] < 128;
        ncl += clear[p];
      }
      int solids = 16 - ncl;
      double cnt = (double)std::max(solids, 1);
      double mean[3];
      for (int i = 0; i < 3; i++) {
        double sum = 0;
        for (int p = 0; p < 16; p++) sum += rgb[p][i] * (clear[p] ? 0.0 : 1.0);
        mean[i] = sum / cnt;
      }
      double dev[16][3];
      for (int p = 0; p < 16; p++)
        for (int i = 0; i < 3; i++) dev[p][i] = (rgb[p][i] - mean[i]) * (clear[p] ? 0.0 : 1.0);
      double cov[3][3];
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
          double sum = 0;
          for (int p = 0; p < 16; p++) sum += dev[p][i] * dev[p][j];
          cov[i][j] = sum;
        }
      double axis[3] = {1, 1, 1};
      for (int it = 0; it < 6; it++) {
        double n[3];
        for (int i = 0; i < 3; i++) n[i] = (cov[i][0] * axis[0] + cov[i][2] * axis[2]) + cov[i][1] * axis[1];
        double nrm = std::sqrt((n[0] * n[0] + n[1] * n[1]) + n[2] * n[2]);
        double dv = std::max(nrm, 1e-9);
        for (int i = 0; i < 3; i++) axis[i] = n[i] / dv;
      }
      int lo = 0, hi = 0;
      if (solids > 0) {
        double best_lo = 0, best_hi = 0;
        bool first = true;
        for (int p = 0; p < 16; p++) {
          if (clear[p]) continue;
          double pr = ((rgb[p][0] - mean[0]) * axis[0] + (rgb[p][2] - mean[2]) * axis[2]) + (rgb[p][1] - mean[1]) * axis[1];
          if (first || pr < best_lo) best_lo = pr, lo = p;
          if (first || pr > best_hi) best_hi = pr, hi = p;
          first = false;
        }
      }
      int c0 = to565(rgb[hi]), c1 = to565(rgb[lo]);
      bool has_clear = ncl > 0;
      bool swap = has_clear ? (c0 > c1) : (c0 < c1);
      if (swap) std::swap(c0, c1);
      bool same = (c0 == c1) && !has_clear;
      if (same && c1 < 0xFFFF) c0 = c0 + 1;
      if (same && c1 == 0xFFFF) c1 = c1 - 1;
      int i0[3], i1[3];
      rgb565(c0, i0);
      rgb565(c1, i1);
      bool four = c0 > c1;
      double pal[4][3];
      for (int k = 0; k < 3; k++) {
        double a = i0[k], b = i1[k];
        pal[0][k] = a;
        pal[1][k] = b;
        pal[2][k] = four ? (2 * a + b) / 3 : (a + b) / 2;
        pal[3][k] = four ? (a + 2 * b) / 3 : 1e9;
      }
      uint8_t* o = &out[(t * 4 + s) * 8];
      o[0] = (uint8_t)(c0 >> 8); o[1] = (uint8_t)(c0 & 255); o[2] = (uint8_t)(c1 >> 8); o[3] = (uint8_t)(c1 & 255);
      for (int row = 0; row < 4; row++) {
        int bits = 0;
        for (int col = 0; col < 4; col++) {
          int p = row * 4 + col, idx = 3;
          if (!clear[p]) {
            double bd = 0;
            for (int c = 0; c < 4; c++) {
              double dx = rgb[p][0] - pal[c][0], dy = rgb[p][1] - pal[c][1], dz = rgb[p][2] - pal[c][2];
              double dd = (dx * dx + dy * dy) + dz * dz;
              if (c == 0 || dd < bd) bd = dd, idx = c;
            }
          }
          bits = (bits << 2) | idx;
        }
        o[4 + row] = (uint8_t)bits;
      }
    }
  return out;
}

std::vector<uint8_t> gx_encode_rgb5a3(const Rgba& img) {
  int tx = (img.w + 3) / 4, ty = (img.h + 3) / 4;
  size_t nt = (size_t)tx * ty;
  std::vector<uint8_t> out(nt * 32);
  uint8_t* o = out.data();
  for (size_t t = 0; t < nt; t++)
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        const uint8_t* q = tile_px(img, tx, 4, 4, t, y, x);
        int r = q[0], g = q[1], b = q[2], a = q[3];
        int v = a >= 0xE0 ? (0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
                          : (((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
        *o++ = (uint8_t)(v >> 8);
        *o++ = (uint8_t)(v & 255);
      }
  return out;
}

std::vector<uint8_t> gx_encode_ia8(const Rgba& img) {
  int tx = (img.w + 3) / 4, ty = (img.h + 3) / 4;
  size_t nt = (size_t)tx * ty;
  std::vector<uint8_t> out(nt * 32);
  uint8_t* o = out.data();
  for (size_t t = 0; t < nt; t++)
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        const uint8_t* q = tile_px(img, tx, 4, 4, t, y, x);
        double mean = (double)(q[0] + q[1] + q[2]) / 3.0;
        *o++ = q[3];
        *o++ = (uint8_t)std::nearbyint(mean);
      }
  return out;
}

struct Coeffs {
  std::vector<int> xmin, xn;
  std::vector<double> k;
  int ksize = 0;
};

static Coeffs precompute(int in_size, int out_size) {
  double scale = (double)in_size / out_size;
  double filterscale = scale < 1.0 ? 1.0 : scale;
  double support = 0.5 * filterscale;
  Coeffs c;
  c.ksize = (int)std::ceil(support) * 2 + 1;
  c.xmin.resize(out_size);
  c.xn.resize(out_size);
  c.k.assign((size_t)out_size * c.ksize, 0.0);
  double ss = 1.0 / filterscale;
  for (int xx = 0; xx < out_size; xx++) {
    double center = (xx + 0.5) * scale;
    int xmin = (int)(center - support + 0.5);
    if (xmin < 0) xmin = 0;
    int xmax = (int)(center + support + 0.5);
    if (xmax > in_size) xmax = in_size;
    xmax -= xmin;
    double* k = &c.k[(size_t)xx * c.ksize];
    double ww = 0.0;
    for (int x = 0; x < xmax; x++) {
      double v = (x + xmin - center + 0.5) * ss;
      double w = (v > -0.5 && v <= 0.5) ? 1.0 : 0.0;
      k[x] = w;
      ww += w;
    }
    if (ww != 0.0)
      for (int x = 0; x < xmax; x++) k[x] /= ww;
    c.xmin[xx] = xmin;
    c.xn[xx] = xmax;
  }
  return c;
}

static std::vector<float> resize_plane(const std::vector<float>& in, int iw, int ih, int ow, int oh) {
  std::vector<float> cur = in;
  int cw = iw;
  if (ow != iw) {
    Coeffs c = precompute(iw, ow);
    std::vector<float> t((size_t)ow * ih);
    for (int y = 0; y < ih; y++)
      for (int xx = 0; xx < ow; xx++) {
        double ss = 0.0;
        const double* k = &c.k[(size_t)xx * c.ksize];
        for (int x = 0; x < c.xn[xx]; x++) ss += (double)cur[(size_t)y * cw + x + c.xmin[xx]] * k[x];
        t[(size_t)y * ow + xx] = (float)ss;
      }
    cur.swap(t);
    cw = ow;
  }
  if (oh != ih) {
    Coeffs c = precompute(ih, oh);
    std::vector<float> t((size_t)cw * oh);
    for (int yy = 0; yy < oh; yy++)
      for (int x = 0; x < cw; x++) {
        double ss = 0.0;
        const double* k = &c.k[(size_t)yy * c.ksize];
        for (int y = 0; y < c.xn[yy]; y++) ss += (double)cur[(size_t)(y + c.xmin[yy]) * cw + x] * k[y];
        t[(size_t)yy * cw + x] = (float)ss;
      }
    cur.swap(t);
  }
  return cur;
}

Rgba gx_resize(const Rgba& img, int w, int h) {
  size_t n = (size_t)img.w * img.h;
  std::vector<float> pl[4];
  for (auto& p : pl) p.resize(n);
  for (size_t i = 0; i < n; i++) {
    float a[4];
    for (int c = 0; c < 4; c++) a[c] = (float)img.px[i * 4 + c] / 255.0f;
    pl[0][i] = a[0] * a[3];
    pl[1][i] = a[1] * a[3];
    pl[2][i] = a[2] * a[3];
    pl[3][i] = a[3];
  }
  std::vector<float> rs[4];
  for (int c = 0; c < 4; c++) rs[c] = resize_plane(pl[c], img.w, img.h, w, h);
  Rgba out(w, h);
  const float eps = 1e-6f;
  for (size_t i = 0; i < (size_t)w * h; i++) {
    float al = rs[3][i];
    float v[4];
    for (int c = 0; c < 3; c++) v[c] = al > eps ? rs[c][i] / std::max(al, eps) : 0.0f;
    v[3] = al;
    for (int c = 0; c < 4; c++) {
      float x = std::min(std::max(v[c], 0.0f), 1.0f);
      out.px[i * 4 + c] = (uint8_t)(x * 255.0f + 0.5f);
    }
  }
  return out;
}

Rgba gx_decode_c14x2(int w, int h, const uint8_t* data, size_t size, int pal_format) {
  int tx = (w + 3) / 4, ty = (h + 3) / 4;
  size_t n = (size_t)tx * ty * 16;
  if (size < n * 2) throw std::runtime_error("cannot reshape array of size into shape");
  size_t npal = (size - n * 2) / 2;
  if (npal == 0) throw std::runtime_error("palette picture without a palette");
  Rgba out(w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t t = (size_t)(y / 4) * tx + (x / 4);
      size_t k = t * 16 + (y % 4) * 4 + (x % 4);
      int idx = ((data[k * 2] << 8) | data[k * 2 + 1]) & 0x3FFF;
      size_t pi = (size_t)idx < npal - 1 ? (size_t)idx : npal - 1;
      int v = (data[n * 2 + pi * 2] << 8) | data[n * 2 + pi * 2 + 1];
      uint8_t* o = &out.px[((size_t)y * w + x) * 4];
      if (pal_format == 1) {
        int c[3];
        rgb565(v, c);
        o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255;
      } else if (pal_format == 2) {
        bool opaque = (v & 0x8000) != 0;
        int r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31;
        int a3 = (v >> 12) & 7, r4 = (v >> 8) & 15, g4 = (v >> 4) & 15, b4 = v & 15;
        o[0] = opaque ? ((r5 << 3) | (r5 >> 2)) : r4 * 17;
        o[1] = opaque ? ((g5 << 3) | (g5 >> 2)) : g4 * 17;
        o[2] = opaque ? ((b5 << 3) | (b5 >> 2)) : b4 * 17;
        o[3] = opaque ? 255 : ((a3 << 5) | (a3 << 2) | (a3 >> 1));
      } else {
        o[0] = o[1] = o[2] = v & 0xFF;
        o[3] = v >> 8;
      }
    }
  return out;
}

}
