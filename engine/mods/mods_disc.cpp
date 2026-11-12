#include "mods_disc.h"

#include <port_disc.h>

namespace engine::mods {

namespace {
void AddFile(const char* path, uint32_t, void* user) { static_cast<std::vector<std::string>*>(user)->push_back(path); }
}

DiscView OpenDiscView() {
  DiscView view;
  view.gameId = PortDisc_GameId();
  PortDisc_ListFiles(&AddFile, &view.files);
  view.read = [](const std::string& path, std::vector<uint8_t>& out) {
    size_t size = 0;
    uint8_t* data = PortDisc_ReadOriginalFile(path.c_str(), &size);
    if (data == nullptr) {
      return false;
    }
    out.assign(data, data + size);
    PortDisc_Free(data);
    return true;
  };
  view.readProgram = [](std::vector<uint8_t>& out) {
    size_t size = 0;
    const uint8_t* dol = PortDisc_Dol(&size);
    if (dol == nullptr || size == 0) {
      return false;
    }
    out.assign(dol, dol + size);
    return true;
  };
  return view;
}

bool InstallOverlay(const LoadResult& result) {
  std::vector<std::string> localFiles;
  localFiles.reserve(result.files.size());
  std::vector<PortDiscOverlayFile> files;
  for (const Replacement& r : result.files) {
    PortDiscOverlayFile f{};
    f.path = r.discPath.c_str();
    if (r.rebuilt) {
      f.data = r.data.data();
      f.size = r.data.size();
    } else {
      const std::u8string u8 = r.file.u8string();
      localFiles.emplace_back(reinterpret_cast<const char*>(u8.data()), u8.size());
      f.localFile = localFiles.back().c_str();
    }
    files.push_back(f);
  }
  if (PortDisc_SetOverlay(files.data(), files.size())) {
    return true;
  }
  PortDisc_SetOverlay(nullptr, 0);
  return false;
}

}
