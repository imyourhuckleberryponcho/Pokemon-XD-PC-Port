#include "room_recipe.h"

#include <cstdio>
#include <cstring>

namespace engine::rooms {

namespace {
uint32_t Rol(uint32_t v, int n) { return v << n | v >> (32 - n); }
}

std::string RoomSha1(const uint8_t* data, size_t size) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::vector<uint8_t> m(data, data + size);
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  const uint64_t bits = uint64_t(size) * 8;
  for (int i = 7; i >= 0; i--) m.push_back(uint8_t(bits >> (8 * i)));
  for (size_t at = 0; at < m.size(); at += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = uint32_t(m[at + 4 * i]) << 24 | uint32_t(m[at + 4 * i + 1]) << 16 | uint32_t(m[at + 4 * i + 2]) << 8 | m[at + 4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
      else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
      else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
      else f = b ^ c ^ d, k = 0xCA62C1D6u;
      const uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d, d = c, c = Rol(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  }
  char out[41];
  for (int i = 0; i < 5; i++) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
  return out;
}

}
