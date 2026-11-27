#ifndef XD_STREAM_OVERLAY_ASSETS_H
#define XD_STREAM_OVERLAY_ASSETS_H

#include <cstdint>
#include <filesystem>
#include <vector>

namespace xd::stream::assets {

void Pump(const std::filesystem::path& folder);

void Join();

struct FontTable {
  std::vector<uint8_t> bytes;
};
std::vector<std::pair<int, std::vector<uint8_t>>> BuildFonts(const std::vector<FontTable>& tables);
std::vector<uint8_t> EncodePng(int w, int h, const std::vector<uint8_t>& rgba);

}

#endif
