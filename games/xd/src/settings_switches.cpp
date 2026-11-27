#include "xd_test_run.h"
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

extern "C" void AuroraSetSkipDraws(int on);
namespace {
bool sNoDrawRead = false, sNoDrawOn = false;
uint32_t sNoDrawFrom = 0, sNoDrawTo = 0;
void NoDrawPoll() {
  if (!sNoDrawRead) {
    sNoDrawRead = true;
    const char* v = std::getenv("XD_TEST_NO_DRAW");
    if (v != nullptr && *v != '\0' && xd::TestRunSwitchSet([](const char* n) { return std::getenv(n); }) != nullptr) {
      unsigned a = 0, b = 0;
      if (std::sscanf(v, "%u-%u", &a, &b) == 2 && b > a) {
        sNoDrawFrom = a;
        sNoDrawTo = b;
        std::fprintf(stderr, "[test no draw] no drawing from game frame %u to %u (XD_TEST_NO_DRAW)\n", a, b);
      }
    }
  }
  if (sNoDrawTo == 0) return;
  const uint32_t now = PortVI_GetFlipCount();
  const bool want = now >= sNoDrawFrom && now < sNoDrawTo;
  if (want != sNoDrawOn) {
    sNoDrawOn = want;
    AuroraSetSkipDraws(want ? 1 : 0);
    std::fprintf(stderr, "[test no draw] game frame %u: drawing %s\n", now, want ? "OFF" : "on again");
  }
}
}

extern "C" void PortXD_SettingsSwitchesUpdate(void) {
  NoDrawPoll();
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
