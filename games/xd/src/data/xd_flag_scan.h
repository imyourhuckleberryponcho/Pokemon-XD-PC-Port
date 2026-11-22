#ifndef XD_FLAG_SCAN_H
#define XD_FLAG_SCAN_H

#include <data_mods.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xd::data {

struct ScannedFlag {
  uint32_t id = 0;
  std::string name;
  std::string meaning;
};

std::vector<ScannedFlag> ScanRoomScriptFlags(const engine::mods::EntryReader& read, std::vector<std::string> archives, const std::set<uint32_t>& named,
                                             const std::map<uint32_t, std::string>& steps);

}

#endif
