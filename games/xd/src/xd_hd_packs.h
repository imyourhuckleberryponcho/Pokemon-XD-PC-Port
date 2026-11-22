#ifndef XD_HD_PACKS_H
#define XD_HD_PACKS_H

#include <filesystem>
#include <string>
#include <vector>

namespace xd::hd {
struct Pack {
  std::filesystem::path folder;
  std::string name;
  bool loose = false;
};
void SetPacks(std::vector<Pack> packs);
bool ReloadPacks(std::vector<Pack> packs);
}

#endif
