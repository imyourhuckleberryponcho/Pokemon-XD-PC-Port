#include <cstdio>
#include <filesystem>
#include <fstream>

#include "folder_disc.h"
#include "pbr_pack.h"

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  try {
    FolderDisc pbr(argv[1], "RPBE01"), xd(argv[2], "GXXE01");
    const auto faces = pbr::gen4_faces(pbr, xd);
    int same = 0, bad = 0;
    for (const auto& f : faces) {
      char pre[8];
      std::snprintf(pre, sizeof pre, "%03d_", f.dex);
      std::filesystem::path found;
      for (auto& e : std::filesystem::directory_iterator(argv[3]))
        if (e.path().filename().string().rfind(pre, 0) == 0 && e.path().extension() == ".face") found = e.path();
      std::ifstream in(found, std::ios::binary);
      std::vector<uint8_t> ref((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (ref == f.data) same++;
      else {
        bad++;
        size_t n = 0;
        for (size_t i = 0; i < ref.size() && i < f.data.size(); i++) n += ref[i] != f.data[i];
        std::printf("DIFFERENT dex %d (%zu vs %zu bytes, %zu bytes differ)\n", f.dex, ref.size(), f.data.size(), n);
      }
    }
    std::printf("%d of %zu identical, %d different\n", same, faces.size(), bad);
    return bad ? 1 : 0;
  } catch (const std::exception& e) {
    std::printf("FAILED: %s\n", e.what());
    return 1;
  }
}

namespace pbr {
void make_gen4_pack(const std::string&, const DiscSource&, const DiscSource&, XdTables&, const std::vector<PackModel>&,
                    const std::vector<PairRow>&, const std::function<void(const std::string&)>&) {}
}
