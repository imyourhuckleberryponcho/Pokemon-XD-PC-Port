
#include "save_thumbnail.h"

#include <cstdio>
#include <filesystem>

namespace th = engine::memcard::thumbnail;

static int sFailed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); sFailed++; } } while (0)

int main() {
  const uint32_t w = 640, h = 480;
  std::vector<uint8_t> src(static_cast<size_t>(w) * h * 4);
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* p = &src[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = static_cast<uint8_t>(x / 3), p[1] = static_cast<uint8_t>(y / 2), p[2] = static_cast<uint8_t>((x + y) / 5), p[3] = 255;
    }
  th::Picture small = th::Shrink(src.data(), w, h);
  CHECK(small.width == 256 && small.height == 192);
  CHECK(small.rgba.size() == 256u * 192 * 4);
  th::Picture wide = th::Shrink(src.data(), w, h / 2);
  CHECK(wide.width == 256 && wide.height == 96);
  th::Picture tiny = th::Shrink(src.data(), 100, 50);
  CHECK(tiny.width == 100 && tiny.height == 50);
  CHECK(th::Shrink(nullptr, 5, 5).Empty());

  const std::vector<uint8_t> png = th::EncodePng(small);
  CHECK(png.size() > 100);
  th::Picture back;
  std::string error;
  CHECK(th::DecodePng(png, back, error));
  CHECK(back.width == small.width && back.height == small.height && back.rgba == small.rgba);

  const std::filesystem::path file = std::filesystem::temp_directory_path() / "xd_thumb_test" / "a" / "k.png";
  std::filesystem::remove_all(file.parent_path().parent_path());
  CHECK(th::WriteFile(file, small, error));
  th::Picture read;
  CHECK(th::ReadFile(file, read, error) && read.rgba == small.rgba);
  CHECK(!th::ReadFile(file.parent_path() / "none.png", read, error));
  std::vector<uint8_t> junk = {1, 2, 3};
  CHECK(!th::DecodePng(junk, read, error));
  std::filesystem::remove_all(file.parent_path().parent_path());
  std::printf(sFailed == 0 ? "save_thumbnail_test: OK\n" : "save_thumbnail_test: FAILED\n");
  return sFailed == 0 ? 0 : 1;
}
