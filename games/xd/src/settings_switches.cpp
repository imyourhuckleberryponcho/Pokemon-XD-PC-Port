#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <port_video.h>
#include <settings.h>

extern "C" uint32_t PortVI_GetFlipCount(void);

namespace {
struct Switch {
  uint32_t frame;
  std::string id;
  std::string value;
};
std::vector<Switch> sSwitches;
size_t sNext = 0;
bool sRead = false;

void ReadSwitches() {
  sRead = true;
  const char* env = std::getenv("XD_SETTINGS_AT");
  if (env == nullptr) return;
  std::string all(env);
  size_t pos = 0;
  while (pos < all.size()) {
    size_t comma = all.find(',', pos);
    if (comma == std::string::npos) comma = all.size();
    const std::string item = all.substr(pos, comma - pos);
    pos = comma + 1;
    const size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    const uint32_t frame = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10));
    std::string rest = item.substr(colon + 1);
    size_t p = 0;
    while (p < rest.size()) {
      size_t semi = rest.find(';', p);
      if (semi == std::string::npos) semi = rest.size();
      const std::string kv = rest.substr(p, semi - p);
      p = semi + 1;
      const size_t eq = kv.find('=');
      if (eq != std::string::npos) sSwitches.push_back({frame, kv.substr(0, eq), kv.substr(eq + 1)});
    }
  }
}
}

extern "C" void PortXD_SettingsSwitchesUpdate(void) {
  if (!sRead) ReadSwitches();
  bool video = false;
  while (sNext < sSwitches.size() && PortVI_GetFlipCount() >= sSwitches[sNext].frame) {
    const Switch& sw = sSwitches[sNext++];
    const bool ok = engine::settings::Global().Override(sw.id, sw.value);
    std::fprintf(stderr, "[settings] game frame %u: %s=%s%s (XD_SETTINGS_AT)\n", PortVI_GetFlipCount(), sw.id.c_str(),
                 sw.value.c_str(), ok ? "" : " REFUSED");
    if (std::strncmp(sw.id.c_str(), "video.", 6) == 0) video = true;
  }
  if (video) PortVideo_ApplySettings();
}
