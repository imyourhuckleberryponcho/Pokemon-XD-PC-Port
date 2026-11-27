#include "room_preview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::rooms {

namespace {

uint32_t Crc(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
  static uint32_t table[256];
  static bool made = false;
  if (!made) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    made = true;
  }
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 24)), v.push_back(uint8_t(x >> 16)), v.push_back(uint8_t(x >> 8)), v.push_back(uint8_t(x)); }
void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
  Put32(png, uint32_t(data.size()));
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  png.insert(png.end(), td.begin(), td.end());
  Put32(png, ~Crc(td.data(), td.size()));
}

}

std::vector<uint8_t> RgbToPng(const std::vector<uint8_t>& rgb, int w, int h) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb.begin() + size_t(y) * w * 3, rgb.begin() + size_t(y + 1) * w * 3);
  }
  std::vector<uint8_t> z{0x78, 0x01};
  size_t at = 0;
  while (at < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + n == raw.size() ? 1 : 0);
    z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t((~n) >> 8));
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    at += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
  Put32(z, b << 16 | a);
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  Put32(ihdr, uint32_t(w)), Put32(ihdr, uint32_t(h));
  ihdr.push_back(8), ihdr.push_back(2), ihdr.push_back(0), ihdr.push_back(0), ihdr.push_back(0);
  Chunk(png, "IHDR", ihdr);
  Chunk(png, "IDAT", z);
  Chunk(png, "IEND", {});
  return png;
}

std::vector<uint8_t> CcdTopViewPng(const Ccd& ccd, int size) {
  float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
  for (const CcdRecord& r : ccd.records) {
    for (const CcdBlock& b : r.block) {
      for (const CcdTri& t : b.tris) {
        for (int i = 0; i < 3; i++) minx = std::min(minx, t.v[i * 3]), maxx = std::max(maxx, t.v[i * 3]), minz = std::min(minz, t.v[i * 3 + 2]), maxz = std::max(maxz, t.v[i * 3 + 2]);
      }
    }
  }
  if (minx > maxx) minx = minz = -10, maxx = maxz = 10;
  const float span = std::max(maxx - minx, maxz - minz) * 1.1f + 1.f, cx = (minx + maxx) / 2, cz = (minz + maxz) / 2, scale = size / span;
  std::vector<uint8_t> img(size_t(size) * size * 3, 24);
  const auto px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    uint8_t* p = &img[(size_t(y) * size + x) * 3];
    p[0] = r, p[1] = g, p[2] = b;
  };
  const auto sx = [&](float x) { return int((x - cx) * scale + size / 2.f); };
  const auto sz = [&](float z) { return int((z - cz) * scale + size / 2.f); };
  const auto line = [&](float x0, float z0, float x1, float z1, uint8_t r, uint8_t g, uint8_t b) {
    const int ax = sx(x0), ay = sz(z0), bx = sx(x1), by = sz(z1);
    const int n = std::max(std::abs(bx - ax), std::abs(by - ay)) + 1;
    for (int i = 0; i <= n; i++) px(ax + (bx - ax) * i / n, ay + (by - ay) * i / n, r, g, b);
  };
  for (float x = std::ceil(minx / 10) * 10; x <= maxx; x += 10) line(x, minz, x, maxz, 40, 40, 48);
  for (float z = std::ceil(minz / 10) * 10; z <= maxz; z += 10) line(minx, z, maxx, z, 40, 40, 48);
  for (const CcdRecord& r : ccd.records) {
    for (const CcdTri& t : r.block[kCcdWalk].tris) {
      for (int i = 0; i < 3; i++) line(t.v[i * 3], t.v[i * 3 + 2], t.v[((i + 1) % 3) * 3], t.v[((i + 1) % 3) * 3 + 2], 70, 110, 70);
    }
  }
  for (const CcdRecord& r : ccd.records) {
    for (const CcdTri& t : r.block[kCcdHit].tris) {
      if (std::fabs(t.v[1] - t.v[4]) < 1e-3f && std::fabs(t.v[1] - t.v[7]) < 1e-3f) continue;
      for (int i = 0; i < 3; i++) {
        const int j = (i + 1) % 3;
        if (std::fabs(t.v[i * 3 + 1] - t.v[j * 3 + 1]) < 1e-3f) line(t.v[i * 3], t.v[i * 3 + 2], t.v[j * 3], t.v[j * 3 + 2], 230, 90, 80);
      }
    }
    for (const CcdTri& t : r.block[kCcdThru].tris) {
      for (int i = 0; i < 3; i++) line(t.v[i * 3], t.v[i * 3 + 2], t.v[((i + 1) % 3) * 3], t.v[((i + 1) % 3) * 3 + 2], 80, 160, 240);
    }
    for (const CcdTri& t : r.block[kCcdCheck].tris) {
      for (int i = 0; i < 3; i++) line(t.v[i * 3], t.v[i * 3 + 2], t.v[((i + 1) % 3) * 3], t.v[((i + 1) % 3) * 3 + 2], 240, 200, 60);
    }
  }
  return RgbToPng(img, size, size);
}

}
