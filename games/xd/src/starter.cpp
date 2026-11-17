#include "xd_starter.h"

#include "xd_test_run.h"
#include "xd_xg.h"

#include <dolphin/types.h>
#include <game/pxdvs/app/hero/hero.hpp>
#include <game/pxdvs/app/memcard/savedata.hpp>
#include <game/pxdvs/app/pokemon/pokemonBios.hpp>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void savedataCreate(void* save, uint16_t* name);
uint8_t heroBiosGetSexDataId(void* hero);
uint32_t heroBiosGetRnd(void* hero);
uint16_t* heroBiosGetNamePtr(void* hero);
char16_t* GSmsgGetGSchar(uint32_t key);
}

namespace {

namespace xr = xd::randomizer;

xr::StarterPlan sPlan;
bool sSwappedLog = false;

constexpr uint16_t kFriendLevel = 0xD7;
constexpr uint32_t kEeveeExpIntoLevel = 250, kEeveeExpLevelSpan = 331;

uint32_t StartExp(uint16_t species, uint32_t level, bool intoLevel) {
  PokemonGrowDataBios* grow = pokemonGrowDataBiosGetPtr(static_cast<uint8_t>(pokemonDB_GetGrowDataId(species)));
  if (grow == nullptr) return 0;
  const uint32_t from = static_cast<uint32_t>(pokemonGrowDataBiosGetExp(grow, static_cast<uint8_t>(level)));
  const uint32_t to = static_cast<uint32_t>(pokemonGrowDataBiosGetExp(grow, static_cast<uint8_t>(level + 1)));
  if (to <= from || !intoLevel) return from;
  return from + (to - from) * kEeveeExpIntoLevel / kEeveeExpLevelSpan;
}

void SwapSlot(int slot, uint32_t species, uint32_t plannedLevel, const uint32_t* moves) {
  Hero* hero = static_cast<Hero*>(savedataGetStatus(nullptr, 2));
  if (hero == nullptr) return;
  Pokemon* orig = hero->getPokemon(slot);
  const bool xg = xd::xg::On() && orig != nullptr && orig->checkValid();
  HeroPokemonGetParam param;
  std::memset(&param, 0, sizeof param);
  param.pokemonDataID = static_cast<u16>(species);
  param.shadowPokemonDataID = 0;
  uint32_t level = plannedLevel;
  if (xg) {
    level = pokemonBiosGetLevel(orig);
    if (level != plannedLevel) std::fprintf(stderr, "[starter] XG slot %d: the game's own is level %u (the plan said %u): level %u is used\n", slot + 1, level, plannedLevel, level);
    param.friendLevel = pokemonBiosGetFriend(orig);
    param.catchFloorID = pokemonBiosGetCatchFloorId(orig);
  } else {
    param.friendLevel = kFriendLevel;
    param.catchFloorID = 0;
  }
  param.catchLevel = static_cast<u8>(level);
  for (int m = 0; m < 4; m++) param.moves[m] = static_cast<u16>(moves[m]);
  param.trainerSexID = heroBiosGetSexDataId(hero);
  param.trainerRnd = heroBiosGetRnd(hero);
  param.trainerNamePtr = reinterpret_cast<char16*>(heroBiosGetNamePtr(hero));
  param.exp = xg ? StartExp(static_cast<uint16_t>(species), level, false) : StartExp(static_cast<uint16_t>(species), level, true);
  hero->deletePokemon(slot);
  const int got = hero->pokemonGet(&param, true);
  std::fprintf(stderr, "[starter] party slot %d: species %u, level %u%s%s\n", got + 1, species, level, sSwappedLog ? " (chosen)" : "", xg ? " (XG: level, friendship and place as the game's own)" : "");
}

void Swap(uint32_t k) {
  if (k >= sPlan.count) return;
  const uint32_t species = sPlan.species[k];
  if (species == xr::kStarterEevee && !sPlan.natural) {
    std::fprintf(stderr, "[starter] the starter stays Eevee (the plan's species and moves are the game's)\n");
    return;
  }
  SwapSlot(0, species, sPlan.level, sPlan.moves[k]);
}

void SwapSecond() {
  if (!sPlan.second || !xd::xg::On()) return;
  if (sPlan.species2 == sPlan.base.species[1] && !sPlan.natural) {
    std::fprintf(stderr, "[starter] the second starter stays as the game has it\n");
    return;
  }
  SwapSlot(1, sPlan.species2, sPlan.level2, sPlan.moves2);
}

bool PickByScript() { return std::getenv("XD_MENU_SCRIPT") != nullptr && std::getenv("XD_MENU_SCRIPT")[0] != 0; }

}

namespace xd::starter {

void SetPlan(const xr::StarterPlan& plan) { sPlan = plan; }
const xr::StarterPlan& Plan() { return sPlan; }
bool Randomized() { return sPlan.on; }

std::string SpeciesName(uint32_t species) {
  auto* data = pokemonDataBiosGetPtr(static_cast<uint16_t>(species));
  const char16_t* name = data != nullptr ? GSmsgGetGSchar(pokemonDataBiosGetName(data)) : nullptr;
  std::string out;
  for (size_t i = 0; name != nullptr && name[i] != 0 && i < 24; i++) {
    const char16_t c = name[i];
    out += c < 0x80 ? static_cast<char>(c) : '?';
  }
  return out.empty() ? "#" + std::to_string(species) : out;
}

void Choose(int index) {
  sSwappedLog = true;
  Swap(static_cast<uint32_t>(index < 0 ? 0 : index));
}

}

extern "C" void PortXD_NewGameCreate(void* save, uint16_t* name) {
  savedataCreate(save, name);
  if (!sPlan.on || sPlan.count == 0) return;
  SwapSecond();
  if (sPlan.choice != 1 || sPlan.count < 2) {
    Swap(0);
    return;
  }
  const char* pick = std::getenv("XD_STARTER_PICK");
  if (pick != nullptr && pick[0] != 0) {
    sSwappedLog = true;
    Swap(static_cast<uint32_t>(std::atoi(pick)) % sPlan.count);
    return;
  }
  if (xd::TestRun() && !PickByScript()) {
    std::fprintf(stderr, "[starter] a test run: the first of the three is taken (XD_STARTER_PICK=<0..2> chooses)\n");
    sSwappedLog = true;
    Swap(0);
    return;
  }
  if (!PortXD_OpenStarterPick()) {
    std::fprintf(stderr, "[starter] the choice page could not open: the first of the three is taken\n");
    Swap(0);
  }
}
