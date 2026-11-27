#include <hd_maker.h>
#include <port_disc.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace h = engine::hdtex;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: xd_hd_make <disc> --pack <folder> --tools <folder> [--scale 2|4] [--limit N] [--work <folder>] [--install]\n");
    return 2;
  }
  h::MakerOptions o;
  fs::path tools;
  bool install = false;
  for (int i = 2; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--pack") o.pack = next();
    else if (a == "--tools") tools = next();
    else if (a == "--work") o.work = next();
    else if (a == "--scale") o.scale = (uint32_t)std::atoi(next().c_str());
    else if (a == "--limit") o.limit = (uint64_t)std::atoll(next().c_str());
    else if (a == "--no-colour-match") o.colourMatch = false;
    else if (a == "--install") install = true;
    else if (a == "--characters") o.includeCharacters = true;
    else if (a == "--effects") o.includeEffects = true;
    else if (a == "--interface") o.includeInterface = true;
    else if (a == "--gpu") o.gpu = next();
    else if (a == "--only-list") { std::ifstream in(next()); std::string l; while (std::getline(in, l)) { while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ')) l.pop_back(); if (l.size() > 4 && l.substr(l.size() - 4) == ".png") l.resize(l.size() - 4); if (!l.empty()) o.only.push_back(l); } }
  }
  if (const char* lim = std::getenv("XD_HD_MAKE_LIMIT")) o.limit = (uint64_t)std::atoll(lim);
  if (o.pack.empty() || tools.empty()) { std::fprintf(stderr, "--pack and --tools are needed\n"); return 2; }
  if (o.work.empty()) o.work = o.pack.parent_path() / (o.pack.filename().string() + "_work");
  fs::path exe;
  if (!h::ToolInstalled(tools, &exe)) {
    if (!install) { std::fprintf(stderr, "the upscaler is not in %s (use --install)\n", tools.string().c_str()); return 3; }
    std::string err;
    std::fprintf(stderr, "downloading the upscaler...\n");
    if (!h::InstallTool(tools, err, [](uint64_t d, uint64_t t) {
          static auto lastPrint = std::chrono::steady_clock::now();
          const auto now = std::chrono::steady_clock::now();
          if (now - lastPrint > std::chrono::milliseconds(700)) {
            lastPrint = now;
            std::printf("progress %llu %llu downloading the upscaler\n", (unsigned long long)(d / 1024), (unsigned long long)(t / 1024));
            std::fflush(stdout);
          }
          return true;
        })) {
      std::printf("error The upscaler could not be installed: %s\n", err.c_str());
      return 1;
    }
    h::ToolInstalled(tools, &exe);
  }
  o.toolExe = exe;
  o.models = tools / "models";
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
  auto last = std::chrono::steady_clock::now() - std::chrono::seconds(10);
  const h::MakerResult r = h::MakePack(src, o, [&](const h::MakerProgress& p) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::milliseconds(900)) {
      last = now;
      std::printf("progress %llu %llu %s\n", (unsigned long long)p.done, (unsigned long long)p.total,
                  std::string(p.phase) == "counting" ? "reading the disc" : p.detail.c_str());
      std::fflush(stdout);
    }
    return true;
  });
  if (!r.ok) {
    std::printf("error %s\n", r.error.empty() ? "the maker stopped" : r.error.c_str());
  } else {
    std::printf("done %llu %llu %llu\n", (unsigned long long)r.made, (unsigned long long)r.resumed, (unsigned long long)r.packBytes);
  }
  std::fflush(stdout);
  std::fprintf(stderr, "ok=%d cancelled=%d error=\"%s\" candidates=%llu made=%llu resumed=%llu tooSmall=%llu failed=%llu packBytes=%llu (%.1f MB) seconds=%.1f colourChange=%.3f\n",
              r.ok, r.cancelled, r.error.c_str(), (unsigned long long)r.candidates, (unsigned long long)r.made,
              (unsigned long long)r.resumed, (unsigned long long)r.tooSmall, (unsigned long long)r.failed,
              (unsigned long long)r.packBytes, r.packBytes / 1048576.0, r.seconds, r.colourChange);
  for (int c = 0; c < 6; c++) std::fprintf(stderr, "  left out %-13s %llu\n", h::ClassName((h::TexClass)c), (unsigned long long)r.skippedByClass[c]);
  return r.ok ? 0 : 1;
}
