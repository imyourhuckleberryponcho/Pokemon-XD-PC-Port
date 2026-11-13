#include "xd_challenges.h"
#include "xd_gen4.h"

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
}

namespace {

struct EvolutionInfo {
  uint8_t kind;
  uint8_t reserved;
  uint16_t parameter;
};

constexpr uint8_t kLevelHolding = 17, kUseFemale = 18, kUseMale = 19;
constexpr uint16_t kEverstoneEffect = 0x26;

std::vector<xd::gen4::ExtraEvolution> sList;
int32_t sCord = -1;

bool Gen4On() { return xd::challenges::Active("gen4_rules"); }

}

namespace xd::gen4 {

void SetExtraEvolutions(const std::vector<ExtraEvolution>& list, int32_t linkingCord) {
  sList = list;
  sCord = linkingCord;
}

}

extern "C" uint16_t pokemonEvolutionCheck(void* pokemon, int32_t mode, uint16_t item, uint16_t* extra, void* infoPtr) {
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
    default:
      break;
    }
    if (!fits) continue;
    result = e.target;
    info->kind = reportKind;
    info->parameter = e.param;
  }
  return result;
}
