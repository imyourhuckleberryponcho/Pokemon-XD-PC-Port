#include <disc_textures.h>
#include <hd_textures.h>
#include <port_disc.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace h = engine::hdtex;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xd_hd_export <disc> --list <file.tsv> [--png <folder>] [--all-classes]\n");
    return 2;
  }
  std::string list, png;
  bool all = false;
  for (int i = 2; i < argc; i++) {
    if (!std::strcmp(argv[i], "--list") && i + 1 < argc) list = argv[++i];
    else if (!std::strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
    else if (!std::strcmp(argv[i], "--all-classes")) all = true;
  }
  char err[256] = "";
  if (!PortDisc_Open(argv[1], err, sizeof err)) { std::fprintf(stderr, "disc: %s\n", err); return 1; }
  h::FileSource src;
  src.list = [] {
    std::vector<std::string> v;
    PortDisc_ListFiles([](const char* p, uint32_t, void* u) { static_cast<std::vector<std::string>*>(u)->push_back(p); }, &v);
    return v;
  };
  src.read = [](const std::string& p, std::vector<uint8_t>& out) {
    size_t n = 0;
    uint8_t* d = PortDisc_ReadOriginalFile(p.c_str(), &n);
    if (!d) return false;
    out.assign(d, d + n);
    PortDisc_Free(d);
    return true;
  };
  FILE* lf = list.empty() ? nullptr : std::fopen(list.c_str(), "wb");
  if (!png.empty()) fs::create_directories(png);
  h::ExportStats st;
  uint64_t pngs = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok = h::ForEachDiscTexture(
      src,
      [&](h::DiscTexture& t) {
        if (lf)
          std::fprintf(lf, "%s\t%s\t%s\t%s\t%s\t%ux%u\t%u\n", t.stem.c_str(), h::ClassName(t.cls), h::SourceName(t.source),
                       t.fsys.c_str(), t.member.c_str(), t.width, t.height, t.format);
        if (!png.empty() && (all || t.cls == h::TexClass::Surface3D)) {
          std::vector<uint8_t> px;
          if (h::DecodeRgba(t, px) && h::WritePngRgba(fs::path(png) / (t.stem + ".png"), t.width, t.height, px.data())) pngs++;
        }
      },
      [](uint32_t d, uint32_t n) { if (d % 200 == 0) std::fprintf(stderr, "archive %u / %u\n", d, n); return true; }, &st);
  if (lf) std::fclose(lf);
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("ok=%d members=%llu found=%llu unique=%llu level0=%.1f MB pngs=%llu time=%.1fs\n", ok,
              (unsigned long long)st.members, (unsigned long long)st.found, (unsigned long long)st.unique,
              st.level0Bytes / 1048576.0, (unsigned long long)pngs, secs);
  for (int c = 0; c < 6; c++) std::printf("  %-13s %llu\n", h::ClassName((h::TexClass)c), (unsigned long long)st.byClass[c]);
  for (int f = 0; f < 16; f++) if (st.byFormat[f]) std::printf("  format %2d %llu\n", f, (unsigned long long)st.byFormat[f]);
  return ok ? 0 : 1;
}
