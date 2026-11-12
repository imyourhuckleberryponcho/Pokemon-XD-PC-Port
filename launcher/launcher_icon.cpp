#include "launcher_icon.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace launcher::icon {

namespace {

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

void PutRGB5A3(uint16_t v, uint8_t* out) {
  if (v & 0x8000) {
    const int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    out[0] = static_cast<uint8_t>(r << 3 | r >> 2);
    out[1] = static_cast<uint8_t>(g << 3 | g >> 2);
    out[2] = static_cast<uint8_t>(b << 3 | b >> 2);
    out[3] = 255;
  } else {
    const int a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    out[0] = static_cast<uint8_t>(r * 17);
    out[1] = static_cast<uint8_t>(g * 17);
    out[2] = static_cast<uint8_t>(b * 17);
    out[3] = static_cast<uint8_t>(a << 5 | a << 2 | a >> 1);
  }
}

Image Blank(int w, int h) {
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  return img;
}

void Le16(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void Le32(std::vector<uint8_t>& b, uint32_t v) {
  Le16(b, v & 0xFFFF);
  Le16(b, v >> 16);
}

}

Image DecodeRGB5A3(const uint8_t* data, int width, int height) {
  Image img = Blank(width, height);
  size_t at = 0;
  for (int ty = 0; ty < height; ty += 4) {
    for (int tx = 0; tx < width; tx += 4) {
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++, at += 2) {
          if (tx + x < width && ty + y < height) {
            PutRGB5A3(Be16(data + at), &img.rgba[(static_cast<size_t>(ty + y) * width + tx + x) * 4]);
          }
        }
      }
    }
  }
  return img;
}

Image DecodeCI8(const uint8_t* data, const uint8_t* palette, int width, int height) {
  Image img = Blank(width, height);
  size_t at = 0;
  for (int ty = 0; ty < height; ty += 4) {
    for (int tx = 0; tx < width; tx += 8) {
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 8; x++, at++) {
          if (tx + x < width && ty + y < height) {
            PutRGB5A3(Be16(palette + data[at] * 2), &img.rgba[(static_cast<size_t>(ty + y) * width + tx + x) * 4]);
          }
        }
      }
    }
  }
  return img;
}

namespace {

uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

void PutRGB565(uint16_t v, uint8_t* out) {
  const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  out[0] = static_cast<uint8_t>(r << 3 | r >> 2);
  out[1] = static_cast<uint8_t>(g << 2 | g >> 4);
  out[2] = static_cast<uint8_t>(b << 3 | b >> 2);
  out[3] = 255;
}

void PutPalette(uint16_t v, uint32_t paletteFormat, uint8_t* out) {
  switch (paletteFormat) {
  case 1:
    out[0] = out[1] = out[2] = static_cast<uint8_t>(v & 0xFF);
    out[3] = static_cast<uint8_t>(v >> 8);
    break;
  case 2: PutRGB565(v, out); break;
  default: PutRGB5A3(v, out); break;
  }
}

}

Image DecodeGsTexture(const uint8_t* file, size_t size) {
  Image img;
  if (file == nullptr || size < 0x80) {
    return img;
  }
  const int w = Be16(file), h = Be16(file + 2);
  const uint32_t format = Be32(file + 8), paletteFormat = Be32(file + 0xC);
  const uint32_t data = Be32(file + 0x28), palette = Be32(file + 0x48);
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || data >= size) {
    return img;
  }
  if (format == 0xB0) {
    const size_t need = static_cast<size_t>((w + 7) / 8) * ((h + 7) / 8) * 32;
    if (data + need > size) {
      return img;
    }
    img = Blank(w, h);
    const uint8_t* p = file + data;
    for (int ty = 0; ty < h; ty += 8) {
      for (int tx = 0; tx < w; tx += 8) {
        for (int sub = 0; sub < 4; sub++, p += 8) {
          uint8_t c[4][4];
          const uint16_t c0 = Be16(p), c1 = Be16(p + 2);
          PutRGB565(c0, c[0]);
          PutRGB565(c1, c[1]);
          for (int k = 0; k < 3; k++) {
            c[2][k] = static_cast<uint8_t>(c0 > c1 ? (2 * c[0][k] + c[1][k]) / 3 : (c[0][k] + c[1][k]) / 2);
            c[3][k] = static_cast<uint8_t>(c0 > c1 ? (c[0][k] + 2 * c[1][k]) / 3 : 0);
          }
          c[2][3] = 255;
          c[3][3] = c0 > c1 ? 255 : 0;
          const uint32_t bitsAll = Be32(p + 4);
          for (int k = 0; k < 16; k++) {
            const int x = tx + (sub % 2) * 4 + k % 4, y = ty + (sub / 2) * 4 + k / 4;
            if (x < w && y < h) {
              std::copy_n(c[(bitsAll >> (30 - 2 * k)) & 3], 4, &img.rgba[(static_cast<size_t>(y) * w + x) * 4]);
            }
          }
        }
      }
    }
    return img;
  }
  int bw = 0, bh = 0, bits = 0;
  switch (format) {
  case 0x00: bw = 8, bh = 8, bits = 4; break;
  case 0x01: case 0x42: case 0xA0: case 0xA1: case 0xA2: case 0xA3:
    bw = 8, bh = 4, bits = 8;
    break;
  case 0x44: case 0x90: bw = 4, bh = 4, bits = 16; break;
  case 0x45: bw = 4, bh = 4, bits = 32; break;
  default: return img;
  }
  const bool indexed = format == 0x00 || format == 0x01;
  const size_t paletteBytes = format == 0x00 ? 32 : 512;
  if (indexed && (palette == 0 || palette + paletteBytes > size)) {
    return img;
  }
  const size_t tilesW = (w + bw - 1) / bw, tilesH = (h + bh - 1) / bh;
  if (data + tilesW * tilesH * bw * bh * bits / 8 > size) {
    return img;
  }
  img = Blank(w, h);
  const uint8_t* p = file + data;
  for (int ty = 0; ty < h; ty += bh) {
    for (int tx = 0; tx < w; tx += bw) {
      if (bits == 32) {
        for (int k = 0; k < 16; k++) {
          const int x = tx + k % 4, y = ty + k / 4;
          if (x < w && y < h) {
            uint8_t* q = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
            q[3] = p[2 * k];
            q[0] = p[2 * k + 1];
            q[1] = p[32 + 2 * k];
            q[2] = p[32 + 2 * k + 1];
          }
        }
        p += 64;
        continue;
      }
      for (int k = 0; k < bw * bh; k++) {
        const int x = tx + k % bw, y = ty + k / bw;
        uint32_t v = bits == 4 ? (p[k / 2] >> (k % 2 == 0 ? 4 : 0)) & 15 : bits == 8 ? p[k] : Be16(p + 2 * k);
        if (x >= w || y >= h) {
          continue;
        }
        uint8_t* q = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
        if (indexed) {
          PutPalette(Be16(file + palette + v * 2), paletteFormat, q);
        } else if (bits == 8) {
          q[0] = q[1] = q[2] = static_cast<uint8_t>(v);
          q[3] = 255;
        } else if (format == 0x44) {
          PutRGB565(static_cast<uint16_t>(v), q);
        } else {
          PutRGB5A3(static_cast<uint16_t>(v), q);
        }
      }
      p += bw * bh * bits / 8;
    }
  }
  return img;
}

Image Crop(const Image& in, int x, int y, int width, int height) {
  x = std::clamp(x, 0, in.width);
  y = std::clamp(y, 0, in.height);
  width = std::clamp(width, 0, in.width - x);
  height = std::clamp(height, 0, in.height - y);
  Image out = Blank(width, height);
  for (int r = 0; r < height; r++) {
    std::copy_n(&in.rgba[(static_cast<size_t>(y + r) * in.width + x) * 4], static_cast<size_t>(width) * 4,
                &out.rgba[static_cast<size_t>(r) * width * 4]);
  }
  return out;
}

Image AlphaFromBrightness(const Image& in, int opaqueAt) {
  Image out = in;
  for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
    const int bright = std::max({out.rgba[i], out.rgba[i + 1], out.rgba[i + 2]});
    out.rgba[i + 3] = static_cast<uint8_t>(out.rgba[i + 3] * std::min(1.0f, bright / static_cast<float>(opaqueAt)));
  }
  return out;
}

Image Trim(const Image& in) {
  int x0 = in.width, y0 = in.height, x1 = -1, y1 = -1;
  for (int y = 0; y < in.height; y++) {
    for (int x = 0; x < in.width; x++) {
      if (in.rgba[(static_cast<size_t>(y) * in.width + x) * 4 + 3] > 16) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) {
    return in;
  }
  x0 = std::max(0, x0 - 1);
  y0 = std::max(0, y0 - 1);
  x1 = std::min(in.width - 1, x1 + 1);
  y1 = std::min(in.height - 1, y1 + 1);
  return Square(Crop(in, x0, y0, x1 - x0 + 1, y1 - y0 + 1));
}

Image FocusTop(const Image& in) {
  const Image t = Trim(in);
  if (t.Empty()) {
    return t;
  }
  int x0 = t.width, y0 = t.height, x1 = -1, y1 = -1;
  for (int y = 0; y < t.height; y++) {
    for (int x = 0; x < t.width; x++) {
      if (t.rgba[(static_cast<size_t>(y) * t.width + x) * 4 + 3] > 16) {
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) {
    return t;
  }
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  const int side = std::max(1, static_cast<int>(h > w * 1.25f ? 0.40f * h : 0.52f * std::max(w, h)));
  double sum = 0, weight = 0;
  for (int y = y0; y < std::min(t.height, y0 + side); y++) {
    for (int x = x0; x <= x1; x++) {
      const double a = t.rgba[(static_cast<size_t>(y) * t.width + x) * 4 + 3];
      sum += a * x, weight += a;
    }
  }
  const int cx = weight > 0 ? static_cast<int>(sum / weight) : (x0 + x1) / 2;
  const int left = std::clamp(cx - side / 2, 0, std::max(0, t.width - side));
  return Square(Crop(t, left, y0, side, side));
}

namespace {

bool Same(uint32_t a, uint32_t b) {
  int d = 0;
  for (int k = 0; k < 32; k += 8) {
    d += std::abs(static_cast<int>((a >> k) & 0xFF) - static_cast<int>((b >> k) & 0xFF));
  }
  return d <= 40;
}

uint32_t At(const Image& in, int x, int y) {
  x = std::clamp(x, 0, in.width - 1);
  y = std::clamp(y, 0, in.height - 1);
  return reinterpret_cast<const uint32_t*>(in.rgba.data())[static_cast<size_t>(y) * in.width + x];
}

}

Image Scale2x(const Image& in) {
  const int w = in.width, h = in.height;
  Image out = Blank(w * 2, h * 2);
  auto* dst = reinterpret_cast<uint32_t*>(out.rgba.data());
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const uint32_t B = At(in, x, y - 1), D = At(in, x - 1, y), E = At(in, x, y), F = At(in, x + 1, y),
                     H = At(in, x, y + 1);
      const uint32_t e[4] = {
          Same(D, B) && !Same(D, H) && !Same(B, F) ? D : E,
          Same(B, F) && !Same(B, D) && !Same(F, H) ? F : E,
          Same(D, H) && !Same(D, B) && !Same(H, F) ? D : E,
          Same(H, F) && !Same(D, H) && !Same(B, F) ? F : E,
      };
      for (int k = 0; k < 4; k++) {
        dst[static_cast<size_t>(y * 2 + k / 2) * out.width + x * 2 + k % 2] = e[k];
      }
    }
  }
  return out;
}

Image Scale3x(const Image& in) {
  const int w = in.width, h = in.height;
  Image out = Blank(w * 3, h * 3);
  auto* dst = reinterpret_cast<uint32_t*>(out.rgba.data());
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const uint32_t A = At(in, x - 1, y - 1), B = At(in, x, y - 1), C = At(in, x + 1, y - 1);
      const uint32_t D = At(in, x - 1, y), E = At(in, x, y), F = At(in, x + 1, y);
      const uint32_t G = At(in, x - 1, y + 1), H = At(in, x, y + 1), I = At(in, x + 1, y + 1);
      const bool db = Same(D, B) && !Same(D, H) && !Same(B, F);
      const bool bf = Same(B, F) && !Same(B, D) && !Same(F, H);
      const bool dh = Same(D, H) && !Same(D, B) && !Same(H, F);
      const bool hf = Same(H, F) && !Same(D, H) && !Same(B, F);
      const uint32_t e[9] = {
          db ? D : E,
          (db && !Same(E, C)) || (bf && !Same(E, A)) ? B : E,
          bf ? F : E,
          (db && !Same(E, G)) || (dh && !Same(E, A)) ? D : E,
          E,
          (bf && !Same(E, I)) || (hf && !Same(E, C)) ? F : E,
          dh ? D : E,
          (dh && !Same(E, I)) || (hf && !Same(E, G)) ? H : E,
          hf ? F : E,
      };
      for (int k = 0; k < 9; k++) {
        dst[static_cast<size_t>(y * 3 + k / 3) * out.width + x * 3 + k % 3] = e[k];
      }
    }
  }
  return out;
}

Image UpscaleTo(const Image& in, int target) {
  Image img = in;
  while (!img.Empty() && std::max(img.width, img.height) < target) {
    const int side = std::max(img.width, img.height);
    img = side * 3 <= target * 3 / 2 ? Scale3x(img) : Scale2x(img);
  }
  const int side = std::max(img.width, img.height);
  if (side > target) {
    img = Shrink(img, std::max(1, img.width * target / side), std::max(1, img.height * target / side));
  }
  return img;
}

Image Shrink(const Image& in, int width, int height) {
  Image out = Blank(width, height);
  const double sx = static_cast<double>(in.width) / width, sy = static_cast<double>(in.height) / height;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      double acc[4] = {0, 0, 0, 0}, area = 0;
      const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
      for (int iy = static_cast<int>(y0); iy < y1 && iy < in.height; iy++) {
        const double wy = std::min<double>(iy + 1, y1) - std::max<double>(iy, y0);
        for (int ix = static_cast<int>(x0); ix < x1 && ix < in.width; ix++) {
          const double wgt = wy * (std::min<double>(ix + 1, x1) - std::max<double>(ix, x0));
          const uint8_t* p = &in.rgba[(static_cast<size_t>(iy) * in.width + ix) * 4];
          const double a = p[3] / 255.0 * wgt;
          acc[0] += p[0] * a;
          acc[1] += p[1] * a;
          acc[2] += p[2] * a;
          acc[3] += a;
          area += wgt;
        }
      }
      uint8_t* q = &out.rgba[(static_cast<size_t>(y) * width + x) * 4];
      if (acc[3] > 0) {
        for (int c = 0; c < 3; c++) {
          q[c] = static_cast<uint8_t>(std::clamp(acc[c] / acc[3] + 0.5, 0.0, 255.0));
        }
        q[3] = static_cast<uint8_t>(std::clamp(acc[3] / area * 255.0 + 0.5, 0.0, 255.0));
      }
    }
  }
  return out;
}

Image Square(const Image& in) {
  const int side = std::max(in.width, in.height);
  if (in.width == side && in.height == side) {
    return in;
  }
  Image out = Blank(side, side);
  const int ox = (side - in.width) / 2, oy = (side - in.height) / 2;
  for (int y = 0; y < in.height; y++) {
    std::copy_n(&in.rgba[static_cast<size_t>(y) * in.width * 4], static_cast<size_t>(in.width) * 4,
                &out.rgba[(static_cast<size_t>(y + oy) * side + ox) * 4]);
  }
  return out;
}

namespace {

float RoundedCover(int x, int y, float lo, float hi, float r) {
  int inside = 0;
  for (int sy = 0; sy < 4; sy++) {
    for (int sx = 0; sx < 4; sx++) {
      const float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
      const float cx = std::clamp(px, lo + r, hi - r), cy = std::clamp(py, lo + r, hi - r);
      const bool in = px >= lo && px <= hi && py >= lo && py <= hi &&
                      (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
      inside += in ? 1 : 0;
    }
  }
  return inside / 16.0f;
}

void Over(uint8_t* dst, const uint8_t* src, float cover) {
  const float a = src[3] / 255.0f * cover;
  const float da = dst[3] / 255.0f;
  const float oa = a + da * (1 - a);
  for (int c = 0; c < 3; c++) {
    const float v = oa > 0 ? (src[c] * a + dst[c] * da * (1 - a)) / oa : 0;
    dst[c] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
  }
  dst[3] = static_cast<uint8_t>(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
}

}

Image Framed(const Image& picture, int inner) {
  constexpr int kSide = 256;
  Image out = Blank(kSide, kSide);
  const uint8_t fill[4] = {30, 14, 56, 255}, rim[4] = {150, 96, 235, 255};
  const float r = kSide * 0.2f, rimWidth = kSide / 32.0f;
  const Image big = UpscaleTo(picture, std::clamp(inner, 16, kSide));
  const int ox = (kSide - big.width) / 2, oy = (kSide - big.height) / 2;
  for (int y = 0; y < kSide; y++) {
    for (int x = 0; x < kSide; x++) {
      uint8_t* q = &out.rgba[(static_cast<size_t>(y) * kSide + x) * 4];
      Over(q, rim, RoundedCover(x, y, 0, static_cast<float>(kSide), r));
      Over(q, fill, RoundedCover(x, y, rimWidth, kSide - rimWidth, r - rimWidth));
      if (x >= ox && x < ox + big.width && y >= oy && y < oy + big.height) {
        Over(q, &big.rgba[(static_cast<size_t>(y - oy) * big.width + x - ox) * 4],
             RoundedCover(x, y, rimWidth, kSide - rimWidth, r - rimWidth));
      }
    }
  }
  return out;
}

std::vector<Image> IconSizes(const Image& square, const Image* forSmall) {
  std::vector<Image> out;
  for (int size : {16, 24, 32, 48, 64, 128, 256}) {
    Image img = size <= 32 && forSmall != nullptr && !forSmall->Empty() ? *forSmall : square;
    while (img.width < size) {
      img = Scale2x(img);
    }
    if (img.width != size) {
      img = Shrink(img, size, size);
    }
    out.push_back(std::move(img));
  }
  return out;
}

bool WriteIco(const std::filesystem::path& file, const std::vector<Image>& sizes) {
  std::vector<uint8_t> head, body;
  Le16(head, 0);
  Le16(head, 1);
  Le16(head, static_cast<uint32_t>(sizes.size()));
  uint32_t offset = static_cast<uint32_t>(6 + 16 * sizes.size());
  for (const Image& img : sizes) {
    const int w = img.width, h = img.height;
    const uint32_t maskRow = ((w + 31) / 32) * 4;
    std::vector<uint8_t> dib;
    Le32(dib, 40);
    Le32(dib, static_cast<uint32_t>(w));
    Le32(dib, static_cast<uint32_t>(h * 2));
    Le16(dib, 1);
    Le16(dib, 32);
    Le32(dib, 0);
    Le32(dib, static_cast<uint32_t>(w * h * 4 + maskRow * h));
    for (int i = 0; i < 4; i++) {
      Le32(dib, 0);
    }
    for (int y = h - 1; y >= 0; y--) {
      for (int x = 0; x < w; x++) {
        const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
        dib.insert(dib.end(), {p[2], p[1], p[0], p[3]});
      }
    }
    dib.insert(dib.end(), static_cast<size_t>(maskRow) * h, 0);
    head.push_back(static_cast<uint8_t>(w >= 256 ? 0 : w));
    head.push_back(static_cast<uint8_t>(h >= 256 ? 0 : h));
    head.push_back(0);
    head.push_back(0);
    Le16(head, 1);
    Le16(head, 32);
    Le32(head, static_cast<uint32_t>(dib.size()));
    Le32(head, offset);
    offset += static_cast<uint32_t>(dib.size());
    body.insert(body.end(), dib.begin(), dib.end());
  }
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const std::filesystem::path temp = file.wstring() + L".partial";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
    out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    if (!out) {
      return false;
    }
  }
  std::filesystem::rename(temp, file, ec);
  return !ec;
}

}
