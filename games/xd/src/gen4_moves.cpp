#include "xd_challenges.h"
#include "xd_gen4_abilities.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

extern "C" {
void* wazaDataBiosGetPtr(uint16_t index);
int32_t PortXD_RealDamageCalc(void* attacker, void* defender, void* attackSide, uint16_t waza, uint16_t iryoku, int32_t zokusei);
uint16_t zokuseiGetWazaJoutai(uint16_t wazaType, uint16_t defenderType);
uint16_t fightOutPokemonGetTokuseiDataId(void* fop);
uint16_t fightOutPokemonGetZokuseiDataId(void* fop, uint8_t index);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
uint8_t fightWaza_GetCritical(void* waza);
uint16_t wazaDB_GetIryoku(uint16_t waza);
uint16_t wazaDB_GetZokuseiDataId(uint16_t waza);
uint16_t wazaDB_GetSeqId(uint16_t waza);
uint8_t wazaDB_GetAvg(uint16_t waza);
void* fightTargetGetPtrAsNowFightType(uint32_t kind, uint32_t arg);
uint32_t fightOutPokemonGetNowNimbleness(void* fop, uint8_t useBadge, uint8_t weather, uint16_t rnd, void* hero);
uint16_t fightFloor_GetTurnCount(void* floor);
void fightSeq_CallWazaSeqCustom(void* floor, void* script, uint32_t flag);
void PortVideo_Notice(const char* text, double seconds);
uint32_t PortXD_SplitCheckDarkWaza(uint16_t waza);
int32_t PortXD_SplitTypeKind(uint16_t type);
uint32_t fightFloorGetNowTenkouDataId(void* floor, uint8_t now);
uint16_t fightOutPokemonMaxHpWaruValue(void* fop, uint16_t divisor);
void fightWaza_SetDamage(void* fw, int32_t damage);
void fightWazaInitJoutai(void* fw);
void fightWazaBiosCopy(void* dst, void* src);
void fightFloor_SetAppointPokemonPtr(void* floor, void* fop);
uint8_t fightSide_GetMakibisiCheckFlag(void* side);
void fightSide_SetMakibisiCheckFlag(void* side, uint8_t flag);
uint8_t fightOutPokemonCheckFightOut(void* fop);
uint8_t fightOutPokemonIsHpMantan(void* fop);
uint16_t fightOutPokemon_GetUseWazaDataId(void* fop);
void fightWaza_SetKaisuu(void* waza, uint8_t count);
void fightOutPokemonSetTokuseiDataId(void* fop, uint16_t id);
void* fightSeq_GetWazaSeq(void);
void fightSeq_CallWazaSeq(void* script);
void fightSeq_NextWazaSeq(int32_t size);
void WS_POKE_RESHUFFLE_END(void);
void WS_RENZOKU(void);
uint8_t PortXD_RealTokuseiWazaHit(void* attacker, void* defender, uint16_t waza);
extern uint8_t ServerWork[8];
uint8_t fightSeqOpenMsg(uint32_t msg);
void fightMsgctrlSetValue(int32_t id, uintptr_t value);
void fightMainWaitFrame(uint32_t frames);
void fightMenuCloseMsg(void);
void fightSeq_JumpWazaSeq(void* target);
void fightFloor_SetAttackPokemonPtr(int32_t floor, void* fop);
void fightSeqFloorJoutaiCheck(void);
void WS_KAIFUKU(void);
uint16_t fightOutPokemonNowHpWaruValue(void* fop, uint8_t divisor);
uint8_t fightOutPokemonIsJoutaiDataId(void* fop, uint16_t id);
void fightOutPokemonInitJoutaiDataId(void* fop, uint16_t id);
uint8_t fightOutPokemonIsJoutaiNormal(void* fop);
uint8_t fightOutPokemonGetSex(void* fop);
uint8_t fightOutPokemonIsGcHeroFightOutPokemon(void* fop);
uint32_t fightOutPokemonGetSoubiItemSoubiDataId(void* fop);
uint16_t fightOutPokemonGetSoubiItemDataId(void* fop);
uint32_t fightOutPokemon_GetDamageAtkValue(void* fop);
uint32_t fightOutPokemon_GetDamageSpeValue(void* fop);
uint16_t fightOutPokemon_GetLastSelectWazaDataId(void* fop);
void fightSideInitJoutaiDataId(void* side, uint16_t id);
uint8_t fightSideIsJoutaiDataId(void* side, uint32_t id);
uint8_t fightOutPokemonBiosGetAbicntPhyAtk(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntPhyDef(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntSpeAtk(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntSpeDef(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntNimbleness(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntAverage(uint8_t* p);
uint8_t fightOutPokemonBiosGetAbicntAvoid(uint8_t* p);
void fightOutPokemonBiosSetAbicntPhyAtk(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntPhyDef(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntSpeAtk(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntSpeDef(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntNimbleness(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntAverage(uint8_t* p, uint8_t v);
void fightOutPokemonBiosSetAbicntAvoid(uint8_t* p, uint8_t v);
void fightWaza_SetCritical(void* fw, uint32_t value);
uint32_t HSD_Rand(void);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t fightOutPokemonGetCmpNimblenessWazaDataId(void* fop);
void fightWazaSetUseWazaStatus(void* fw, uint16_t waza);
void* fightSeqSelectDefensePokemonPtr(void* atk, uint16_t waza, uint32_t type, uint32_t a, uint32_t b, uint32_t c, int32_t d);
void fightFloor_SetDefPokemonAndAtkFightWazaTargetId(int32_t floor, void* target);
uint8_t fightFloor_GetFightType(int32_t floor);
void fightWaza_SetZokusei(void* fw, uint32_t type);
uint16_t fightOutPokemonDoItemSoubi(void* fop, uint16_t item, uint8_t mode, uint8_t update);
uint16_t pokemon_GetPokemonWazaDataId(void* pokemon, uint32_t slot);
void pokemon_SetPokemonWazaPp(void* pokemon, uint32_t slot, uint8_t pp);
uint8_t pokemonWazaGetMaxPP(void* pokemon, uint16_t slot);
int16_t pokemonGetStatus(void* fop, uint32_t a, uint16_t id, uint32_t b);
void pokemonSetStatus(void* fop, int32_t a, int32_t id, int32_t b, uint32_t value);
uint16_t pokemon_GetPhyAtk(void* pokemon);
uint16_t pokemon_GetPhyDef(void* pokemon);
int32_t fightWaza_GetHitDamage(void* fw);
uint16_t fightWaza_GetUseWazaDataId(void* fw);
void WS_COUNTER(void);
int8_t fightWaza_GetWazaBanme(void* fw);
uint8_t pokemon_GetPokemonWazaPp(void* pokemon, uint32_t slot);
uint8_t fightOutPokemonIsNokoriHpFollowing(void* fop, uint8_t divisor);
uint8_t fightOutPokemonIsHinsi(void* fop);
uint8_t fightOutPokemonIsZokuseiDataId(void* fop, uint16_t type);
void wazaSequenceSysFreeAllWaza(void);
extern uint32_t ServerStatusFlag;
void* PortXD_GameAddress(uint32_t gcAddress);
void PortXD_AddScriptBlock(uint32_t gcAddress, void* bytes, uint32_t size);
}

namespace {

constexpr uint16_t kIronFist = 89, kAdaptability = 91, kSniper = 97, kNoGuard = 99, kTechnician = 101, kTintedLens = 110,
                   kFilter = 111, kScrappy = 113, kSolidRock = 116, kReckless = 120;
constexpr uint16_t kHeatproof = 85, kDrySkin = 87, kSolarPower = 94, kSkillLink = 92, kSnowCloak = 81;

bool Gen4On() { return xd::challenges::Active("gen4_rules"); }

uint16_t PowerOverride(uint16_t waza, void* attacker, void* defender);
bool GravityActive();
bool MagnetRiseActive(void* fop);
bool MiracleEyeActive(void* fop);
bool LuckyChantOn(void* fop);
bool EmbargoActive(void* fop);

bool IsPunch(uint16_t waza) {
  switch (waza) {
  case 4: case 5: case 7: case 8: case 9: case 146: case 183: case 223: case 264: case 309: case 325: case 327:
    return true;
  default:
    break;
  }
  if (waza < 375) return false;
  const auto* row = static_cast<const uint8_t*>(wazaDataBiosGetPtr(waza));
  return row != nullptr && row[0x16] == 0xA4 && (row[0x17] & 1) != 0;
}

bool IsRecoil(uint16_t waza) {
  const uint16_t seq = wazaDB_GetSeqId(waza);
  return seq == 45 || seq == 48 || seq == 198;
}

int32_t Scale(int32_t damage, int32_t num, int32_t den) { return (damage - 2) * num / den + 2; }

int Effect(uint16_t moveType, void* defender, bool scrappy) {
  const uint16_t t1 = fightOutPokemonGetZokuseiDataId(defender, 0), t2 = fightOutPokemonGetZokuseiDataId(defender, 1);
  int mult = 4;
  for (uint16_t t : {t1, static_cast<uint16_t>(t2 != t1 ? t2 : 0xFF)}) {
    if (t == 0xFF || t == 9) continue;
    const uint16_t r = zokuseiGetWazaJoutai(moveType, t);
    if (r == 0x43) {
      if (scrappy && t == 7 && moveType <= 1) continue;
      return 0;
    }
    if (r == 0x41) mult *= 2;
    if (r == 0x42) mult /= 2;
  }
  return mult;
}

}

extern "C" {

int32_t fightSeqWazaDamageCalc(void* attacker, void* defender, void* attackSide, uint16_t waza, uint16_t iryoku, int32_t zokusei) {
  xd::gen4::StageGuard stages(attacker, defender);
  if (Gen4On() && attacker != nullptr && defender != nullptr) {
    if (const uint16_t p = PowerOverride(waza, attacker, defender)) iryoku = p;
  }
  int32_t damage = PortXD_RealDamageCalc(attacker, defender, attackSide, waza, iryoku, zokusei);
  if (!Gen4On() || attacker == nullptr || defender == nullptr || damage <= 2) {
    return damage;
  }
  const uint16_t atk = fightOutPokemonGetTokuseiDataId(attacker);
  uint16_t def = fightOutPokemonGetTokuseiDataId(defender);
  if (atk == 104) def = 0;
  if (atk < 77 && def < 77) {
    return damage;
  }
  const uint16_t power = iryoku != 0 ? iryoku : wazaDB_GetIryoku(waza);
  const uint16_t type = zokusei < 0 ? wazaDB_GetZokuseiDataId(waza) : static_cast<uint16_t>(zokusei);
  switch (atk) {
  case kTechnician:
    if (power != 0 && power <= 60) damage = Scale(damage, 3, 2);
    break;
  case kIronFist:
    if (IsPunch(waza)) damage = Scale(damage, 6, 5);
    break;
  case kReckless:
    if (IsRecoil(waza)) damage = Scale(damage, 6, 5);
    break;
  case kAdaptability:
    if (type == fightOutPokemonGetZokuseiDataId(attacker, 0) || type == fightOutPokemonGetZokuseiDataId(attacker, 1)) {
      damage = Scale(damage, 4, 3);
    }
    break;
  case kSniper:
    if (fightWaza_GetCritical(fightOutPokemon_GetFightWazaPtr(attacker)) == 2) damage = Scale(damage, 3, 2);
    break;
  case kTintedLens:
    if (const int e = Effect(type, defender, false); e > 0 && e < 4) damage = Scale(damage, 2, 1);
    break;
  case kSolarPower:
    if (fightFloorGetNowTenkouDataId(nullptr, 1) == 1) {
      PortXD_SplitCheckDarkWaza(waza);
      if (PortXD_SplitTypeKind(type) == 2) damage = Scale(damage, 3, 2);
    }
    break;
  default:
    break;
  }
  damage = xd::gen4::AbilityDamage(attacker, defender, waza, type, damage);
  if (def == kHeatproof && type == 10) damage = Scale(damage, 1, 2);
  if (def == kDrySkin && type == 10) damage = Scale(damage, 5, 4);
  if (def == kFilter || def == kSolidRock) {
    if (Effect(type, defender, atk == kScrappy) >= 8) damage = Scale(damage, 3, 4);
  }
  return damage;
}

uint16_t PortXD_Gen4TypeJoutai(uint16_t wazaType, uint16_t defenderType) {
  const uint16_t r = zokuseiGetWazaJoutai(wazaType, defenderType);
  if (Gen4On() && (wazaType == 4 || wazaType == 17)) {
    void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
    if (defender != nullptr) {
      if (wazaType == 4) {
        if (defenderType == 2 && r == 0x43 && GravityActive()) return 0;
        if (MagnetRiseActive(defender)) return 0x43;
      } else if (defenderType == 14 && r == 0x43 && MiracleEyeActive(defender)) {
        return 0;
      }
    }
  }
  if (r == 0x43 && defenderType == 7 && wazaType <= 1 && Gen4On()) {
    void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
    if (attacker != nullptr && fightOutPokemonGetTokuseiDataId(attacker) == kScrappy) {
      return 0;
    }
  }
  return r;
}

uint8_t PortXD_Gen4GetAvg(uint16_t waza) {
  uint8_t avg = wazaDB_GetAvg(waza);
  if (Gen4On()) {
    if (GravityActive() && avg != 0 && avg < 100) avg = static_cast<uint8_t>(avg * 5 / 3 > 100 ? 100 : avg * 5 / 3);
    void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
    void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
    if ((attacker != nullptr && fightOutPokemonGetTokuseiDataId(attacker) == kNoGuard) ||
        (defender != nullptr && fightOutPokemonGetTokuseiDataId(defender) == kNoGuard)) {
      return 255;
    }
    if (defender != nullptr && avg != 0 && avg < 255 && fightOutPokemonGetTokuseiDataId(defender) == kSnowCloak &&
        fightFloorGetNowTenkouDataId(nullptr, 1) == 4 && (attacker == nullptr || fightOutPokemonGetTokuseiDataId(attacker) != 104)) {
      std::fprintf(stderr, "[gen4] ability: Snow Cloak in hail: accuracy %d -> %d\n", avg, avg * 4 / 5);
      return static_cast<uint8_t>(avg * 4 / 5);
    }
    return xd::gen4::AbilityAccuracy(attacker, defender, avg);
  }
  return avg;
}

}

namespace {

constexpr uint32_t kScriptBase = 0x8F000000u;
constexpr uint32_t kWazaEnd = 0x8041410Fu;
constexpr uint32_t kWazaEffectTbl = 0x80417EDFu;
constexpr int kUTurnSlot = 12, kCloseCombatSlot = 14, kStealthRockSlot = 15, kTrickRoomSlot = 21, kHammerArmSlot = 22,
              kRoostSlot = 55, kTailwindSlot = 56, kRecoverSlot = 32, kSuperpowerSlot = 182;
constexpr uint32_t kTrickOffset = 0, kUTurnOffset = 0x40, kCloseOffset = 0x120, kHammerOffset = 0x1C0, kRockOffset = 0x1E0,
                   kTailwindOffset = 0x200, kRockHitOffset = 0x220;
constexpr uint32_t kSelfOffset = 0x300, kFoeOffset = 0x330, kSeedFlareOffset = 0x360, kChargeBeamOffset = 0x380,
                   kSuckerOffset = 0x3A0, kAquaHealOffset = 0x3E0;
constexpr int kSelfSlot = 61, kFoeSlot = 63, kSeedFlareSlot = 64, kChargeBeamSlot = 74, kSuckerSlot = 96;
constexpr uint32_t kHitExtraOffset = 0x440, kCopyOffset = 0x4E0, kWishOffset = 0x500, kWishHealOffset = 0x540, kLunarHealOffset = 0x570,
                   kBerryHealOffset = 0x5A0;
constexpr int kHitExtraSlot = 110, kCopySlot = 131, kWishSlot = 141;
constexpr uint8_t kPortOp = 0x53, kPortMark = 0xF0;
constexpr uint32_t kBank = 0x7D0u << 20;
constexpr uint32_t kFailed = 0x80415C93u;
constexpr uint32_t kFailedWithMessage = 0x80415C91u;
enum : uint8_t { kCmdCheck = 1, kCmdApply = 2, kCmdSucker = 3, kCmdTrickRoom = 4, kCmdRockSet = 5, kCmdTailwind = 6, kCmdRockMsg = 7,
                 kCmdHitExtra = 8, kCmdCopy = 9, kCmdHitSetup = 10 };

struct MoveText {
  uint32_t id;
  const char* text;
};
constexpr uint32_t kMsgGravityOn = 0xA000, kMsgGravityOff = 0xA001, kMsgMagnetOn = 0xA002, kMsgMagnetOff = 0xA003, kMsgAquaOn = 0xA004,
                   kMsgAquaHeal = 0xA005, kMsgToxicSet = 0xA006, kMsgToxicAbsorb = 0xA008, kMsgLuckyOn = 0xA009,
                   kMsgLuckyOff = 0xA00B, kMsgEyeOn = 0xA00D, kMsgGastro = 0xA00E, kMsgWorry = 0xA00F, kMsgEmbargoOn = 0xA010,
                   kMsgEmbargoOff = 0xA011, kMsgHealBlockOn = 0xA012, kMsgHealBlockOff = 0xA013, kMsgPowerSwap = 0xA014,
                   kMsgGuardSwap = 0xA015, kMsgHeartSwap = 0xA016, kMsgPsychoShift = 0xA017, kMsgRockSet = 0xA018,
                   kMsgRockHit = 0xA01A, kMsgTrickOn = 0xA01B, kMsgTrickOff = 0xA01C, kMsgTailwindOn = 0xA01D,
                   kMsgTailwindOff = 0xA01F, kMsgHealBlocked = 0xA021, kMsgWishCame = 0xA022, kMsgLunarCame = 0xA023,
                   kMsgBerryAte = 0xA024, kMsgBerryHeal = 0xA025, kMsgPowerTrick = 0xA026, kMsgGravityNo = 0xA027;
constexpr MoveText kMoveTexts[] = {
    {kMsgGravityOn, "Gravity intensified!"},
    {kMsgGravityOff, "Gravity returned to normal!"},
    {kMsgMagnetOn, "{10} levitated with electromagnetism!"},
    {kMsgMagnetOff, "{10}'s electromagnetism wore off!"},
    {kMsgAquaOn, "{10} wrapped itself in a veil of water!"},
    {kMsgAquaHeal, "A veil of water restored {10}'s HP!"},
    {kMsgToxicSet, "Poison spikes were scattered around your team!"},
    {kMsgToxicSet + 1, "Poison spikes were scattered around the foe's team!"},
    {kMsgToxicAbsorb, "{10} absorbed the poison spikes!"},
    {kMsgLuckyOn, "Your team is shielded from critical hits!"},
    {kMsgLuckyOn + 1, "The foe's team is shielded from critical hits!"},
    {kMsgLuckyOff, "Your team's Lucky Chant wore off!"},
    {kMsgLuckyOff + 1, "The foe's team's Lucky Chant wore off!"},
    {kMsgEyeOn, "{10} was identified!"},
    {kMsgGastro, "{10}'s ability was suppressed!"},
    {kMsgWorry, "{10} acquired Insomnia!"},
    {kMsgEmbargoOn, "{10} can't use items anymore!"},
    {kMsgEmbargoOff, "{10} can use items again!"},
    {kMsgHealBlockOn, "{10} was prevented from healing!"},
    {kMsgHealBlockOff, "{10}'s Heal Block wore off!"},
    {kMsgPowerSwap, "{10} swapped Attack and Sp. Atk boosts!"},
    {kMsgGuardSwap, "{10} swapped Defense and Sp. Def boosts!"},
    {kMsgHeartSwap, "{10} swapped stat changes with its foe!"},
    {kMsgPsychoShift, "{10} moved its status problem to the foe!"},
    {kMsgRockSet, "Pointed stones float around your team!"},
    {kMsgRockSet + 1, "Pointed stones float around the foe's team!"},
    {kMsgRockHit, "Pointed stones dug into {10}!"},
    {kMsgTrickOn, "{10} twisted the dimensions!"},
    {kMsgTrickOff, "The twisted dimensions returned to normal!"},
    {kMsgTailwindOn, "The tailwind blew from behind your team!"},
    {kMsgTailwindOn + 1, "The tailwind blew from behind the foe's team!"},
    {kMsgTailwindOff, "Your team's tailwind petered out!"},
    {kMsgTailwindOff + 1, "The foe's team's tailwind petered out!"},
    {kMsgHealBlocked, "{10} was prevented from healing!"},
    {kMsgWishCame, "The healing wish came true for {10}!"},
    {kMsgLunarCame, "{10} became cloaked in mystical moonlight!"},
    {kMsgBerryAte, "{10} ate the foe's berry!"},
    {kMsgBerryHeal, "{10} ate the foe's berry and was healed!"},
    {kMsgPowerTrick, "{10} switched its Attack and Defense!"},
    {kMsgGravityNo, "{10} can't use that move in this gravity!"},
};
constexpr uint32_t kPlainHit = 0x80414090u;
constexpr uint32_t kBatonPass = 0x80415722u;
constexpr uint32_t kMakibishi[3] = {0x80416FFCu, 0x8041702Fu, 0x80417062u};
constexpr uint32_t kMakibishiSize = 0x33, kMakibishiMessage = 0x80417095u;
constexpr uint32_t kServerWork3 = 0x804E85C3u;
constexpr uint8_t kSelf = 0x40, kAlways = 0x80;
uint8_t sScripts[0x5D0];
bool sInstalled = false;

struct SideState {
  void* side = nullptr;
  bool rocks = false;
  int tailwindTurn = -1;
  int toxicLayers = 0;
  int luckyTurn = -1;
  int wish = 0;
};
SideState sSides[8];
struct MonState {
  void* mon = nullptr;
  int magnetTurn = -1;
  bool aquaRing = false;
  bool miracleEye = false;
  int embargoTurn = -1;
  int healBlockTurn = -1;
  int movedTurn = -1;
  bool powerTrick = false;
  uint8_t usedMask = 0;
  int meFirstTurn = -1;
};
uint16_t sLastMove = 0;
MonState sMons[8];
int sGravityTurn = -1;
int sTrickSetTurn = -1, sLastTurn = -1;
void* sRoostMon = nullptr;
int sRoostTurn = -1;
void* sRockVictim = nullptr;

const MonState* FindMon(void* mon) {
  for (auto& m : sMons) if (m.mon == mon && mon != nullptr) return &m;
  return nullptr;
}

MonState& MonOf(void* mon) {
  for (auto& m : sMons) if (m.mon == mon) return m;
  for (auto& m : sMons) if (m.mon == nullptr) { m.mon = mon; return m; }
  sMons[0] = MonState();
  sMons[0].mon = mon;
  return sMons[0];
}

SideState& SideOf(void* side) {
  for (auto& s : sSides) if (s.side == side) return s;
  for (auto& s : sSides) if (s.side == nullptr) { s.side = side; return s; }
  sSides[0] = SideState();
  sSides[0].side = side;
  return sSides[0];
}

uint32_t Narrow(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
void* SideOfPokemon(void* fop) { return fightTargetGetPtrAsNowFightType(2, Narrow(fop)); }

void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
uint32_t GetBe32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

int Relocate(uint8_t* p, size_t n, uint32_t from, uint32_t to) {
  int count = 0;
  for (size_t i = 0; i + 4 <= n; i++) {
    if (GetBe32(p + i) == from) {
      PutBe32(p + i, to);
      count++;
      i += 3;
    }
  }
  return count;
}

bool CopyPlainHit(uint8_t* out, uint32_t base) {
  const auto* hit = static_cast<const uint8_t*>(PortXD_GameAddress(kPlainHit));
  if (hit == nullptr || hit[0] != 0xFA || GetBe32(hit + 0x7F) != 0x2F804EB9u) return false;
  std::memcpy(out, hit, 0x7F);
  return Relocate(out, 0x7F, 0x804140DEu, base + 0x4E) == 2;
}

size_t BuildUTurn(uint8_t* out, size_t cap, uint32_t base) {
  const auto* baton = static_cast<const uint8_t*>(PortXD_GameAddress(kBatonPass));
  if (baton == nullptr || cap < 0x7F + 7 + 0x26 + 5) return 0;
  if (baton[0] != 0x50 || baton[7] != 0x0A || baton[8] != 0x0B || baton[0x2E] != 0x53 || !CopyPlainHit(out, base)) return 0;
  size_t n = 0x7F;
  std::memcpy(out + n, baton, 7);
  n += 7;
  std::memcpy(out + n, baton + 10, 0x30 - 10);
  n += 0x30 - 10;
  if (Relocate(out + 0x7F, n - 0x7F, 0x80415C93u, kWazaEnd) < 2) return 0;
  out[n++] = 0x29;
  PutBe32(out + n, kWazaEnd);
  return n + 4;
}

size_t BuildCloseCombat(uint8_t* out, size_t cap, uint32_t base) {
  if (cap < 6 + 0x7F + 6 + 1 + 5 || !CopyPlainHit(out + 6, base + 6)) return 0;
  const uint8_t set[6] = {0x2F, 0x80, 0x4E, 0x85, 0xC3, uint8_t(kAlways | kSelf | 0x17)};
  std::memcpy(out, set, 6);
  size_t n = 6 + 0x7F;
  const uint8_t set2[6] = {0x2F, 0x80, 0x4E, 0x85, 0xC3, uint8_t(kSelf | 0x1A)};
  std::memcpy(out + n, set2, 6);
  n += 6;
  out[n++] = 0x17;
  out[n++] = 0x29;
  PutBe32(out + n, kWazaEnd);
  return n + 4;
}

size_t BuildHammerArm(uint8_t* out, size_t cap) {
  const auto* table = static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kSuperpowerSlot));
  const auto* super = static_cast<const uint8_t*>(PortXD_GameAddress(GetBe32(table)));
  if (super == nullptr || cap < 11 || super[0] != 0x2F) return 0;
  if (super[0] != 0x2F || GetBe32(super + 1) != kServerWork3 || super[5] != 0xE5 || super[6] != 0x29 || GetBe32(super + 7) != kPlainHit) {
    return 0;
  }
  std::memcpy(out, super, 11);
  out[5] = kAlways | kSelf | 0x18;
  return 11;
}

size_t BuildPlainStatus(uint8_t* out, uint8_t portId = 0) {
  uint8_t* o = out;
  for (uint8_t b : {0x00, 0x02, 0x04, 0x0A, 0x0B, 0xFF}) *o++ = b;
  if (portId != 0) {
    for (uint8_t b : {kPortOp, kPortMark, portId, uint8_t(0), uint8_t(0)}) *o++ = b;
  }
  *o++ = 0x29;
  PutBe32(o, kWazaEnd);
  return static_cast<size_t>(o + 4 - out);
}

void PutPort(uint8_t*& o, uint8_t id) {
  for (uint8_t b : {kPortOp, kPortMark, id, uint8_t(0), uint8_t(0)}) *o++ = b;
}

size_t BuildStatusMove(uint8_t* out, bool foe) {
  uint8_t* o = out;
  *o++ = 0x00;
  if (foe) {
    *o++ = 0x01;
    PutBe32(o, 0x8041412Du);
    o += 4;
    *o++ = 0;
    *o++ = 0;
  }
  *o++ = 0x02;
  *o++ = 0x04;
  PutPort(o, kCmdCheck);
  for (uint8_t b : {0x0A, 0x0B, 0xFF}) *o++ = b;
  PutPort(o, kCmdApply);
  *o++ = 0x29;
  PutBe32(o, kWazaEnd);
  return static_cast<size_t>(o + 4 - out);
}

size_t BuildHitWithEffect(uint8_t* out, uint8_t code) {
  const uint8_t s[] = {0x2F, 0x80, 0x4E, 0x85, 0xC3, code, 0x29, 0, 0, 0, 0};
  std::memcpy(out, s, sizeof s);
  PutBe32(out + 7, kPlainHit);
  return sizeof s;
}

size_t BuildSuckerPunch(uint8_t* out) {
  uint8_t* o = out;
  *o++ = 0x00;
  PutPort(o, kCmdSucker);
  *o++ = 0x29;
  PutBe32(o, kPlainHit);
  return static_cast<size_t>(o + 4 - out);
}

size_t BuildHealScript(uint8_t* out, uint32_t msg) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(0x80417E41u));
  if (src == nullptr || src[0] != 0x46 || src[0x13] != 0x11 || src[0x20] != 0x3D) return 0;
  std::memcpy(out, src, 0x21);
  PutBe32(out + 0x14, kBank | msg);
  return 0x21;
}
size_t BuildAquaHeal(uint8_t* out) { return BuildHealScript(out, kMsgAquaHeal); }

size_t BuildHitExtra(uint8_t* out, uint32_t base) {
  uint8_t* o = out;
  PutPort(o, kCmdHitSetup);
  if (!CopyPlainHit(o, base + 5)) return 0;
  o += 0x7F;
  PutPort(o, kCmdHitExtra);
  *o++ = 0x29;
  PutBe32(o, kWazaEnd);
  return static_cast<size_t>(o + 4 - out);
}

size_t BuildCopy(uint8_t* out) {
  uint8_t* o = out;
  *o++ = 0x00;
  *o++ = 0x02;
  *o++ = 0x04;
  PutPort(o, kCmdCopy);
  *o++ = 0x29;
  PutBe32(o, kFailed);
  return static_cast<size_t>(o + 4 - out);
}

size_t BuildWish(uint8_t* out) {
  const auto* baton = static_cast<const uint8_t*>(PortXD_GameAddress(kBatonPass));
  if (baton == nullptr || baton[0] != 0x50) return 0;
  uint8_t* o = out;
  *o++ = 0x00;
  *o++ = 0x02;
  *o++ = 0x04;
  std::memcpy(o, baton, 7);
  o += 7;
  *o++ = 0x7B;
  for (uint8_t b : {0x0A, 0x0B, 0x04}) *o++ = b;
  PutPort(o, kCmdApply);
  *o++ = 0x1A;
  *o++ = 0x11;
  PutBe32(o, 0);
  o += 4;
  *o++ = 0x00;
  *o++ = 0x29;
  PutBe32(o, kWazaEnd);
  return static_cast<size_t>(o + 4 - out);
}

bool BuildRockHit(uint8_t* out, int which, uint32_t base) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(kMakibishi[which]));
  if (src == nullptr || src[0] != 0x36) return false;
  std::memcpy(out, src, kMakibishiSize);
  for (uint32_t i = 0; i + 4 < kMakibishiSize; i++) {
    const uint32_t v = GetBe32(out + i + 1);
    if (out[i] == 0x42 && v == kMakibishiMessage) {
      out[i] = kPortOp;
      out[i + 1] = kPortMark;
      out[i + 2] = kCmdRockMsg;
      out[i + 3] = 0;
      out[i + 4] = 0;
      i += 4;
    }
  }
  for (uint32_t i = 0; i + 4 <= kMakibishiSize; i++) {
    const uint32_t v = GetBe32(out + i);
    if (v >= kMakibishi[which] && v < kMakibishi[which] + kMakibishiSize) {
      PutBe32(out + i, v - kMakibishi[which] + base);
      i += 3;
    }
  }
  return true;
}

void SetSlot(int slot, uint32_t script) { PutBe32(static_cast<uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * slot)), script); }

void EnsureInstalled() {
  if (sInstalled) return;
  sInstalled = true;
  const size_t trick = BuildPlainStatus(sScripts + kTrickOffset, kCmdTrickRoom);
  const size_t uturn = BuildUTurn(sScripts + kUTurnOffset, kCloseOffset - kUTurnOffset, kScriptBase + kUTurnOffset);
  const size_t close = BuildCloseCombat(sScripts + kCloseOffset, kHammerOffset - kCloseOffset, kScriptBase + kCloseOffset);
  const size_t hammer = BuildHammerArm(sScripts + kHammerOffset, kRockOffset - kHammerOffset);
  BuildPlainStatus(sScripts + kRockOffset, kCmdRockSet);
  BuildPlainStatus(sScripts + kTailwindOffset, kCmdTailwind);
  BuildStatusMove(sScripts + kSelfOffset, false);
  BuildStatusMove(sScripts + kFoeOffset, true);
  BuildHitWithEffect(sScripts + kSeedFlareOffset, 0x33);
  BuildHitWithEffect(sScripts + kChargeBeamOffset, kSelf | 0x12);
  BuildSuckerPunch(sScripts + kSuckerOffset);
  const size_t aqua = BuildAquaHeal(sScripts + kAquaHealOffset);
  const size_t hitExtra = BuildHitExtra(sScripts + kHitExtraOffset, kScriptBase + kHitExtraOffset);
  BuildCopy(sScripts + kCopyOffset);
  const size_t wish = BuildWish(sScripts + kWishOffset);
  BuildHealScript(sScripts + kWishHealOffset, kMsgWishCame);
  BuildHealScript(sScripts + kLunarHealOffset, kMsgLunarCame);
  BuildHealScript(sScripts + kBerryHealOffset, kMsgBerryHeal);
  bool rocks = true;
  for (int i = 0; i < 3; i++) rocks = BuildRockHit(sScripts + kRockHitOffset + 0x40 * i, i, kScriptBase + kRockHitOffset + 0x40 * i) && rocks;
  PortXD_AddScriptBlock(kScriptBase, sScripts, sizeof sScripts);
  (void)trick;
  SetSlot(kTrickRoomSlot, kScriptBase + kTrickOffset);
  if (uturn != 0) SetSlot(kUTurnSlot, kScriptBase + kUTurnOffset);
  if (close != 0) SetSlot(kCloseCombatSlot, kScriptBase + kCloseOffset);
  if (hammer != 0) SetSlot(kHammerArmSlot, kScriptBase + kHammerOffset);
  if (rocks) SetSlot(kStealthRockSlot, kScriptBase + kRockOffset);
  SetSlot(kTailwindSlot, kScriptBase + kTailwindOffset);
  SetSlot(kSelfSlot, kScriptBase + kSelfOffset);
  SetSlot(kFoeSlot, kScriptBase + kFoeOffset);
  SetSlot(kSeedFlareSlot, kScriptBase + kSeedFlareOffset);
  SetSlot(kChargeBeamSlot, kScriptBase + kChargeBeamOffset);
  SetSlot(kSuckerSlot, kScriptBase + kSuckerOffset);
  if (hitExtra != 0) SetSlot(kHitExtraSlot, kScriptBase + kHitExtraOffset);
  SetSlot(kCopySlot, kScriptBase + kCopyOffset);
  if (wish != 0) SetSlot(kWishSlot, kScriptBase + kWishOffset);
  std::fprintf(stderr, "[gen4] round 4 scripts installed: hit with extra (place %d: %s), copy a move (%d), Healing Wish (%d: %s)\n",
               kHitExtraSlot, hitExtra ? "built" : "NOT built", kCopySlot, kWishSlot, wish ? "built" : "NOT built");
  std::fprintf(stderr, "[gen4] round 3 scripts installed: self/field status (place %d), foe status (%d), Seed Flare (%d), Charge Beam (%d), Sucker Punch (%d), Aqua Ring heal (%s)\n",
               kSelfSlot, kFoeSlot, kSeedFlareSlot, kChargeBeamSlot, kSuckerSlot, aqua ? "built" : "NOT built");
  SetSlot(kRoostSlot, GetBe32(static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kRecoverSlot))));
  std::fprintf(stderr,
               "[gen4] effect scripts installed: Trick Room (place 21), U-turn (12: %s), Close Combat (14: %s), Hammer Arm (22: %s), "
               "Stealth Rock (15: %s), Tailwind (56), Roost (55)\n",
               uturn ? "built" : "NOT built", close ? "built" : "NOT built", hammer ? "built" : "NOT built",
               rocks ? "built" : "NOT built");
}

void Tick() {
  const int turn = fightFloor_GetTurnCount(nullptr);
  if (turn < sLastTurn) {
    std::fprintf(stderr, "[gen4] new battle (turn %d after %d): Trick Room, Tailwind, Stealth Rock and Roost forgotten\n", turn, sLastTurn);
    sTrickSetTurn = -1;
    sGravityTurn = -1;
    sLastMove = 0;
    for (auto& m : sMons) m = MonState();
    for (auto& s : sSides) s = SideState();
    sRoostMon = nullptr;
    sRoostTurn = -1;
  }
  sLastTurn = turn;
}

bool TrickRoomActive() {
  Tick();
  return sTrickSetTurn >= 0 && sLastTurn > sTrickSetTurn && sLastTurn <= sTrickSetTurn + 4;
}

bool TailwindActive(void* fop) {
  const SideState& s = SideOf(SideOfPokemon(fop));
  return s.tailwindTurn >= 0 && sLastTurn > s.tailwindTurn && sLastTurn <= s.tailwindTurn + 2;
}

uint32_t RockDamage(void* p) {
  const int e = Effect(5, p, false);
  uint32_t d = static_cast<uint32_t>(fightOutPokemonMaxHpWaruValue(p, 8)) * static_cast<uint32_t>(e) / 4;
  return d == 0 ? 1 : d;
}

bool RocksHit(void* p, uint8_t sideCode) {
  void* side = SideOfPokemon(p);
  const uint32_t damage = RockDamage(p);
  fightSide_SetMakibisiCheckFlag(side, 1);
  ServerStatusFlag &= ~0x40u;
  void* waza = fightOutPokemon_GetFightWazaPtr(fightTargetGetPtrAsNowFightType(0x11, 0));
  uint8_t saved[0xAC];
  fightWazaBiosCopy(saved, waza);
  fightWaza_SetDamage(waza, static_cast<int32_t>(damage));
  fightWazaInitJoutai(waza);
  fightFloor_SetAppointPokemonPtr(nullptr, p);
  const int which = sideCode == 0x12 ? 1 : (sideCode == 0x11 || sideCode == 0x14) ? 0 : 2;
  std::fprintf(stderr, "[gen4] Stealth Rock hits a Pokemon coming in: %u damage (x%d/4 of 1/8)\n", damage, Effect(5, p, false));
  sRockVictim = p;
  fightSeq_CallWazaSeq(PortXD_GameAddress(kScriptBase + kRockHitOffset + 0x40 * which));
  fightWazaBiosCopy(waza, saved);
  if (!fightSide_GetMakibisiCheckFlag(side)) {
    fightSeq_NextWazaSeq(2);
    return true;
  }
  fightSide_SetMakibisiCheckFlag(side, 0);
  return false;
}

constexpr uint16_t kGravity = 376, kMiracleEye = 377, kAcupressure = 387, kEmbargo = 393, kPsychoShift = 395, kHealBlock = 397,
                   kGastroAcid = 400, kLuckyChant = 401, kPowerSwap = 404, kGuardSwap = 405, kWorrySeed = 408, kToxicSpikes = 410,
                   kHeartSwap = 411, kAquaRing = 412, kMagnetRise = 413, kDefog = 452, kCaptivate = 465;
constexpr uint16_t kTrumpCard = 396, kWakeUpSlap = 378, kGyroBall = 380, kBrine = 382, kPayback = 391, kAssurance = 392, kWringOut = 398,
                   kPunishment = 406, kCrushGrip = 482;
constexpr uint16_t kHealingWish = 381, kNaturalGift = 383, kFeint = 384, kPluck = 385, kMetalBurst = 388, kFling = 394, kPowerTrick = 399,
                   kMeFirst = 402, kCopycat = 403, kLastResort = 407, kFlareBlitz = 414, kThunderFang = 442, kIceFang = 443,
                   kFireFang = 444, kBugBite = 470, kHeadSmash = 477, kLunarDance = 481, kShadowForce = 487;
constexpr uint16_t kLevitate = 26, kInsomnia = 15;
constexpr uint32_t kWazaSinkuro = 0x80417C25u;

void* Target(uint32_t kind, uint32_t arg = 0) { return fightTargetGetPtrAsNowFightType(kind, arg); }
int Turn() { return fightFloor_GetTurnCount(nullptr); }
bool OnPlayersSide(void* fop) { return fightOutPokemonIsGcHeroFightOutPokemon(fop) != 0; }

void Say(uint32_t id, void* mon, uint32_t frames = 0x60) {
  fightMsgctrlSetValue(0x10, reinterpret_cast<uintptr_t>(mon));
  fightSeqOpenMsg(kBank | id);
  if (ServerWork[7] == 1) {
    fightMainWaitFrame(frames);
    fightMenuCloseMsg();
    ServerWork[7] = 0;
  }
}
void SaySide(uint32_t id, void* mon) { Say(id + (OnPlayersSide(mon) ? 0 : 1), mon); }

bool GravityOn() { return sGravityTurn >= 0 && sLastTurn >= sGravityTurn && sLastTurn <= sGravityTurn + 4; }
bool InWindow(int setTurn, int length) { return setTurn >= 0 && sLastTurn >= setTurn && sLastTurn <= setTurn + length - 1; }

void GetStages(void* fop, uint8_t v[7]) {
  auto* p = static_cast<uint8_t*>(fop);
  v[0] = fightOutPokemonBiosGetAbicntPhyAtk(p);
  v[1] = fightOutPokemonBiosGetAbicntPhyDef(p);
  v[2] = fightOutPokemonBiosGetAbicntNimbleness(p);
  v[3] = fightOutPokemonBiosGetAbicntSpeAtk(p);
  v[4] = fightOutPokemonBiosGetAbicntSpeDef(p);
  v[5] = fightOutPokemonBiosGetAbicntAverage(p);
  v[6] = fightOutPokemonBiosGetAbicntAvoid(p);
}
void SetStages(void* fop, const uint8_t v[7]) {
  auto* p = static_cast<uint8_t*>(fop);
  fightOutPokemonBiosSetAbicntPhyAtk(p, v[0]);
  fightOutPokemonBiosSetAbicntPhyDef(p, v[1]);
  fightOutPokemonBiosSetAbicntNimbleness(p, v[2]);
  fightOutPokemonBiosSetAbicntSpeAtk(p, v[3]);
  fightOutPokemonBiosSetAbicntSpeDef(p, v[4]);
  fightOutPokemonBiosSetAbicntAverage(p, v[5]);
  fightOutPokemonBiosSetAbicntAvoid(p, v[6]);
}

void ApplyEffect(uint8_t code, bool onAttacker) {
  ServerWork[3] = static_cast<uint8_t>(code | (onAttacker ? kSelf : 0));
  ServerStatusFlag |= 0x2000;
  fightSeq_CallWazaSeq(PortXD_GameAddress(kWazaSinkuro));
}

bool Fail() {
  fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailed));
  return true;
}

uint32_t Speed(void* fop) { return fightOutPokemonGetNowNimbleness(fop, 0, 0, 0, nullptr); }

int PositiveStages(void* fop) {
  uint8_t v[7];
  GetStages(fop, v);
  int sum = 0;
  for (uint8_t s : v) if (s > 6) sum += s - 6;
  return sum;
}

bool Asleep(void* fop) { return fightOutPokemonIsJoutaiDataId(fop, 8) != 0; }

uint8_t StatusCode(void* fop) {
  static const uint8_t ids[6] = {8, 3, 6, 7, 5, 4};
  static const uint8_t codes[6] = {1, 2, 3, 4, 5, 6};
  for (int i = 0; i < 6; i++) if (fightOutPokemonIsJoutaiDataId(fop, ids[i]) != 0) return codes[i];
  return 0;
}
void CureStatus(void* fop, uint8_t code) {
  static const uint8_t ids[7] = {0, 8, 3, 6, 7, 5, 4};
  if (code >= 1 && code <= 6) fightOutPokemonInitJoutaiDataId(fop, ids[code]);
}

void ClearSideEffects(void* side, SideState& s) {
  s.rocks = false;
  s.toxicLayers = 0;
  for (uint16_t id : {0x48, 0x49, 0x4A, 0x4B, 0x4C}) {
    if (side != nullptr && fightSideIsJoutaiDataId(side, id) != 0) fightSideInitJoutaiDataId(side, id);
  }
}

bool MoveCanWork(uint16_t move, void* atk, void* def) {
  switch (move) {
  case kGravity: return !GravityOn();
  case kMagnetRise: return !GravityOn() && !InWindow(MonOf(atk).magnetTurn, 5);
  case kAquaRing: return !MonOf(atk).aquaRing;
  case kToxicSpikes: return SideOf(Target(3, Narrow(atk))).toxicLayers < 2;
  case kLuckyChant: return !InWindow(SideOf(SideOfPokemon(atk)).luckyTurn, 5);
  case kAcupressure: {
    uint8_t v[7];
    GetStages(atk, v);
    for (uint8_t s : v) if (s < 12) return true;
    return false;
  }
  case kMiracleEye: return def != nullptr && !MonOf(def).miracleEye;
  case kGastroAcid: return def != nullptr && fightOutPokemonGetTokuseiDataId(def) != 0;
  case kWorrySeed: return def != nullptr && fightOutPokemonGetTokuseiDataId(def) != kInsomnia;
  case kEmbargo: return def != nullptr && !InWindow(MonOf(def).embargoTurn, 5);
  case kHealBlock: return def != nullptr && !InWindow(MonOf(def).healBlockTurn, 5);
  case kCaptivate: {
    if (def == nullptr) return false;
    const uint8_t a = fightOutPokemonGetSex(atk), b = fightOutPokemonGetSex(def);
    std::fprintf(stderr, "[gen4] Captivate: user sex %u, target sex %u\n", a, b);
    return a <= 1 && b <= 1 && a != b;
  }
  case kPsychoShift: return def != nullptr && StatusCode(atk) != 0 && StatusCode(def) == 0 && fightOutPokemonIsJoutaiNormal(def) != 0;
  default: return true;
  }
}

void MoveWork(uint16_t move, void* atk, void* def, int turn) {
  switch (move) {
  case kGravity:
    sGravityTurn = turn;
    Say(kMsgGravityOn, atk);
    break;
  case kMagnetRise:
    MonOf(atk).magnetTurn = turn;
    Say(kMsgMagnetOn, atk);
    break;
  case kAquaRing:
    MonOf(atk).aquaRing = true;
    Say(kMsgAquaOn, atk);
    break;
  case kToxicSpikes: {
    SideState& s = SideOf(Target(3, Narrow(atk)));
    s.toxicLayers++;
    Say(kMsgToxicSet + (OnPlayersSide(atk) ? 1 : 0), atk);
    break;
  }
  case kLuckyChant:
    SideOf(SideOfPokemon(atk)).luckyTurn = turn;
    SaySide(kMsgLuckyOn, atk);
    break;
  case kAcupressure: {
    uint8_t v[7];
    GetStages(atk, v);
    int open[7], n = 0;
    for (int i = 0; i < 7; i++) if (v[i] < 12) open[n++] = i;
    if (n > 0) ApplyEffect(static_cast<uint8_t>(0x27 + open[HSD_Rand() % n]), true);
    break;
  }
  case kDefog: {
    ClearSideEffects(Target(2, Narrow(def)), SideOf(Target(2, Narrow(def))));
    ApplyEffect(0x1C, false);
    break;
  }
  case kMiracleEye:
    MonOf(def).miracleEye = true;
    {
      uint8_t v[7];
      GetStages(def, v);
      if (v[6] > 6) {
        v[6] = 6;
        SetStages(def, v);
      }
    }
    Say(kMsgEyeOn, def);
    break;
  case kGastroAcid:
    fightOutPokemonSetTokuseiDataId(def, 0);
    Say(kMsgGastro, def);
    break;
  case kWorrySeed:
    fightOutPokemonSetTokuseiDataId(def, kInsomnia);
    Say(kMsgWorry, def);
    break;
  case kEmbargo:
    MonOf(def).embargoTurn = turn;
    Say(kMsgEmbargoOn, def);
    break;
  case kHealBlock:
    MonOf(def).healBlockTurn = turn;
    Say(kMsgHealBlockOn, def);
    break;
  case kCaptivate:
    ApplyEffect(0x31, false);
    break;
  case kPowerSwap:
  case kGuardSwap:
  case kHeartSwap: {
    uint8_t a[7], b[7];
    GetStages(atk, a);
    GetStages(def, b);
    const int first = move == kGuardSwap ? 1 : 0;
    for (int i = 0; i < 7; i++) {
      const bool swap = move == kHeartSwap || (move == kPowerSwap ? (i == 0 || i == 3) : (i == 1 || i == 4));
      (void)first;
      if (swap) std::swap(a[i], b[i]);
    }
    SetStages(atk, a);
    SetStages(def, b);
    Say(move == kPowerSwap ? kMsgPowerSwap : move == kGuardSwap ? kMsgGuardSwap : kMsgHeartSwap, atk);
    break;
  }
  case kPsychoShift: {
    const uint8_t code = StatusCode(atk);
    CureStatus(atk, code);
    ApplyEffect(code, false);
    break;
  }
  case kPowerTrick:
    MonOf(atk).powerTrick = !MonOf(atk).powerTrick;
    Say(kMsgPowerTrick, atk);
    break;
  case kHealingWish:
  case kLunarDance:
    SideOf(SideOfPokemon(atk)).wish = move == kHealingWish ? 1 : 2;
    break;
  default:
    break;
  }
  std::fprintf(stderr, "[gen4] move %u worked at turn %d\n", move, turn);
}

bool IsBerry(uint16_t item) { return item >= 133 && item <= 175; }
uint16_t BerryType(uint16_t item) {
  static const uint8_t types[16] = {10, 11, 13, 12, 15, 1, 3, 4, 2, 14, 6, 5, 7, 16, 17, 8};
  const int n = item <= 148 ? item - 133 : item <= 162 ? item - 149 : (item - 163 + 14) % 16;
  return types[n];
}
uint16_t BerryPower(uint16_t item) { return item <= 148 ? 60 : item <= 162 ? 70 : 80; }

uint16_t PowerOf(uint16_t move, void* atk, void* def) {
  const MonState* am = FindMon(atk);
  if (am != nullptr && am->meFirstTurn == Turn() && move != kMeFirst && wazaDB_GetIryoku(move) > 1) {
    return static_cast<uint16_t>(wazaDB_GetIryoku(move) * 3 / 2);
  }
  switch (move) {
  case kNaturalGift: {
    const uint16_t item = fightOutPokemonGetSoubiItemDataId(atk);
    return IsBerry(item) ? BerryPower(item) : 0;
  }
  case kFling: {
    const uint16_t item = fightOutPokemonGetSoubiItemDataId(atk);
    return item == 0 ? 0 : IsBerry(item) ? 10 : 30;
  }
  case kGyroBall: {
    const uint32_t us = Speed(atk), them = Speed(def);
    if (us == 0) return 0;
    const uint32_t p = 25 * them / us + 1;
    return static_cast<uint16_t>(p > 150 ? 150 : p);
  }
  case kBrine:
    return fightOutPokemonIsNokoriHpFollowing(def, 2) != 0 ? 130 : 65;
  case kWringOut:
  case kCrushGrip: {
    const uint32_t cur = fightOutPokemonNowHpWaruValue(def, 1), max = fightOutPokemonMaxHpWaruValue(def, 1);
    if (max == 0) return 0;
    const uint32_t p = 120 * cur / max;
    return static_cast<uint16_t>(p == 0 ? 1 : p);
  }
  case kPunishment: {
    const int p = 60 + 20 * PositiveStages(def);
    return static_cast<uint16_t>(p > 200 ? 200 : p);
  }
  case kPayback:
    return FindMon(def) != nullptr && FindMon(def)->movedTurn == Turn() ? 100 : 50;
  case kAssurance:
    return fightOutPokemon_GetDamageAtkValue(def) + fightOutPokemon_GetDamageSpeValue(def) > 0 ? 100 : 50;
  case kWakeUpSlap:
    return Asleep(def) ? 120 : 60;
  case kTrumpCard: {
    void* pokemon = fightOutPokemonGetPokemonPtr(atk);
    const int slot = fightWaza_GetWazaBanme(fightOutPokemon_GetFightWazaPtr(atk));
    if (pokemon == nullptr || slot < 0) return 40;
    const uint8_t pp = pokemon_GetPokemonWazaPp(pokemon, static_cast<uint32_t>(slot));
    return pp >= 4 ? 40 : pp == 3 ? 50 : pp == 2 ? 60 : pp == 1 ? 80 : 200;
  }
  default:
    return 0;
  }
}

uint16_t ChosenMove(void* fop) {
  const uint16_t m = fightOutPokemonGetCmpNimblenessWazaDataId(fop);
  return m != 0 ? m : fightOutPokemon_GetLastSelectWazaDataId(fop);
}

bool SuckerCanWork(void* def) {
  if (def == nullptr || (FindMon(def) != nullptr && FindMon(def)->movedTurn == Turn())) return false;
  const uint16_t chosen = ChosenMove(def);
  return chosen != 0 && wazaDB_GetIryoku(chosen) > 0;
}

bool LastResortCanWork(void* atk) {
  void* pokemon = fightOutPokemonGetPokemonPtr(atk);
  if (pokemon == nullptr) return false;
  const int self = fightWaza_GetWazaBanme(fightOutPokemon_GetFightWazaPtr(atk));
  const MonState* m = FindMon(atk);
  const uint8_t used = m != nullptr ? m->usedMask : 0;
  int known = 0;
  for (int i = 0; i < 4; i++) {
    if (pokemon_GetPokemonWazaDataId(pokemon, static_cast<uint32_t>(i)) == 0) continue;
    known++;
    if (i != self && ((used >> i) & 1) == 0) return false;
  }
  return known >= 2;
}

void HealWith(void* mon, uint32_t amount, uint32_t scriptOffset) {
  const uint32_t cur = fightOutPokemonNowHpWaruValue(mon, 1), max = fightOutPokemonMaxHpWaruValue(mon, 1);
  if (cur + amount > max) amount = max - cur;
  if (amount == 0) return;
  fightFloor_SetAttackPokemonPtr(0, mon);
  fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(mon), -static_cast<int32_t>(amount));
  fightSeq_CallWazaSeq(PortXD_GameAddress(kScriptBase + scriptOffset));
}

void EatBerry(void* user, void* target) {
  const uint16_t item = fightOutPokemonGetSoubiItemDataId(target);
  if (!IsBerry(item)) return;
  fightOutPokemonDoItemSoubi(target, 0, 0, 0);
  const uint8_t code = StatusCode(user);
  uint32_t heal = 0;
  if (item == 139) heal = 10;
  else if (item == 142) heal = fightOutPokemonMaxHpWaruValue(user, 4);
  const bool cures = (item == 141 && code != 0) || (item == 133 && code == 5) || (item == 134 && code == 1) ||
                     (item == 135 && (code == 2 || code == 6)) || (item == 136 && code == 3) || (item == 137 && code == 4);
  std::fprintf(stderr, "[gen4] berry %u eaten by the user (heal %u, cure %d)\n", item, heal, cures ? 1 : 0);
  if (heal != 0 && fightOutPokemonIsHpMantan(user) == 0 && !InWindow(MonOf(user).healBlockTurn, 5)) {
    HealWith(user, heal, kBerryHealOffset);
  } else {
    Say(kMsgBerryAte, user);
    if (cures) CureStatus(user, code);
  }
}

void HitExtra(uint16_t move, void* atk, void* def) {
  const bool alive = def != nullptr && fightOutPokemonCheckFightOut(def) != 0;
  static const int percent = [] { const char* e = std::getenv("XD_GEN4_EXTRA_PERCENT"); return e != nullptr ? std::atoi(e) : 10; }();
  switch (move) {
  case kFireFang:
  case kIceFang:
  case kThunderFang:
    if (alive && static_cast<int>(HSD_Rand() % 100) < percent) ApplyEffect(8, false);
    break;
  case kFlareBlitz:
    if (alive && static_cast<int>(HSD_Rand() % 100) < percent) ApplyEffect(3, false);
    break;
  case kFling:
  case kNaturalGift:
    fightOutPokemonDoItemSoubi(atk, 0, 0, 0);
    break;
  case kPluck:
  case kBugBite:
    if (alive) EatBerry(atk, def);
    break;
  default:
    break;
  }
}

bool CopyMove(uint16_t move, void* atk, void* def) {
  uint16_t copy = 0;
  if (move == kCopycat) {
    copy = sLastMove;
  } else if (def != nullptr && !(FindMon(def) != nullptr && FindMon(def)->movedTurn == Turn())) {
    const uint16_t chosen = ChosenMove(def);
    if (chosen != 0 && wazaDB_GetIryoku(chosen) > 1) copy = chosen;
  }
  if (copy == 0 || copy == kCopycat || copy == kMeFirst) {
    std::fprintf(stderr, "[gen4] move %u failed at turn %d (nothing to copy)\n", move, Turn());
    return Fail();
  }
  if (move == kMeFirst) MonOf(atk).meFirstTurn = Turn();
  fightWazaSetUseWazaStatus(fightOutPokemon_GetFightWazaPtr(atk), copy);
  void* target = fightSeqSelectDefensePokemonPtr(atk, copy, fightFloor_GetFightType(0), 0, 1, 1, -1);
  fightFloor_SetDefPokemonAndAtkFightWazaTargetId(0, target);
  ServerStatusFlag &= ~0x400u;
  std::fprintf(stderr, "[gen4] move %u uses move %u at turn %d\n", move, copy, Turn());
  fightSeq_JumpWazaSeq(PortXD_GameAddress(GetBe32(static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * wazaDB_GetSeqId(copy))))));
  return true;
}

uint16_t PowerOverride(uint16_t waza, void* attacker, void* defender) { return PowerOf(waza, attacker, defender); }
bool GravityActive() { Tick(); return Gen4On() && GravityOn(); }
bool MagnetRiseActive(void* fop) { Tick(); return Gen4On() && InWindow(MonOf(fop).magnetTurn, 5); }
bool MiracleEyeActive(void* fop) { return Gen4On() && MonOf(fop).miracleEye; }
bool LuckyChantOn(void* fop) { Tick(); return Gen4On() && InWindow(SideOf(SideOfPokemon(fop)).luckyTurn, 5); }
bool EmbargoActive(void* fop) { Tick(); return Gen4On() && InWindow(MonOf(fop).embargoTurn, 5); }
bool HealBlocked(void* fop) { Tick(); return Gen4On() && InWindow(MonOf(fop).healBlockTurn, 5); }

bool PortCommand(uint8_t id) {
  Tick();
  void* atk = Target(0x11);
  void* def = Target(0x12);
  const int turn = Turn();
  const uint16_t move = atk != nullptr ? fightOutPokemon_GetUseWazaDataId(atk) : 0;
  switch (id) {
  case kCmdCheck:
    if (!MoveCanWork(move, atk, def)) {
      std::fprintf(stderr, "[gen4] move %u failed at turn %d\n", move, turn);
      return Fail();
    }
    return false;
  case kCmdApply:
    MoveWork(move, atk, def, turn);
    return false;
  case kCmdSucker:
    if (move == kLastResort ? !LastResortCanWork(atk) : !SuckerCanWork(def)) {
      std::fprintf(stderr, "[gen4] move %u failed at turn %d\n", move, turn);
      fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage));
      return true;
    }
    return false;
  case kCmdHitSetup: {
    ServerWork[3] = 0;
    uint16_t item = atk != nullptr ? fightOutPokemonGetSoubiItemDataId(atk) : 0;
    if (atk != nullptr && EmbargoActive(atk)) item = 0;
    if ((move == kNaturalGift && !IsBerry(item)) || (move == kFling && item == 0)) {
      std::fprintf(stderr, "[gen4] move %u failed at turn %d (no item to use)\n", move, turn);
      fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailedWithMessage));
      return true;
    }
    if (move == kNaturalGift) fightWaza_SetZokusei(fightOutPokemon_GetFightWazaPtr(atk), BerryType(item));
    if (move == kFireFang) ServerWork[3] = 3;
    else if (move == kIceFang) ServerWork[3] = 4;
    else if (move == kThunderFang) ServerWork[3] = 5;
    else if (move == kFlareBlitz) ServerWork[3] = 0xE6;
    return false;
  }
  case kCmdHitExtra:
    HitExtra(move, atk, def);
    return false;
  case kCmdCopy:
    return CopyMove(move, atk, def);
  case kCmdTrickRoom:
    if (TrickRoomActive()) {
      sTrickSetTurn = -1;
      Say(kMsgTrickOff, atk);
    } else {
      sTrickSetTurn = turn;
      Say(kMsgTrickOn, atk);
    }
    std::fprintf(stderr, "[gen4] Trick Room %s at turn %d\n", sTrickSetTurn >= 0 ? "on" : "off", turn);
    return false;
  case kCmdRockSet: {
    SideState& s = SideOf(Target(3, Narrow(atk)));
    if (s.rocks) {
      std::fprintf(stderr, "[gen4] Stealth Rock failed (already there) at turn %d\n", turn);
      return Fail();
    }
    s.rocks = true;
    Say(kMsgRockSet + (OnPlayersSide(atk) ? 1 : 0), atk);
    std::fprintf(stderr, "[gen4] Stealth Rock set at turn %d\n", turn);
    return false;
  }
  case kCmdTailwind: {
    SideState& s = SideOf(SideOfPokemon(atk));
    if (s.tailwindTurn >= 0 && turn <= s.tailwindTurn + 2) {
      std::fprintf(stderr, "[gen4] Tailwind failed at turn %d\n", turn);
      return Fail();
    }
    s.tailwindTurn = turn;
    SaySide(kMsgTailwindOn, atk);
    std::fprintf(stderr, "[gen4] Tailwind at turn %d\n", turn);
    return false;
  }
  case kCmdRockMsg:
    Say(kMsgRockHit, sRockVictim);
    return false;
  default:
    return false;
  }
}

void AquaHeal(void* mon) {
  if (fightOutPokemonIsHpMantan(mon) != 0 || InWindow(MonOf(mon).healBlockTurn, 5)) return;
  uint32_t heal = fightOutPokemonMaxHpWaruValue(mon, 16);
  if (heal == 0) heal = 1;
  const uint32_t cur = fightOutPokemonNowHpWaruValue(mon, 1), max = fightOutPokemonMaxHpWaruValue(mon, 1);
  if (cur + heal > max) heal = max - cur;
  fightFloor_SetAttackPokemonPtr(0, mon);
  fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(mon), -static_cast<int32_t>(heal));
  std::fprintf(stderr, "[gen4] Aqua Ring heals %u\n", heal);
  fightSeq_CallWazaSeq(PortXD_GameAddress(kScriptBase + kAquaHealOffset));
}

void EndOfTurn() {
  Tick();
  const int turn = sLastTurn;
  if (sGravityTurn >= 0 && turn == sGravityTurn + 4) {
    sGravityTurn = -1;
    Say(kMsgGravityOff, nullptr);
  }
  if (sTrickSetTurn >= 0 && turn == sTrickSetTurn + 4) {
    sTrickSetTurn = -1;
    Say(kMsgTrickOff, nullptr);
  }
  for (auto& s : sSides) {
    if (s.side == nullptr) continue;
    void* mon = nullptr;
    for (auto& m : sMons) {
      if (m.mon != nullptr && SideOfPokemon(m.mon) == s.side) mon = m.mon;
    }
    if (s.tailwindTurn >= 0 && turn == s.tailwindTurn + 2) {
      s.tailwindTurn = -1;
      if (mon != nullptr) SaySide(kMsgTailwindOff, mon);
    }
    if (s.luckyTurn >= 0 && turn == s.luckyTurn + 4) {
      s.luckyTurn = -1;
      if (mon != nullptr) SaySide(kMsgLuckyOff, mon);
    }
  }
  for (auto& m : sMons) {
    if (m.mon == nullptr || fightOutPokemonCheckFightOut(m.mon) == 0) continue;
    if (m.magnetTurn >= 0 && turn == m.magnetTurn + 4) {
      m.magnetTurn = -1;
      Say(kMsgMagnetOff, m.mon);
    }
    if (m.embargoTurn >= 0 && turn == m.embargoTurn + 4) {
      m.embargoTurn = -1;
      Say(kMsgEmbargoOff, m.mon);
    }
    if (m.healBlockTurn >= 0 && turn == m.healBlockTurn + 4) {
      m.healBlockTurn = -1;
      Say(kMsgHealBlockOff, m.mon);
    }
    if (m.aquaRing && fightOutPokemonIsHinsi(m.mon) == 0) AquaHeal(m.mon);
  }
}

void ComingIn(void* p, uint8_t sideCode) {
  MonOf(p) = MonState();
  MonOf(p).mon = p;
  SideState& s = SideOf(SideOfPokemon(p));
  if (s.wish != 0 && fightOutPokemonCheckFightOut(p) != 0) {
    const int w = s.wish;
    s.wish = 0;
    if (uint8_t code = StatusCode(p)) CureStatus(p, code);
    if (w == 2) {
      void* pokemon = fightOutPokemonGetPokemonPtr(p);
      for (uint32_t i = 0; pokemon != nullptr && i < 4; i++) {
        if (pokemon_GetPokemonWazaDataId(pokemon, i) != 0) pokemon_SetPokemonWazaPp(pokemon, i, pokemonWazaGetMaxPP(pokemon, static_cast<uint16_t>(i)));
      }
    }
    std::fprintf(stderr, "[gen4] %s comes true for the Pokemon coming in\n", w == 1 ? "Healing Wish" : "Lunar Dance");
    if (fightOutPokemonIsHpMantan(p) != 0) Say(w == 1 ? kMsgWishCame : kMsgLunarCame, p);
    else HealWith(p, fightOutPokemonMaxHpWaruValue(p, 1), w == 1 ? kWishHealOffset : kLunarHealOffset);
  }
  if (s.toxicLayers <= 0 || fightOutPokemonCheckFightOut(p) == 0) return;
  if (fightOutPokemonIsZokuseiDataId(p, 3) != 0) {
    s.toxicLayers = 0;
    Say(kMsgToxicAbsorb, p);
    return;
  }
  const bool flying = fightOutPokemonIsZokuseiDataId(p, 2) != 0 || fightOutPokemonGetTokuseiDataId(p) == kLevitate;
  if ((flying && !GravityOn()) || fightOutPokemonIsZokuseiDataId(p, 8) != 0 || fightOutPokemonIsJoutaiNormal(p) == 0) return;
  if (sideCode != 0x11 && sideCode != 0x12) return;
  ApplyEffect(s.toxicLayers >= 2 ? 6 : 2, sideCode == 0x11);
}

}

extern "C" {

uint32_t PortXD_Gen4NowNimbleness(void* fop, uint8_t useBadge, uint8_t weather, uint16_t rnd, void* hero) {
  uint32_t speed = fightOutPokemonGetNowNimbleness(fop, useBadge, weather, rnd, hero);
  if (!Gen4On()) return speed;
  EnsureInstalled();
  Tick();
  if (fop != nullptr && TailwindActive(fop)) speed *= 2;
  speed = xd::gen4::AbilitySpeed(fop, speed);
  if (!TrickRoomActive()) return speed;
  return 65535u - (speed > 65535u ? 65535u : speed);
}

void PortXD_Gen4CallWazaSeq(void* floor, void* script, uint32_t flag) {
  if (Gen4On()) {
    EnsureInstalled();
    Tick();
    const int turn = fightFloor_GetTurnCount(nullptr);
    if (void* mover = fightTargetGetPtrAsNowFightType(0x11, 0)) {
      MonState& ms = MonOf(mover);
      ms.movedTurn = turn;
      const uint16_t used = fightOutPokemon_GetUseWazaDataId(mover);
      const int slot = fightWaza_GetWazaBanme(fightOutPokemon_GetFightWazaPtr(mover));
      if (slot >= 0 && slot < 4) ms.usedMask |= static_cast<uint8_t>(1u << slot);
      if (used != kCopycat && used != kMeFirst) sLastMove = used;
      if (GravityActive() && (used == 19 || used == 340 || used == 150 || used == 136 || used == 26)) {
        std::fprintf(stderr, "[gen4] move %u stopped by Gravity at turn %d\n", used, turn);
        script = PortXD_GameAddress(kFailedWithMessage);
      }
      if (used == kFeint || used == kShadowForce) {
        void* def = fightTargetGetPtrAsNowFightType(0x12, 0);
        if (def != nullptr && fightOutPokemonIsJoutaiDataId(def, 0x2B) != 0) {
          fightOutPokemonInitJoutaiDataId(def, 0x2B);
          std::fprintf(stderr, "[gen4] move %u ended the target's Protect at turn %d\n", used, turn);
        }
      }
    }
    if (script == PortXD_GameAddress(GetBe32(static_cast<const uint8_t*>(PortXD_GameAddress(kWazaEffectTbl + 4 * kRecoverSlot))))) {
      void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
      if (attacker != nullptr && wazaDB_GetSeqId(fightOutPokemon_GetUseWazaDataId(attacker)) == kRoostSlot &&
          fightOutPokemonIsHpMantan(attacker) == 0) {
        sRoostMon = attacker;
        sRoostTurn = turn;
        std::fprintf(stderr, "[gen4] Roost at turn %d: no Flying type until the turn ends\n", turn);
      }
    }
  }
  fightSeq_CallWazaSeqCustom(floor, script, flag);
}

uint16_t PortXD_Gen4GetType(void* fop, uint8_t index) {
  const uint16_t t = fightOutPokemonGetZokuseiDataId(fop, index);
  if (t == 2 && fop == sRoostMon && fop != nullptr && Gen4On() && fightFloor_GetTurnCount(nullptr) == sRoostTurn) return 9;
  return t;
}

void PortXD_Gen4ReshuffleEnd(void) {
  if (Gen4On()) {
    Tick();
    const auto* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
    if (cmd != nullptr && cmd[1] == kPortMark) {
      if (!PortCommand(cmd[2])) fightSeq_NextWazaSeq(5);
      return;
    }
    void* p = cmd != nullptr ? fightTargetGetPtrAsNowFightType(cmd[1], 0) : nullptr;
    if (p != nullptr) {
      ComingIn(p, cmd[1]);
      if (SideOf(SideOfPokemon(p)).rocks && fightOutPokemonCheckFightOut(p) != 0 && RocksHit(p, cmd[1])) return;
    }
  }
  WS_POKE_RESHUFFLE_END();
}

void PortXD_Gen4TurnEnd(void) {
  fightSeqFloorJoutaiCheck();
  if (Gen4On()) {
    EndOfTurn();
    wazaSequenceSysFreeAllWaza();
  }
}

void PortXD_Gen4Kaifuku(void) {
  if (Gen4On()) {
    void* atk = fightTargetGetPtrAsNowFightType(0x11, 0);
    if (atk != nullptr && HealBlocked(atk)) {
      Say(kMsgHealBlocked, atk);
      fightSeq_JumpWazaSeq(PortXD_GameAddress(kFailed));
      return;
    }
  }
  WS_KAIFUKU();
}

uint32_t PortXD_Gen4SoubiKind(void* fop) {
  if (Gen4On() && fop != nullptr && EmbargoActive(fop)) return 0;
  return fightOutPokemonGetSoubiItemSoubiDataId(fop);
}

void PortXD_Gen4SetCritical(void* fw, uint32_t value) {
  if (value == 2 && Gen4On()) {
    void* def = fightTargetGetPtrAsNowFightType(0x12, 0);
    if (def != nullptr && LuckyChantOn(def)) value = 1;
  }
  fightWaza_SetCritical(fw, value);
}

static bool PowerTricked(void* pokemon) {
  for (const auto& m : sMons) {
    if (m.mon != nullptr && m.powerTrick && fightOutPokemonGetPokemonPtr(m.mon) == pokemon) return true;
  }
  return false;
}
uint16_t PortXD_Gen4PhyAtk(void* pokemon) {
  if (Gen4On() && PowerTricked(pokemon)) {
    static int shown = 0;
    if (shown++ < 4) std::fprintf(stderr, "[gen4] Power Trick: Attack read as Defense (%u instead of %u)\n", pokemon_GetPhyDef(pokemon), pokemon_GetPhyAtk(pokemon));
    return pokemon_GetPhyDef(pokemon);
  }
  return pokemon_GetPhyAtk(pokemon);
}
uint16_t PortXD_Gen4PhyDef(void* pokemon) {
  if (Gen4On() && PowerTricked(pokemon)) {
    static int shown = 0;
    if (shown++ < 4) std::fprintf(stderr, "[gen4] Power Trick: Defense read as Attack (%u instead of %u)\n", pokemon_GetPhyAtk(pokemon), pokemon_GetPhyDef(pokemon));
    return pokemon_GetPhyAtk(pokemon);
  }
  return pokemon_GetPhyDef(pokemon);
}

int32_t PortXD_Gen4GetHitDamage(void* fw) {
  const int32_t v = fightWaza_GetHitDamage(fw);
  if (Gen4On() && (ServerWork[3] & 0x3F) == 0x26 && fightWaza_GetUseWazaDataId(fw) == kHeadSmash) return v * 3 / 2;
  return v;
}

void PortXD_Gen4Counter(void) {
  void* atk = Gen4On() ? fightTargetGetPtrAsNowFightType(0x11, 0) : nullptr;
  if (atk != nullptr && fightOutPokemon_GetUseWazaDataId(atk) == kMetalBurst) {
    const int16_t pt = pokemonGetStatus(atk, 0, 0x103, 0), pv = pokemonGetStatus(atk, 0, 0x102, 0);
    const int16_t st = pokemonGetStatus(atk, 0, 0x105, 0), sv = pokemonGetStatus(atk, 0, 0x104, 0);
    const bool special = sv > pv;
    pokemonSetStatus(atk, 0, 0x103, 0, static_cast<uint32_t>(special ? st : pt));
    pokemonSetStatus(atk, 0, 0x102, 0, static_cast<uint32_t>((special ? sv : pv) * 3 / 4));
    std::fprintf(stderr, "[gen4] Metal Burst: %d physical, %d special taken\n", pv, sv);
    WS_COUNTER();
    pokemonSetStatus(atk, 0, 0x103, 0, static_cast<uint32_t>(pt));
    pokemonSetStatus(atk, 0, 0x102, 0, static_cast<uint32_t>(pv));
    return;
  }
  WS_COUNTER();
}

void PortXD_Gen4MoveTexts(void (*add)(void* ctx, uint32_t id, const char* text), void* ctx) {
  if (!Gen4On()) return;
  for (const MoveText& t : kMoveTexts) add(ctx, t.id, t.text);
}

void PortXD_Gen4Renzoku(void) {
  const auto* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
  const bool fixed = cmd != nullptr && cmd[1] != 0;
  WS_RENZOKU();
  if (!fixed && Gen4On()) {
    void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
    if (attacker != nullptr && fightOutPokemonGetTokuseiDataId(attacker) == kSkillLink) {
      fightWaza_SetKaisuu(fightOutPokemon_GetFightWazaPtr(attacker), 5);
      std::fprintf(stderr, "[gen4] ability: Skill Link: 5 hits\n");
    }
  }
}

uint8_t fightSeqFightOutPokemonTokuseiCheckWazaHit(void* attacker, void* defender, uint16_t waza) {
  if (Gen4On() && defender != nullptr) {
    uint8_t motor = 0;
    if (xd::gen4::MotorDriveHit(attacker, defender, waza, &motor)) return motor;
    xd::gen4::MoldScope mold(attacker, defender);
    const uint16_t ab = fightOutPokemonGetTokuseiDataId(defender);
    if (ab == kLevitate && GravityActive()) {
      fightOutPokemonSetTokuseiDataId(defender, 0);
      const uint8_t r = PortXD_RealTokuseiWazaHit(attacker, defender, waza);
      fightOutPokemonSetTokuseiDataId(defender, kLevitate);
      return r;
    }
    if (ab == kDrySkin) {
      fightOutPokemonSetTokuseiDataId(defender, 11);
      const uint8_t r = PortXD_RealTokuseiWazaHit(attacker, defender, waza);
      fightOutPokemonSetTokuseiDataId(defender, kDrySkin);
      return r;
    }
    return PortXD_RealTokuseiWazaHit(attacker, defender, waza);
  }
  return PortXD_RealTokuseiWazaHit(attacker, defender, waza);
}

}
