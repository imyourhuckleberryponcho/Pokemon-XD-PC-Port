#include "xd_challenges.h"
#include "xd_forms.h"
#include "xd_gen4.h"
#include "xd_gen_rules.h"

#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" {
uint16_t PortXD_RealEvolutionCheck(void* pokemon, int32_t mode, uint16_t item, uint16_t* extra, void* info);
uint8_t pokemonCheckValid(void* pokemon);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
uint16_t pokemonBiosGetItemDataId(void* pokemon);
uint16_t pokemonGetSoubiItemDataId(void* pokemon);
void* itemDataBiosGetPtr(uint16_t item);
uint16_t itemDataBiosGetItemSoubiDataId(void* itemData);
uint32_t pokemonGetSex(void* pokemon);
uint8_t pokemonBiosGetLevel(void* pokemon);
int8_t pokemonSearchWazaDataId(void* pokemon, uint16_t move);
void* pokemonDataBiosGetPtr(uint16_t species);
uint8_t pokemonDataBiosGetZokuseiDataId(void* data, uint16_t index);
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
}

namespace {

struct EvolutionInfo {
  uint8_t kind;
  uint8_t reserved;
  uint16_t parameter;
};

constexpr uint8_t kLevelHolding = 17, kUseFemale = 18, kUseMale = 19;
constexpr uint8_t kLevelDark = 20, kLevelFemale = 21, kKnowsMove = 22;
constexpr uint8_t kLevelHoldingRules = 24;
constexpr uint8_t kLevelOnly = 23;
constexpr uint8_t kDarkType = 17;
constexpr uint16_t kEverstoneEffect = 0x26;

std::vector<xd::gen4::ExtraEvolution> sList;
int32_t sCord = -1;

bool DarkInParty() {
  for (uint16_t slot = 0; slot < 6; slot++) {
    void* other = reinterpret_cast<void*>(heroGetStatus(nullptr, 3, slot));
    if (other == nullptr || !(pokemonCheckValid(other) & 0xFF)) continue;
    const uint16_t sp = pokemonBiosGetPokemonDataId(other);
    void* data = sp != 0 ? pokemonDataBiosGetPtr(sp) : nullptr;
    if (data != nullptr && (pokemonDataBiosGetZokuseiDataId(data, 0) == kDarkType || pokemonDataBiosGetZokuseiDataId(data, 1) == kDarkType)) return true;
  }
  return false;
}

bool Gen4On() { return xd::challenges::Active("gen4_pokemon"); }

}

namespace xd::gen4 {

void SetExtraEvolutions(const std::vector<ExtraEvolution>& list, int32_t linkingCord) {
  sList = list;
  sCord = linkingCord;
}

const std::vector<ExtraEvolution>& ExtraEvolutions() { return sList; }

void AddExtraEvolution(const ExtraEvolution& e) {
  for (const ExtraEvolution& x : sList) if (x.species == e.species && x.kind == e.kind && x.param == e.param && x.target == e.target) return;
  sList.push_back(e);
}

}

static uint16_t Gen4EvolutionCheck(void* pokemon, int32_t mode, uint16_t item, uint16_t* extra, void* infoPtr) {
  if (!Gen4On() || (sList.empty() && sCord < 0)) return PortXD_RealEvolutionCheck(pokemon, mode, item, extra, infoPtr);
  const bool cord = mode == 1 && sCord >= 0 && item == static_cast<uint16_t>(sCord);
  const int32_t askMode = cord ? 2 : mode;
  uint16_t result = PortXD_RealEvolutionCheck(pokemon, askMode, item, extra, infoPtr);
  if (!(pokemonCheckValid(pokemon) & 0xFF)) return result;
  const uint16_t heldFor = pokemonGetSoubiItemDataId(pokemon);
  if (heldFor != 0 && itemDataBiosGetItemSoubiDataId(itemDataBiosGetPtr(heldFor)) == kEverstoneEffect) return result;
  const uint16_t species = pokemonBiosGetPokemonDataId(pokemon);
  auto* info = static_cast<EvolutionInfo*>(infoPtr);
  for (const xd::gen4::ExtraEvolution& e : sList) {
    if (e.species != species) continue;
    bool fits = false;
    uint8_t reportKind = e.kind;
    switch (e.kind) {
    case 5:
      fits = askMode == 2;
      break;
    case 6:
      fits = askMode == 2 && pokemonBiosGetItemDataId(pokemon) == e.param;
      break;
    case 7:
      fits = askMode == 1 && item == e.param;
      break;
    case kUseFemale:
      fits = askMode == 1 && item == e.param && (pokemonGetSex(pokemon) & 0xFF) == 1;
      reportKind = 7;
      break;
    case kUseMale:
      fits = askMode == 1 && item == e.param && (pokemonGetSex(pokemon) & 0xFF) == 0;
      reportKind = 7;
      break;
    case kLevelHolding:
      fits = askMode == 0 && pokemonBiosGetItemDataId(pokemon) == e.param;
      reportKind = 6;
      break;
    case kLevelHoldingRules:
      fits = askMode == 0 && xd::rulegen::AtLeast(5) && pokemonBiosGetItemDataId(pokemon) == e.param;
      reportKind = 6;
      break;
    case kLevelDark:
      fits = askMode == 0 && pokemonBiosGetLevel(pokemon) >= e.param && DarkInParty();
      reportKind = 4;
      break;
    case kLevelFemale:
      fits = askMode == 0 && pokemonBiosGetLevel(pokemon) >= e.param && (pokemonGetSex(pokemon) & 0xFF) == 1;
      reportKind = 4;
      break;
    case kLevelOnly:
      fits = askMode == 0 && pokemonBiosGetLevel(pokemon) >= e.param;
      reportKind = 4;
      break;
    case kKnowsMove:
      fits = askMode == 0 && pokemonSearchWazaDataId(pokemon, e.param) >= 0;
      reportKind = 4;
      break;
    default:
      break;
    }
    if (!fits) continue;
    result = e.target;
    info->kind = reportKind;
    info->parameter = e.kind == kKnowsMove ? 0 : e.param;
  }
  return result;
}

extern "C" uint16_t pokemonEvolutionCheck(void* pokemon, int32_t mode, uint16_t item, uint16_t* extra, void* infoPtr) {
  const uint16_t result = Gen4EvolutionCheck(pokemon, mode, item, extra, infoPtr);
  if (result == 0 || !xd::forms::Loaded()) return result;
  return static_cast<uint16_t>(xd::forms::AlolaEvolution(pokemonBiosGetPokemonDataId(pokemon), pokemonGetSoubiItemDataId(pokemon), result));
}
