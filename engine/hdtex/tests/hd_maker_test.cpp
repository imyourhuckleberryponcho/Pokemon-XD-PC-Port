
#include "hd_maker.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace h = engine::hdtex;
static int sFailed = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);  \
      sFailed++;                                                      \
    }                                                                 \
  } while (0)

int main() {

  CHECK(h::Xxh64(reinterpret_cast<const uint8_t*>(""), 0) == 0xEF46DB3751D8E999ull);
  CHECK(h::Xxh64(reinterpret_cast<const uint8_t*>("abc"), 3) == 0x44BC2CF5AD770999ull);
  std::vector<uint8_t> big(1000);
  for (size_t i = 0; i < big.size(); i++) big[i] = (uint8_t)(i * 31 + 7);
  CHECK(h::Xxh64(big.data(), big.size()) != 0);


  h::DiscTexture t;
  t.width = 4; t.height = 4; t.format = 4;
  t.level0.assign(32, 0);
  for (int i = 0; i < 16; i++) { t.level0[i * 2] = 0xF8; t.level0[i * 2 + 1] = 0x00; }
  std::vector<uint8_t> px;
  CHECK(h::DecodeRgba(t, px));
  CHECK(px.size() == 64 && px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);

  t.width = 8; t.height = 8; t.format = 14;
  t.level0.assign(32, 0);
  for (int sb = 0; sb < 4; sb++) { t.level0[sb * 8] = 0xFF; t.level0[sb * 8 + 1] = 0xFF; }
  CHECK(h::DecodeRgba(t, px) && px[0] == 255 && px[(7 * 8 + 7) * 4 + 1] == 255);


  std::vector<uint8_t> a(16 * 16 * 4, 255), b = a;
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) a[(y * 16 + x) * 4 + 3] = ((x + y) & 1) ? 0 : 255;
  for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) b[(y * 16 + x) * 4 + 3] = 0;
  CHECK(h::HasAlphaDither(a.data(), 16, 16));
  CHECK(!h::HasAlphaDither(b.data(), 16, 16));


  const uint32_t w = 32, hh = 32, s = 2;
  std::vector<uint8_t> orig(w * hh * 4, 255), res(w * s * hh * s * 4, 255);
  for (uint32_t y = 0; y < hh; y++) for (uint32_t x = 0; x < w; x++) for (int c = 0; c < 3; c++) orig[(y * w + x) * 4 + c] = (uint8_t)(60 + x * 4 + c * 10);
  for (uint32_t y = 0; y < hh * s; y++) for (uint32_t x = 0; x < w * s; x++) for (int c = 0; c < 3; c++) {
    const int v = 60 + (x / s) * 4 + c * 10 + 30;
    res[(y * w * s + x) * 4 + c] = (uint8_t)std::min(255, v + ((x + y) & 1 ? 3 : -3));
  }
  const double before = h::MeanColourDifference(res, orig, w, hh, s);
  h::ColourMatch(res, orig, w, hh, s, false);
  const double after = h::MeanColourDifference(res, orig, w, hh, s);
  std::printf("colour difference before %.2f after %.2f\n", before, after);
  CHECK(before > 25 && after < 4);


  std::vector<uint8_t> p2(4 * 4 * 4, 0);
  p2[0] = 200; p2[1] = 100; p2[2] = 50; p2[3] = 255;
  h::BleedColour(p2, 4, 4);
  CHECK(p2[(3 * 4 + 3) * 4] == 200 && p2[(3 * 4 + 3) * 4 + 3] == 0);


  const auto dir = std::filesystem::temp_directory_path() / "hd_maker_test";
  std::filesystem::create_directories(dir);
  CHECK(h::WritePng(dir / "a.png", w, hh, orig.data(), true));
  h::Image im;
  CHECK(h::ReadPng(dir / "a.png", im) && im.w == w && im.h == hh && im.rgba == orig);
  CHECK(h::WritePng(dir / "b.png", w, hh, orig.data(), false));
  CHECK(h::ReadPng(dir / "b.png", im) && im.w == w && im.rgba == orig);
  std::filesystem::remove_all(dir);

  CHECK(h::OfficialTool().sha256.size() == 64 && !h::OfficialTool().url.empty());
  std::printf(sFailed ? "FAILED\n" : "all ok\n");
  return sFailed ? 1 : 0;
}
