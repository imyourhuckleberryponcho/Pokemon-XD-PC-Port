#include <cstdint>

extern "C" {

void (*PortXD_ScriptOnFlag)(uint32_t id, uint32_t oldValue, uint32_t newValue) = nullptr;
void (*PortXD_ScriptOnMoney)(int32_t oldValue, int32_t newValue) = nullptr;
void (*PortXD_ScriptOnCaught)(void* pokemon, int32_t slot) = nullptr;
void (*PortXD_ScriptOnMenu)(int32_t which) = nullptr;

void PortXD_RealFlagSet(uint32_t id, uint32_t value);
void PortXD_RealFlagOn(uint32_t id);
void PortXD_RealFlagOff(uint32_t id);
uint32_t GSflagGet(uint32_t id);
void PortXD_RealSetPokedoru(void* hero, int32_t value);
int32_t heroBiosGetPokedoru(void* hero);
int16_t heroCatchPokemon(void* hero, void* pokemon, uint32_t floorId, uint32_t ball, uint8_t allowPCBox);
int32_t menuFieldMain(uint8_t p0);

namespace {
bool sInFlag = false, sInMoney = false;
}

void GSflagSet(uint32_t id, uint32_t value) {
  if (PortXD_ScriptOnFlag == nullptr || sInFlag) {
    PortXD_RealFlagSet(id, value);
    return;
  }
  const uint32_t before = GSflagGet(id);
  PortXD_RealFlagSet(id, value);
  const uint32_t after = GSflagGet(id);
  if (after != before && PortXD_ScriptOnFlag != nullptr) {
    sInFlag = true;
    PortXD_ScriptOnFlag(id, before, after);
    sInFlag = false;
  }
}
void GSflagOn(uint32_t id) { GSflagSet(id, 1); }
void GSflagOff(uint32_t id) { GSflagSet(id, 0); }

void heroBiosSetPokedoru(void* hero, int32_t value) {
  if (PortXD_ScriptOnMoney == nullptr || sInMoney || hero == nullptr) {
    PortXD_RealSetPokedoru(hero, value);
    return;
  }
  const int32_t before = heroBiosGetPokedoru(hero);
  PortXD_RealSetPokedoru(hero, value);
  const int32_t after = heroBiosGetPokedoru(hero);
  if (after != before && PortXD_ScriptOnMoney != nullptr) {
    sInMoney = true;
    PortXD_ScriptOnMoney(before, after);
    sInMoney = false;
  }
}

int16_t PortXD_HeroCatchHook(void* hero, void* pokemon, uint32_t floorId, uint32_t ball, uint8_t allowPCBox) {
  const int16_t slot = heroCatchPokemon(hero, pokemon, floorId, ball, allowPCBox);
  if (PortXD_ScriptOnCaught != nullptr && slot >= -1) PortXD_ScriptOnCaught(pokemon, slot);
  return slot;
}

void PortXD_FieldMenuHook(int32_t p0) {
  if (PortXD_ScriptOnMenu != nullptr) PortXD_ScriptOnMenu(0);
  menuFieldMain(static_cast<uint8_t>(p0));
}

}
