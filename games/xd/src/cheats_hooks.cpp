#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "savestate_features.h"

extern "C" {
extern void* floordata;
int PortXD_CheatOn(int id);
int PortXD_CheatExpFactor(void);
int PortXD_CheatShinyDivisor(void);

enum { kCatch = 0, kTrainers, kBalls, kExp, kLevelCap, kMoney, kCoupons, kShiny, kIvs, kEvs, kNoDamage, kWin };

extern unsigned char xdExpLevelLimit;

void PortXD_RealDecPokedoru(void* hero, uint32_t offset);
void PortXD_RealDecPokecoupon(void* hero, int32_t offset);
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
void heroSetStatus(void* hero, uint32_t selector, uintptr_t value);
int32_t GSfloorGetCurrent(void);

void heroDecPokedoru(void* hero, uint32_t offset) {
  if (PortXD_CheatOn(kMoney) && static_cast<int32_t>(offset) > 0) {
    std::fprintf(stderr, "[cheats] payment of %u not taken%c", offset, 10);
    return;
  }
  PortXD_RealDecPokedoru(hero, offset);
}
void heroDecPokecoupon(void* hero, int32_t offset) {
  if (PortXD_CheatOn(kCoupons) && offset > 0) {
    std::fprintf(stderr, "[cheats] %d coupons not taken%c", offset, 10);
    return;
  }
  PortXD_RealDecPokecoupon(hero, offset);
}

void fightMsgctrlSetValue(uint32_t key, uintptr_t value);
uint32_t figthPokemonGetExp(void* fp);
void figthPokemonSetExp(void* fp, uint32_t exp);
uint32_t figthPokemonGetLevel(void* fp);
uint32_t fightPokemonGetLevelToExp(void* fp, uint8_t level);
void fightPokemonGetEffortFromPokemon(void* fp, uint8_t pokerus, uint16_t enemyDataId);
uint32_t pokemon_GetPoolExp(void* poke);
void pokemon_SetPoolExp(void* poke, uint32_t data);
}

namespace {
int sExpState = 2;
uint32_t sExpPlain = 0;
const xd::savestate::Feature sStateFeature(
    "cheats_hooks", [] { return std::any(std::pair<int, uint32_t>(sExpState, sExpPlain)); },
    [](const std::any& a) {
      const auto& p = std::any_cast<const std::pair<int, uint32_t>&>(a);
      sExpState = p.first;
      sExpPlain = p.second;
    },
    [](const std::any& a, portstate::Out& out) {
      const auto& p = std::any_cast<const std::pair<int, uint32_t>&>(a);
      out.pod(p.first);
      out.pod(p.second);
    },
    [](portstate::In& in, std::any& a) {
      const int first = in.pod<int>();
      const uint32_t second = in.pod<uint32_t>();
      if (!in.ok()) return false;
      a = std::pair<int, uint32_t>(first, second);
      return true;
    });
}

extern "C" {
void PortXD_ExpMsgValue(uint32_t key, uintptr_t value) {
  const int factor = PortXD_CheatExpFactor();
  if (factor > 1) {
    if (key == 0xE) {
      sExpState = 0;
    } else if (key == 0x2F && sExpState == 0) {
      sExpPlain = static_cast<uint32_t>(value);
      sExpState = 1;
      value = static_cast<uintptr_t>(sExpPlain) * static_cast<uintptr_t>(factor);
    }
  }
  fightMsgctrlSetValue(key, value);
}

void PortXD_ExpEffort(void* fp, uint8_t pokerus, uint16_t enemyDataId) {
  const int factor = PortXD_CheatExpFactor();
  if (factor > 1 && sExpState == 1 && fp != nullptr) {
    const uint32_t extra = sExpPlain * static_cast<uint32_t>(factor - 1);
    const uint32_t now = figthPokemonGetExp(fp);
    const uint32_t level = figthPokemonGetLevel(fp);
    if (level < xdExpLevelLimit) {
      const uint32_t most = fightPokemonGetLevelToExp(fp, xdExpLevelLimit);
      const uint32_t to = std::min<uint64_t>(static_cast<uint64_t>(now) + extra, std::max(most, now));
      figthPokemonSetExp(fp, to);
      std::fprintf(stderr, "[cheats] experience x%d: +%u extra (plain gain %u)\n", factor, to - now, sExpPlain);
    }
  }
  if (sExpState == 1) sExpState = 2;
  fightPokemonGetEffortFromPokemon(fp, pokerus, enemyDataId);
}

void PortXD_PoolExp(void* poke, uint32_t data) {
  const int factor = PortXD_CheatExpFactor();
  if (factor > 1 && poke != nullptr) {
    const uint32_t cur = pokemon_GetPoolExp(poke);
    if (data > cur) data = cur + (data - cur) * static_cast<uint32_t>(factor);
  }
  pokemon_SetPoolExp(poke, data);
}

int32_t PortXD_RealHeroCatch(void* hero, void* pokemon, uint32_t a3, uint32_t a4, uint8_t allowPCBox);
void pokemonBiosSetMaxHpRnd(void* p, uint16_t v);
void pokemonBiosSetPhyAtkRnd(void* p, uint16_t v);
void pokemonBiosSetPhyDefRnd(void* p, uint16_t v);
void pokemonBiosSetSpeAtkRnd(void* p, uint16_t v);
void pokemonBiosSetSpeDefRnd(void* p, uint16_t v);
void pokemonBiosSetNimblenessRnd(void* p, uint16_t v);
void pokemonBiosSetMaxHpEffort(void* p, uint16_t v);
void pokemonBiosSetPhyAtkEffort(void* p, uint16_t v);
void pokemonBiosSetPhyDefEffort(void* p, uint16_t v);
void pokemonBiosSetSpeAtkEffort(void* p, uint16_t v);
void pokemonBiosSetSpeDefEffort(void* p, uint16_t v);
void pokemonBiosSetNimblenessEffort(void* p, uint16_t v);
void pokemonResetBasisStatus(void* p);
uint16_t pokemonBiosGetPokemonDataId(void* p);
uint8_t pokemonIsDarkPokemon(void* p);
int32_t pokemon_GetHp(void* p);
int32_t pokemon_GetMaxHp(void* p);
void pokemon_SetHp(void* p, uint16_t v);

int32_t heroCatchPokemon(void* hero, void* pokemon, uint32_t a3, uint32_t a4, uint8_t allowPCBox) {
  if ((PortXD_CheatOn(kIvs) || PortXD_CheatOn(kEvs)) && pokemon != nullptr && pokemonBiosGetPokemonDataId(pokemon) != 0 &&
      pokemonIsDarkPokemon(pokemon) == 0 && heroGetStatus(hero, 3, 0) == heroGetStatus(nullptr, 3, 0)) {
    const int32_t hp = pokemon_GetHp(pokemon), maxHp = pokemon_GetMaxHp(pokemon);
    if (PortXD_CheatOn(kIvs)) {
      pokemonBiosSetMaxHpRnd(pokemon, 31); pokemonBiosSetPhyAtkRnd(pokemon, 31); pokemonBiosSetPhyDefRnd(pokemon, 31);
      pokemonBiosSetSpeAtkRnd(pokemon, 31); pokemonBiosSetSpeDefRnd(pokemon, 31); pokemonBiosSetNimblenessRnd(pokemon, 31);
    }
    if (PortXD_CheatOn(kEvs)) {
      pokemonBiosSetMaxHpEffort(pokemon, 100); pokemonBiosSetPhyAtkEffort(pokemon, 100); pokemonBiosSetPhyDefEffort(pokemon, 100);
      pokemonBiosSetSpeAtkEffort(pokemon, 100); pokemonBiosSetSpeDefEffort(pokemon, 100); pokemonBiosSetNimblenessEffort(pokemon, 100);
    }
    pokemonResetBasisStatus(pokemon);
    const int32_t newMax = pokemon_GetMaxHp(pokemon);
    if (maxHp > 0 && hp > 0) pokemon_SetHp(pokemon, static_cast<uint16_t>(std::max<int64_t>(1, static_cast<int64_t>(hp) * newMax / maxHp)));
    std::fprintf(stderr, "[cheats] received Pokemon %u: IVs %s, EVs %s\n", pokemonBiosGetPokemonDataId(pokemon), PortXD_CheatOn(kIvs) ? "31" : "as drawn",
                 PortXD_CheatOn(kEvs) ? "100" : "as drawn");
  }
  return PortXD_RealHeroCatch(hero, pokemon, a3, a4, allowPCBox);
}

uint8_t fightFloor_GetFightKindBackSaveDataFlag(void* floor);
uint8_t fightFloorIsNowEncountSnatchOnly(void* floor);
uint8_t fightIsFight(void);

uint32_t PortXD_MenuIsDark(void* pokemon) {
  const uint32_t real = pokemonIsDarkPokemon(pokemon);
  if (real != 0 || !PortXD_CheatOn(kTrainers)) return real;
  if (fightIsFight() == 0 || fightFloor_GetFightKindBackSaveDataFlag(nullptr) == 0 || fightFloorIsNowEncountSnatchOnly(nullptr) != 0) return real;
  return 1;
}

void WS_ADD_POKE(void);
uint8_t fightFloor_GetFightKind(void* floor);
void* fightTargetGetPtrAsNowFightType(uint32_t kind, uint32_t arg);
}

namespace {
bool sForceWild = false;
}

extern "C" {
void* fightOutPokemon_GetMotoFightPokemonPtr(void* fop);
void* fightPokemonGetPokemonPtr(void* fp);

void PortXD_WsAddPoke(void) {
  if (PortXD_CheatOn(kTrainers) && fightFloor_GetFightKind(nullptr) != 0xC) {
    void* target = fightTargetGetPtrAsNowFightType(0x12, 0);
    void* pokemon = target != nullptr ? fightPokemonGetPokemonPtr(fightOutPokemon_GetMotoFightPokemonPtr(target)) : nullptr;
    if (pokemon != nullptr && pokemonIsDarkPokemon(pokemon) == 0 && fightFloor_GetFightKindBackSaveDataFlag(nullptr) != 0) {
      std::fprintf(stderr, "[cheats] a trainer's Pokemon (species %u) is caught\n", pokemonBiosGetPokemonDataId(pokemon));
      sForceWild = true;
      WS_ADD_POKE();
      sForceWild = false;
      return;
    }
  }
  WS_ADD_POKE();
}
uint32_t PortXD_ItemFightKind(void* floor) { return sForceWild ? 0xC : fightFloor_GetFightKind(floor); }

void* fightSeq_GetWazaSeq(void);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
void* fightOutPokemonGetPokemonPtr(void* fop);
int32_t fightWaza_GetDamage(void* waza);
void fightWaza_SetDamage(void* waza, int32_t value);
bool fightWazaIsHit(void* waza);
uint8_t fightFloor_GetFightType(void* floor);
uint8_t fightTargetIsHostSide(void* target, uint32_t type);

void PortXD_CheatHit(void) {
  const bool noDamage = PortXD_CheatOn(kNoDamage) != 0, win = PortXD_CheatOn(kWin) != 0;
  if (!noDamage && !win) return;
  const uint8_t* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
  void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
  void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
  void* target = cmd != nullptr ? fightTargetGetPtrAsNowFightType(cmd[1], 0) : nullptr;
  if (attacker == nullptr || defender == nullptr || attacker == defender || target != defender) return;
  void* waza = fightOutPokemon_GetFightWazaPtr(attacker);
  if (waza == nullptr || !fightWazaIsHit(waza)) return;
  const int32_t damage = fightWaza_GetDamage(waza);
  if (damage <= 0) return;
  const uint32_t type = fightFloor_GetFightType(nullptr);
  const bool defenderMine = fightTargetIsHostSide(defender, type) == 1, attackerMine = fightTargetIsHostSide(attacker, type) == 1;
  if (noDamage && defenderMine) {
    fightWaza_SetDamage(waza, 0);
    return;
  }
  if (win && attackerMine && !defenderMine) {
    void* pokemon = fightOutPokemonGetPokemonPtr(defender);
    if (pokemon == nullptr) return;
    const int32_t hp = pokemon_GetHp(pokemon);
    const int32_t want = pokemonIsDarkPokemon(pokemon) != 0 ? std::max<int32_t>(0, hp - 1) : hp;
    if (want > damage) fightWaza_SetDamage(waza, std::min<int32_t>(want, 9999));
  }
}

void PortXD_CheatTopUp(void) {
  if (!PortXD_CheatOn(kMoney) && !PortXD_CheatOn(kCoupons) && !PortXD_CheatOn(kNoDamage)) return;
  if (floordata == nullptr || GSfloorGetCurrent() == 0) return;
  if (PortXD_CheatOn(kMoney) && heroGetStatus(nullptr, 0xC, 0) < 999999u) heroSetStatus(nullptr, 0xC, 999999u);
  if (PortXD_CheatOn(kCoupons) && heroGetStatus(nullptr, 0xD, 0) < 99999u) heroSetStatus(nullptr, 0xD, 99999u);
  if (PortXD_CheatOn(kNoDamage) && fightIsFight() != 0) {
    for (uint16_t slot = 0; slot < 6; slot++) {
      void* p = reinterpret_cast<void*>(heroGetStatus(nullptr, 3, slot));
      if (p == nullptr || pokemonBiosGetPokemonDataId(p) == 0) continue;
      const int32_t hp = pokemon_GetHp(p), maxHp = pokemon_GetMaxHp(p);
      if (hp > 0 && hp < maxHp) pokemon_SetHp(p, static_cast<uint16_t>(maxHp));
    }
  }
}

int PortXD_CheatIgnoresLevelCap(void) { return PortXD_CheatOn(kLevelCap); }
}
