#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pbr {

struct Rgba {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  Rgba() = default;
  Rgba(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4) {}
};

bool gx_tile(int fmt, int* bw, int* bh);

Rgba gx_decode(int fmt, int w, int h, const uint8_t* data, size_t size);

Rgba gx_decode_c14x2(int w, int h, const uint8_t* data, size_t size, int pal_format);

std::vector<uint8_t> gx_encode_cmpr(const Rgba& img);
std::vector<uint8_t> gx_encode_rgb5a3(const Rgba& img);
std::vector<uint8_t> gx_encode_ia8(const Rgba& img);

Rgba gx_resize(const Rgba& img, int w, int h);

}
