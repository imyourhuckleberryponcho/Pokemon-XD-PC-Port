#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include "n3ds_image.h"
using namespace n3ds;
static std::vector<uint8_t> rd(const char* p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
int main(int argc, char** argv) {
  try {
    if (argc < 3) return 2;
    std::string c = argv[1];
    Image8 im = png_decode(rd(argv[2]));
    if (c == "ok") { printf("%d\n", face_ok(im) ? 1 : 0); return 0; }
    Image8 o;
    if (c == "round") o = round_face(im);
    else if (c == "box") { if (!box_icon_face(im, &o)) { printf("empty\n"); return 0; } }
    else if (c == "resize4" || c == "resize3") {
      int w = atoi(argv[4]), h = atoi(argv[5]);
      if (c == "resize3") { Image8 t; t.w = im.w; t.h = im.h; t.ch = 3; for (size_t i = 0; i < (size_t)im.w * im.h; i++) for (int k = 0; k < 3; k++) t.px.push_back(im.px[i * 4 + k]); im = t; }
      o = pil_resize_lanczos(im, w, h);
    } else return 2;
    auto b = png_encode(o);
    std::ofstream f(argv[3], std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
  } catch (const std::exception& e) { fprintf(stderr, "error %s\n", e.what()); return 1; }
  return 0;
}
