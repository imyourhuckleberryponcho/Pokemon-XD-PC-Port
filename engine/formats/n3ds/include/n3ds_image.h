#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace n3ds {

struct Image8 {
  int w = 0, h = 0, ch = 4;
  std::vector<uint8_t> px;
};

Image8 png_decode(const std::vector<uint8_t>& file);
std::vector<uint8_t> png_encode(const Image8& im);
std::vector<uint8_t> zlib_inflate(const uint8_t* d, size_t n);
std::vector<uint8_t> zlib_deflate(const std::vector<uint8_t>& raw);

Image8 pil_resize_lanczos(const Image8& in, int nw, int nh);

Image8 round_face(const Image8& rendered, int size = 84);
bool face_ok(const Image8& im, double lo = 0.35, double hi = 0.995);
bool box_icon_face(const Image8& icon, Image8* out);

}
