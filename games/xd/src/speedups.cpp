#include <port_speedups.h>
#include <settings.h>

#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <port_seh.h>
#else
#include <port_win_compat.h>
#endif

extern "C" {
uint32_t timeGetLastFrameTime(void);
void spriteSetEnv(int32_t layer);
uint8_t fightIsFight(void);
uint16_t wazaSequenceSysGetActiveWazaNum(void);
}

namespace {

namespace es = engine::settings;

constexpr uint32_t kInstant = 500;

struct TextSpeed {
  bool ready = false;
  es::EnumSetting setting;
  int32_t x2 = -1, x3 = -1, instant = -1;
} sText;

bool sPrinterNext = false;

uint32_t TextMultiplier() {
  if (!sText.ready) {
    sText.ready = true;
    const es::Registry& r = es::Global();
    sText.setting = r.Enum("gameplay.text_speed");
    sText.x2 = r.ChoiceIndex("gameplay.text_speed", "2x");
    sText.x3 = r.ChoiceIndex("gameplay.text_speed", "3x");
    sText.instant = r.ChoiceIndex("gameplay.text_speed", "instant");
  }
  if (!sText.setting.Valid()) return 1;
  const int32_t v = sText.setting.Get();
  return v == sText.x2 ? 2 : v == sText.x3 ? 3 : v == sText.instant ? kInstant : 1;
}

int SafeBattleAnimation() {
  PORT_SEH_TRY {
    return fightIsFight() != 0 && wazaSequenceSysGetActiveWazaNum() > 0 ? 1 : 0;
  } PORT_SEH_EXCEPT {
    return 0;
  }
}

struct BattleAnims {
  bool ready = false;
  es::BoolSetting setting;
} sAnims;

}

extern "C" {

void PortXD_MsgSpriteSetEnv(int32_t layer) {
  sPrinterNext = true;
  spriteSetEnv(layer);
}

uint32_t PortXD_MsgFrameTime(void) {
  const uint32_t frames = timeGetLastFrameTime();
  if (!sPrinterNext) {
    return frames;
  }
  sPrinterNext = false;
  const uint32_t m = TextMultiplier();
  if (m == 1) {
    return frames;
  }
  static uint32_t sNoted = 0;
  if (sNoted != m) {
    sNoted = m;
    PortSpeedups_Used(m == 2 ? "text speed 2x" : m == 3 ? "text speed 3x" : "text speed instant");
  }
  return (frames == 0 ? 1 : frames) * m;
}

void PortXD_SpeedupsFrame(void) {
  if (!sAnims.ready) {
    sAnims.ready = true;
    sAnims.setting = es::Global().Bool("gameplay.fast_battle_animations");
  }
  const bool anim = sAnims.setting.Get() && SafeBattleAnimation() != 0;
  PortSpeedups_Frame(anim);
}

}
