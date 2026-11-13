#include <cstdio>
#include <filesystem>

#include "folder_disc.h"
#include "pbr_pack.h"

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  FolderDisc disc(argv[1], "RPBE01");
  int ok = 0, bad = 0;
  auto rd = [](const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  };
  for (const pbr::CryOut& c : pbr::gen4_cries(disc)) {
    char pre[8];
    std::snprintf(pre, sizeof pre, "%03d_", c.dex);
    std::filesystem::path h, b;
    for (auto& e : std::filesystem::directory_iterator(argv[2])) {
      const std::string n = e.path().filename().string();
      if (n.rfind(pre, 0) != 0) continue;
      if (e.path().extension() == ".hdr") h = e.path();
      if (e.path().extension() == ".body") b = e.path();
    }
    if (!h.empty() && !b.empty() && rd(h) == c.hdr && rd(b) == c.body) {
      ok++;
    } else {
      bad++;
      std::printf("DIFF %d\n", c.dex);
    }
  }
  std::printf("%d identical, %d different\n", ok, bad);
  return bad != 0;
}
