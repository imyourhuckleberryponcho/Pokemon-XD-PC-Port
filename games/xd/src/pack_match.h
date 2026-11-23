#ifndef XD_PACK_MATCH_H
#define XD_PACK_MATCH_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace xd::packmatch {

constexpr uint32_t kDiscSndFile = 516, kDiscSndData = 1507, kDiscWaves = 389;
constexpr uint32_t kReservedCries = 106;

struct Gen4Pack {
  bool usable = false;
  std::filesystem::path folder;
  uint32_t cryRows = 0, waves = 0;
  std::string tocHash;
};
Gen4Pack ReadGen4(const std::filesystem::path& folder);

struct Gen57Pack {
  bool usable = false;
  bool ownCries = false;
  uint32_t reserved = 0;
  std::string gen4Toc;
  uint32_t firstCryRow = 0;
};
Gen57Pack ReadGen57(const std::filesystem::path& folder);

std::filesystem::path Gen4Folder(const std::filesystem::path& assets, bool gen57Wanted);

enum class Problem {
  None,
  LegacyOtherGen4,
  LegacyGen4Gone,
  TocDiffers,
  TooManyCries,
};
Problem CheckGen57(const std::filesystem::path& assets);
inline bool Works(Problem p) { return p == Problem::None || p == Problem::LegacyOtherGen4; }

std::string Fnv64Hex(const void* data, size_t size);

}

#endif
