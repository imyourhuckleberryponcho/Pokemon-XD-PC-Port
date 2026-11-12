#include "disc_overlay.hpp"
#include "port_disc.h"

#include <aurora/dvd.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace port_disc;

namespace {

void* overlay_open(void* userData) {
  const auto& entry = *static_cast<const OverlayEntry*>(userData);
  auto* reader = new OverlayReader(entry);
  if (!reader->Ok()) {
    std::fprintf(stderr, "[disc overlay] could not open %s\n", entry.path.c_str());
    delete reader;
    return nullptr;
  }
  if (!entry.opened.exchange(true)) {
    std::fprintf(stderr, "[disc overlay] the game reads %s from a mod\n", entry.path.c_str());
  }
  return reader;
}

void overlay_close(void* handle) { delete static_cast<OverlayReader*>(handle); }

int64_t overlay_read(void* handle, uint8_t* buf, size_t len) {
  return handle != nullptr ? static_cast<OverlayReader*>(handle)->Read(buf, len) : -1;
}

int64_t overlay_seek(void* handle, int64_t offset, int32_t whence) {
  return handle != nullptr ? static_cast<OverlayReader*>(handle)->Seek(offset, whence) : -1;
}

}

extern "C" void PortDisc_InstallOverlayInAurora(void) {
  const auto& overlay = Overlay();
  if (overlay.empty()) {
    return;
  }
  static const AuroraOverlayCallbacks kCallbacks = {overlay_open, overlay_close, overlay_read, overlay_seek};
  aurora_dvd_overlay_callbacks(&kCallbacks);

  std::vector<std::string> names;
  std::vector<AuroraOverlayFile> files;
  names.reserve(overlay.size());
  for (const auto& e : overlay) {
    names.push_back("/" + e->path);
  }
  for (size_t i = 0; i < overlay.size(); i++) {
    files.push_back({names[i].c_str(), overlay[i].get(), static_cast<size_t>(overlay[i]->size)});
  }
  std::vector<s32> entryNums(files.size(), -1);
  aurora_dvd_overlay_files(files.data(), files.size(), entryNums.data());
  for (size_t i = 0; i < files.size(); i++) {
    if (entryNums[i] < 0) {
      std::fprintf(stderr, "[disc overlay] Aurora did not take %s\n", names[i].c_str());
    }
  }
  std::fprintf(stderr, "[disc overlay] %zu file(s) replaced or added\n", files.size());
}
