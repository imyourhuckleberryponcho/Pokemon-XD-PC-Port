#include "xd_mod_battle.h"

#include "data/xd_battle_defs.h"
#include "xd_forms.h"
#include "xd_gen57.h"
#include "xd_hidden_ability.h"
#include "xd_modern_types.h"
#include "xd_xg.h"
#include "xd_scripts.h"

#include <mod_layout.h>
#include <mods.h>
#include <script_names.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <i18n.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <map>
#include <set>

extern "C" {
extern uint8_t ServerWork[8];
extern uint32_t ServerStatusFlag;
extern uint32_t pokemon_tokusei_data_number;
extern uint8_t pokemon_tokusei_data[];
uint32_t HSD_Rand(void);
int16_t wazaDB_GetIryoku(uint16_t waza);
uint8_t wazaDB_GetZokuseiDataId(uint16_t waza);
uint8_t wazaDB_GetDageki(uint16_t waza);
uint8_t wazaDB_GetBouon(uint16_t waza);
uint8_t wazaDB_GetMajikku(uint16_t waza);
uint32_t wazaDB_GetAddFightKoukaAvg(uint16_t waza);
void* wazaDataBiosGetPtr(uint16_t index);
uint16_t wazaDB_GetSeqId(uint16_t waza);
int32_t wazaDB_GetPri(uint16_t waza);
uint16_t fightOutPokemonGetCmpNimblenessWazaDataId(void* fop);
void* fightTargetGetPtrAsNowFightType(uint32_t kind, uint32_t arg);
void* fightFloorGetFightOutBanmeToValidFightOutPokemon(void* floor, uint8_t banme);
uint16_t fightOutPokemon_GetUseWazaDataId(void* fop);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t fightOutPokemonGetTokuseiDataId(void* fop);
uint16_t fightOutPokemonGetSoubiItemDataId(void* fop);
uint16_t fightOutPokemonDoItemSoubi(void* fop, uint16_t item, uint8_t mode, uint8_t update);
uint8_t fightOutPokemonCheckFightOut(void* fop);
uint8_t fightOutPokemonIsHpMantan(void* fop);
uint8_t fightOutPokemonIsJoutaiNormal(void* fop);
uint8_t fightOutPokemonCheckWriteJoutaiDataId(void* fop, uint16_t id);
void fightOutPokemonWriteJoutaiDataId(void* fop, uint16_t id, uint32_t value);
void fightOutPokemonInitJoutaiDataId(void* fop, uint16_t id);
uint16_t fightOutPokemonGetZokuseiDataId(void* fop, uint8_t index);
uint8_t fightWaza_GetCritical(void* waza);
void fightWaza_SetCritical(void* waza, uint32_t value);
uint16_t fightOutPokemonMaxHpWaruValue(void* fop, uint8_t divisor);
uint8_t fightOutPokemon_GetPassPpdecFlag(void* fop);
uint8_t fightWazaIsHit(void* fw);
void fightWaza_SetDamage(void* fw, int32_t damage);
uint16_t pokemon_GetHp(void* pokemon);
uint16_t pokemon_GetMaxHp(void* pokemon);
void pokemon_SetHp(void* pokemon, uint16_t hp);
uint16_t zokuseiGetWazaJoutai(uint16_t wazaType, uint16_t defenderType);
uint32_t fightFloorGetNowTenkouDataId(void* floor, uint8_t now);
uint16_t fightFloor_GetTurnCount(void* floor);
uint8_t fightFloorCheckWriteJoutaiDataId(void* floor, uint16_t id);
void fightFloorWriteJoutaiDataId(void* floor, uint16_t id, uint16_t value);
void fightFloor_SetAttackPokemonPtr(void* floor, void* fop);
void fightFloor_SetAppointPokemonPtr(void* floor, void* fop);
uint16_t fightFloorGetFightOutPokemonPtrAry(void* floor, uint8_t fightOut, uint8_t mode, void* fop, void** ary);
uint8_t fightFloorLoopValidFightOutPokemon(void* floor, int32_t (*func)(void*, uint16_t, void*), void* data, uint8_t useAry);
uint8_t fightOutPokemonIsGcHeroFightOutPokemon(void* fop);
void fightSeq_CallWazaSeq(void* script);
void fightSeq_JumpWazaSeq(void* script);
void fightSeq_NextWazaSeq(int32_t size);
void* fightSeq_GetWazaSeq(void);
void wazaSequenceSysFreeAllWaza(void);
void fightMsgctrlSetValue(int32_t id, uintptr_t value);
uint8_t fightSeqOpenMsg(uint32_t msg);
void fightMainWaitFrame(uint32_t frames);
void fightMenuCloseMsg(void);
uint32_t PortXD_SplitCheckDarkWaza(uint16_t waza);
int32_t PortXD_SplitTypeKind(int32_t type);
uint8_t pokemonBiosGetLevel(void* poke);
void PortXD_PokemonSetLevelStats(void* poke, uint8_t level);
uint32_t pokemon_GetExp(void* pokemon);
void pokemon_SetExp(void* pokemon, uint32_t exp);
uint32_t pokemonGetLevelToExp(void* pokemon, uint8_t level);
void* fightTrainerGetValidFightPokemonPtr(void* trainer, uint16_t index);
void* fightPokemon_GetPokemonBuffPtr(void* fp);
void fightPokemon_SetLevelUpFlag(void* fp, uint8_t flag);
void* fightPokemon_GetMotoPokemonPtr(void* fp);
void fightTrainerBackFightPokemonToTemotiPokemon(void* trainer, uint8_t restoreItem);
void* PortXD_GameAddress(uint32_t gcAddress);
void PortXD_AddScriptBlock(uint32_t gcAddress, void* bytes, uint32_t size);
uint16_t pokemonDB_GetWeight(uint32_t species);
uint16_t figthOutPokemonGetPokemonDataId(void* fop);
uint32_t fightOutPokemonGetNowNimbleness(void* fop, uint8_t useBadge, uint8_t weather, uint16_t rnd, void* hero);
uint8_t fightOutPokemonBiosGetAbicntPhyAtk(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntPhyDef(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntSpeAtk(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntSpeDef(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntNimbleness(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntAverage(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntAvoid(uint8_t* p);
void fightOutPokemonBiosSetAbicntPhyAtk(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntPhyDef(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntSpeDef(uint8_t* p, uint8_t v);
uint8_t fightOutPokemonIsJoutaiDataId(void* fop, uint16_t id);
void fightOutPokemonSetTokuseiDataId(void* fop, uint16_t id);
uint16_t pokemon_GetPhyAtk(void* pokemon);
uint16_t pokemon_GetPhyDef(void* pokemon);
uint16_t pokemon_GetSpeAtk(void* pokemon);
uint16_t pokemon_GetSpeDef(void* pokemon);
void WS_BADGECHECK(void);
void WS_GUARDCHECK(void);
void WS_CONDCHG(void);
uint8_t wazaDB_GetMamoru(uint16_t waza);
void WS_TSUIKA_CHECK(void);
void WS_SIZENKAIHUKU(void);
uint16_t fightOutPokemon_GetUsedItemDataId(void* fop);
void fightFloor_SetDefPokemonAndAtkFightWazaTargetId(void* floor, void* fop);
void fightOutPokemonSetIdBuffJoutaiDataId(void* fop, uint16_t id, uint16_t value);
void WS_HENSHIN(void);
void WS_CHECK_TYPE(void);
uint8_t fightFloorCheckFightActionFightOutPokemonIrekaeSelect(void* floor, void* pokemon, void** out);
void WS_WAZAEND(void);
uint32_t fightFloor_GetEncountDataId(void* floor);
void* fightEncountDataBiosGetPtr(uint16_t id);
uint32_t fightEncountDataBiosGetFightKind(void* enc);
void fightWazaSetUseWazaStatus(void* fw, uint16_t waza);
void* fightSeqSelectDefensePokemonPtr(void* atk, uint16_t waza, uint32_t type, uint32_t a, uint32_t b, uint32_t c, int32_t d);
uint8_t fightFloor_GetFightType(int32_t floor);
extern uint32_t ServerStatusFlag;
uint8_t fightOutPokemonIsZokuseiDataId(void* fop, uint16_t type);
const void* fightOutPokemonGetNamePtr(void* fop);
}

namespace xd::modbattle {
namespace {

namespace em = engine::mods;
namespace xb = xd::battle;

constexpr uint32_t kBank = 0x7D0u << 20;
constexpr uint32_t kAbilityText = 0x8000, kMessageText = 0xB000;
constexpr uint32_t kScriptBase = 0x8F002000u;
constexpr uint32_t kWazaEnd = 0x8041410Fu, kWazaEffectTbl = 0x80417EDFu, kPlainHit = 0x80414090u, kBatonPass = 0x80415722u;
constexpr uint32_t kWazaSinkuro = 0x80417C25u, kNoHit = 0x8041412Du, kFailedWithMessage = 0x80415C91u;
constexpr uint32_t kAmeukezara = 0x8041791Cu, kDokudoku = 0x804174FDu;
constexpr uint32_t kRain = 0x8041783Cu, kSand = 0x80417858u, kSun = 0x80417874u;
constexpr int kSlot = 163;
constexpr uint8_t kPortOp = 0x53, kPortMark = 0xF1;
enum : uint8_t { kCmdDispatch = 1, kCmdSetup = 2, kCmdAfter = 3, kCmdApply = 4, kCmdAbsorb = 5, kCmdForce = 6, kCmdVanish = 7, kCmdUnvanishA = 8, kCmdUnvanishB = 9, kCmdDance = 10 };
constexpr uint8_t kSelf = 0x40;
constexpr size_t kBlockSize = 0x2000;
constexpr uint32_t kOffDispatch = 0x000, kOffHit = 0x040, kOffHitSwitch = 0x120, kOffSelf = 0x200, kOffFoe = 0x240, kOffAbsorb = 0x280,
                   kOffHail = 0x2C0, kOffHitForce = 0x300, kOffFoeSwitch = 0x3A0, kOffScripts = 0x420;
constexpr uint32_t kRoarSeq = 28;
constexpr int kMaxScripts = (kBlockSize - kOffScripts) / 0x40;
constexpr uint32_t kScriptBase2 = 0x8F004000u;
constexpr size_t kBlock2Size = 0x2000;
constexpr uint32_t kOff2Switch = 0x000, kOff2Dance = 0x0C0, kOff2Charge = 0x100;
constexpr size_t kChargeStride = 0x300, kChargeCopy = 0x240;
constexpr size_t kVanishCopy = 0x13A;
constexpr uint32_t kVanishAfterA = 0x804140E6u, kVanishAfterB = 0x8041412Du;
constexpr int kMaxCharge = 8;

xb::Defs sDefs;
bool sLoaded = false;
uint64_t sDigest = 0;
std::map<int64_t, const xb::MoveDef*> sMoves;
const xb::AbilityDef* sAbility[256];
std::vector<std::string> sMessages;
std::map<const xb::Block*, int> sBlockScript;
int sFreeScript = 0;
uint8_t sScripts[kBlockSize];
uint8_t sScripts2[kBlock2Size];
bool sInstalled = false;
std::map<int64_t, int> sChargeIndex;
int sMsgDisabled = -1, sMsgTransformed = -1;

void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
uint32_t GetBe32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
void Log(const char* what, const std::string& text) { std::fprintf(stderr, "[mod battle] %s%s\n", what, text.c_str()); }

struct Mon {
  void* fop = nullptr;
  int entry = 0;
  int type = -1;
  int protect = 0;
  bool berryNoted = false;
  int charging = 0;
  bool heating = false;
  bool shellArmed = false, shellHit = false;
};
Mon sMons[16];
int sTerrain = 0;
int sTerrainEnd = -1;
xb::Absorb sImmune;
int sMsgImmune = -1;
int sVeilEnd[2] = {-1, -1};
bool sWeb[2] = {false, false};
int sMsgVeil = -1, sMsgVeilEnd = -1, sMsgWeb = -1, sMsgWebHit = -1;
int sMsgTerrain[5] = {-1, -1, -1, -1, -1}, sMsgTerrainEnd[5] = {-1, -1, -1, -1, -1};
xb::Block sSpikyHurt;
xb::Block sDisguiseHurt;
xb::Block sTerrainHeal;
std::vector<int> sFieldEnd;
int sLastTurn = -1;
int32_t sDealt = 0;
void* sDealtTo = nullptr;
void* sDropReact = nullptr;
const xb::AbilityDef* sAbsorbing = nullptr;
const xb::Absorb* sAbsorbEntry = nullptr;
void* sBounceFop = nullptr;
void* sPendingSwitch = nullptr;
uint16_t sDanceMove = 0;
void* sSwitchKeepAtk = nullptr;
void* sCorrode = nullptr;
bool sAnyUnnerve = false, sAnyWind = false, sAnyInfiltrator = false, sAnyDancer = false;
const xb::RuleSet* sRules = nullptr;
bool sRuleWeatherDone = false;

Mon& MonOf(void* fop) {
  for (auto& m : sMons) if (m.fop == fop) return m;
  for (auto& m : sMons) if (m.fop == nullptr) { m.fop = fop; m.entry = sLastTurn < 0 ? 0 : sLastTurn; return m; }
  sMons[0] = Mon();
  sMons[0].fop = fop;
  return sMons[0];
}

void ResetBattle() {
  for (auto& m : sMons) m = Mon();
  for (auto& f : sFieldEnd) f = -1;
  sTerrain = 0; sTerrainEnd = -1;
  sVeilEnd[0] = sVeilEnd[1] = -1; sWeb[0] = sWeb[1] = false;
  sDealt = 0; sDealtTo = nullptr; sRuleWeatherDone = false; sBounceFop = nullptr; sPendingSwitch = nullptr; sSwitchKeepAtk = nullptr;
}

void Tick() {
  const int turn = fightFloor_GetTurnCount(nullptr);
  if (turn < sLastTurn) ResetBattle();
  sLastTurn = turn;
}

void* Target(uint32_t kind) { return fightTargetGetPtrAsNowFightType(kind, 0); }
bool Alive(void* f) { return f != nullptr && fightOutPokemonCheckFightOut(f) != 0; }
bool sRawAbility = false;
const xb::AbilityDef* AbilityOf(void* fop) {
  if (fop == nullptr) return nullptr;
  sRawAbility = true;
  const uint16_t id = fightOutPokemonGetTokuseiDataId(fop);
  sRawAbility = false;
  return id < 256 ? sAbility[id] : nullptr;
}
int Weather() { return static_cast<int>(fightFloorGetNowTenkouDataId(nullptr, 1)); }
bool OnPlayersSide(void* fop) { return fightOutPokemonIsGcHeroFightOutPokemon(fop) != 0; }

void Say(int id, void* mon, uint32_t frames = 0x60) {
  if (id < 0) return;
  fightMsgctrlSetValue(0x10, reinterpret_cast<uintptr_t>(mon));
  fightSeqOpenMsg(kBank | (kMessageText + static_cast<uint32_t>(id)));
  if (ServerWork[7] == 1) {
    fightMainWaitFrame(frames);
    fightMenuCloseMsg();
    ServerWork[7] = 0;
  }
}

int TerrainNow() { return sTerrain != 0 && sTerrainEnd >= sLastTurn ? sTerrain : 0; }
bool Grounded(void* fop);

struct Cx {
  void* holder = nullptr;
  void* other = nullptr;
  uint16_t move = 0;
  uint16_t type = 0;
};

bool HpBelow(void* fop, int num, int den) {
  void* pk = fightOutPokemonGetPokemonPtr(fop);
  if (pk == nullptr) return false;
  return static_cast<int>(pokemon_GetHp(pk)) * den < static_cast<int>(pokemon_GetMaxHp(pk)) * num;
}
bool Grounded(void* fop) {
  if (fop == nullptr) return false;
  if (fightOutPokemonGetZokuseiDataId(fop, 0) == 2 || fightOutPokemonGetZokuseiDataId(fop, 1) == 2) return false;
  return fightOutPokemonGetTokuseiDataId(fop) != 26;
}
bool HasStatus(void* fop) { return fop != nullptr && fightOutPokemonIsJoutaiNormal(fop) == 0; }
bool HasType(void* fop, int type) {
  return fop != nullptr && (fightOutPokemonGetZokuseiDataId(fop, 0) == type || fightOutPokemonGetZokuseiDataId(fop, 1) == type);
}
int Effect(uint16_t moveType, void* def) {
  if (def == nullptr) return 4;
  int mult = 4;
  const uint16_t t1 = fightOutPokemonGetZokuseiDataId(def, 0), t2 = fightOutPokemonGetZokuseiDataId(def, 1);
  for (uint16_t t : {t1, static_cast<uint16_t>(t2 != t1 ? t2 : 0xFF)}) {
    if (t == 0xFF || t == 9) continue;
    const uint16_t r = zokuseiGetWazaJoutai(moveType, t);
    if (r == 0x43) return 0;
    if (r == 0x41) mult *= 2;
    if (r == 0x42) mult /= 2;
  }
  return mult;
}

bool Test(const xb::Condition& c, const Cx& x) {
  bool r = false;
  const uint16_t type = x.type != 0 || x.move == 0 ? x.type : wazaDB_GetZokuseiDataId(x.move);
  switch (c.kind) {
  case xb::Cond::FullHp: r = x.holder != nullptr && fightOutPokemonIsHpMantan(x.holder) != 0; break;
  case xb::Cond::NotFullHp: r = x.holder != nullptr && fightOutPokemonIsHpMantan(x.holder) == 0; break;
  case xb::Cond::HpBelowHalf: r = x.holder != nullptr && HpBelow(x.holder, 1, 2); break;
  case xb::Cond::HpAboveHalf: r = x.holder != nullptr && !HpBelow(x.holder, 1, 2); break;
  case xb::Cond::HpBelowThird: r = x.holder != nullptr && HpBelow(x.holder, 1, 3); break;
  case xb::Cond::TargetFullHp: r = x.other != nullptr && fightOutPokemonIsHpMantan(x.other) != 0; break;
  case xb::Cond::TargetHpBelowHalf: r = x.other != nullptr && HpBelow(x.other, 1, 2); break;
  case xb::Cond::StatusMove: r = x.move != 0 && wazaDB_GetIryoku(x.move) == 0; break;
  case xb::Cond::DamagingMove: r = x.move != 0 && wazaDB_GetIryoku(x.move) > 0; break;
  case xb::Cond::Contact: r = x.move != 0 && wazaDB_GetDageki(x.move) != 0; break;
  case xb::Cond::Physical: case xb::Cond::Special: {
    if (x.move == 0) break;
    PortXD_SplitCheckDarkWaza(x.move);
    const int kind = PortXD_SplitTypeKind(type);
    r = c.kind == xb::Cond::Physical ? kind == 1 : kind == 2;
    break;
  }
  case xb::Cond::MoveType: r = type == static_cast<uint16_t>(c.arg); break;
  case xb::Cond::UserType: r = HasType(x.holder, c.arg); break;
  case xb::Cond::TargetType: r = HasType(x.other, c.arg); break;
  case xb::Cond::Weather: r = c.arg == 9 ? Weather() != 0 : Weather() == c.arg; break;
  case xb::Cond::HasStatus: r = HasStatus(x.holder); break;
  case xb::Cond::NoStatus: r = x.holder != nullptr && !HasStatus(x.holder); break;
  case xb::Cond::TargetHasStatus: r = HasStatus(x.other); break;
  case xb::Cond::TargetNoStatus: r = x.other != nullptr && !HasStatus(x.other); break;
  case xb::Cond::UserHasItem: r = x.holder != nullptr && fightOutPokemonGetSoubiItemDataId(x.holder) != 0; break;
  case xb::Cond::UserNoItem: r = x.holder != nullptr && fightOutPokemonGetSoubiItemDataId(x.holder) == 0; break;
  case xb::Cond::TargetHasItem: r = x.other != nullptr && fightOutPokemonGetSoubiItemDataId(x.other) != 0; break;
  case xb::Cond::TargetNoItem: r = x.other != nullptr && fightOutPokemonGetSoubiItemDataId(x.other) == 0; break;
  case xb::Cond::FirstTurn: r = x.holder != nullptr && sLastTurn <= MonOf(x.holder).entry + 1; break;
  case xb::Cond::MoveHasSecondary: {
    r = x.move != 0 && wazaDB_GetAddFightKoukaAvg(x.move) > 0;
    const auto it = sMoves.find(x.move);
    if (it != sMoves.end()) {
      r = false;
      for (const auto& b : it->second->effects) if (b.chance < 100 && (b.kind == xb::BlockKind::Stat || b.kind == xb::BlockKind::Status)) r = true;
    }
    break;
  }
  case xb::Cond::SuperEffective: r = x.move != 0 && Effect(type, x.other) >= 8; break;
  case xb::Cond::NotVeryEffective: { const int e = Effect(type, x.other); r = x.move != 0 && e > 0 && e < 4; break; }
  case xb::Cond::MoveIs: r = x.move == static_cast<uint16_t>(c.arg); break;
  case xb::Cond::Field: r = c.arg >= 0 && c.arg < static_cast<int>(sFieldEnd.size()) && sFieldEnd[c.arg] >= sLastTurn && sFieldEnd[c.arg] >= 0; break;
  case xb::Cond::Sound: r = x.move != 0 && wazaDB_GetBouon(x.move) != 0; break;
  case xb::Cond::Punch: r = false; break;
  case xb::Cond::TargetStatusKind: case xb::Cond::UserStatusKind: {
    void* who = c.kind == xb::Cond::TargetStatusKind ? x.other : x.holder;
    if (who == nullptr) break;
    r = c.arg == 3 ? (fightOutPokemonIsJoutaiDataId(who, 3) != 0 || fightOutPokemonIsJoutaiDataId(who, 4) != 0) : fightOutPokemonIsJoutaiDataId(who, static_cast<uint16_t>(c.arg)) != 0;
    break;
  }
  case xb::Cond::MoveInList: r = false; for (int m : c.list) if (m == x.move) r = true; break;
  case xb::Cond::HpCrossedHalf: {
    if (x.holder == nullptr || sDealtTo != x.holder) break;
    void* pk = fightOutPokemonGetPokemonPtr(x.holder);
    if (pk == nullptr) break;
    const int now = pokemon_GetHp(pk), max = pokemon_GetMaxHp(pk);
    r = now * 2 <= max && (now + sDealt) * 2 > max;
    static const bool force = std::getenv("XD_WIMP_FORCE") != nullptr && std::getenv("XD_SETTINGS_DIR") != nullptr;
    if (force && now > 0) r = true;
    break;
  }
  case xb::Cond::SlowerThanTarget:
    r = x.holder != nullptr && x.other != nullptr && fightOutPokemonGetNowNimbleness(x.holder, 0, 0, 0, nullptr) < fightOutPokemonGetNowNimbleness(x.other, 0, 0, 0, nullptr);
    break;
  case xb::Cond::TargetFainted: { void* pk = x.other != nullptr ? fightOutPokemonGetPokemonPtr(x.other) : nullptr; r = pk != nullptr && pokemon_GetHp(pk) == 0; break; }
  case xb::Cond::TargetFirstTurn: r = x.other != nullptr && MonOf(x.other).entry >= sLastTurn; break;
  case xb::Cond::Terrain: r = TerrainNow() != 0 && (c.arg == 9 || TerrainNow() == c.arg); break;
  case xb::Cond::TargetGrounded: r = Grounded(x.other); break;
  case xb::Cond::UserGrounded: r = Grounded(x.holder); break;
  }
  return c.negate ? !r : r;
}
bool All(const std::vector<xb::Condition>& when, const Cx& x) {
  for (const auto& c : when) if (!Test(c, x)) return false;
  return true;
}

int32_t Scale(int32_t damage, int percent) { return (damage - 2) * percent / 100 + 2; }

size_t BuildCopy(uint8_t* out, uint32_t from, size_t size, uint8_t b0, size_t at, uint8_t atByte) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(from));
  if (src == nullptr || src[0] != b0 || src[at] != atByte) return 0;
  std::memcpy(out, src, size);
  return size;
}
void PutPort(uint8_t*& o, uint8_t id) { for (uint8_t b : {kPortOp, kPortMark, id, uint8_t(0), uint8_t(0)}) *o++ = b; }
int Relocate(uint8_t* p, size_t n, uint32_t from, uint32_t to) {
  int count = 0;
  for (size_t i = 0; i + 4 <= n; i++) if (GetBe32(p + i) == from) { PutBe32(p + i, to); count++; i += 3; }
  return count;
}
bool CopyPlainHit(uint8_t* out, uint32_t base) {
  const auto* hit = static_cast<const uint8_t*>(PortXD_GameAddress(kPlainHit));
  if (hit == nullptr || hit[0] != 0xFA || GetBe32(hit + 0x7F) != 0x2F804EB9u) return false;
  std::memcpy(out, hit, 0x7F);
  return Relocate(out, 0x7F, 0x804140DEu, base + 0x4E) == 2;
}

int MakeScript(bool heal, int messageId) {
  if (sFreeScript >= kMaxScripts) return -1;
  uint8_t* at = sScripts + kOffScripts + 0x40 * sFreeScript;
  const uint32_t msg = kBank | (kMessageText + static_cast<uint32_t>(messageId));
  if (heal) {
    if (BuildCopy(at, kAmeukezara, 35, 0x36, 20, 0x11) == 0 || at[34] != 0x40) return -1;
    PutBe32(at + 21, msg);
  } else {
    if (BuildCopy(at, kDokudoku, 40, 0x65, 15, 0x11) == 0 || at[20] != 0x13 || at[39] != 0x3F) return -1;
    PutBe32(at + 16, msg);
  }
  return sFreeScript++;
}
void* ScriptAt(uint32_t off) { return PortXD_GameAddress(kScriptBase + off); }

void ApplyTo(void* p, uint8_t code) {
  if (!Alive(p)) return;
  void* atk = Target(0x11);
  void* def = Target(0x12);
  uint8_t value = code;
  void* restore = nullptr;
  if (p == atk) value |= kSelf;
  else if (p != def) { restore = atk; fightFloor_SetAttackPokemonPtr(nullptr, p); value |= kSelf; }
  ServerWork[3] = value;
  ServerStatusFlag |= 0x2000;
  fightSeq_CallWazaSeq(PortXD_GameAddress(kWazaSinkuro));
  if (restore != nullptr) fightFloor_SetAttackPokemonPtr(nullptr, restore);
}

int BestStat(void* fop) {
  void* pk = fightOutPokemonGetPokemonPtr(fop);
  if (pk == nullptr) return 1;
  const int v[5] = {pokemon_GetPhyAtk(pk), pokemon_GetPhyDef(pk), 0, 0, pokemon_GetSpeDef(pk)};
  int best = 1, bv = v[0];
  const int spe = static_cast<int>(fightOutPokemonGetNowNimbleness(fop, 0, 0, 0, nullptr)), spa = pokemon_GetSpeAtk(pk);
  const int all[5] = {v[0], v[1], spe, spa, v[4]};
  for (int i = 1; i < 5; i++) if (all[i] > bv) { bv = all[i]; best = i + 1; }
  return best;
}

void ChangeStat(void* p, int stat, int stages) {
  int left = stages < 0 ? -stages : stages;
  while (left > 0) {
    const int step = left >= 2 ? 2 : 1;
    const uint8_t code = static_cast<uint8_t>(stages > 0 ? (step == 2 ? 0x26 : 0x0E) + stat : (step == 2 ? 0x2D : 0x15) + stat);
    ApplyTo(p, code);
    left -= step;
  }
}

uint32_t HealAmount(void* p, int permille) {
  uint32_t d = static_cast<uint32_t>(fightOutPokemonMaxHpWaruValue(p, 1)) * static_cast<uint32_t>(permille) / 1000u;
  return d == 0 ? 1 : d;
}

void HealWith(void* p, uint32_t amount, int script, bool silent) {
  void* pk = fightOutPokemonGetPokemonPtr(p);
  if (pk == nullptr || amount == 0) return;
  const uint32_t cur = pokemon_GetHp(pk), max = pokemon_GetMaxHp(pk);
  if (cur >= max) return;
  if (cur + amount > max) amount = max - cur;
  if (silent || script < 0) { pokemon_SetHp(pk, static_cast<uint16_t>(cur + amount)); return; }
  fightFloor_SetAttackPokemonPtr(nullptr, p);
  fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(p), -static_cast<int32_t>(amount));
  fightSeq_CallWazaSeq(ScriptAt(kOffScripts + 0x40 * script));
}
void HurtWith(void* p, uint32_t amount, int script, bool turnEnd) {
  if (!Alive(p) || amount == 0 || script < 0) return;
  if (turnEnd) ServerStatusFlag = ServerStatusFlag | 0x01000000u | 0x20u;
  fightFloor_SetAttackPokemonPtr(nullptr, p);
  fightFloor_SetAppointPokemonPtr(nullptr, p);
  fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(p), static_cast<int32_t>(amount));
  fightSeq_CallWazaSeq(ScriptAt(kOffScripts + 0x40 * script));
  if (turnEnd) {
    ServerStatusFlag &= 0xFEFFFFDFu;
    wazaSequenceSysFreeAllWaza();
  }
}

void StartWeather(void* p, int weather) {
  static const uint16_t ids[5] = {0, 0x54, 0x55, 0x56, 0x53};
  if (weather < 1 || weather > 4 || fightFloorCheckWriteJoutaiDataId(nullptr, ids[weather]) != 2) return;
  fightFloorWriteJoutaiDataId(nullptr, ids[weather], 0);
  fightFloor_SetAppointPokemonPtr(nullptr, p);
  if (weather == 4) {
    ServerWork[5] = 5;
    fightFloor_SetAttackPokemonPtr(nullptr, p);
    fightSeq_CallWazaSeq(ScriptAt(kOffHail));
    return;
  }
  fightSeq_CallWazaSeq(PortXD_GameAddress(weather == 1 ? kSun : weather == 2 ? kRain : kSand));
}

bool IsBerry(uint16_t item) { return item >= 0x85 && item <= 0xAF; }

void DoTransform(void* user, void* foe, int messageId) {
  if (!Alive(user) || !Alive(foe) || user == foe) return;
  const void* seq = fightSeq_GetWazaSeq();
  void* keepAtk = Target(0x11);
  void* keepDef = Target(0x12);
  fightFloor_SetAttackPokemonPtr(nullptr, user);
  fightFloor_SetDefPokemonAndAtkFightWazaTargetId(nullptr, foe);
  ServerWork[5] = 0;
  WS_HENSHIN();
  const bool ok = ServerWork[5] == 0;
  Log("Imposter: the owner transforms into the foe: ", ok ? "done" : "failed");
  fightSeq_JumpWazaSeq(const_cast<void*>(seq));
  if (keepAtk != nullptr) fightFloor_SetAttackPokemonPtr(nullptr, keepAtk);
  if (keepDef != nullptr) fightFloor_SetDefPokemonAndAtkFightWazaTargetId(nullptr, keepDef);
  if (ok) {
    fightMsgctrlSetValue(0xD, reinterpret_cast<uintptr_t>(fightOutPokemonGetNamePtr(foe)));
    Say(messageId, user);
  }
}

struct Run {
  void* user = nullptr;
  void* other = nullptr;
  uint16_t move = 0;
  bool hit = true;
  bool silent = false;
  bool turnEnd = false;
};

void Foes(void* of, void** out, uint16_t& n) {
  n = fightFloorGetFightOutPokemonPtrAry(nullptr, 1, 2, of, out);
  if (n > 8) n = 8;
}

void RunBlocks(const std::vector<xb::Block>& blocks, const Run& run, bool noChances = false, bool ownerFainted = false) {
  Cx cx;
  cx.holder = run.user; cx.other = run.other; cx.move = run.move;
  for (const xb::Block& b : blocks) {
    if (!All(b.when, cx)) continue;
    if (noChances && b.chance < 100) continue;
    if (b.kind == xb::BlockKind::SwitchOut || b.kind == xb::BlockKind::ForceSwitch) continue;
    void* targets[8] = {};
    uint16_t n = 0;
    if (b.onFoes) Foes(run.user, targets, n);
    else { targets[0] = b.onUser ? run.user : run.other; n = 1; }
    if (b.kind == xb::BlockKind::Side && b.field == 1) { targets[0] = run.user; n = 1; }
    if (b.kind == xb::BlockKind::Message || (b.kind == xb::BlockKind::SetType && b.onUser) || b.kind == xb::BlockKind::Field || b.kind == xb::BlockKind::Weather || b.kind == xb::BlockKind::Terrain) {
      n = 1;
      targets[0] = run.user;
    }
    if (b.chance < 100 && static_cast<int>(HSD_Rand() % 100) >= b.chance) continue;
    for (uint16_t i = 0; i < n; i++) {
      void* t = targets[i];
      if (t == nullptr || (!Alive(t) && !(ownerFainted && t == run.user))) continue;
      switch (b.kind) {
      case xb::BlockKind::Stat: ChangeStat(t, b.stat == 8 ? BestStat(t) : b.stat, b.stages); break;
      case xb::BlockKind::Status:
        ApplyTo(t, static_cast<uint8_t>(b.code));
        if (b.code == 0x20) {
          void* out = nullptr;
          Log("trap: the game's own switch check for the target says ", std::to_string(fightFloorCheckFightActionFightOutPokemonIrekaeSelect(nullptr, t, &out)) + " (1 = cannot switch), joutai 0x16 = " + std::to_string(fightOutPokemonIsJoutaiDataId(t, 0x16)));
        }
        break;
      case xb::BlockKind::Heal:
        if (fightOutPokemonIsHpMantan(t) == 0) HealWith(t, HealAmount(t, b.permille), sBlockScript.count(&b) ? sBlockScript[&b] : -1, run.silent);
        break;
      case xb::BlockKind::Hurt: {
        const auto it = sBlockScript.find(&b);
        HurtWith(t, HealAmount(t, b.permille), it == sBlockScript.end() ? -1 : it->second, run.turnEnd);
        break;
      }
      case xb::BlockKind::Recoil: {
        const auto it = sBlockScript.find(&b);
        if (sDealt > 0) HurtWith(run.user, std::max<int32_t>(1, sDealt * b.permille / 1000), it == sBlockScript.end() ? -1 : it->second, false);
        break;
      }
      case xb::BlockKind::Drain: {
        const auto it = sBlockScript.find(&b);
        if (sDealt > 0 && fightOutPokemonIsHpMantan(run.user) == 0) HealWith(run.user, std::max<int32_t>(1, sDealt * b.permille / 1000), it == sBlockScript.end() ? -1 : it->second, false);
        break;
      }
      case xb::BlockKind::HurtDealt: {
        const auto it = sBlockScript.find(&b);
        if (sDealt > 0) HurtWith(t, std::max<int32_t>(1, sDealt * b.permille / 1000), it == sBlockScript.end() ? -1 : it->second, false);
        break;
      }
      case xb::BlockKind::Moody: {
        const int up = 1 + static_cast<int>(HSD_Rand() % 7);
        int down = 1 + static_cast<int>(HSD_Rand() % 7);
        if (down == up) down = down % 7 + 1;
        ChangeStat(t, up, 2);
        ChangeStat(t, down, -1);
        break;
      }
      case xb::BlockKind::Weather: StartWeather(t, b.weather); break;
      case xb::BlockKind::RestoreBerry: {
        const uint16_t used = fightOutPokemon_GetUsedItemDataId(t);
        if (IsBerry(used) && fightOutPokemonGetSoubiItemDataId(t) == 0) {
          fightOutPokemonDoItemSoubi(t, used, 1, 1);
          Log("Harvest: the berry is back: item ", std::to_string(used));
          MonOf(t).berryNoted = false;
          Say(b.messageId, t);
        }
        break;
      }
      case xb::BlockKind::Transform: DoTransform(run.user, t, b.messageId); break;
      case xb::BlockKind::RemoveItem:
        if (fightOutPokemonGetSoubiItemDataId(t) != 0) { fightOutPokemonDoItemSoubi(t, 0, 0, 0); Say(b.messageId, t); }
        break;
      case xb::BlockKind::Message: Say(b.messageId, t); break;
      case xb::BlockKind::SetAbility:
        if (b.ability > 0 && b.ability < 256) fightOutPokemonSetTokuseiDataId(t, static_cast<uint16_t>(b.ability));
        break;
      case xb::BlockKind::CureStatus:
        for (uint16_t id = 3; id <= 8; id++) if (fightOutPokemonIsJoutaiDataId(t, id)) fightOutPokemonInitJoutaiDataId(t, id);
        break;
      case xb::BlockKind::StealItem: {
        void* from = t == run.user ? run.other : run.user;
        void* to = t;
        if (from == nullptr || to == nullptr || from == to) break;
        const uint16_t item = fightOutPokemonGetSoubiItemDataId(from);
        if (item == 0 || fightOutPokemonGetSoubiItemDataId(to) != 0 || fightOutPokemonGetTokuseiDataId(from) == 60) break;
        fightOutPokemonDoItemSoubi(from, 0, 0, 0);
        fightOutPokemonDoItemSoubi(to, item, 1, 0);
        Say(b.messageId, to);
        break;
      }
      case xb::BlockKind::Side: {
        const int side = OnPlayersSide(t) ? 0 : 1;
        if (b.field == 1) { sVeilEnd[side] = sLastTurn + 4; Say(sMsgVeil, t); }
        else if (!sWeb[side]) { sWeb[side] = true; Say(sMsgWeb, t); }
        break;
      }
      case xb::BlockKind::Terrain:
        if (b.field >= 1 && b.field <= 4) {
          sTerrain = b.field; sTerrainEnd = sLastTurn + 4;
          Say(sMsgTerrain[b.field], t);
        }
        break;
      case xb::BlockKind::SetType: {
        Mon& m = MonOf(t);
        m.type = b.type >= 0 ? b.type : (run.move != 0 ? wazaDB_GetZokuseiDataId(run.move) : -1);
        break;
      }
      case xb::BlockKind::Field:
        if (b.field >= 0 && b.field < static_cast<int>(sFieldEnd.size())) { sFieldEnd[b.field] = sLastTurn + b.turns - 1; Say(b.messageId, t); }
        break;
      default: break;
      }
      if (b.kind != xb::BlockKind::Message && b.kind != xb::BlockKind::Field && b.kind != xb::BlockKind::RemoveItem && b.kind != xb::BlockKind::RestoreBerry && b.kind != xb::BlockKind::Transform && b.kind != xb::BlockKind::Heal &&
          b.kind != xb::BlockKind::Hurt && b.kind != xb::BlockKind::Recoil && b.kind != xb::BlockKind::Drain && b.kind != xb::BlockKind::StealItem && !b.message.empty()) Say(b.messageId, t);
    }
  }
}

void SetupScripts() {
  uint8_t* o = sScripts + kOffDispatch;
  PutPort(o, kCmdDispatch);
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  o = sScripts + kOffHit;
  PutPort(o, kCmdSetup);
  CopyPlainHit(o, kScriptBase + kOffHit + 5);
  o += 0x7F;
  PutPort(o, kCmdAfter);
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  o = sScripts + kOffHitSwitch;
  PutPort(o, kCmdSetup);
  CopyPlainHit(o, kScriptBase + kOffHitSwitch + 5);
  o += 0x7F;
  PutPort(o, kCmdAfter);
  const auto* baton = static_cast<const uint8_t*>(PortXD_GameAddress(kBatonPass));
  if (baton != nullptr && baton[0] == 0x50 && baton[7] == 0x0A && baton[8] == 0x0B && baton[0x2E] == 0x53) {
    uint8_t* tail = o;
    std::memcpy(o, baton, 7); o += 7;
    std::memcpy(o, baton + 10, 0x30 - 10); o += 0x30 - 10;
    Relocate(tail, static_cast<size_t>(o - tail), 0x80415C93u, kWazaEnd);
  }
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  o = sScripts + kOffHitForce;
  PutPort(o, kCmdSetup);
  CopyPlainHit(o, kScriptBase + kOffHitForce + 5);
  o += 0x7F;
  PutPort(o, kCmdAfter);
  PutPort(o, kCmdForce);
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  o = sScripts + kOffFoeSwitch;
  *o++ = 0x00; *o++ = 0x01; PutBe32(o, kNoHit); o += 4; *o++ = 0; *o++ = 0;
  *o++ = 0x02; *o++ = 0x04;
  for (uint8_t b : {0x0A, 0x0B, 0xFF}) *o++ = b;
  PutPort(o, kCmdApply);
  {
    const auto* baton = static_cast<const uint8_t*>(PortXD_GameAddress(kBatonPass));
    if (baton != nullptr && baton[0] == 0x50 && baton[7] == 0x0A && baton[8] == 0x0B && baton[0x2E] == 0x53) {
      uint8_t* tail = o;
      std::memcpy(o, baton, 7); o += 7;
      std::memcpy(o, baton + 10, 0x30 - 10); o += 0x30 - 10;
      Relocate(tail, static_cast<size_t>(o - tail), 0x80415C93u, kWazaEnd);
    }
  }
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  for (int foe = 0; foe < 2; foe++) {
    o = sScripts + (foe ? kOffFoe : kOffSelf);
    *o++ = 0x00;
    if (foe) { *o++ = 0x01; PutBe32(o, kNoHit); o += 4; *o++ = 0; *o++ = 0; }
    *o++ = 0x02; *o++ = 0x04;
    for (uint8_t b : {0x0A, 0x0B, 0xFF}) *o++ = b;
    PutPort(o, kCmdApply);
    *o++ = 0x29; PutBe32(o, kWazaEnd);
  }
  o = sScripts + kOffAbsorb;
  *o++ = 0x04; *o++ = 0x02;
  PutPort(o, kCmdAbsorb);
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  o = sScripts + kOffAbsorb + 0x20;
  *o++ = 0x02;
  PutPort(o, kCmdAbsorb);
  *o++ = 0x29; PutBe32(o, kWazaEnd);
  const uint8_t hail[] = {0x14, 0x80, 0x2F, 0x8F, 0x38, 0x13, 0x00, 0x60, 0x0B, 0x04, 0x40};
  std::memcpy(sScripts + kOffHail, hail, sizeof hail);
}

}

bool Active() { return sLoaded; }
uint64_t Digest() { return sDigest; }
std::string ActiveText() {
  if (!sLoaded) return std::string();
  char text[64];
  std::snprintf(text, sizeof text, "battle effects %016llx", static_cast<unsigned long long>(sDigest));
  return text;
}

namespace {
std::string ForBank(std::string text, bool script) {
  const std::string code = script ? "{0F}" : "{10}";
  for (size_t at = text.find("{name}"); at != std::string::npos; at = text.find("{name}", at + code.size())) text.replace(at, 6, code);
  for (size_t at = text.find("{foe}"); at != std::string::npos; at = text.find("{foe}", at + 4)) text.replace(at, 5, "{0D}");
  return text;
}
int AddMessage(const std::string& text, bool script) {
  sMessages.push_back(ForBank(text.empty() ? text : std::string(engine::i18n::Tr(text.c_str())), script));
  return static_cast<int>(sMessages.size() - 1);
}
void PrepareBlocks(std::vector<xb::Block>& blocks) {
  for (xb::Block& b : blocks) {
    const bool script = b.kind == xb::BlockKind::Heal || b.kind == xb::BlockKind::Hurt || b.kind == xb::BlockKind::Recoil || b.kind == xb::BlockKind::Drain || b.kind == xb::BlockKind::HurtDealt;
    std::string text = b.message;
    if (script && text.empty()) {
      text = b.kind == xb::BlockKind::Heal || b.kind == xb::BlockKind::Drain ? "{name} restored some HP!" : "{name} was hurt!";
    }
    if (!text.empty()) b.messageId = AddMessage(text, script);
    if (b.kind == xb::BlockKind::Field && !b.endMessage.empty()) AddMessage(b.endMessage, false);
  }
}
}

void Load(const em::LoadResult& result, const std::vector<em::ModFolder>& found, const std::string& gameId, std::vector<std::string>& problems) {
  sAnyUnnerve = sAnyWind = sAnyInfiltrator = false; sChargeIndex.clear(); std::memset(sScripts2, 0, sizeof sScripts2); sCorrode = nullptr;
  sLoaded = false; sDigest = 0; sDefs = xb::Defs(); sMoves.clear(); sMessages.clear(); sBlockScript.clear(); sFreeScript = 0; sInstalled = false; sRules = nullptr;
  std::memset(sAbility, 0, sizeof sAbility);
  std::memset(sScripts, 0, sizeof sScripts);
  engine::script::Names names = xd::scripts::BuildNames(result);
  const xb::Resolver resolve = [&](const std::string& cat, const std::string& name, int64_t& v) { return names.Find(cat, name, v); };
  bool any = false;
  if (xd::gen57::Loaded()) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(xd::gen57::PackFolder() / "battle", ec)) {
      if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
      std::ifstream in(f, std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      xb::ParseFile("gen57", "battle/" + f.filename().string(), text, resolve, sDefs);
      any = true;
    }
  }
  for (const em::Manifest& m : result.active) {
    for (const em::ModFolder& f : found) {
      if (f.manifest.id != m.id || f.texturePack) continue;
      for (const em::LayoutFile& file : em::LayoutFiles(f.folder, gameId, "battle", false)) {
        if (file.rel.size() < 5 || file.rel.substr(file.rel.size() - 5) != ".json") continue;
        std::ifstream in(file.path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        xb::ParseFile(m.id, "battle/" + file.rel, text, resolve, sDefs);
        any = true;
      }
    }
  }
  if (!any) return;
  xb::Finish(sDefs);
  int chargeLeftOut = 0;
  for (const std::string& p : sDefs.problems) {
    if (xd::xg::On() && p.find("the donor is one of the game's own two-turn moves") != std::string::npos) { chargeLeftOut++; continue; }
    problems.push_back(p);
  }
  if (chargeLeftOut != 0) Log("XG: no charge turn for ", std::to_string(chargeLeftOut) + " move(s): the two-turn donor Razor Wind is not on XG, they act at once");  if (xd::xg::On() && xd::gen57::Loaded()) {
    std::set<int64_t> added;
    for (const auto& r : result.addedRows) {
      if (r.table == "waza_data") added.insert(static_cast<int64_t>(r.id));
    }
    const size_t before = sDefs.moves.size();
    sDefs.moves.erase(std::remove_if(sDefs.moves.begin(), sDefs.moves.end(),
                                     [&](const xb::MoveDef& m) { return m.mod == "gen57" && added.count(m.id) == 0; }),
                      sDefs.moves.end());
    Log("XG: the pack's battle code for ", std::to_string(sDefs.moves.size()) + " of its " + std::to_string(before) + " moves (the others are XG's own moves)");
  }
  if (sDefs.moves.empty() && sDefs.abilities.empty() && sDefs.ruleSets.empty()) return;
  sFieldEnd.assign(sDefs.fieldNames.size(), -1);
  {
    static const char* start[5] = {"", "Grass grew to cover the battlefield!", "An electric current runs across the battlefield!", "The battlefield got weird!", "Mist swirled about the battlefield!"};
    static const char* stop[5] = {"", "The grass disappeared from the battlefield.", "The electricity disappeared from the battlefield.", "The weirdness disappeared from the battlefield.", "The mist disappeared from the battlefield."};
    for (int i = 1; i <= 4; i++) { sMsgTerrain[i] = AddMessage(start[i], false); sMsgTerrainEnd[i] = AddMessage(stop[i], false); }
    sMsgVeil = AddMessage("Aurora Veil made {name}'s team stronger against physical and special moves!", false);
    sMsgWeb = AddMessage("A sticky web spreads out on the ground at {name}'s feet!", false);
    sMsgWebHit = AddMessage("{name} was caught in a sticky web!", false);
    sMsgImmune = AddMessage("{name} is not affected!", false);
    sMsgDisabled = AddMessage("{name}'s move was disabled!", false);
    sSpikyHurt = xb::Block();
    sSpikyHurt.kind = xb::BlockKind::Hurt;
    sSpikyHurt.messageId = AddMessage("{name} was hurt!", true);
    sBlockScript[&sSpikyHurt] = -2;
    sDisguiseHurt = xb::Block();
    sDisguiseHurt.kind = xb::BlockKind::Hurt;
    sDisguiseHurt.messageId = AddMessage("{name} was hurt!", true);
    sBlockScript[&sDisguiseHurt] = -2;
    sTerrainHeal = xb::Block();
    sTerrainHeal.kind = xb::BlockKind::Heal;
    sTerrainHeal.messageId = AddMessage("{name} was healed by the grassy terrain!", true);
    sBlockScript[&sTerrainHeal] = -2;
  }
  for (xb::MoveDef& m : sDefs.moves) {
    PrepareBlocks(m.effects);
    if (m.chargeDonor != 0 && !m.chargeMessage.empty()) m.chargeMessageId = AddMessage(m.chargeMessage, false);
  }
  for (xb::AbilityDef& a : sDefs.abilities) {
    for (auto* v : {&a.switchIn, &a.endOfTurn, &a.ko, &a.contact, &a.switchOut, &a.beforeMove, &a.hit, &a.hitDealt, &a.fainted, &a.statDropped, &a.anyFaint, &a.berryEaten, &a.halfHp}) PrepareBlocks(*v);
    for (xb::Absorb& o : a.absorb) {
      std::string text = o.message;
      if (text.empty()) text = o.healPermille > 0 ? "{name} absorbed the move and restored HP!" : "{name} absorbed the move!";
      o.messageId = AddMessage(text, false);
    }
  }
  auto scripts = [&](std::vector<xb::Block>& blocks) {
    for (xb::Block& b : blocks) {
      if (b.kind != xb::BlockKind::Heal && b.kind != xb::BlockKind::Hurt && b.kind != xb::BlockKind::Recoil && b.kind != xb::BlockKind::Drain && b.kind != xb::BlockKind::HurtDealt) continue;
      sBlockScript[&b] = -2;
    }
  };
  for (xb::MoveDef& m : sDefs.moves) { scripts(m.effects); sMoves[m.id] = &m; }
  for (xb::AbilityDef& a : sDefs.abilities) {
    for (auto* v : {&a.switchIn, &a.endOfTurn, &a.ko, &a.contact, &a.switchOut, &a.beforeMove, &a.hit, &a.hitDealt, &a.fainted, &a.statDropped, &a.anyFaint, &a.berryEaten, &a.halfHp}) scripts(*v);
    sAbility[a.id] = &a;
    sAnyUnnerve |= a.unnerve; sAnyWind |= a.strongWind; sAnyInfiltrator |= a.infiltrator; sAnyDancer |= !a.dancer.empty();
  }
  for (const xb::MoveDef& m : sDefs.moves) {
    for (const xb::Block& b : m.effects) {
      (void)b;
    }
  }
  for (const xb::RuleSet& r : sDefs.ruleSets) {
    if (r.forceDouble) Log("not built yet, left out: force_double of the rule set ", r.name);
  }
  xd::hidden::SetTables(sDefs.hiddenNat, sDefs.hiddenRow);
  sDigest = xb::Digest(sDefs);
  sLoaded = true;
  Log("definitions: ", std::to_string(sDefs.moves.size()) + " moves, " + std::to_string(sDefs.abilities.size()) + " abilities, " + std::to_string(sDefs.ruleSets.size()) +
                           " rule sets, " + std::to_string(sMessages.size()) + " texts");
}

void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts) {
  if (!sLoaded) return;
  for (const auto& a : sDefs.abilities) {
    if (a.id >= 0 && xd::gen57::AbilityName(static_cast<uint32_t>(a.id)) != nullptr) continue;
    std::string shown = a.name;
    for (char& c : shown) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    texts.emplace_back((0x7D0u << 20) | (kAbilityText + static_cast<uint32_t>(a.id) * 2), shown);
    texts.emplace_back((0x7D0u << 20) | (kAbilityText + static_cast<uint32_t>(a.id) * 2 + 1), a.text.empty() ? a.name : a.text);
  }
  for (size_t i = 0; i < sMessages.size(); i++) texts.emplace_back((0x7D0u << 20) | (kMessageText + static_cast<uint32_t>(i)), sMessages[i]);
}

void InstallAbilityRows() {
  if (!sLoaded || sDefs.abilities.empty()) return;
  static bool done = false;
  if (done) return;
  done = true;
  uint32_t top = pokemon_tokusei_data_number;
  for (const auto& a : sDefs.abilities) {
    xd::xg::SetAbilityRow(pokemon_tokusei_data, static_cast<uint32_t>(a.id), (0x7D0u << 20) | (kAbilityText + static_cast<uint32_t>(a.id) * 2),
                          (0x7D0u << 20) | (kAbilityText + static_cast<uint32_t>(a.id) * 2 + 1));
    if (static_cast<uint32_t>(a.id) + 1 > top) top = static_cast<uint32_t>(a.id) + 1;
  }
  Log("ability table grown: ", std::to_string(pokemon_tokusei_data_number) + " -> " + std::to_string(top) + " rows");
  pokemon_tokusei_data_number = top;
}

void AppendAbilityNames(std::vector<std::pair<std::string, int64_t>>& out) {
  if (!sLoaded) return;
  for (const auto& a : sDefs.abilities) out.emplace_back(a.name, a.id);
}

void EnsureInstalled() {
  if (!sLoaded || sInstalled) return;
  sInstalled = true;
  for (auto& it : sBlockScript) {
    const xb::Block& b = *it.first;
    it.second = MakeScript(b.kind == xb::BlockKind::Heal || b.kind == xb::BlockKind::Drain, b.messageId);
    if (it.second < 0) Log("no room or the game's script is not as read for a block at line ", std::to_string(b.line));
  }
  SetupScripts();
  if (const char* dump = std::getenv("XD_MODBATTLE_DUMP")) {
    for (const char* at = dump; *at != 0;) {
      const bool raw = at[0] == '0' && (at[1] == 'x' || at[1] == 'X');
      const int move = raw ? 0 : std::atoi(at);
      const uint16_t seq = raw ? 0 : wazaDB_GetSeqId(static_cast<uint16_t>(move));
      const uint32_t addr = raw ? static_cast<uint32_t>(std::strtoul(at, nullptr, 16)) : GetBe32(static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * seq)));
      const auto* bytes = static_cast<const uint8_t*>(PortXD_GameAddress(addr));
      std::string line;
      char hex[8];
      const int dumpLen = std::getenv("XD_MODBATTLE_DUMP_LEN") != nullptr ? std::atoi(std::getenv("XD_MODBATTLE_DUMP_LEN")) : 0x80;
      for (int i = 0; bytes != nullptr && i < dumpLen; i++) { std::snprintf(hex, sizeof hex, "%02X ", bytes[i]); line += hex; }
      std::fprintf(stderr, "[mod battle] script of move %d: effect place %u at 0x%08X: %s\n", move, seq, addr, line.c_str());
      while (*at != 0 && *at != ',') at++;
      if (*at == ',') at++;
    }
  }
  {
    uint8_t* o = sScripts2 + kOff2Switch;
    const auto* baton = static_cast<const uint8_t*>(PortXD_GameAddress(kBatonPass));
    if (baton != nullptr && baton[0] == 0x50 && baton[7] == 0x0A && baton[8] == 0x0B && baton[0x2E] == 0x53) {
      uint8_t* tail = o;
      std::memcpy(o, baton, 7); o += 7;
      std::memcpy(o, baton + 10, 0x30 - 10); o += 0x30 - 10;
      Relocate(tail, static_cast<size_t>(o - tail), 0x80415C93u, kWazaEnd);
    }
    *o++ = 0x29; PutBe32(o, kWazaEnd);
    {
      uint8_t* d = sScripts2 + kOff2Dance;
      *d++ = 0x00; *d++ = 0x02; *d++ = 0x04;
      PutPort(d, kCmdDance);
      *d++ = 0x29; PutBe32(d, kWazaEnd);
    }
    int ci = 0;
    for (const auto& m : sDefs.moves) {
      if (m.chargeDonor == 0) continue;
      if (ci >= kMaxCharge) { Log("too many two-turn moves, left out: ", m.name); continue; }
      const uint16_t seq = wazaDB_GetSeqId(static_cast<uint16_t>(m.chargeDonor));
      const auto* tbl = static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * seq));
      const auto* src = tbl != nullptr ? static_cast<const uint8_t*>(PortXD_GameAddress(GetBe32(tbl))) : nullptr;
      if (src == nullptr) { Log("the donor's script was not found: ", m.name); continue; }
      const uint32_t addr = GetBe32(tbl);
      uint8_t* dst = sScripts2 + kOff2Charge + kChargeStride * ci;
      const uint32_t base = kScriptBase2 + kOff2Charge + static_cast<uint32_t>(kChargeStride) * ci;
      const bool vanish = seq == wazaDB_GetSeqId(19);
      const size_t copyLen = vanish ? kVanishCopy : kChargeCopy;
      std::memcpy(dst, src, copyLen);
      int jumps = 0, tables = 0, vanishes = 0;
      for (size_t i = 0; i + 4 <= copyLen; i++) {
        const uint32_t v = GetBe32(dst + i);
        if (dst[i] == 0x80 && v >= addr && v < addr + copyLen) { PutBe32(dst + i, v - addr + base); jumps++; i += 3; }
      }
      if (vanish) {
        for (size_t i = 0; i + 8 <= copyLen; i++) {
          if (dst[i] == 0x2B && dst[i + 1] == 0x00 && dst[i + 2] == 0x80 && dst[i + 6] == ((m.chargeDonor >> 8) & 0xFF) && dst[i + 7] == (m.chargeDonor & 0xFF)) {
            dst[i + 6] = static_cast<uint8_t>(m.id >> 8); dst[i + 7] = static_cast<uint8_t>(m.id & 0xFF); vanishes++; i += 7;
          }
        }
        for (size_t i = 0; i + 6 <= copyLen; i++) {
          if ((dst[i] != 0xC5 && dst[i] != 0xC6) || dst[i + 1] != 0x29) continue;
          const uint32_t to = GetBe32(dst + i + 2);
          uint8_t cmd = 0;
          if (dst[i] == 0xC5 && to == kWazaEnd) cmd = kCmdVanish;
          else if (dst[i] == 0xC6 && to == kVanishAfterA) cmd = kCmdUnvanishA;
          else if (dst[i] == 0xC6 && to == kVanishAfterB) cmd = kCmdUnvanishB;
          if (cmd == 0) continue;
          dst[i] = kPortOp; dst[i + 1] = kPortMark; dst[i + 2] = cmd; dst[i + 3] = 0; dst[i + 4] = 0; dst[i + 5] = 0;
          vanishes++; i += 5;
        }
        tables = vanishes;
      }
      if (m.chargeMessageId >= 0 && !vanish) {
        for (size_t i = 0; i + 5 <= copyLen; i++) {
          if (dst[i] == 0x14 && dst[i + 1] == 0x80 && dst[i + 2] == 0x2F) {
            if (tables == 0) for (int k = 0; k < 16; k++) PutBe32(dst + kChargeCopy + 4 * k, kBank | (kMessageText + static_cast<uint32_t>(m.chargeMessageId)));
            PutBe32(dst + i + 1, base + static_cast<uint32_t>(kChargeCopy));
            tables++;
          }
        }
      }
      sChargeIndex[m.id] = ci++;
      Log(("two-turn move " + m.name + ": ").c_str(), "the script of " + std::to_string(m.chargeDonor) + " copied (" + std::to_string(jumps) + " jumps, " + std::to_string(tables) + (vanish ? " vanish places)" : " message tables)"));
    }
    PortXD_AddScriptBlock(kScriptBase2, sScripts2, sizeof sScripts2);
  }
  PortXD_AddScriptBlock(kScriptBase, sScripts, sizeof sScripts);
  PutBe32(static_cast<uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kSlot)), kScriptBase + kOffDispatch);
  for (const auto& m : sDefs.moves) {
    bool acts = !m.effects.empty() || m.chargeDonor != 0 || m.crit || m.ignoreBoosts || m.useTargetAttack || m.defendWithDef || m.firstTurnOnly || m.needsPhysicalHit || m.heatsUp;
    if (!acts) continue;
    auto* row = static_cast<uint8_t*>(wazaDataBiosGetPtr(static_cast<uint16_t>(m.id)));
    if (row == nullptr) continue;
    row[0x1C] = kSlot & 0xFF;
    row[0x1D] = kSlot >> 8;
    const bool ok = wazaDB_GetSeqId(static_cast<uint16_t>(m.id)) == kSlot;
    Log(ok ? "move uses the mod effect: " : "COULD NOT set the effect place of ", m.name + " (#" + std::to_string(m.id) + ")");
  }
}

bool PortCommand(uint8_t id) {
  Tick();
  EnsureInstalled();
  void* atk = Target(0x11);
  void* def = Target(0x12);
  const uint16_t move = atk != nullptr ? fightOutPokemon_GetUseWazaDataId(atk) : 0;
  const auto it = sMoves.find(move);
  switch (id) {
  case kCmdDispatch: {
    if (it == sMoves.end()) { fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage)); return true; }
    const xb::MoveDef& m = *it->second;
    if (m.chargeDonor != 0) {
      const auto ci = sChargeIndex.find(move);
      if (ci != sChargeIndex.end()) { fightSeq_JumpWazaSeq(PortXD_GameAddress(kScriptBase2 + kOff2Charge + static_cast<uint32_t>(kChargeStride) * ci->second)); return true; }
    }
    const bool damaging = wazaDB_GetIryoku(move) > 0;
    bool switchOut = false, foe = false, force = false;
    for (const auto& b : m.effects) {
      switchOut |= b.kind == xb::BlockKind::SwitchOut;
      force |= b.kind == xb::BlockKind::ForceSwitch;
      foe |= !b.onUser || b.onFoes;
    }
    if (m.needsPhysicalHit && atk != nullptr) {
      Mon& sm = MonOf(atk);
      const bool ok = sm.shellHit;
      sm.shellArmed = false; sm.shellHit = false;
      Log(ok ? "Shell Trap: a physical move hit the user first: the move goes off" : "Shell Trap: nothing hit the user first: the move fails", "");
      if (!ok) { fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage)); return true; }
    }
    if (m.firstTurnOnly) {
      void* user = atk;
      if (user != nullptr && sLastTurn > MonOf(user).entry + 1) { fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage)); return true; }
    }
    uint32_t off = damaging ? (switchOut ? kOffHitSwitch : force ? kOffHitForce : kOffHit) : (switchOut ? kOffFoeSwitch : foe ? kOffFoe : kOffSelf);
    fightSeq_JumpWazaSeq(ScriptAt(off));
    return true;
  }
  case kCmdSetup:
    sDealt = 0; sDealtTo = nullptr;
    return false;
  case kCmdAfter: case kCmdApply: {
    if (it == sMoves.end() || atk == nullptr) return false;
    const bool hit = id == kCmdApply || fightWazaIsHit(fightOutPokemon_GetFightWazaPtr(atk)) != 0;
    if (!hit) return false;
    Run run;
    run.user = atk; run.other = def; run.move = move;
    const xb::AbilityDef* ab = AbilityOf(atk);
    RunBlocks(it->second->effects, run, ab != nullptr && ab->removesSecondary && id == kCmdAfter);
    return false;
  }
  case kCmdDance: {
    if (atk == nullptr || sDanceMove == 0) return false;
    const uint16_t copy = sDanceMove;
    void* target = fightSeqSelectDefensePokemonPtr(atk, copy, fightFloor_GetFightType(0), 0, 1, 1, -1);
    fightFloor_SetDefPokemonAndAtkFightWazaTargetId(0, target);
    ServerStatusFlag &= ~0x400u;
    Log("Dancer: copies the dance move ", std::to_string(copy));
    const auto* table = static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * wazaDB_GetSeqId(copy)));
    if (table == nullptr) return false;
    fightSeq_JumpWazaSeq(PortXD_GameAddress(GetBe32(table)));
    return true;
  }
  case kCmdForce: {
    if (def == nullptr || atk == nullptr || def == atk || !Alive(def)) return false;
    void* pk = fightOutPokemonGetPokemonPtr(def);
    if (pk == nullptr || pokemon_GetHp(pk) == 0 || fightWazaIsHit(fightOutPokemon_GetFightWazaPtr(atk)) == 0) return false;
    const auto* table = static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kRoarSeq));
    if (table == nullptr) return false;
    const uint32_t roar = GetBe32(table);
    const auto* bytes = static_cast<const uint8_t*>(PortXD_GameAddress(roar + 35));
    if (bytes == nullptr || bytes[0] != 0x90) { Log("Roar's script is not as read: forced switch left out", ""); return false; }
    fightSeq_JumpWazaSeq(PortXD_GameAddress(roar + 35));
    return true;
  }
  case kCmdVanish: case kCmdUnvanishA: case kCmdUnvanishB: {
    const auto vit = sMoves.find(move);
    const int64_t donor = vit != sMoves.end() ? vit->second->chargeDonor : 19;
    const uint16_t state = donor == 91 ? 0x20 : donor == 291 ? 0x21 : 0x1F;
    if (atk != nullptr) {
      if (id == kCmdVanish) { if (fightOutPokemonCheckWriteJoutaiDataId(atk, state) == 2) fightOutPokemonWriteJoutaiDataId(atk, state, 0); }
      else if (fightOutPokemonIsJoutaiDataId(atk, state) == 1) fightOutPokemonInitJoutaiDataId(atk, state);
    }
    fightSeq_JumpWazaSeq(PortXD_GameAddress(id == kCmdVanish ? kWazaEnd : id == kCmdUnvanishA ? kVanishAfterA : kVanishAfterB));
    return true;
  }
  case kCmdAbsorb: {
    if (sAbsorbEntry == nullptr) return false;
    const xb::Absorb& a = *sAbsorbEntry;
    void* holder = sAbsorbing != nullptr ? def : nullptr;
    sAbsorbEntry = nullptr; sAbsorbing = nullptr;
    if (holder == nullptr) return false;
    if (a.healPermille > 0 && fightOutPokemonIsHpMantan(holder) == 0) {
      HealWith(holder, HealAmount(holder, a.healPermille), -1, true);
    }
    Say(a.messageId, holder);
    if (a.stat != 0) ChangeStat(holder, a.stat, a.stages);
    return false;
  }
  default:
    return false;
  }
}

namespace {
int WeightOf(void* fop) {
  int w = pokemonDB_GetWeight(figthOutPokemonGetPokemonDataId(fop));
  const uint16_t ab = fightOutPokemonGetTokuseiDataId(fop);
  if (ab == 134) w *= 2;
  else if (ab == 135) w /= 2;
  return w < 1 ? 1 : w;
}
int FormulaPower(int formula, void* atk, void* def) {
  switch (formula) {
  case 1: {
    const int r = WeightOf(atk) / WeightOf(def);
    return r >= 5 ? 120 : r >= 4 ? 100 : r >= 3 ? 80 : r >= 2 ? 60 : 40;
  }
  case 2: {
    const uint32_t us = fightOutPokemonGetNowNimbleness(atk, 0, 0, 0, nullptr), them = std::max<uint32_t>(1, fightOutPokemonGetNowNimbleness(def, 0, 0, 0, nullptr));
    const uint32_t r = us / them;
    return r >= 4 ? 150 : r >= 3 ? 120 : r >= 2 ? 80 : r >= 1 ? 60 : 40;
  }
  case 3: {
    auto* p = static_cast<uint8_t*>(atk);
    const uint8_t v[7] = {fightOutPokemonBiosGetAbicntPhyAtk(p), fightOutPokemonBiosGetAbicntPhyDef(p), fightOutPokemonBiosGetAbicntNimbleness(p),
                          fightOutPokemonBiosGetAbicntSpeAtk(p), fightOutPokemonBiosGetAbicntSpeDef(p), fightOutPokemonBiosGetAbicntAverage(p),
                          fightOutPokemonBiosGetAbicntAvoid(p)};
    int up = 0;
    for (uint8_t s : v) if (s > 6) up += s - 6;
    return 20 + 20 * up;
  }
  default: return 0;
  }
}
}

uint16_t PowerOf(uint16_t waza, uint16_t power, void* attacker, void* defender) {
  const auto it = sMoves.find(waza);
  if (it == sMoves.end() || (it->second->power.empty() && it->second->powerFormula == 0)) return 0;
  int32_t p = power != 0 ? power : wazaDB_GetIryoku(waza);
  if (p <= 0) return 0;
  bool changed = false;
  if (it->second->powerFormula != 0 && attacker != nullptr && defender != nullptr) {
    if (const int fp = FormulaPower(it->second->powerFormula, attacker, defender)) {
      static int shown = 0;
      if (shown++ < 4) std::fprintf(stderr, "[mod battle] power formula of %s: %d\n", it->second->name.c_str(), fp);
      p = fp; changed = true;
    }
  }
  Cx cx;
  cx.holder = attacker; cx.other = defender; cx.move = waza;
  for (const auto& r : it->second->power) {
    if (!All(r.when, cx)) continue;
    p = p * r.percent / 100;
    changed = true;
  }
  if (!changed) return 0;
  return static_cast<uint16_t>(std::max(1, std::min(p, 65000)));
}

namespace {
struct AuraScan { int type; int percent; bool broken; bool found; };
int32_t ScanAura(void* fop, uint16_t, void* data) {
  auto* s = static_cast<AuraScan*>(data);
  if (!Alive(fop)) return 1;
  const xb::AbilityDef* a = AbilityOf(fop);
  if (a == nullptr) return 1;
  if (a->auraType == s->type) { s->found = true; s->percent = a->auraPercent; }
  if (a->breaksAuras) s->broken = true;
  return 1;
}
const xb::Convert* ConvertFor(void* attacker, uint16_t waza, uint16_t type) {
  const xb::AbilityDef* a = AbilityOf(attacker);
  if (a == nullptr || a->convert.empty()) return nullptr;
  Cx cx;
  cx.holder = attacker; cx.move = waza; cx.type = type;
  for (const auto& k : a->convert) {
    if ((k.from < 0 || k.from == type) && All(k.when, cx) && !(k.to == 18 && !xd::modern::Loaded())) return &k;
  }
  return nullptr;
}
int32_t AuraAndConvert(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage) {
  (void)defender;
  if (const xb::Convert* k = ConvertFor(attacker, waza, type); k != nullptr && wazaDB_GetIryoku(waza) > 0) {
    damage = Scale(damage, k->percent);
    const bool hadFrom = HasType(attacker, type), hasTo = HasType(attacker, k->to);
    if (hadFrom && !hasTo) damage = Scale(damage, 67);
    else if (!hadFrom && hasTo) damage = Scale(damage, 150);
  }
  const uint16_t shownType = [&]() { const xb::Convert* k = ConvertFor(attacker, waza, type); return k != nullptr ? static_cast<uint16_t>(k->to) : type; }();
  AuraScan s{shownType, 133, false, false};
  fightFloorLoopValidFightOutPokemon(nullptr, ScanAura, &s, 0);
  if (s.found) damage = Scale(damage, s.broken ? 75 : s.percent);
  return damage;
}
}

uint16_t TypeOf(void* fop, uint8_t index, uint16_t type);
uint8_t WazaType(void* fop, uint8_t type) {
  if (!sLoaded || fop == nullptr) return type;
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(fop);
  if (move == 0) return type;
  if (const auto mit = sMoves.find(move); mit != sMoves.end() && mit->second->typeFromUser) return static_cast<uint8_t>(TypeOf(fop, 0, fightOutPokemonGetZokuseiDataId(fop, 0)));
  if (const xb::Convert* k = ConvertFor(fop, move, type); k != nullptr && wazaDB_GetIryoku(move) > 0) return static_cast<uint8_t>(k->to);
  return type;
}

bool StrongWindOn();
int32_t Damage(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage) {
  if (!sLoaded || attacker == nullptr || defender == nullptr || damage <= 2) return damage;
  if (xd::forms::DisguiseIntact(defender) && wazaDB_GetIryoku(waza) > 0) return 0;
  if (const int terrain = TerrainNow()) {
    const uint16_t ty = type;
    if (Grounded(attacker)) {
      if ((terrain == 1 && ty == 12) || (terrain == 2 && ty == 13) || (terrain == 3 && ty == 14)) damage = Scale(damage, 150);
    }
    if (Grounded(defender)) {
      if (terrain == 4 && ty == 16) damage = Scale(damage, 50);
      if (terrain == 1 && (waza == 89 || waza == 222 || waza == 543)) damage = Scale(damage, 50);
    }
  }
  damage = AuraAndConvert(attacker, defender, waza, type, damage);
  if (sAnyWind && StrongWindOn() && HasType(defender, 2) && zokuseiGetWazaJoutai(type, 2) == 0x41) {
    static int told = 0;
    if (told++ < 4) Log("Delta Stream: Flying's weakness to this move is gone", "");
    damage = Scale(damage, 50);
  }
  if (sVeilEnd[OnPlayersSide(defender) ? 0 : 1] >= sLastTurn && fightWaza_GetCritical(fightOutPokemon_GetFightWazaPtr(attacker)) != 2 &&
      !(sAnyInfiltrator && AbilityOf(attacker) != nullptr && AbilityOf(attacker)->infiltrator)) damage = Scale(damage, 67);
  const xb::AbilityDef* a = AbilityOf(attacker);
  const xb::AbilityDef* d = AbilityOf(defender);
  if (a == nullptr && d == nullptr) return damage;
  Cx atk;
  atk.holder = attacker; atk.other = defender; atk.move = waza; atk.type = type;
  Cx def;
  def.holder = defender; def.other = attacker; def.move = waza; def.type = type;
  PortXD_SplitCheckDarkWaza(waza);
  const int kind = PortXD_SplitTypeKind(type);
  if (a != nullptr) {
    for (const auto& r : a->damageDealt) if (All(r.when, atk)) damage = Scale(damage, r.percent);
    for (const auto& s : a->statMods) {
      if (!All(s.when, atk)) continue;
      if ((s.stat == 1 && kind == 1) || (s.stat == 4 && kind == 2)) damage = Scale(damage, s.percent);
    }
  }
  if (d != nullptr) {
    for (const auto& r : d->damageTaken) if (All(r.when, def)) damage = Scale(damage, r.percent);
    for (const auto& s : d->statMods) {
      if (!All(s.when, def)) continue;
      if ((s.stat == 2 && kind == 1) || (s.stat == 5 && kind == 2)) damage = Scale(damage, 10000 / s.percent);
    }
  }
  return damage;
}

uint8_t Accuracy(uint16_t waza, uint8_t avg) {
  if (sLoaded && avg != 0 && avg < 255) {
    void* atk = Target(0x11);
    void* def = Target(0x12);
    Cx cx;
    cx.holder = atk; cx.other = def; cx.move = waza;
    if (const xb::AbilityDef* d = AbilityOf(def)) for (const auto& r : d->accuracyAgainst) if (r.accuracy >= 0 && All(r.when, Cx{def, atk, waza, 0}) && avg > r.accuracy) avg = static_cast<uint8_t>(r.accuracy);
    if (const xb::AbilityDef* a = AbilityOf(atk)) for (const auto& r : a->accuracyDealt) if (All(r.when, cx)) avg = static_cast<uint8_t>(std::min(100, avg * r.percent / 100));
  }
  const auto it = sMoves.find(waza);
  if (it == sMoves.end() || it->second->accuracy.empty() || avg == 0) return avg;
  Cx cx;
  cx.holder = Target(0x11); cx.other = Target(0x12); cx.move = waza;
  for (const auto& r : it->second->accuracy) if (All(r.when, cx)) avg = static_cast<uint8_t>(r.accuracy);
  return avg;
}

uint32_t CriticalValue(uint32_t value) {
  if (!sLoaded || value != 1) return value;
  void* atk = Target(0x11);
  if (atk == nullptr) return value;
  const auto it = sMoves.find(fightOutPokemon_GetUseWazaDataId(atk));
  if (const xb::AbilityDef* a = AbilityOf(atk)) {
    Cx cx; cx.holder = atk; cx.other = Target(0x12); cx.move = fightOutPokemon_GetUseWazaDataId(atk);
    if (!a->critWhen.empty() && All(a->critWhen, cx)) {
      void* d = Target(0x12);
      const uint16_t ab = d != nullptr ? fightOutPokemonGetTokuseiDataId(d) : 0;
      if (ab != 4 && ab != 0x4B && wazaDB_GetIryoku(cx.move) > 0) return 2;
    }
  }
  if (it == sMoves.end() || it->second->crit != 1) return value;
  void* def = Target(0x12);
  if (def != nullptr) { const uint16_t ab = fightOutPokemonGetTokuseiDataId(def); if (ab == 4 || ab == 0x4B) return value; }
  return 2;
}

void AfterCritical() {
  if (!sLoaded) return;
  void* atk = Target(0x11);
  if (atk == nullptr) return;
  void* fw = fightOutPokemon_GetFightWazaPtr(atk);
  const uint32_t v = fightWaza_GetCritical(fw);
  const uint32_t nv = CriticalValue(v);
  if (nv != v) fightWaza_SetCritical(fw, nv);
}

namespace {
void* sFoulAtk = nullptr; void* sFoulDef = nullptr; void* sPsyDef = nullptr;
}
DamageGuard::DamageGuard(void* attacker, void* defender, uint16_t waza) {
  if (!sLoaded || attacker == nullptr || defender == nullptr) return;
  const auto it = sMoves.find(waza);
  if (it == sMoves.end()) return;
  const xb::MoveDef& m = *it->second;
  if (!m.ignoreBoosts && !m.useTargetAttack && !m.defendWithDef) return;
  atk_ = attacker; def_ = defender;
  auto* a = static_cast<uint8_t*>(attacker);
  auto* d = static_cast<uint8_t*>(defender);
  saved_[0] = fightOutPokemonBiosGetAbicntPhyAtk(a);
  saved_[1] = fightOutPokemonBiosGetAbicntPhyDef(d);
  saved_[2] = fightOutPokemonBiosGetAbicntSpeDef(d);
  on_ = true;
  if (m.ignoreBoosts) { fightOutPokemonBiosSetAbicntPhyDef(d, 6); fightOutPokemonBiosSetAbicntSpeDef(d, 6); }
  if (m.useTargetAttack) {
    fightOutPokemonBiosSetAbicntPhyAtk(a, fightOutPokemonBiosGetAbicntPhyAtk(d));
    sFoulAtk = fightOutPokemonGetPokemonPtr(attacker); sFoulDef = fightOutPokemonGetPokemonPtr(defender);
  }
  if (m.defendWithDef) {
    fightOutPokemonBiosSetAbicntSpeDef(d, m.ignoreBoosts ? 6 : saved_[1]);
    sPsyDef = fightOutPokemonGetPokemonPtr(defender);
  }
}
DamageGuard::~DamageGuard() {
  if (!on_) return;
  fightOutPokemonBiosSetAbicntPhyAtk(static_cast<uint8_t*>(atk_), saved_[0]);
  fightOutPokemonBiosSetAbicntPhyDef(static_cast<uint8_t*>(def_), saved_[1]);
  fightOutPokemonBiosSetAbicntSpeDef(static_cast<uint8_t*>(def_), saved_[2]);
  sFoulAtk = sFoulDef = sPsyDef = nullptr;
}
uint16_t PhyAtkRead(void* pokemon) {
  if (sFoulAtk != nullptr && pokemon == sFoulAtk) {
    static int shown = 0;
    if (shown++ < 3) std::fprintf(stderr, "[mod battle] Foul Play: Attack read from the target (%u instead of %u)\n", pokemon_GetPhyAtk(sFoulDef), pokemon_GetPhyAtk(pokemon));
    return pokemon_GetPhyAtk(sFoulDef);
  }
  return 0;
}
uint16_t SpeDefRead(void* pokemon) {
  if (sPsyDef != nullptr && pokemon == sPsyDef) {
    static int shown = 0;
    if (shown++ < 3) std::fprintf(stderr, "[mod battle] Psyshock: Sp. Def read as Defense (%u instead of %u)\n", pokemon_GetPhyDef(pokemon), pokemon_GetSpeDef(pokemon));
    return pokemon_GetPhyDef(pokemon);
  }
  return pokemon_GetSpeDef(pokemon);
}

uint32_t Speed(void* fop, uint32_t speed) {
  const xb::AbilityDef* a = AbilityOf(fop);
  if (a == nullptr) return speed;
  Cx cx;
  cx.holder = fop;
  for (const auto& s : a->statMods) if (s.stat == 3 && All(s.when, cx)) speed = speed * static_cast<uint32_t>(s.percent) / 100;
  return speed;
}

int PriorityOf(void* fop, uint16_t waza, int pri) {
  const xb::AbilityDef* a = AbilityOf(fop);
  if (a == nullptr || waza == 0) return pri;
  Cx cx;
  cx.holder = fop; cx.move = waza;
  for (const auto& r : a->priority) if (All(r.when, cx)) pri += r.add;
  return pri;
}

void FlushDrops();
bool StatusBlocked(void* p, uint8_t effect) {
  if (const int terrain = TerrainNow()) {
    const int e = effect & 0x3F;
    if (p != nullptr && Grounded(p) && ((terrain == 2 && e == 1) || (terrain == 4 && e >= 1 && e <= 7))) return true;
  }
  const xb::AbilityDef* a = AbilityOf(p);
  if (a == nullptr) return false;
  int e = effect & 0x3F;
  if (a->contrary && ((e >= 15 && e <= 28) || (e >= 39 && e <= 52))) {
    const bool up = (e >= 15 && e <= 21) || (e >= 39 && e <= 45);
    e = up ? e + 7 : e - 7;
    ServerWork[3] = static_cast<uint8_t>((ServerWork[3] & 0xC0) | e);
  }
  if (p == Target(0x11)) return false;
  if (e >= 1 && e <= 9) {
    for (int c : a->statusBlock) if (c == 0 || c == e) return true;
    return false;
  }
  int stat = 0;
  if (e >= 22 && e <= 28) stat = e - 21;
  else if (e >= 46 && e <= 52) stat = e - 45;
  if (stat != 0) {
    for (int s : a->statDropBlock) if (s == 0 || s == stat) return true;
    if (!a->statDropped.empty()) sDropReact = p;
  }
  return false;
}

namespace {
struct FieldScan { void* holder = nullptr; bool found = false; };
int32_t ScanUnnerve(void* f, uint16_t, void* data) {
  auto* s = static_cast<FieldScan*>(data);
  if (Alive(f) && f != s->holder && OnPlayersSide(f) != OnPlayersSide(s->holder)) { const xb::AbilityDef* a = AbilityOf(f); if (a != nullptr && a->unnerve) s->found = true; }
  return 1;
}
int32_t ScanWind(void* f, uint16_t, void* data) {
  if (Alive(f)) { const xb::AbilityDef* a = AbilityOf(f); if (a != nullptr && a->strongWind) static_cast<FieldScan*>(data)->found = true; }
  return 1;
}
}
bool StrongWindOn() {
  FieldScan s;
  fightFloorLoopValidFightOutPokemon(nullptr, ScanWind, &s, 0);
  return s.found;
}

uint16_t SoubiKind(void* fop, uint16_t kind) {
  if (sLoaded && sAnyUnnerve && kind != 0 && fop != nullptr && IsBerry(fightOutPokemonGetSoubiItemDataId(fop))) {
    FieldScan s; s.holder = fop;
    fightFloorLoopValidFightOutPokemon(nullptr, ScanUnnerve, &s, 0);
    if (s.found) {
      static int told = 0;
      if (told++ < 4) Log("Unnerve: a berry's effect is read as none", "");
      return 0;
    }
  }
  if (sRules == nullptr || sRules->bannedItems.empty() || fop == nullptr || kind == 0) return kind;
  const uint16_t item = fightOutPokemonGetSoubiItemDataId(fop);
  for (int b : sRules->bannedItems) if (b == item) return 0;
  return kind;
}

void CorrosionBegin() {
  sCorrode = nullptr;
  if (!sLoaded) return;
  const uint8_t v = ServerWork[3];
  const int e = v & 0x3F;
  { static int seen = 0; if (seen++ < 12) Log("status effect about to be applied: effect number ", std::to_string(v)); }
  if ((e != 2 && e != 6) || (v & 0x40) != 0) return;
  void* atk = Target(0x11);
  void* def = Target(0x12);
  const xb::AbilityDef* a = AbilityOf(atk);
  if (a != nullptr && a->poisonAnyType && def != nullptr && def != atk) {
    sCorrode = def;
    static int told = 0;
    if (told++ < 4) Log("Corrosion: poison may land on any type", "");
  }
}
bool CorrosionBeginType(uint8_t targetKind, uint8_t type) {
  sCorrode = nullptr;
  if (!sLoaded || targetKind != 0x12 || (type != 3 && type != 8)) return false;
  void* atk = Target(0x11);
  void* def = Target(0x12);
  const xb::AbilityDef* a = AbilityOf(atk);
  if (a == nullptr || !a->poisonAnyType || def == nullptr || def == atk) return false;
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(atk);
  if (wazaDB_GetZokuseiDataId(move) != 3) return false;
  sCorrode = def;
  static int told = 0;
  if (told++ < 4) Log("Corrosion: the poison move does not see the target's Poison / Steel type", "");
  return true;
}
void CorrosionEnd() { sCorrode = nullptr; }
bool TypeHiddenFrom(void* fop, uint16_t type) { return sCorrode != nullptr && sCorrode == fop && (type == 3 || type == 8); }

bool SideIgnored(void* side, uint16_t id) {
  if (!sLoaded || !sAnyInfiltrator || (id != 0x48 && id != 0x49 && id != 0x4B && id != 0x4C)) return false;
  void* atk = Target(0x11);
  const xb::AbilityDef* a = AbilityOf(atk);
  if (a == nullptr || !a->infiltrator) return false;
  using SideFn = void* (*)(uint32_t, void*);
  void* own = reinterpret_cast<SideFn>(&fightTargetGetPtrAsNowFightType)(2, atk);
  const bool ignored = side != own;
  static int told = 0;
  if (ignored && told++ < 4) Log(("Infiltrator: the other side's screen state " + std::to_string(id) + " is not seen").c_str(), "");
  return ignored;
}

bool IgnoresSubstitute(void* attacker) {
  if (!sLoaded || !sAnyInfiltrator) return false;
  const xb::AbilityDef* a = AbilityOf(attacker);
  static int told = 0;
  const bool yes = a != nullptr && a->infiltrator;
  if (yes && told++ < 4) Log("Infiltrator: the hit goes past the Substitute", "");
  return yes;
}

uint16_t TypeOf(void* fop, uint8_t index, uint16_t type) {
  if (!sLoaded || fop == nullptr) return type;
  if (sCorrode == fop && (type == 3 || type == 8)) return 9;
  if (const xb::AbilityDef* a = AbilityOf(fop); a != nullptr && !a->typeByItem.empty()) {
    const uint16_t item = fightOutPokemonGetSoubiItemDataId(fop);
    for (const auto& e : a->typeByItem) if (e.item == item) return index == 0 ? static_cast<uint16_t>(e.type) : 9;
  }
  for (const auto& m : sMons) {
    if (m.fop == fop && m.type >= 0) return index == 0 ? static_cast<uint16_t>(m.type) : 9;
  }
  return type;
}

uint32_t ActsAs(uint32_t id) {
  if (!sLoaded || sRawAbility || id >= 256 || sAbility[id] == nullptr || sAbility[id]->actsAs == 0) return id;
  return static_cast<uint32_t>(sAbility[id]->actsAs);
}

uint16_t InverseJoutai(uint16_t r) {
  if (sRules == nullptr || !sRules->inverse) return r;
  if (r == 0x41) return 0x42;
  if (r == 0x42 || r == 0x43) return 0x41;
  return r;
}
bool Rules() { return sRules != nullptr; }

bool AbsorbHit(void* attacker, void* defender, uint16_t waza, uint8_t* result) {
  if (!sLoaded || attacker == nullptr || defender == nullptr || attacker == defender) return false;
  const xb::AbilityDef* d = AbilityOf(defender);
  if (d == nullptr || (d->absorb.empty() && d->immuneMoves.empty()) || waza == 0 || wazaDB_GetIryoku(waza) == 0 || !Alive(defender)) return false;
  Tick();
  for (int m : d->immuneMoves) {
    if (m != waza) continue;
    EnsureInstalled();
    sImmune.messageId = sMsgImmune;
    sAbsorbing = d;
    sAbsorbEntry = &sImmune;
    fightSeq_JumpWazaSeq(ScriptAt(kOffAbsorb + (fightOutPokemon_GetPassPpdecFlag(attacker) ? 0x20 : 0)));
    *result = 1;
    return true;
  }
  const uint16_t type = wazaDB_GetZokuseiDataId(waza);
  for (const auto& o : d->absorb) {
    if (o.type != type) continue;
    EnsureInstalled();
    sAbsorbing = d;
    sAbsorbEntry = &o;
    fightSeq_JumpWazaSeq(ScriptAt(kOffAbsorb + (fightOutPokemon_GetPassPpdecFlag(attacker) ? 0x20 : 0)));
    *result = 1;
    return true;
  }
  return false;
}

void NoteDamage(void* attacker, void* defender, int32_t dealt) {
  if (!sLoaded) return;
  (void)attacker;
  sDealt = dealt;
  sDealtTo = defender;
}

void FlushDrops() {
  if (sDropReact == nullptr) return;
  void* p = sDropReact;
  sDropReact = nullptr;
  const xb::AbilityDef* a = AbilityOf(p);
  if (a == nullptr || !Alive(p)) return;
  EnsureInstalled();
  void* foes[8] = {};
  uint16_t n = 0;
  Foes(p, foes, n);
  Run run; run.user = p; run.other = n > 0 ? foes[0] : nullptr;
  RunBlocks(a->statDropped, run);
}

void CheckBerry(void* fop) {
  if (fop == nullptr || !sLoaded) return;
  const uint16_t used = fightOutPokemon_GetUsedItemDataId(fop);
  Mon& m = MonOf(fop);
  if (!IsBerry(used)) { m.berryNoted = false; return; }
  if (m.berryNoted || !Alive(fop)) return;
  m.berryNoted = true;
  const xb::AbilityDef* a = AbilityOf(fop);
  if (a == nullptr || a->berryEaten.empty()) return;
  Log("Cheek Pouch: a berry was eaten: item ", std::to_string(used));
  EnsureInstalled();
  void* foes[8] = {};
  uint16_t n = 0;
  Foes(fop, foes, n);
  Run run; run.user = fop; run.other = n > 0 ? foes[0] : nullptr;
  RunBlocks(a->berryEaten, run);
}

void DisableMove(void* atk, uint16_t move) {
  if (!Alive(atk) || move == 0 || fightOutPokemonCheckWriteJoutaiDataId(atk, 0x29) != 2) return;
  fightOutPokemonWriteJoutaiDataId(atk, 0x29, 0);
  fightOutPokemonSetIdBuffJoutaiDataId(atk, 0x29, move);
  Log("Cursed Body: the attacker's move is disabled: move ", std::to_string(move));
  Say(sMsgDisabled, atk);
}

void SwitchOwner(void* p) {
  if (!Alive(p)) return;
  sPendingSwitch = p;
}

struct DanceScan { void* user; uint16_t move; void* found; };
int32_t ScanDancer(void* f, uint16_t, void* data) {
  auto* s = static_cast<DanceScan*>(data);
  if (s->found != nullptr || f == s->user || !Alive(f)) return 1;
  const xb::AbilityDef* a = AbilityOf(f);
  if (a == nullptr) return 1;
  for (int m : a->dancer) if (m == s->move) { s->found = f; break; }
  return 1;
}

bool TryDance() {
  if (!sLoaded || !sAnyDancer) return false;
  void* user = Target(0x11);
  if (user == nullptr) return false;
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(user);
  if (move == 0) return false;
  DanceScan s{user, move, nullptr};
  fightFloorLoopValidFightOutPokemon(nullptr, ScanDancer, &s, 0);
  if (s.found == nullptr) return false;
  EnsureInstalled();
  sDanceMove = move;
  sSwitchKeepAtk = user;
  fightFloor_SetAttackPokemonPtr(nullptr, s.found);
  fightWazaSetUseWazaStatus(fightOutPokemon_GetFightWazaPtr(s.found), move);
  fightSeq_JumpWazaSeq(PortXD_GameAddress(kScriptBase2 + kOff2Dance));
  return true;
}

bool WazaEndHook() {
  if (sLoaded) { CheckBerry(Target(0x12)); CheckBerry(Target(0x11)); }
  if (sPendingSwitch == nullptr) {
    if (sSwitchKeepAtk != nullptr) { fightFloor_SetAttackPokemonPtr(nullptr, sSwitchKeepAtk); sSwitchKeepAtk = nullptr; sDanceMove = 0; return false; }
    return TryDance();
  }
  void* p = sPendingSwitch;
  sPendingSwitch = nullptr;
  if (!sLoaded || !Alive(p)) return false;
  EnsureInstalled();
  if (!OnPlayersSide(p) && fightEncountDataBiosGetFightKind(fightEncountDataBiosGetPtr(static_cast<uint16_t>(fightFloor_GetEncountDataId(nullptr)))) == 0xC) {
    const auto* table = static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kRoarSeq));
    const uint32_t roar = table != nullptr ? GetBe32(table) : 0;
    const auto* bytes = roar != 0 ? static_cast<const uint8_t*>(PortXD_GameAddress(roar + 35)) : nullptr;
    if (bytes != nullptr && bytes[0] == 0x90) {
      Log("Wimp Out / Emergency Exit: a wild Pokemon flees (the game's own forced-out script ends the battle)", "");
      fightFloor_SetDefPokemonAndAtkFightWazaTargetId(nullptr, p);
      fightSeq_JumpWazaSeq(PortXD_GameAddress(roar + 35));
      return true;
    }
  }
  sSwitchKeepAtk = Target(0x11);
  fightFloor_SetAttackPokemonPtr(nullptr, p);
  Log("Wimp Out / Emergency Exit: the owner goes back at the end of the move", "");
  fightSeq_JumpWazaSeq(PortXD_GameAddress(kScriptBase2 + kOff2Switch));
  return true;
}

void HitAfter(void* attacker, void* defender) {
  FlushDrops();
  if (sLoaded) { CheckBerry(attacker); CheckBerry(defender); }
  if (sLoaded && attacker != nullptr && defender != nullptr && attacker != defender && Alive(defender) &&
      wazaDB_GetIryoku(fightOutPokemon_GetUseWazaDataId(attacker)) > 0 && xd::forms::DisguiseIntact(defender)) {
    EnsureInstalled();
    const auto it = sBlockScript.find(&sDisguiseHurt);
    HurtWith(defender, std::max<uint32_t>(1, fightOutPokemonMaxHpWaruValue(defender, 8)), it == sBlockScript.end() ? -1 : it->second, false);
    if (Alive(defender)) xd::forms::DisguiseBust(defender);
    sDealt = 0;
    return;
  }
  if (!sLoaded || attacker == nullptr || defender == nullptr || attacker == defender || sDealtTo != defender || sDealt <= 0) return;
  Tick();
  EnsureInstalled();
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(attacker);
  void* pk = fightOutPokemonGetPokemonPtr(defender);
  const bool ko = pk != nullptr && pokemon_GetHp(pk) == 0;
  xd::forms::BattleFormHit(attacker, defender, move, ko);
  if (const xb::AbilityDef* d = AbilityOf(defender)) {
    const xb::AbilityDef* atkAb = AbilityOf(attacker);
    if (!d->contact.empty() && wazaDB_GetDageki(move) != 0 && !(atkAb != nullptr && atkAb->noContact)) {
      Run run; run.user = defender; run.other = attacker; run.move = move;
      RunBlocks(d->contact, run);
    }
  }
  if (const xb::AbilityDef* d = AbilityOf(defender)) {
    if (!ko && !d->hit.empty()) { Run run; run.user = defender; run.other = attacker; run.move = move; RunBlocks(d->hit, run); }
    if (ko && !d->fainted.empty()) { Run run; run.user = defender; run.other = attacker; run.move = move; RunBlocks(d->fainted, run, false, true); }
  }
  if (const xb::AbilityDef* a = AbilityOf(attacker)) {
    if (!a->hitDealt.empty() && Alive(attacker) && wazaDB_GetIryoku(move) > 0) { Run run; run.user = attacker; run.other = defender; run.move = move; RunBlocks(a->hitDealt, run); }
  }
  if (ko) {
    for (uint8_t b = 0; b < 4; b++) {
      void* o = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
      const xb::AbilityDef* ad = Alive(o) && o != defender ? AbilityOf(o) : nullptr;
      if (ad == nullptr || ad->anyFaint.empty()) continue;
      Run run; run.user = o; run.other = defender; run.move = move;
      RunBlocks(ad->anyFaint, run);
    }
  }
  if (ko) {
    if (const xb::AbilityDef* a = AbilityOf(attacker)) {
      if (!a->ko.empty() && Alive(attacker)) {
        Run run; run.user = attacker; run.other = defender; run.move = move;
        RunBlocks(a->ko, run);
      }
    }
  }
  {
    Mon& dm = MonOf(defender);
    if (dm.shellArmed) {
      Cx pcx; pcx.holder = defender; pcx.other = attacker; pcx.move = move;
      xb::Condition ph; ph.kind = xb::Cond::Physical;
      if (Test(ph, pcx)) { dm.shellHit = true; Log("Shell Trap: a physical move hit the waiting user", ""); }
    }
    if (dm.heating && wazaDB_GetDageki(move) != 0 && Alive(attacker)) {
      const xb::AbilityDef* atkAb = AbilityOf(attacker);
      if (!(atkAb != nullptr && atkAb->noContact)) { Log("Beak Blast: a contact move hit the heating beak: the attacker is burned", ""); ApplyTo(attacker, 3); }
    }
  }
  if (!ko && Alive(attacker) && Alive(defender) && wazaDB_GetIryoku(move) > 0) {
    const xb::AbilityDef* d = AbilityOf(defender);
    if (d != nullptr && d->disableOnHit > 0 && static_cast<int>(HSD_Rand() % 100) < d->disableOnHit) DisableMove(attacker, move);
  }
  if (!ko && Alive(defender)) {
    const xb::AbilityDef* d = AbilityOf(defender);
    if (d != nullptr && !d->halfHp.empty()) {
      Cx cx; cx.holder = defender; cx.other = attacker; cx.move = move;
      xb::Condition cross; cross.kind = xb::Cond::HpCrossedHalf;
      if (Test(cross, cx)) {
        bool sw = false;
        std::vector<xb::Block> rest;
        for (const auto& b : d->halfHp) { if (b.kind == xb::BlockKind::SwitchOut) sw = true; else rest.push_back(b); }
        Run run; run.user = defender; run.other = attacker; run.move = move;
        if (!rest.empty()) RunBlocks(rest, run);
        if (sw && Alive(defender)) SwitchOwner(defender);
      }
    }
  }
  if (const auto mit = sMoves.find(move); mit != sMoves.end() && mit->second->chargeDonor != 0 && Alive(attacker) && !mit->second->effects.empty()) {
    Run run; run.user = attacker; run.other = defender; run.move = move;
    RunBlocks(mit->second->effects, run);
  }
  sDealt = 0;
}

uint32_t MoveTypeFromUser(void* attacker, void* fightWaza, uint32_t type) {
  if (!sLoaded || attacker == nullptr || fightOutPokemon_GetFightWazaPtr(attacker) != fightWaza) return type;
  const auto mit = sMoves.find(fightOutPokemon_GetUseWazaDataId(attacker));
  if (mit == sMoves.end() || !mit->second->typeFromUser) return type;
  return TypeOf(attacker, 0, fightOutPokemonGetZokuseiDataId(attacker, 0));
}
void ChosenMove(void* fop, uint16_t move) {
  if (!sLoaded || fop == nullptr || move == 0) return;
  const auto it = sMoves.find(move);
  if (it == sMoves.end() || (!it->second->heatsUp && !it->second->needsPhysicalHit)) return;
  Tick();
  Mon& m = MonOf(fop);
  if (it->second->heatsUp && !m.heating) { m.heating = true; Log("Beak Blast: the user is heating up its beak (a contact move on it burns the attacker until its move)", ""); }
  if (it->second->needsPhysicalHit && !m.shellArmed) { m.shellArmed = true; m.shellHit = false; }
}

void MoveStarting(void* attacker, uint16_t move) {
  if (!sLoaded || attacker == nullptr) return;
  Tick();
  MonOf(attacker).heating = false;
  std::fprintf(stderr, "[mod battle] move used: turn %d, %s, move %u\n", sLastTurn, OnPlayersSide(attacker) ? "player's side" : "foe's side", static_cast<unsigned>(move));
  if (const auto cit = sMoves.find(move); cit != sMoves.end() && cit->second->chargeDonor != 0) {
    Mon& cm = MonOf(attacker);
    if (cm.charging == static_cast<int>(move)) {
      cm.charging = 0;
      if (wazaDB_GetIryoku(move) == 0 && !cit->second->effects.empty()) {
        EnsureInstalled();
        Run run; run.user = attacker; run.other = Target(0x12); run.move = move;
        RunBlocks(cit->second->effects, run);
      }
    } else {
      cm.charging = static_cast<int>(move);
    }
  }
  { const auto pit = sMoves.find(move); MonOf(attacker).protect = pit != sMoves.end() ? pit->second->protectKind : 0; }
  xd::forms::BattleFormMove(attacker, move, MonOf(attacker).protect == 1, wazaDB_GetIryoku(move) > 0);
  const xb::AbilityDef* a = AbilityOf(attacker);
  if (a == nullptr || a->beforeMove.empty()) return;
  Run run; run.user = attacker; run.other = Target(0x12); run.move = move;
  RunBlocks(a->beforeMove, run);
}

void Appear(void* p) {
  if (!sLoaded || !Alive(p)) return;
  Tick();
  EnsureInstalled();
  FlushDrops();
  Mon& m = MonOf(p);
  m = Mon();
  m.fop = p;
  m.entry = sLastTurn;
  m.berryNoted = IsBerry(fightOutPokemon_GetUsedItemDataId(p));
  if (sRules != nullptr && sRules->weather != 0 && !sRuleWeatherDone) {
    sRuleWeatherDone = true;
    StartWeather(p, sRules->weather);
  }
  if (sWeb[OnPlayersSide(p) ? 0 : 1] && Grounded(p)) { Say(sMsgWebHit, p); ChangeStat(p, 3, -1); }
  xd::forms::BattleFormEntry(p);
  const xb::AbilityDef* a = AbilityOf(p);
  if (a == nullptr || a->switchIn.empty()) return;
  void* foes[8] = {};
  uint16_t n = 0;
  Foes(p, foes, n);
  Run run; run.user = p; run.other = n > 0 ? foes[0] : nullptr;
  RunBlocks(a->switchIn, run);
}

void SwitchingOut(void* p) {
  if (!sLoaded || !Alive(p)) return;
  Tick();
  MonOf(p).type = -1;
  const xb::AbilityDef* a = AbilityOf(p);
  if (a == nullptr || a->switchOut.empty()) return;
  EnsureInstalled();
  Run run; run.user = p; run.silent = true;
  RunBlocks(a->switchOut, run);
}

namespace {
int32_t EachMon(void* fop, uint16_t, void*) {
  if (!Alive(fop)) return 1;
  const xb::AbilityDef* a = AbilityOf(fop);
  if (a == nullptr || a->endOfTurn.empty()) return 1;
  void* foes[8] = {};
  uint16_t n = 0;
  Foes(fop, foes, n);
  Run run; run.user = fop; run.other = n > 0 ? foes[0] : nullptr; run.turnEnd = true;
  RunBlocks(a->endOfTurn, run);
  return 1;
}
}

void EndOfTurn() {
  if (!sLoaded) return;
  Tick();
  EnsureInstalled();
  FlushDrops();
  for (auto& m : sMons) { m.heating = false; m.shellArmed = false; m.shellHit = false; }
  fightFloorLoopValidFightOutPokemon(nullptr, [](void* fop, uint16_t, void*) -> int32_t { CheckBerry(fop); return 1; }, nullptr, 0);
  fightFloorLoopValidFightOutPokemon(nullptr, EachMon, nullptr, 0);
  xd::forms::BattleFormTurnEnd();
  if (sTerrain != 0 && sTerrainEnd >= sLastTurn && sTerrain == 1) {
    fightFloorLoopValidFightOutPokemon(nullptr, [](void* fop, uint16_t, void*) -> int32_t {
      if (Alive(fop) && Grounded(fop) && fightOutPokemonIsHpMantan(fop) == 0) {
        const auto it = sBlockScript.find(&sTerrainHeal);
        HealWith(fop, std::max<uint32_t>(1, fightOutPokemonMaxHpWaruValue(fop, 16)), it == sBlockScript.end() ? -1 : it->second, false);
      }
      return 1;
    }, nullptr, 0);
  }
  if (sTerrain != 0 && sTerrainEnd == sLastTurn) {
    void* any = nullptr;
    fightFloorLoopValidFightOutPokemon(nullptr, [](void* fop, uint16_t, void* out) -> int32_t {
      if (*static_cast<void**>(out) == nullptr && Alive(fop)) *static_cast<void**>(out) = fop;
      return 1;
    }, &any, 0);
    if (any != nullptr) Say(sMsgTerrainEnd[sTerrain], any);
    sTerrain = 0; sTerrainEnd = -1;
  }
  for (size_t i = 0; i < sFieldEnd.size(); i++) {
    if (sFieldEnd[i] >= 0 && sFieldEnd[i] == sLastTurn) {
      sFieldEnd[i] = -1;
      std::fprintf(stderr, "[mod battle] field %s ends\n", sDefs.fieldNames[i].c_str());
    }
  }
}

void BadgeCheck() {
  if (!sLoaded) return;
  void* atk = Target(0x11);
  void* def = Target(0x12);
  if (atk == nullptr || def == nullptr || atk == def || !Alive(def) || (ServerStatusFlag & 0x02000000u) != 0) return;
  const xb::AbilityDef* d = AbilityOf(def);
  if (d == nullptr || !d->reflectStatus || AbilityOf(atk) == d) return;
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(atk);
  if (wazaDB_GetMajikku(move) != 1) return;
  if (fightOutPokemonCheckWriteJoutaiDataId(def, 0x37) == 2) {
    fightOutPokemonWriteJoutaiDataId(def, 0x37, 0);
    sBounceFop = def;
  }
}
void ProtectPenalty(void* atk, void* def, uint16_t move) {
  if (!sLoaded || atk == nullptr || def == nullptr || !Alive(atk)) return;
  const int kind = MonOf(def).protect;
  if (kind == 0 || wazaDB_GetDageki(move) == 0) return;
  EnsureInstalled();
  if (kind == 1) ChangeStat(atk, 1, -2);
  else if (kind == 3) ApplyTo(atk, 2);
  else if (kind == 2) {
    const auto it = sBlockScript.find(&sSpikyHurt);
    HurtWith(atk, std::max<uint32_t>(1, fightOutPokemonMaxHpWaruValue(atk, 8)), it == sBlockScript.end() ? -1 : it->second, false);
  }
}
bool PriorityBlocked() {
  if (!sLoaded) return false;
  void* atk = Target(0x11);
  void* def = Target(0x12);
  if (atk == nullptr || def == nullptr || atk == def || !Alive(def) || OnPlayersSide(atk) == OnPlayersSide(def)) return false;
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(atk);
  if (move == 0 || PriorityOf(atk, move, wazaDB_GetPri(move)) <= 0) return false;
  const xb::AbilityDef* d = AbilityOf(def);
  const bool ability = d != nullptr && d->blocksPriority;
  const bool terrain = TerrainNow() == 3 && Grounded(def);
  if (!ability && !terrain) return false;
  EnsureInstalled();
  fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage));
  return true;
}

void StatsMayHaveFallen(void* def, const uint8_t before[7]) {
  if (!sLoaded || def == nullptr) return;
  const xb::AbilityDef* a = AbilityOf(def);
  if (a == nullptr || a->statDropped.empty()) return;
  auto* p = static_cast<uint8_t*>(def);
  const uint8_t now[7] = {fightOutPokemonBiosGetAbicntPhyAtk(p), fightOutPokemonBiosGetAbicntPhyDef(p), fightOutPokemonBiosGetAbicntNimbleness(p),
                          fightOutPokemonBiosGetAbicntSpeAtk(p), fightOutPokemonBiosGetAbicntSpeDef(p), fightOutPokemonBiosGetAbicntAverage(p),
                          fightOutPokemonBiosGetAbicntAvoid(p)};
  for (int i = 0; i < 7; i++) {
    if (now[i] < before[i]) { sDropReact = def; FlushDrops(); return; }
  }
}

void BadgeCheckDone() {
  if (sBounceFop != nullptr) { fightOutPokemonInitJoutaiDataId(sBounceFop, 0x37); sBounceFop = nullptr; }
}

void TsuikaCheck() {
  if (!sLoaded) return;
  void* atk = Target(0x11);
  const xb::AbilityDef* a = AbilityOf(atk);
  if (a != nullptr && a->removesSecondary && atk != nullptr && wazaDB_GetIryoku(fightOutPokemon_GetUseWazaDataId(atk)) > 0) ServerWork[3] = 0;
}

struct RealMon { uint8_t level; uint32_t exp; uint16_t hp, maxHp; };
static std::map<void*, RealMon> sReal;

static int ScaledHp(int hp, int fromMax, int toMax) {
  if (hp <= 0 || fromMax <= 0) return 0;
  int v = static_cast<int>((static_cast<int64_t>(hp) * toMax + fromMax / 2) / fromMax);
  if (v < 1) v = 1;
  if (v > toMax) v = toMax;
  return v;
}

void BattleEnded() {
  if (!sLoaded) return;
  ResetBattle();
  if (sRules != nullptr) Log("rules back to normal: ", sRules->name);
  sRules = nullptr;
}

void RuleLevel(void* pokemon) {
  if (sRules == nullptr || pokemon == nullptr) return;
  const int now = pokemonBiosGetLevel(pokemon);
  int target = now;
  if (sRules->levelAll > 0) target = sRules->levelAll;
  else if (sRules->levelCap > 0 && now > sRules->levelCap) target = sRules->levelCap;
  if (target != now && target >= 1 && target <= 100) {
    const RealMon real{static_cast<uint8_t>(now), pokemon_GetExp(pokemon), pokemon_GetHp(pokemon), pokemon_GetMaxHp(pokemon)};
    PortXD_PokemonSetLevelStats(pokemon, static_cast<uint8_t>(target));
    const int newMax = pokemon_GetMaxHp(pokemon);
    pokemon_SetHp(pokemon, static_cast<uint16_t>(ScaledHp(real.hp, real.maxHp, newMax)));
    pokemon_SetExp(pokemon, pokemonGetLevelToExp(pokemon, static_cast<uint8_t>(target)));
    sReal[pokemon] = real;
    std::fprintf(stderr, "[mod battle] rule %s: level %d -> %d (real level %d, exp %u, HP %u/%u kept for the end)\n", sRules->name.c_str(), now, target, now,
                 static_cast<unsigned>(real.exp), static_cast<unsigned>(real.hp), static_cast<unsigned>(real.maxHp));
  }
}

void ForgetCopy(void* pokemon) { sReal.erase(pokemon); }

bool RestoreRealLevels(void* trainer) {
  if (sReal.empty() || trainer == nullptr) return false;
  bool any = false;
  for (uint16_t i = 0; i < 6; i++) {
    void* fp = fightTrainerGetValidFightPokemonPtr(trainer, i);
    if (fp == nullptr) continue;
    void* buff = fightPokemon_GetPokemonBuffPtr(fp);
    auto it = sReal.find(buff);
    if (it == sReal.end()) continue;
    const RealMon real = it->second;
    const int curHp = pokemon_GetHp(buff), curMax = pokemon_GetMaxHp(buff);
    pokemon_SetExp(buff, real.exp);
    PortXD_PokemonSetLevelStats(buff, real.level);
    const int newMax = pokemon_GetMaxHp(buff);
    const int hp = ScaledHp(curHp, curMax, newMax);
    pokemon_SetHp(buff, static_cast<uint16_t>(hp));
    fightPokemon_SetLevelUpFlag(fp, 0);
    std::fprintf(stderr, "[mod battle] level back to the real %d (exp %u, HP %d/%d)\n", static_cast<int>(real.level), static_cast<unsigned>(real.exp), hp, newMax);
    sReal.erase(it);
    any = true;
  }
  return any;
}

bool SetRules(const std::string& name) {
  if (name.empty() || name == "none") { sRules = nullptr; return true; }
  auto low = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
  for (const auto& r : sDefs.ruleSets) {
    if (low(r.name) == low(name)) {
      sRules = &r;
      sRuleWeatherDone = false;
      Log("rules for the battle: ", r.name);
      if (r.forceDouble) Log("force_double is not available yet (the rule set ", r.name + " keeps the battle's own style)");
      return true;
    }
  }
  return false;
}

}

extern "C" {

static int InfiltratorIgnoresSubstitute(void* attacker, void*, uint16_t) { return xd::modbattle::IgnoresSubstitute(attacker) ? 1 : 0; }
extern "C" int (*PortXD_IgnoresSubstitute)(void* attacker, void* target, uint16_t waza);
namespace {
struct SubstituteHookInstaller { SubstituteHookInstaller() { PortXD_IgnoresSubstitute = &InfiltratorIgnoresSubstitute; } } sSubstituteHookInstaller;
}

uint8_t PortXD_RealSideJoutai(void* side, uint16_t id);
uint8_t fightSideIsJoutaiDataId(void* side, uint16_t id) {
  const uint8_t r = PortXD_RealSideJoutai(side, id);
  if (r == 1 && xd::modbattle::SideIgnored(side, id)) return 0;
  return r;
}

void PortXD_ModBackFightPokemon(void* trainer, uint8_t restoreItem) {
  const bool touched = xd::modbattle::RestoreRealLevels(trainer);
  fightTrainerBackFightPokemonToTemotiPokemon(trainer, restoreItem);
  if (touched) {
    for (uint16_t i = 0; i < 6; i++) {
      void* fp = fightTrainerGetValidFightPokemonPtr(trainer, i);
      void* moto = fp ? fightPokemon_GetMotoPokemonPtr(fp) : nullptr;
      if (moto) std::fprintf(stderr, "[mod battle] copied back into the party: slot %u level %u exp %u HP %u/%u\n", i, static_cast<unsigned>(pokemonBiosGetLevel(moto)),
                             static_cast<unsigned>(pokemon_GetExp(moto)), static_cast<unsigned>(pokemon_GetHp(moto)), static_cast<unsigned>(pokemon_GetMaxHp(moto)));
    }
  }
}

void PortXD_ModBadgeCheck(void) {
  if (xd::modbattle::PriorityBlocked()) return;
  xd::modbattle::BadgeCheck();
  WS_BADGECHECK();
  xd::modbattle::BadgeCheckDone();
}
void PortXD_ModCondChg(void) {
  void* atk = fightTargetGetPtrAsNowFightType(0x11, 0);
  void* def = fightTargetGetPtrAsNowFightType(0x12, 0);
  const bool track = xd::modbattle::Active() && atk != nullptr && def != nullptr && atk != def;
  uint8_t before[7] = {};
  if (track) {
    auto* p = static_cast<uint8_t*>(def);
    before[0] = fightOutPokemonBiosGetAbicntPhyAtk(p); before[1] = fightOutPokemonBiosGetAbicntPhyDef(p); before[2] = fightOutPokemonBiosGetAbicntNimbleness(p);
    before[3] = fightOutPokemonBiosGetAbicntSpeAtk(p); before[4] = fightOutPokemonBiosGetAbicntSpeDef(p); before[5] = fightOutPokemonBiosGetAbicntAverage(p);
    before[6] = fightOutPokemonBiosGetAbicntAvoid(p);
  }
  WS_CONDCHG();
  if (track) xd::modbattle::StatsMayHaveFallen(def, before);
}
void PortXD_ModGuardCheck(void) {
  void* atk = fightTargetGetPtrAsNowFightType(0x11, 0);
  void* def = fightTargetGetPtrAsNowFightType(0x12, 0);
  const uint16_t move = atk != nullptr ? fightOutPokemon_GetUseWazaDataId(atk) : 0;
  const bool blocked = xd::modbattle::Active() && atk != nullptr && def != nullptr && move != 0 && wazaDB_GetMamoru(move) == 1 && fightOutPokemonIsJoutaiDataId(def, 0x2B) == 1;
  WS_GUARDCHECK();
  if (blocked) xd::modbattle::ProtectPenalty(atk, def, move);
}
void PortXD_ModCheckType(void) {
  const auto* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
  const bool on = cmd != nullptr && xd::modbattle::CorrosionBeginType(cmd[1], cmd[2]);
  WS_CHECK_TYPE();
  if (on) {
    const auto* after = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
    static int told = 0;
    if (told++ < 6) std::fprintf(stderr, "[mod battle] Corrosion: the type check %s\n", after == cmd + 7 ? "passed (the script goes on)" : "still refused (the script jumped)");
    xd::modbattle::CorrosionEnd();
  }
}
uint8_t PortXD_ModIsType(void* fop, uint32_t type) {
  const uint8_t r = fightOutPokemonIsZokuseiDataId(fop, static_cast<uint16_t>(type));
  return r != 0 && xd::modbattle::TypeHiddenFrom(fop, static_cast<uint16_t>(type)) ? 0 : r;
}
void PortXD_ModWazaEnd(void) {
  if (xd::modbattle::WazaEndHook()) return;
  WS_WAZAEND();
}
void PortXD_ModTsuikaCheck(void) {
  xd::modbattle::TsuikaCheck();
  WS_TSUIKA_CHECK();
}
void PortXD_ModSizenkaifuku(void) {
  const auto* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
  if (cmd != nullptr && xd::modbattle::Active()) xd::modbattle::SwitchingOut(fightTargetGetPtrAsNowFightType(cmd[1], 0));
  WS_SIZENKAIHUKU();
}

static void* sCmpFop[2];
static int sCmpN = 0, sPriN = 0;
uint16_t PortXD_ModCmpWaza(void* fop) {
  const uint16_t chosen = fightOutPokemonGetCmpNimblenessWazaDataId(fop);
  if (xd::modbattle::Active()) {
    if (sCmpN >= 2) sCmpN = 0;
    sCmpFop[sCmpN++] = fop;
    xd::modbattle::ChosenMove(fop, chosen);
  }
  return chosen;
}
int8_t PortXD_ModPri(uint16_t waza) {
  const int32_t pri = wazaDB_GetPri(waza);
  if (!xd::modbattle::Active()) return static_cast<int8_t>(pri);
  void* fop = sCmpN == 2 ? sCmpFop[sPriN] : nullptr;
  sPriN = (sPriN + 1) % 2;
  if (sPriN == 0) sCmpN = 0;
  return static_cast<int8_t>(xd::modbattle::PriorityOf(fop, waza, pri));
}

}
