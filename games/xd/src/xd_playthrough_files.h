#ifndef XD_PLAYTHROUGH_FILES_H
#define XD_PLAYTHROUGH_FILES_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::playthrough {

std::filesystem::path CardFolder();
std::filesystem::path SetupFolder();

struct SaveId {
  bool found = false;
  std::string key;
  std::string trainer;
  uint32_t trainerId = 0, secretId = 0;
  std::filesystem::path file;
  std::string error;
};

SaveId SaveIdOf(const std::vector<uint8_t>& saveFile);
SaveId SaveIdOnCard();
std::filesystem::path SetupFileFor(const SaveId& id);

bool LooksOrSound(const std::string& path, int fsysType);

}

#endif
