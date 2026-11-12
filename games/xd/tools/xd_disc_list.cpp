#include <gs_formats.h>
#include <port_disc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
std::vector<std::string> sFsys;

void collect(const char* path, uint32_t, void*) {
  const size_t n = std::strlen(path);
  if (n > 5 && std::strcmp(path + n - 5, ".fsys") == 0) sFsys.emplace_back(path);
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [type]\n", argv[0]);
    return 2;
  }
  const long onlyType = argc >= 3 ? std::strtol(argv[2], nullptr, 0) : -1;
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  PortDisc_ListFiles(collect, nullptr);
  std::map<uint32_t, int> typeCounts;
  for (const std::string& path : sFsys) {
    size_t size = 0;
    uint8_t* data = PortDisc_ReadFile(path.c_str(), &size);
    FsysArchive fsys{};
    if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
      std::printf("%s: not readable as FSYS\n", path.c_str());
      PortDisc_Free(data);
      continue;
    }
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (!Fsys_Entry(&fsys, i, &e)) continue;
      typeCounts[e.type]++;
      if (onlyType < 0 || static_cast<uint32_t>(onlyType) == e.type) {
        std::printf("%-40s #%-3u %4u  type %2u  %8u bytes  %s%s%s%s", path.c_str(), i, e.id, e.type, e.size,
                    e.name != nullptr ? e.name : "?", e.fullName[0] != '\0' ? "  (" : "", e.fullName,
                    e.fullName[0] != '\0' ? ")" : "");
        if (e.type == 14) {
          uint32_t relSize = 0;
          uint8_t* rel = Fsys_Extract(&fsys, i, &relSize);
          RelModule m{};
          if (rel != nullptr && Rel_Parse(rel, relSize, &m)) {
            uint32_t code = 0, dataBytes = 0, calls = 0, mainRefs = 0;
            for (uint32_t s = 0; s < m.numSections; s++) {
              (m.sections[s].code ? code : dataBytes) += m.sections[s].size;
            }
            for (uint32_t r = 0; r < m.numRelocs; r++) {
              if (m.relocs[r].module == 0) {
                mainRefs++;
                if (m.relocs[r].type == REL_R_REL24) calls++;
              }
            }
            std::printf("  [module %u: code %u, data %u bytes; %u references to the main program, %u calls]", m.id,
                        code, dataBytes, mainRefs, calls);
            RelPrologStore stores[512];
            const int n = Rel_ReadPrologStores(&m, stores, 512);
            if (n < 0) {
              std::printf("  stores: prolog not understood");
            } else {
              std::printf("  stores %d:", n);
              for (int k = 0; k < n; k++) {
                if (stores[k].isRelAddress) {
                  std::printf(" %08X=s%u+%X", stores[k].dolAddress, stores[k].section, stores[k].offset);
                } else {
                  std::printf(" %08X=#%X", stores[k].dolAddress, stores[k].constant);
                }
              }
            }
            Rel_Free(&m);
          }
          std::free(rel);
        }
        std::printf("\n");
      }
    }
    PortDisc_Free(data);
  }
  std::printf("-- %zu archives; entries by type:", sFsys.size());
  for (const auto& [type, count] : typeCounts) std::printf(" %u:%d", type, count);
  std::printf("\n");
  return 0;
}
