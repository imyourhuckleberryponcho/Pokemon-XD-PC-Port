#include <gx_texture_decode.h>

#include <cstring>

namespace {

constexpr uint32_t kI4 = 0, kI8 = 1, kIA4 = 2, kIA8 = 3, kRGB565 = 4, kRGB5A3 = 5, kRGBA8 = 6, kCI4 = 8, kCI8 = 9,
                   kCMPR = 0xE;
constexpr uint32_t kTlIA8 = 0, kTlRGB565 = 1, kTlRGB5A3 = 2;

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

void Put(uint8_t* out, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (x >= w || y >= h) return;
  uint8_t* p = out + (static_cast<size_t>(y) * w + x) * 4;
  p[0] = r, p[1] = g, p[2] = b, p[3] = a;
}

void FromRGB565(uint16_t v, uint8_t* c) {
  const uint8_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  c[0] = static_cast<uint8_t>(r << 3 | r >> 2);
  c[1] = static_cast<uint8_t>(g << 2 | g >> 4);
  c[2] = static_cast<uint8_t>(b << 3 | b >> 2);
  c[3] = 255;
}

void FromRGB5A3(uint16_t v, uint8_t* c) {
  if (v & 0x8000) {
    const uint8_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    c[0] = static_cast<uint8_t>(r << 3 | r >> 2);
    c[1] = static_cast<uint8_t>(g << 3 | g >> 2);
    c[2] = static_cast<uint8_t>(b << 3 | b >> 2);
    c[3] = 255;
  } else {
    const uint8_t a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    c[0] = static_cast<uint8_t>(r * 17);
    c[1] = static_cast<uint8_t>(g * 17);
    c[2] = static_cast<uint8_t>(b * 17);
    c[3] = static_cast<uint8_t>(a << 5 | a << 2 | a >> 1);
  }
}

void FromTlut(const uint8_t* tlut, uint32_t format, uint32_t index, uint8_t* c) {
  const uint16_t v = Be16(tlut + index * 2);
  if (format == kTlRGB565) {
    FromRGB565(v, c);
  } else if (format == kTlRGB5A3) {
    FromRGB5A3(v, c);
  } else {
    c[0] = c[1] = c[2] = static_cast<uint8_t>(v & 0xFF);
    c[3] = static_cast<uint8_t>(v >> 8);
  }
}

}

extern "C" bool PortGX_DecodeTexture(const void* data, uint32_t w, uint32_t h, uint32_t format, const void* tlut,
                                     uint32_t tlutFormat, uint8_t* out) {
  const auto* src = static_cast<const uint8_t*>(data);
  if (src == nullptr || w == 0 || h == 0) return false;
  uint32_t bw, bh;
  switch (format) {
  case kI4: case kCI4: case kCMPR: bw = 8, bh = 8; break;
  case kI8: case kIA4: case kCI8: bw = 8, bh = 4; break;
  case kIA8: case kRGB565: case kRGB5A3: case kRGBA8: bw = 4, bh = 4; break;
  default: return false;
  }
  if ((format == kCI4 || format == kCI8) && tlut == nullptr) return false;
  const auto* pal = static_cast<const uint8_t*>(tlut);
  uint8_t c[4];
  for (uint32_t by = 0; by < h; by += bh) {
    for (uint32_t bx = 0; bx < w; bx += bw) {
      switch (format) {
      case kI4:
      case kCI4:
        for (uint32_t y = 0; y < 8; y++) {
          for (uint32_t x = 0; x < 8; x += 2, src++) {
            const uint8_t v[2] = {static_cast<uint8_t>(*src >> 4), static_cast<uint8_t>(*src & 15)};
            for (int k = 0; k < 2; k++) {
              if (format == kI4) {
                const uint8_t i = static_cast<uint8_t>(v[k] * 17);
                Put(out, w, h, bx + x + k, by + y, i, i, i, i);
              } else {
                FromTlut(pal, tlutFormat, v[k], c);
                Put(out, w, h, bx + x + k, by + y, c[0], c[1], c[2], c[3]);
              }
            }
          }
        }
        break;
      case kI8:
      case kCI8:
      case kIA4:
        for (uint32_t y = 0; y < 4; y++) {
          for (uint32_t x = 0; x < 8; x++, src++) {
            if (format == kI8) {
              Put(out, w, h, bx + x, by + y, *src, *src, *src, *src);
            } else if (format == kIA4) {
              const uint8_t a = static_cast<uint8_t>((*src >> 4) * 17), i = static_cast<uint8_t>((*src & 15) * 17);
              Put(out, w, h, bx + x, by + y, i, i, i, a);
            } else {
              FromTlut(pal, tlutFormat, *src, c);
              Put(out, w, h, bx + x, by + y, c[0], c[1], c[2], c[3]);
            }
          }
        }
        break;
      case kIA8:
      case kRGB565:
      case kRGB5A3:
        for (uint32_t y = 0; y < 4; y++) {
          for (uint32_t x = 0; x < 4; x++, src += 2) {
            if (format == kIA8) {
              Put(out, w, h, bx + x, by + y, src[1], src[1], src[1], src[0]);
            } else {
              if (format == kRGB565) FromRGB565(Be16(src), c);
              else FromRGB5A3(Be16(src), c);
              Put(out, w, h, bx + x, by + y, c[0], c[1], c[2], c[3]);
            }
          }
        }
        break;
      case kRGBA8:
        for (uint32_t i = 0; i < 16; i++) {
          const uint8_t* ar = src + i * 2;
          const uint8_t* gb = src + 32 + i * 2;
          Put(out, w, h, bx + i % 4, by + i / 4, ar[1], gb[0], gb[1], ar[0]);
        }
        src += 64;
        break;
      case kCMPR:
        for (uint32_t sub = 0; sub < 4; sub++, src += 8) {
          const uint32_t sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
          const uint16_t c0 = Be16(src), c1 = Be16(src + 2);
          uint8_t pal4[4][4];
          FromRGB565(c0, pal4[0]);
          FromRGB565(c1, pal4[1]);
          for (int k = 0; k < 3; k++) {
            if (c0 > c1) {
              pal4[2][k] = static_cast<uint8_t>((2 * pal4[0][k] + pal4[1][k]) / 3);
              pal4[3][k] = static_cast<uint8_t>((pal4[0][k] + 2 * pal4[1][k]) / 3);
            } else {
              pal4[2][k] = static_cast<uint8_t>((pal4[0][k] + pal4[1][k]) / 2);
              pal4[3][k] = pal4[2][k];
            }
          }
          pal4[2][3] = 255;
          pal4[3][3] = c0 > c1 ? 255 : 0;
          for (uint32_t y = 0; y < 4; y++) {
            const uint8_t bits = src[4 + y];
            for (uint32_t x = 0; x < 4; x++) {
              const uint8_t* p = pal4[(bits >> (6 - 2 * x)) & 3];
              Put(out, w, h, sx + x, sy + y, p[0], p[1], p[2], p[3]);
            }
          }
        }
        break;
      }
    }
  }
  return true;
}
