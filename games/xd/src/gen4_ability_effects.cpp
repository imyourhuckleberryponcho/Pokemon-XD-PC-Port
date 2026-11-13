#include "xd_challenges.h"
#include "xd_gen4_abilities.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
uint16_t fightOutPokemonGetTokuseiDataId(void* fop);
void fightOutPokemonSetTokuseiDataId(void* fop, uint16_t id);
uint16_t fightOutPokemonGetSoubiItemSoubiDataId(void* fop);
void* fightTargetGetPtrAsNowFightType(uint32_t kind, uint32_t arg);
uint8_t fightOutPokemonCheckFightOut(void* fop);
uint8_t fightOutPokemonIsJoutaiDataId(void* fop, uint16_t id);
uint8_t fightOutPokemonIsJoutaiNormal(void* fop);
void fightOutPokemonInitJoutaiDataId(void* fop, uint16_t id);
void fightOutPokemonWriteJoutaiDataId(void* fop, uint16_t id, uint32_t value);
uint16_t fightOutPokemonMaxHpWaruValue(void* fop, uint8_t divisor);
uint8_t fightOutPokemonIsHpMantan(void* fop);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
void fightWaza_SetDamage(void* fw, int32_t damage);
uint8_t fightWaza_GetCritical(void* fw);
void fightWaza_SetCritical(void* fw, uint8_t value);
void fightFloor_SetAttackPokemonPtr(void* floor, void* fop);
void fightFloor_SetAppointPokemonPtr(void* floor, void* fop);
void fightSeq_CallWazaSeq(void* script);
void fightSeq_JumpWazaSeq(void* script);
void fightSeq_NextWazaSeq(int32_t size);
void wazaSequenceSysFreeAllWaza(void);
uint32_t fightFloorGetNowTenkouDataId(void* floor, uint8_t checkAbility);
uint16_t fightFloor_GetTurnCount(void* floor);
uint8_t fightFloorLoopValidFightOutPokemon(void* floor, int32_t (*func)(void*, uint16_t, void*), void* data, uint8_t useAry);
uint16_t fightFloorGetFightOutPokemonPtrAry(void* floor, uint8_t fightOut, uint8_t mode, void* fop, void** ary);
uint8_t fightFloorCheckWriteJoutaiDataId(void* floor, uint16_t id);
void fightFloorWriteJoutaiDataId(void* floor, uint16_t id, uint16_t value);
void fightFloorSetNowKaisuuJoutaiDataId(void* floor, uint16_t id, int8_t value);
uint8_t fightOutPokemonGetSex(void* fop);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t pokemon_GetPhyDef(void* pokemon);
uint16_t pokemon_GetSpeDef(void* pokemon);
uint16_t pokemon_GetHp(void* pokemon);
uint16_t pokemon_GetPokemonWazaDataId(void* pokemon, uint32_t slot);
uint8_t fightOutPokemon_GetAbicntPhyAtk(void* fop);
uint8_t fightOutPokemon_GetAbicntPhyDef(void* fop);
uint8_t fightOutPokemon_GetAbicntSpeAtk(void* fop);
uint8_t fightOutPokemon_GetAbicntSpeDef(void* fop);
void fightOutPokemonBiosSetAbicntPhyAtk(uint8_t* fop, uint8_t v);
void fightOutPokemonBiosSetAbicntPhyDef(uint8_t* fop, uint8_t v);
void fightOutPokemonBiosSetAbicntSpeAtk(uint8_t* fop, uint8_t v);
void fightOutPokemonBiosSetAbicntSpeDef(uint8_t* fop, uint8_t v);
intptr_t pokemonGetStatus(void* fop, int32_t a, int32_t id, int32_t b);
void pokemonSetStatus(void* fop, int32_t a, int32_t id, int32_t b, uint32_t value);
int32_t fightSeqCondChgActTypeToPokemonStatusId(uint8_t type);
uint16_t fightOutPokemon_GetUseWazaDataId(void* fop);
uint8_t fightOutPokemonGetWazaZokuseiDataId(void* fop);
uint8_t fightOutPokemon_GetPassPpdecFlag(void* fop);
uint16_t wazaDB_GetIryoku(uint16_t waza);
uint16_t wazaDB_GetZokuseiDataId(uint16_t waza);
uint16_t wazaDB_GetSeqId(uint16_t waza);
uint8_t wazaDB_GetDageki(uint16_t waza);
uint16_t zokuseiGetWazaJoutai(uint16_t wazaType, uint16_t defenderType);
uint16_t fightOutPokemonGetZokuseiDataId(void* fop, uint8_t index);
uint16_t figthOutPokemonGetPokemonDataId(void* fop);
uint8_t fightWazaIsHit(void* fw);
int16_t fightOutPokemon_GetMyselfDamageAtkValue(void* fop);
int16_t fightOutPokemon_GetMyselfDamageSpeValue(void* fop);
uint16_t HSD_Rand(void);
void* fightWazaCriticalDataBiosGetPtr(uint16_t index);
uint8_t fightWazaCriticalDataBiosGetBunbo(void* row);
extern int32_t fight_waza_critical_data_number;
uint8_t fightFloor_GetFightKindDoCriticalAttackFlag(void* floor);
void PortVideo_Notice(const char* text, double seconds);
uint32_t PortXD_SplitCheckDarkWaza(uint16_t waza);
int32_t PortXD_SplitTypeKind(uint16_t type);
int32_t PortXD_RealWazaHitBefore(void* attacker, void* defender);
int32_t PortXD_RealWazaHitAfter(void* attacker, void* defender);
void PortXD_RealAppear(void* fop);
uint8_t PortXD_RealAttackJoutaiCheck(void);
void fightSeqSideJoutaiCheck(void);
void WS_CRITICAL_CHECK(void);
void* fightSeq_GetWazaSeq(void);
uint8_t wazaDB_GetRangeId(uint16_t waza);
uint16_t fightWaza_GetZokusei(void* fw);
uint8_t fightOutPokemonIsNokoriHpFollowing(void* fop, uint16_t waru);
uint16_t fightFloorCheckFightOutPokemonPtrAryPokemonTokuseiDataId(void* floor, uint16_t tokusei, uint8_t mode, void* fop);
void* fightFloorGetFightOutPokemonPtrAryPokemonTokuseiDataIdFirst(void* floor, uint16_t tokusei, uint8_t useAry, uint8_t mode, void* fop);
void* PortXD_RealSelectDefense(void* p, uint16_t waza, uint32_t type, void* select, uint8_t random, uint8_t special, int8_t rangeOverride);
uint8_t PortXD_RealWazaType(void* fop);
void WS_TSUIKA_INDIRECT_ACT(void);
void WS_TSUIKA_DIRECT_ACT(void);
void* PortXD_GameAddress(uint32_t gcAddress);
void PortXD_AddScriptBlock(uint32_t gcAddress, void* bytes, uint32_t size);
extern uint32_t ServerStatusFlag;
extern uint8_t ServerWork[8];
extern uint8_t* UserWork;
}

namespace {

constexpr uint16_t kGluttony = 82, kFrisk = 119, kForewarn = 108, kHeatproof = 85;
constexpr uint16_t kStormDrain = 114, kLeafGuard = 102, kNormalize = 96, kStall = 100, kMagicGuard = 98;
constexpr uint16_t kTangledFeet = 77, kMotorDrive = 78, kRivalry = 79, kSteadfast = 80, kAngerPoint = 83, kUnburden = 84,
                   kSimple = 86, kDrySkin = 87, kDownload = 88, kPoisonHeal = 90, kHydration = 93, kSolarPower = 94,
                   kQuickFeet = 95, kKlutz = 103, kMoldBreaker = 104, kSuperLuck = 105, kAftermath = 106, kAnticipation = 107,
                   kUnaware = 109, kSlowStart = 112, kIceBody = 115, kSnowWarning = 117, kFlowerGift = 122, kBadDreams = 123,
                   kInert = 77;
constexpr uint32_t kBlock = 0x8F001000u;
constexpr uint32_t kWazaEnd = 0x8041410Fu, kKasoku = 0x80417957u, kAmeukezara = 0x8041791Cu, kDokudoku = 0x804174FDu;
constexpr uint32_t kBank = 0x7D0u << 20;
enum Msg : uint32_t { kMsgSolarHurt = 0x9000, kMsgDryHurt, kMsgBadDreams, kMsgAftermath, kMsgDryHeal, kMsgIceHeal, kMsgPoisonHeal,
                      kMsgHydration, kMsgAnticipation, kMsgFrisk, kMsgForewarn, kMsgCount = 11 };
constexpr uint32_t kMotorOff = 0, kAtkOff = 0x40, kSpaOff = 0x80, kHurtOff = 0x100, kHealOff = 0x200,
                   kMsgOff = 0x2C0, kHailOff = 0x3C0;
uint8_t sScripts[0x400];
bool sInstalled = false;

bool On() { return xd::challenges::Active("gen4_rules"); }
uint16_t Ab(void* f) { return f != nullptr ? fightOutPokemonGetTokuseiDataId(f) : 0; }
bool Alive(void* f) { return f != nullptr && fightOutPokemonCheckFightOut(f) != 0; }
int Weather() { return static_cast<int>(fightFloorGetNowTenkouDataId(nullptr, 1)); }

void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

struct Mon {
  void* fop = nullptr;
  int entry = 0;
  bool hadItem = false;
  bool lost = false;
  bool poisoned = false;
};
Mon sMons[16];
int sLastTurn = -1;

void Tick() {
  const int turn = fightFloor_GetTurnCount(nullptr);
  if (turn < sLastTurn) {
    for (auto& m : sMons) m = Mon();
  }
  sLastTurn = turn;
}
Mon& M(void* fop) {
  for (auto& m : sMons) if (m.fop == fop) return m;
  for (auto& m : sMons) if (m.fop == nullptr) { m.fop = fop; m.entry = sLastTurn < 0 ? 0 : sLastTurn; return m; }
  sMons[0] = Mon();
  sMons[0].fop = fop;
  return sMons[0];
}

size_t RaiseScript(uint8_t* out, uint8_t code, bool jump) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(kKasoku));
  if (src == nullptr || src[0] != 0x2F || src[11] != 0x11 || src[33] != 0x40) return 0;
  size_t n = 0;
  if (jump) {
    out[n++] = 0x04;
    out[n++] = 0x02;
  }
  std::memcpy(out + n, src, 33);
  out[n + 11] = code;
  n += 33;
  if (jump) {
    out[n++] = 0x29;
    PutBe32(out + n, kWazaEnd);
    return n + 4;
  }
  out[n++] = 0x40;
  return n;
}

size_t HurtScript(uint8_t* out, uint32_t msg) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(kDokudoku));
  if (src == nullptr || src[0] != 0x65 || src[15] != 0x11 || src[20] != 0x13 || src[39] != 0x3F) return 0;
  std::memcpy(out, src, 40);
  PutBe32(out + 16, kBank | msg);
  return 40;
}

size_t HealScript(uint8_t* out, uint32_t msg) {
  const auto* src = static_cast<const uint8_t*>(PortXD_GameAddress(kAmeukezara));
  if (src == nullptr || src[0] != 0x36 || src[20] != 0x11 || src[34] != 0x40) return 0;
  std::memcpy(out, src, 35);
  PutBe32(out + 21, kBank | msg);
  return 35;
}

size_t MessageScript(uint8_t* out, uint32_t msg) {
  const uint8_t s[] = {0x11, 0, 0, 0, 0, 0x13, 0x00, 0x60, 0x0B, 0x04, 0x40};
  std::memcpy(out, s, sizeof s);
  PutBe32(out + 1, kBank | msg);
  return sizeof s;
}

size_t HailScript(uint8_t* out) {
  const uint8_t s[] = {0x14, 0x80, 0x2F, 0x8F, 0x38, 0x13, 0x00, 0x60, 0x0B, 0x04, 0x40};
  std::memcpy(out, s, sizeof s);
  return sizeof s;
}

void Install() {
  if (sInstalled) return;
  sInstalled = true;
  const size_t a = RaiseScript(sScripts + kMotorOff, 0x11, true);
  const size_t b = RaiseScript(sScripts + kAtkOff, 0x0F, false);
  const size_t c = RaiseScript(sScripts + kSpaOff, 0x12, false);
  size_t d = 1;
  const uint32_t hurtMsg[4] = {kMsgSolarHurt, kMsgDryHurt, kMsgBadDreams, kMsgAftermath};
  for (int i = 0; i < 4; i++) d &= HurtScript(sScripts + kHurtOff + 0x40 * i, hurtMsg[i]) != 0;
  const uint32_t healMsg[3] = {kMsgDryHeal, kMsgIceHeal, kMsgPoisonHeal};
  for (int i = 0; i < 3; i++) d &= HealScript(sScripts + kHealOff + 0x40 * i, healMsg[i]) != 0;
  MessageScript(sScripts + kMsgOff, kMsgHydration);
  MessageScript(sScripts + kMsgOff + 0x40, kMsgAnticipation);
  MessageScript(sScripts + kMsgOff + 0x80, kMsgFrisk);
  MessageScript(sScripts + kMsgOff + 0xC0, kMsgForewarn);
  HailScript(sScripts + kHailOff);
  PortXD_AddScriptBlock(kBlock, sScripts, sizeof sScripts);
  std::fprintf(stderr, "[gen4] ability scripts: Motor Drive %s, Attack raise %s, Sp. Atk raise %s, hurt %s\n", a ? "built" : "NOT built",
               b ? "built" : "NOT built", c ? "built" : "NOT built", d ? "built" : "NOT built");
}
void* Script(uint32_t off) { return PortXD_GameAddress(kBlock + off); }

bool sInHook = false;
bool sTurnHookSeen = false;
bool sStatusHookSeen = false;
void TurnScript(void* p, int32_t damage, void* script) {
  if (!sInHook) ServerStatusFlag = ServerStatusFlag | 0x01000000u | 0x20u;
  fightFloor_SetAttackPokemonPtr(nullptr, p);
  fightFloor_SetAppointPokemonPtr(nullptr, p);
  fightFloor_SetAttackPokemonPtr(nullptr, p);
  fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(p), damage);
  fightSeq_CallWazaSeq(script);
  if (!sInHook) {
    ServerStatusFlag &= 0xFEFFFFDFu;
    wazaSequenceSysFreeAllWaza();
  }
}

void Hurt(void* p, int divisor, int which, const char* text) {
  if (!Alive(p)) return;
  int32_t d = fightOutPokemonMaxHpWaruValue(p, static_cast<uint8_t>(divisor));
  if (d < 1) d = 1;
  std::fprintf(stderr, "[gen4] ability: %s (%d damage)\n", text, d);
  TurnScript(p, d, Script(kHurtOff + 0x40 * which));
}
void Heal(void* p, int divisor, int which, const char* text) {
  if (!Alive(p) || fightOutPokemonIsHpMantan(p) != 0) return;
  int32_t d = fightOutPokemonMaxHpWaruValue(p, static_cast<uint8_t>(divisor));
  if (d < 1) d = 1;
  std::fprintf(stderr, "[gen4] ability: %s (+%d HP)\n", text, d);
  TurnScript(p, -d, Script(kHealOff + 0x40 * which));
}

int Stage(void* fop, int which) {
  return static_cast<int>(pokemonGetStatus(fop, 0, fightSeqCondChgActTypeToPokemonStatusId(static_cast<uint8_t>(which)), 0));
}
bool Raise(void* fop, int which, int by) {
  const int id = fightSeqCondChgActTypeToPokemonStatusId(static_cast<uint8_t>(which));
  const int cur = static_cast<int>(pokemonGetStatus(fop, 0, id, 0));
  if (cur >= 12) return false;
  pokemonSetStatus(fop, 0, id, 0, static_cast<uint32_t>(cur + by > 12 ? 12 : cur + by));
  return true;
}

struct Turn {
  int phase;
};

void TurnAbility(void* fop) {
  if (!Alive(fop)) return;
  const uint16_t ab = Ab(fop);
  Mon& m = M(fop);
  const int weather = Weather();
  switch (ab) {
  case kPoisonHeal:
    if (m.poisoned) Heal(fop, 8, 2, "Poison Heal restored HP");
    break;
  case kSolarPower:
    if (weather == 1) Hurt(fop, 8, 0, "Solar Power hurt the Pokemon");
    break;
  case kDrySkin:
    if (weather == 2) Heal(fop, 8, 0, "Dry Skin restored HP in the rain");
    else if (weather == 1) Hurt(fop, 8, 1, "Dry Skin hurt the Pokemon in the sun");
    break;
  case kIceBody:
    if (weather == 4) Heal(fop, 16, 1, "Ice Body restored HP in the hail");
    break;
  case kHydration:
    if (weather == 2 && !fightOutPokemonIsJoutaiNormal(fop)) {
      for (uint16_t j : {3, 4, 5, 6, 7, 8}) fightOutPokemonInitJoutaiDataId(fop, j);
      std::fprintf(stderr, "[gen4] ability: Hydration cured the status in the rain\n");
      fightFloor_SetAppointPokemonPtr(nullptr, fop);
      fightFloor_SetAttackPokemonPtr(nullptr, fop);
      fightSeq_CallWazaSeq(Script(kMsgOff));
    }
    break;
  case kBadDreams: {
    void* foes[8] = {};
    const uint16_t n = fightFloorGetFightOutPokemonPtrAry(nullptr, 1, 2, fop, foes);
    for (uint16_t i = 0; i < n && i < 8; i++) {
      if (Alive(foes[i]) && fightOutPokemonIsJoutaiDataId(foes[i], 8) == 1) Hurt(foes[i], 8, 2, "Bad Dreams tormented the sleeping Pokemon");
    }
    break;
  }
  case kSnowWarning:
    if (weather == 4) fightFloorSetNowKaisuuJoutaiDataId(nullptr, 0x53, 0);
    break;
  default:
    break;
  }
}

int32_t EachMon(void* fop, uint16_t, void* user) {
  const int phase = static_cast<Turn*>(user)->phase;
  if (!Alive(fop)) return 1;
  const uint16_t ab = Ab(fop);
  Mon& m = M(fop);
  if (phase == 0) {
    m.poisoned = false;
    if (ab == kPoisonHeal && (fightOutPokemonIsJoutaiDataId(fop, 3) == 1 || fightOutPokemonIsJoutaiDataId(fop, 4) == 1)) {
      m.poisoned = true;
      fightOutPokemonInitJoutaiDataId(fop, 3);
      fightOutPokemonInitJoutaiDataId(fop, 4);
    }
    return 1;
  }
  if (ab == kPoisonHeal && m.poisoned) fightOutPokemonWriteJoutaiDataId(fop, 3, 0);
  if (!sTurnHookSeen) TurnAbility(fop);
  m.poisoned = false;
  return 1;
}

}

namespace xd::gen4 {

StageGuard::StageGuard(void* attacker, void* defender) {
  if (!On() || attacker == nullptr || defender == nullptr) return;
  const uint16_t a = Ab(attacker), d = Ab(defender);
  if (a != kUnaware && a != kSimple && d != kUnaware && d != kSimple) return;
  atk_ = attacker;
  def_ = defender;
  saved_[0] = fightOutPokemon_GetAbicntPhyAtk(attacker);
  saved_[1] = fightOutPokemon_GetAbicntSpeAtk(attacker);
  saved_[2] = fightOutPokemon_GetAbicntPhyDef(defender);
  saved_[3] = fightOutPokemon_GetAbicntSpeDef(defender);
  on_ = true;
  auto twice = [](uint8_t s) { const int v = 6 + 2 * (static_cast<int>(s) - 6); return static_cast<uint8_t>(v < 0 ? 0 : v > 12 ? 12 : v); };
  uint8_t pa = saved_[0], sa = saved_[1], pd = saved_[2], sd = saved_[3];
  if (a == kUnaware) pd = sd = 6;
  if (d == kUnaware) pa = sa = 6;
  if (a == kSimple) { pa = twice(pa); sa = twice(sa); }
  if (d == kSimple) { pd = twice(pd); sd = twice(sd); }
  fightOutPokemonBiosSetAbicntPhyAtk(static_cast<uint8_t*>(attacker), pa);
  fightOutPokemonBiosSetAbicntSpeAtk(static_cast<uint8_t*>(attacker), sa);
  fightOutPokemonBiosSetAbicntPhyDef(static_cast<uint8_t*>(defender), pd);
  fightOutPokemonBiosSetAbicntSpeDef(static_cast<uint8_t*>(defender), sd);
  std::fprintf(stderr, "[gen4] ability: stages read for the damage: Atk %d->%d SpA %d->%d Def %d->%d SpD %d->%d (Unaware / Simple)\n",
               saved_[0], pa, saved_[1], sa, saved_[2], pd, saved_[3], sd);
}
StageGuard::~StageGuard() {
  if (!on_) return;
  fightOutPokemonBiosSetAbicntPhyAtk(static_cast<uint8_t*>(atk_), saved_[0]);
  fightOutPokemonBiosSetAbicntSpeAtk(static_cast<uint8_t*>(atk_), saved_[1]);
  fightOutPokemonBiosSetAbicntPhyDef(static_cast<uint8_t*>(def_), saved_[2]);
  fightOutPokemonBiosSetAbicntSpeDef(static_cast<uint8_t*>(def_), saved_[3]);
}

int32_t AbilityDamage(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage) {
  if (!On() || attacker == nullptr || defender == nullptr || damage <= 2) return damage;
  auto scale = [](int32_t d, int n, int den) { return (d - 2) * n / den + 2; };
  const uint16_t a = Ab(attacker), d = Ab(defender);
  if (a < 77 && d < 77) return damage;
  PortXD_SplitCheckDarkWaza(waza);
  const int kind = PortXD_SplitTypeKind(type);
  Tick();
  switch (a) {
  case kRivalry: {
    const uint8_t sa = fightOutPokemonGetSex(attacker), sd = fightOutPokemonGetSex(defender);
    if (sa < 2 && sd < 2) damage = sa == sd ? scale(damage, 5, 4) : scale(damage, 3, 4);
    std::fprintf(stderr, "[gen4] ability: Rivalry: sexes %d / %d -> damage %d\n", sa, sd, damage);
    break;
  }
  case kSlowStart:
    if (kind == 1 && sLastTurn < M(attacker).entry + 5) damage = scale(damage, 1, 2);
    break;
  case kFlowerGift:
    if (kind == 1 && Weather() == 1) damage = scale(damage, 3, 2);
    break;
  default:
    break;
  }
  if (d == kFlowerGift && kind == 2 && Weather() == 1 && a != kMoldBreaker) damage = scale(damage, 2, 3);
  std::fprintf(stderr, "[gen4] damage question: attacker ability %u, defender ability %u, weather %d, %s move, number %d\n", a, d,
               Weather(), kind == 1 ? "physical" : kind == 2 ? "special" : "other", damage);
  return damage;
}

uint32_t AbilitySpeed(void* fop, uint32_t speed) {
  if (!On() || fop == nullptr) return speed;
  const uint16_t ab = Ab(fop);
  if (ab < 77) return speed;
  Tick();
  switch (ab) {
  case kStall:
    speed = 0;
    break;
  case kQuickFeet:
    if (!fightOutPokemonIsJoutaiNormal(fop)) {
      if (fightOutPokemonIsJoutaiDataId(fop, 5) == 1) speed *= 4;
      speed = speed * 3 / 2;
    }
    break;
  case kUnburden:
    if (M(fop).lost) speed *= 2;
    break;
  case kSlowStart:
    if (sLastTurn < M(fop).entry + 5) speed /= 2;
    break;
  default:
    break;
  }
  return speed;
}

uint8_t AbilityAccuracy(void* attacker, void* defender, uint8_t avg) {
  if (!On() || defender == nullptr || avg == 0 || avg == 255) return avg;
  if (attacker != nullptr && Ab(attacker) == kMoldBreaker) return avg;
  if (Ab(defender) == kTangledFeet && fightOutPokemonIsJoutaiDataId(defender, 9) == 1) {
    std::fprintf(stderr, "[gen4] ability: Tangled Feet while confused: accuracy %d -> %d\n", avg, avg / 2);
    return static_cast<uint8_t>(avg / 2);
  }
  return avg;
}

bool MotorDriveHit(void* attacker, void* defender, uint16_t waza, uint8_t* result) {
  if (!On() || attacker == nullptr || defender == nullptr || attacker == defender || Ab(defender) != kMotorDrive) return false;
  if (Ab(attacker) == kMoldBreaker || waza == 0 || waza == 0x176 || !Alive(defender)) return false;
  if (fightOutPokemonGetWazaZokuseiDataId(attacker) != 0xD || wazaDB_GetIryoku(waza) == 0) return false;
  Install();
  const bool raised = Raise(defender, 3, 1);
  fightFloor_SetAppointPokemonPtr(nullptr, defender);
  UserWork[0x160A4] = 0x11;
  UserWork[0x160A5] = 0;
  std::fprintf(stderr, "[gen4] ability: Motor Drive absorbed the Electric move, Speed %s\n", raised ? "+1" : "already at +6");
  fightSeq_JumpWazaSeq(Script(kMotorOff + (fightOutPokemon_GetPassPpdecFlag(attacker) ? 1 : 0)));
  *result = 1;
  return true;
}

MoldScope::MoldScope(void* attacker, void* defender) {
  if (!On() || attacker == nullptr || defender == nullptr || attacker == defender || Ab(attacker) != kMoldBreaker) return;
  def_ = defender;
  old_ = fightOutPokemonGetTokuseiDataId(defender);
  fightOutPokemonSetTokuseiDataId(defender, kInert);
}
MoldScope::~MoldScope() {
  if (def_ != nullptr) fightOutPokemonSetTokuseiDataId(def_, old_);
}

void MagicGuardHpDec() {
  if (!On()) return;
  if ((ServerStatusFlag & 0x20u) == 0) {
    void* a = fightTargetGetPtrAsNowFightType(0x11, 0);
    const auto* c = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
    if (a == nullptr || c == nullptr || Ab(a) != kMagicGuard || fightTargetGetPtrAsNowFightType(c[1], 0) != a) return;
    const uint16_t seq = wazaDB_GetSeqId(fightOutPokemon_GetUseWazaDataId(a));
    if (seq != 45 && seq != 48 && seq != 198) return;
    fightWaza_SetDamage(fightOutPokemon_GetFightWazaPtr(a), 0);
    std::fprintf(stderr, "[gen4] ability: Magic Guard: recoil turned into 0\n");
    return;
  }
  const auto* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
  void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
  void* target = cmd != nullptr ? fightTargetGetPtrAsNowFightType(cmd[1], 0) : nullptr;
  if (attacker == nullptr || target == nullptr) return;
  const uint16_t tab = Ab(target);
  if (tab != kMagicGuard && tab != kHeatproof) return;
  void* fw = fightOutPokemon_GetFightWazaPtr(attacker);
  if (tab == kMagicGuard) {
    fightWaza_SetDamage(fw, 0);
    std::fprintf(stderr, "[gen4] ability: Magic Guard: indirect damage turned into 0\n");
  } else if (fightOutPokemonIsJoutaiDataId(target, 6) == 1 && fightOutPokemonIsJoutaiDataId(target, 3) == 0 &&
             fightOutPokemonIsJoutaiDataId(target, 4) == 0 && fightOutPokemonIsJoutaiDataId(target, 0x1C) == 0 &&
             fightOutPokemonIsJoutaiDataId(target, 0x17) == 0 && fightOutPokemonIsJoutaiDataId(target, 0x18) == 0) {
    const int32_t half = static_cast<int32_t>(fightOutPokemonMaxHpWaruValue(target, 16));
    fightWaza_SetDamage(fw, half < 1 ? 1 : half);
    std::fprintf(stderr, "[gen4] ability: Heatproof: burn damage halved to %d\n", half);
  }
}

}

extern "C" {

void* fightSeqSelectDefensePokemonPtr(void* p, uint16_t waza, uint32_t type, void* select, uint8_t random, uint8_t special,
                                      int8_t rangeOverride) {
  void* result = PortXD_RealSelectDefense(p, waza, type, select, random, special, rangeOverride);
  if (!On() || special != 1 || result == nullptr || p == nullptr) return result;
  const uint8_t range = rangeOverride < 0 ? wazaDB_GetRangeId(waza) : static_cast<uint8_t>(rangeOverride);
  if (range == 0 && wazaDB_GetZokuseiDataId(waza) == 0xB && Ab(result) != kStormDrain &&
      fightFloorCheckFightOutPokemonPtrAryPokemonTokuseiDataId(nullptr, kStormDrain, 2, p) != 0) {
    void* t = fightFloorGetFightOutPokemonPtrAryPokemonTokuseiDataIdFirst(nullptr, kStormDrain, 1, 2, p);
    if (t != nullptr) {
      std::fprintf(stderr, "[gen4] ability: Storm Drain draws the Water move\n");
      result = t;
    }
  }
  return result;
}

uint8_t PortXD_Gen4NokoriHp(void* fop, uint16_t waru) {
  if (waru == 4 && On() && fop != nullptr && Ab(fop) == kGluttony) return fightOutPokemonIsNokoriHpFollowing(fop, 2);
  return fightOutPokemonIsNokoriHpFollowing(fop, waru);
}

uint32_t PortXD_Gen4WazaZokusei(void* fw) {
  const uint32_t t = fightWaza_GetZokusei(fw);
  if (!On() || fw == nullptr) return t;
  void* a = fightTargetGetPtrAsNowFightType(0x11, 0);
  if (a != nullptr && Ab(a) == kNormalize && fightOutPokemon_GetFightWazaPtr(a) == fw) return 0;
  return t;
}
uint8_t PortXD_Gen4WazaType(void* fop) {
  const uint8_t t = fightOutPokemonGetWazaZokuseiDataId(fop);
  return (On() && fop != nullptr && Ab(fop) == kNormalize) ? 0 : t;
}

static bool LeafGuardBlocks() {
  const uint8_t v = ServerWork[3];
  if (sStatusHookSeen || !On() || (v & 0x40) != 0 || (v & 0x3F) == 0 || (v & 0x3F) >= 10) return false;
  void* def = fightTargetGetPtrAsNowFightType(0x12, 0);
  return def != nullptr && Ab(def) == kLeafGuard && Weather() == 1 && Alive(def);
}
void PortXD_Gen4TsuikaIndirect(void) {
  if (LeafGuardBlocks()) {
    std::fprintf(stderr, "[gen4] ability: Leaf Guard in sun: status %u not inflicted\n", ServerWork[3] & 0x3F);
    ServerWork[3] = 0;
  }
  WS_TSUIKA_INDIRECT_ACT();
}
void PortXD_Gen4TsuikaDirect(void) {
  if (LeafGuardBlocks()) {
    std::fprintf(stderr, "[gen4] ability: Leaf Guard in sun: status %u not inflicted\n", ServerWork[3] & 0x3F);
    ServerWork[3] = 0;
  }
  WS_TSUIKA_DIRECT_ACT();
}

void PortXD_TokuseiCheckTurnExtra(void* p, uint16_t tokusei) {
  if (!On() || p == nullptr) return;
  sTurnHookSeen = true;
  if (tokusei < 77) return;
  Install();
  Tick();
  sInHook = true;
  TurnAbility(p);
  sInHook = false;
}

int PortXD_StatusBlockedByAbility(void* p, uint8_t effect) {
  if (!On() || p == nullptr) return 0;
  sStatusHookSeen = true;
  const uint8_t e = effect & 0x3F;
  void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
  if (Ab(p) == kLeafGuard && Weather() == 1 && e >= 1 && e < 10 && p != attacker) {
    std::fprintf(stderr, "[gen4] ability: Leaf Guard in sun: status %u not inflicted (the game's hook)\n", e);
    return 1;
  }
  return 0;
}

uint16_t PortXD_DamageDefenderTokusei(void* attacker, void* defender, uint16_t tokusei) {
  (void)defender;
  if (On() && attacker != nullptr && Ab(attacker) == kMoldBreaker) return kInert;
  return tokusei;
}

void PortXD_Gen4SideCheck(void) {
  if (!On()) {
    fightSeqSideJoutaiCheck();
    return;
  }
  Install();
  Tick();
  Turn t{0};
  fightFloorLoopValidFightOutPokemon(nullptr, EachMon, &t, 0);
  fightSeqSideJoutaiCheck();
  t.phase = 1;
  fightFloorLoopValidFightOutPokemon(nullptr, EachMon, &t, 0);
}

extern "C" uint32_t PortXD_Gen4SoubiKind(void* fop);
uint16_t PortXD_Gen4GetSoubi(void* fop) {
  const uint16_t item = fightOutPokemonGetSoubiItemSoubiDataId(fop);
  if (!On() || fop == nullptr) return item;
  const bool embargo = PortXD_Gen4SoubiKind(fop) == 0 && item != 0;
  const uint16_t ab = Ab(fop);
  if (ab == kKlutz) {
    static int told = 0;
    if (item != 0 && told++ < 3) std::fprintf(stderr, "[gen4] ability: Klutz: held item effect %u read as none\n", item);
    return 0;
  }
  if (ab == kUnburden) {
    Tick();
    Mon& m = M(fop);
    if (item != 0) {
      m.hadItem = true;
    } else if (m.hadItem && !m.lost) {
      m.lost = true;
      std::fprintf(stderr, "[gen4] ability: Unburden: the item is gone, Speed doubled\n");
    }
  }
  return embargo ? 0 : item;
}

int32_t fightSeqFightOutPokemonTokuseiCheckWazaHitBefore(void* attacker, void* defender) {
  xd::gen4::MoldScope mold(attacker, defender);
  return PortXD_RealWazaHitBefore(attacker, defender);
}

int32_t fightSeqFightOutPokemonTokuseiCheckWazaHitAfter(void* attacker, void* defender) {
  if (!On() || attacker == nullptr || defender == nullptr || attacker == defender) return PortXD_RealWazaHitAfter(attacker, defender);
  const uint16_t d = Ab(defender);
  if (d == kAngerPoint || d == kAftermath) {
    Install();
    void* fw = fightOutPokemon_GetFightWazaPtr(attacker);
    const bool landed = fightWazaIsHit(fw) == 1 && (fightOutPokemon_GetMyselfDamageAtkValue(defender) != 0 ||
                                                     fightOutPokemon_GetMyselfDamageSpeValue(defender) != 0);
    if (d == kAngerPoint && landed && fightWaza_GetCritical(fw) == 2 && Alive(defender) && Stage(defender, 1) < 12) {
      Raise(defender, 1, 6);
      fightFloor_SetAppointPokemonPtr(nullptr, defender);
      UserWork[0x160A4] = 0x0F;
      UserWork[0x160A5] = 0;
      std::fprintf(stderr, "[gen4] ability: Anger Point: Attack to +6 after a critical hit\n");
      fightSeq_CallWazaSeq(Script(kAtkOff));
      return 1;
    }
    if (d == kAftermath && landed && wazaDB_GetDageki(fightOutPokemon_GetUseWazaDataId(attacker)) != 0 && Alive(attacker) &&
        pokemon_GetHp(fightOutPokemonGetPokemonPtr(defender)) == 0) {
      int32_t damage = fightOutPokemonMaxHpWaruValue(attacker, 4);
      if (damage < 1) damage = 1;
      fightWaza_SetDamage(fw, damage);
      fightFloor_SetAppointPokemonPtr(nullptr, attacker);
      std::fprintf(stderr, "[gen4] ability: Aftermath: %d damage to the attacker (a quarter of its HP)\n", damage);
      fightSeq_CallWazaSeq(Script(kHurtOff + 0x40 * 3));
      return 1;
    }
  }
  return PortXD_RealWazaHitAfter(attacker, defender);
}

void fightSeqFightOutPokemonTokuseiCheckAppear(void* p) {
  PortXD_RealAppear(p);
  if (!On() || !Alive(p)) return;
  Install();
  Tick();
  Mon& m = M(p);
  m = Mon();
  m.fop = p;
  m.entry = sLastTurn;
  void* foes[8] = {};
  const uint16_t n = fightFloorGetFightOutPokemonPtrAry(nullptr, 1, 2, p, foes);
  switch (Ab(p)) {
  case kSnowWarning:
    if (fightFloorCheckWriteJoutaiDataId(nullptr, 0x53) == 2) {
      fightFloorWriteJoutaiDataId(nullptr, 0x53, 0);
      ServerWork[5] = 5;
      fightFloor_SetAppointPokemonPtr(nullptr, p);
      fightFloor_SetAttackPokemonPtr(nullptr, p);
      std::fprintf(stderr, "[gen4] ability: Snow Warning: hail\n");
      fightSeq_CallWazaSeq(Script(kHailOff));
    }
    break;
  case kDownload: {
    int def = 0, spd = 0;
    for (uint16_t i = 0; i < n && i < 8; i++) {
      void* pk = fightOutPokemonGetPokemonPtr(foes[i]);
      if (Alive(foes[i]) && pk != nullptr) {
        def += pokemon_GetPhyDef(pk);
        spd += pokemon_GetSpeDef(pk);
      }
    }
    const bool atk = def < spd;
    if (Raise(p, atk ? 1 : 4, 1)) {
      fightFloor_SetAppointPokemonPtr(nullptr, p);
      fightFloor_SetAttackPokemonPtr(nullptr, p);
      UserWork[0x160A4] = atk ? 0x0F : 0x12;
      UserWork[0x160A5] = 0;
      std::fprintf(stderr, "[gen4] ability: Download: foes' Def %d, Sp. Def %d -> %s +1\n", def, spd, atk ? "Attack" : "Sp. Atk");
      fightSeq_CallWazaSeq(Script(atk ? kAtkOff : kSpaOff));
    }
    break;
  }
  case kFrisk:
  case kForewarn: {
    bool any = false;
    for (uint16_t i = 0; i < n && i < 8; i++) {
      if (!Alive(foes[i])) continue;
      if (Ab(p) == kFrisk ? fightOutPokemonGetSoubiItemSoubiDataId(foes[i]) != 0 : true) any = true;
    }
    if (any) {
      std::fprintf(stderr, "[gen4] ability: %s\n", Ab(p) == kFrisk ? "Frisk found a held item" : "Forewarn warned of a move");
      fightFloor_SetAppointPokemonPtr(nullptr, p);
      fightFloor_SetAttackPokemonPtr(nullptr, p);
      fightSeq_CallWazaSeq(Script(kMsgOff + (Ab(p) == kFrisk ? 0x80 : 0xC0)));
    }
    break;
  }
  case kAnticipation: {
    bool danger = false;
    for (uint16_t i = 0; i < n && i < 8 && !danger; i++) {
      void* pk = fightOutPokemonGetPokemonPtr(foes[i]);
      if (!Alive(foes[i]) || pk == nullptr) continue;
      for (uint32_t s = 0; s < 4 && !danger; s++) {
        const uint16_t mv = pokemon_GetPokemonWazaDataId(pk, s);
        if (mv == 0 || wazaDB_GetIryoku(mv) == 0) continue;
        int mult = 4;
        const uint16_t t1 = fightOutPokemonGetZokuseiDataId(p, 0), t2 = fightOutPokemonGetZokuseiDataId(p, 1);
        for (uint16_t t : {t1, static_cast<uint16_t>(t2 != t1 ? t2 : 0xFF)}) {
          if (t == 0xFF || t == 9) continue;
          const uint16_t r = zokuseiGetWazaJoutai(wazaDB_GetZokuseiDataId(mv), t);
          if (r == 0x41) mult *= 2;
          else if (r == 0x42) mult /= 2;
          else if (r == 0x43) mult = 0;
        }
        if (mult > 4) danger = true;
      }
    }
    if (danger) {
      fightFloor_SetAppointPokemonPtr(nullptr, p);
      fightFloor_SetAttackPokemonPtr(nullptr, p);
      fightSeq_CallWazaSeq(Script(kMsgOff + 0x40));
      std::fprintf(stderr, "[gen4] ability: Anticipation: a foe has a super effective move, shuddered\n");
    }
    break;
  }
  default:
    break;
  }
}

void PortXD_Gen4CriticalCheck(void) {
  void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
  if (!On() || attacker == nullptr || Ab(attacker) != kSuperLuck) {
    WS_CRITICAL_CHECK();
    return;
  }
  void* target = fightTargetGetPtrAsNowFightType(0x12, 0);
  void* fw = fightOutPokemon_GetFightWazaPtr(attacker);
  const uint16_t move = fightOutPokemon_GetUseWazaDataId(attacker);
  const uint16_t item = fightOutPokemonGetSoubiItemSoubiDataId(attacker);
  const uint16_t species = figthOutPokemonGetPokemonDataId(attacker);
  const uint16_t seq = wazaDB_GetSeqId(move);
  uint32_t score = (fightOutPokemonIsJoutaiDataId(attacker, 0xF) == 1 ? 2u : 0u) + (seq == 0x2B) + (seq == 0x4B) + (seq == 200) +
                   (seq == 0xD1) + (item == 0x29) + (item == 0x3F && species == 0x71 ? 2u : 0u) + (item == 0x42 && species == 0x53 ? 2u : 0u);
  score += 1;
  const uint32_t top = static_cast<uint32_t>(fight_waza_critical_data_number - 1);
  if (score > top) score = top;
  const uint16_t t = Ab(target);
  if (t != 4 && t != 0x4B && fightFloor_GetFightKindDoCriticalAttackFlag(nullptr) == 1) {
    const uint16_t rnd = HSD_Rand();
    const uint8_t den = fightWazaCriticalDataBiosGetBunbo(fightWazaCriticalDataBiosGetPtr(static_cast<uint16_t>(score)));
    fightWaza_SetCritical(fw, (den != 0 && rnd % den == 0) ? 2 : 1);
    std::fprintf(stderr, "[gen4] ability: Super Luck: critical stage %u, 1 in %u, roll %u -> %s\n", score, den, rnd % (den ? den : 1),
                 (den != 0 && rnd % den == 0) ? "critical" : "no");
  } else {
    fightWaza_SetCritical(fw, 1);
  }
  fightSeq_NextWazaSeq(1);
}

uint8_t fightSeqAttackPokemonJoutaiCheck(void) {
  void* p = fightTargetGetPtrAsNowFightType(0x11, 0);
  const bool flinch = On() && p != nullptr && Ab(p) == kSteadfast && fightOutPokemonIsJoutaiDataId(p, 0x11) == 1;
  const uint8_t result = PortXD_RealAttackJoutaiCheck();
  if (flinch && result != 0 && fightOutPokemonIsJoutaiDataId(p, 0x11) == 0 && Raise(p, 3, 1)) {
    fightFloor_SetAppointPokemonPtr(nullptr, p);
    fightFloor_SetAttackPokemonPtr(nullptr, p);
    UserWork[0x160A4] = 0x11;
    UserWork[0x160A5] = 0;
    fightSeq_CallWazaSeq(PortXD_GameAddress(kKasoku));
    std::fprintf(stderr, "[gen4] ability: Steadfast: flinched, Speed +1\n");
  }
  return result;
}

}
