#include "common_script.h"

#include "data/xd_data_mods.h"

#include <mod_layout.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" void* tableResBiosGetResPtr(uint32_t index);

namespace {
std::vector<uint8_t>* sScript = nullptr;
std::string sFromMod;
}

extern "C" void* PortXD_CommonScriptRes(uint32_t index) {
  void* real = tableResBiosGetResPtr(index);
  if (sScript != nullptr && real != nullptr && std::memcmp(real, "TCOD", 4) == 0) return sScript->data();
  return real;
}

namespace xd::common_script {

namespace em = engine::mods;
namespace fs = std::filesystem;

void Install(const em::LoadResult& result, const std::vector<em::ModFolder>& found, std::string_view gameId, std::vector<std::string>& messages) {
  sScript = nullptr;
  sFromMod.clear();
  for (const em::Manifest& m : result.active) {
    for (const em::ModFolder& f : found) {
      if (f.manifest.id != m.id || f.texturePack) continue;
      std::vector<em::LayoutFile> files = em::LayoutFiles(f.folder, gameId, "common_script", false);
      if (files.empty()) continue;
      if (files.size() > 1) {
        messages.push_back("The mod \"" + m.name + "\" has " + std::to_string(files.size()) + " files in common_script/; the game has one common script, so only the first (" + files[0].rel + ") is used.");
      }
      std::vector<uint8_t> data;
      {
        std::ifstream in(files[0].path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      std::string why;
      if (data.empty() || !xd::data::CheckModScript(m.id, data, result.addedRows, why)) {
        messages.push_back("The mod \"" + m.name + "\": common_script/" + files[0].rel + " is not used (" + (data.empty() ? std::string("it could not be read or is empty") : why) +
                           "); the game keeps its own common script.");
        continue;
      }
      if (sScript != nullptr) {
        messages.push_back("Conflict: the mods \"" + sFromMod + "\" and \"" + m.name + "\" both replace the common script. \"" + m.name + "\" (further down the list) is used.");
      }
      data.resize((data.size() + 31) & ~static_cast<size_t>(31), 0);
      sScript = new std::vector<uint8_t>(std::move(data));
      sFromMod = m.name;
    }
  }
  if (sScript != nullptr) std::fprintf(stderr, "[mods] common script: from the mod %s (%zu bytes)\n", sFromMod.c_str(), sScript->size());
}

bool Active() { return sScript != nullptr; }

}
