#include <settings.h>

#include <cstdint>

extern "C" {
uint32_t wazaCheckDarkWaza(uint16_t waza);
int32_t zokuseiBiosGetWazaKind(uint16_t type);
}

namespace {

constexpr uint8_t kCategory[354] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 1, 2, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,
    1, 1, 0, 1, 0, 0, 0, 0, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0, 0, 1, 2, 0, 0, 0, 2,
    0, 2, 2, 2, 2, 0, 2, 1, 1, 1, 1, 0, 2, 2, 0, 0, 0, 1, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1,
    1, 1, 2, 2, 1, 2, 1, 1, 2, 1, 1, 1, 0, 0, 0, 1, 0, 2, 0, 1, 1, 0, 1, 0, 2, 1, 0, 0, 2, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0,
    2, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 1, 0, 0, 2, 2, 2, 0, 2, 0, 0, 0, 2, 0, 1, 0, 1,
    0, 2, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 2, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 2, 1, 2, 0,
    0, 1, 2, 0, 1, 2, 2, 2, 1, 2, 1, 1, 2, 0, 2, 0, 2, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1,
    0, 1, 1, 2, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 2, 2, 0, 0, 1, 0, 1, 1, 0, 2, 1, 1, 2, 2, 1, 1, 2, 0, 0, 2, 2, 0, 1, 2, 0, 0,
    0, 0, 2, 2, 1, 2, 1, 1, 2, 2, 1, 1, 1, 0, 0, 0, 1, 2, 0, 1, 2, 1, 1, 1, 2, 0, 0, 1, 0, 1, 2, 2, 2, 2
};

uint16_t sAskedMove = 0;

bool SplitOn() {
  static bool ready = false;
  static engine::settings::BoolSetting setting;
  if (!ready) {
    ready = true;
    setting = engine::settings::Global().Bool("gameplay.physical_special_split");
  }
  return setting.Valid() && setting.Get();
}

}

extern "C" {

uint32_t PortXD_SplitCheckDarkWaza(uint16_t waza) {
  sAskedMove = waza;
  return wazaCheckDarkWaza(waza);
}

int32_t PortXD_SplitTypeKind(uint16_t type) {
  const int32_t game = zokuseiBiosGetWazaKind(type);
  if (!SplitOn() || sAskedMove < 1 || sAskedMove > sizeof kCategory) {
    return game;
  }
  const uint8_t category = kCategory[sAskedMove - 1];
  return category != 0 ? category : game;
}

}
