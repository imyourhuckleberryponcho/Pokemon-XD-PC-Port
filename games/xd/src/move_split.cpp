#include <settings.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
void* wazaDataBiosGetPtr(uint16_t index);
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

static int32_t SplitKind(uint16_t type, int32_t game);
int32_t PortXD_SplitTypeKind(uint16_t type) {
  const int32_t game = zokuseiBiosGetWazaKind(type);
  const int32_t got = SplitKind(type, game);
  static const bool log = std::getenv("XD_SPLIT_LOG") != nullptr;
  if (log && SplitOn()) std::fprintf(stderr, "[split] move %u type %u: game %d, answer %d\n", static_cast<unsigned>(sAskedMove), static_cast<unsigned>(type), game, got);
  return got;
}

static int32_t SplitKind(uint16_t type, int32_t game) {
  if (SplitOn() && sAskedMove >= 375) {
    const auto* row = static_cast<const uint8_t*>(wazaDataBiosGetPtr(sAskedMove));
    if (row != nullptr && (row[0x37] == 1 || row[0x37] == 2)) {
      return row[0x37];
    }
    if (row != nullptr && (row[0x37] == 0x81 || row[0x37] == 0x82)) {
      return row[0x37] - 0x80;
    }
    return game;
  }
  if (!SplitOn() || sAskedMove < 1 || sAskedMove > sizeof kCategory) {
    return game;
  }
  const uint8_t category = kCategory[sAskedMove - 1];
  return category != 0 ? category : game;
}

}
