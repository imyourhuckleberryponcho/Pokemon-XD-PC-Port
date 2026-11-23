#include "xd_forms.h"

#include "online_battle.h"
#include "xd_gen57.h"
#include "xd_challenges.h"

#include <dolphin/pad.h>
#include <imgui.h>
#include <i18n.h>
#include "ui_kit2.h"
#include <game/winwork.h>
#include <game/menuwazastatus.h>
#include <vector>
#include <string>

#include <atomic>
#include <mutex>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t figthOutPokemonGetPokemonDataId(void* fop);
uint16_t fightOutPokemonGetSoubiItemDataId(void* fop);
uint8_t fightOutPokemonIsUseHensinBuff(void* fop);
uint8_t fightOutPokemonUseHensinBuff(void* fop);
void pokemonSetStatus(void* ptr, int32_t a, int32_t id, int32_t b, uint32_t value);
uint8_t pokemon_GetLevel(void* pokemon);
uint16_t pokemon_GetHp(void* pokemon);
uint16_t pokemon_GetMaxHp(void* pokemon);
void pokemon_SetHp(void* pokemon, uint16_t hp);
uint16_t fightOutPokemonGetTokuseiDataId(void* fop);
void PortXD_PokemonSetLevelStats(void* poke, uint8_t level);
uint8_t pokemonDB_GetZokuseiDataId(uint16_t id, uint16_t idx);
uint16_t pokemonGetTokuseiDataId(void* pokemon);
void fightOutPokemonSetZokuseiDataId(void* fop, uint8_t idx, uint16_t zokusei);
void fightOutPokemonSetTokuseiDataId(void* fop, uint16_t id);
uint8_t fightOutPokemonCheckFightOut(void* fop);
void* fightFloorGetFightOutBanmeToValidFightOutPokemon(void* floor, uint8_t banme);
uint8_t fightTargetIsHostSide(void* fop, uint16_t type);
uint8_t fightOutPokemonIsGcHeroFightOutPokemon(void* fop);
int32_t fightFloor_GetFightType(void* floor);
void* fightOutPokemon_GetSequencePtr(void* fop);
void fightOutPokemon_SetSequencePtr(void* fop, void* seq);
void* fightOutPokemonCreateSequence(void* fop, uint16_t tenki, int8_t rare);
void* Sequence_LoadWaza(void* seq, uint16_t id, uint16_t kind, uint8_t flag);
void Sequence_Free(void* seq);
void battleGridReplacePokemon(void* oldSeq, void* newSeq);
void battleGridUpdate(void);
void fightSeqFightOutPokemonEffect(uint16_t id, void* fop, uint8_t wait, uint8_t openMenu);
void fightSeqFightOutPokemonTokuseiCheckAppear(void* fop);
void fightMsgctrlSetValue(int32_t id, uintptr_t value);
uint8_t fightSeqOpenMsg(uint32_t msg);
void fightMainWaitFrame(uint32_t frames);
void fightMenuCloseMsg(void);
uint32_t fightFloorGetNowTenkouDataId(void* floor, uint8_t checkTokusei);
uint16_t fightFloor_GetTurnCount(void* floor);
uint16_t fightGetFightEncountDataId(void);
uint16_t fightOutPokemonDoItemSoubi(void* fop, uint16_t item, uint8_t mode, uint8_t update);
extern uint8_t ServerWork[8];
uint8_t menuIsCheck(uint32_t id);
void* itemDataBiosGetPtr(uint16_t item);
uint32_t itemDataBiosGetName(void* data);
const uint16_t* GSmsgGetGSchar(uint32_t id);
const char* pokemonDB_GetName(uint16_t id);
void* menuItemBiosGetPtr(int32_t menuID, uint32_t index);
float PortXD_GameTextWidth(const char* text, float scale);
void PortXD_GameText(float x, float y, const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale);
void PortVideo_SetFrameDrawer(void (*draw)());
void PortMenu_PictureRect(float* x, float* y, float* scale);
void PortInput_SetPadObserver(void (*observer)(const PADStatus* status));
}

namespace {
constexpr uint32_t kBank = 0x7D0u << 20;
constexpr uint32_t kTextReact = 0xBF00, kTextMega = 0xBF01, kTextPrimal = 0xBF02;
constexpr uint32_t kTextZenOn = 0xBF10, kTextZenOff = 0xBF11, kTextBlade = 0xBF12, kTextShield = 0xBF13, kTextSchoolOn = 0xBF14, kTextSchoolOff = 0xBF15,
                   kTextShellOff = 0xBF16, kTextShellOn = 0xBF17, kTextBusted = 0xBF18, kTextPirouette = 0xBF19, kTextAria = 0xBF1A, kTextAsh = 0xBF1B, kTextComplete = 0xBF20;
void* sFormed[16];
int sFormedCount = 0;
bool IsFormed(void* fop) { for (int i = 0; i < sFormedCount; i++) if (sFormed[i] == fop) return true; return false; }

bool sArmed = false;
bool sFriendApplying = false;
bool sUsed[2] = {false, false};
void* sPending[4] = {nullptr, nullptr, nullptr, nullptr};
bool sChoiceMega[4] = {false, false, false, false};
void* sSeen[16];
int sLastTurn = -1;
int sSeenCount = 0;

bool TestArm() {
  static int on = -1;
  if (on < 0) { const char* e = std::getenv("XD_MEGA"); on = e && e[0] == '1'; }
  return on == 1;
}

bool BossBattle() {
  static int forced = -1;
  if (forced < 0) { const char* e = std::getenv("XD_MEGA_BOSS"); forced = e != nullptr && e[0] == '1'; }
  if (forced == 1) return true;
  static const uint16_t kBoss[] = {224, 235, 268, 278, 289, 306, 326, 327, 329, 330, 336, 347, 349, 367, 375, 393, 395, 396, 398, 401, 402, 441};
  const uint16_t id = fightGetFightEncountDataId();
  for (uint16_t b : kBoss) if (b == id) return true;
  return false;
}
void* sGiven[8];
int sGivenCount = 0;

int SideOf(void* fop) { return fightTargetIsHostSide(fop, static_cast<uint16_t>(fightFloor_GetFightType(nullptr))) ? 0 : 1; }

int Slot(void* fop) {
  for (uint8_t b = 0; b < 4; b++) {
    if (fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b) == fop) return b;
  }
  return -1;
}

const xd::forms::Form* MegaFormFor(void* fop) {
  if (fop == nullptr || !fightOutPokemonCheckFightOut(fop)) return nullptr;
  if (fightOutPokemonIsUseHensinBuff(fop)) return nullptr;
  const uint32_t item = fightOutPokemonGetSoubiItemDataId(fop);
  if (item == 0) return nullptr;
  const uint32_t base = xd::forms::BaseOf(figthOutPokemonGetPokemonDataId(fop));
  for (const auto& f : xd::forms::All()) {
    if (f.kind == xd::forms::Kind::Mega && f.stone == item && f.base == base) return &f;
  }
  return nullptr;
}

void GiveBossStone(void* fop) {
  if (fightOutPokemonGetSoubiItemDataId(fop) != 0 || !BossBattle()) return;
  for (int i = 0; i < sGivenCount; i++) if (sGiven[i] == fop) return;
  if (sGivenCount < 8) sGiven[sGivenCount++] = fop;
  const uint32_t base = xd::forms::BaseOf(figthOutPokemonGetPokemonDataId(fop));
  for (const auto& f : xd::forms::All()) {
    if (f.kind == xd::forms::Kind::Mega && f.base == base && f.stone != 0) {
      fightOutPokemonDoItemSoubi(fop, static_cast<uint16_t>(f.stone), 1, 0);
      std::fprintf(stderr, "[forms] boss battle %u: Pokemon %u holds stone item %u\n", static_cast<unsigned>(fightGetFightEncountDataId()), base, f.stone);
      return;
    }
  }
}

void Say(uint32_t id, void* mon, const uint16_t* word = nullptr) {
  fightMsgctrlSetValue(0x10, reinterpret_cast<uintptr_t>(mon));
  fightMsgctrlSetValue(0xE, reinterpret_cast<uintptr_t>(word));
  fightSeqOpenMsg(kBank | id);
  if (ServerWork[7] == 1) {
    fightMainWaitFrame(0x60);
    fightMenuCloseMsg();
    ServerWork[7] = 0;
  }
}

bool SwapTo(void* fop, const xd::forms::Form& form, uint32_t messageId, bool runAppear = true) {
  if (!IsFormed(fop)) {
    if (!fightOutPokemonUseHensinBuff(fop)) return false;
    if (sFormedCount < 16) sFormed[sFormedCount++] = fop;
  }
  void* p = fightOutPokemonGetPokemonPtr(fop);
  const uint16_t hpBefore = pokemon_GetHp(p);
  pokemonSetStatus(p, 0, 0x6E, 0, form.row);
  PortXD_PokemonSetLevelStats(p, pokemon_GetLevel(p));
  pokemon_SetHp(p, std::min<uint16_t>(hpBefore, pokemon_GetMaxHp(p)));
  for (uint8_t i = 0; i < 2; i++) fightOutPokemonSetZokuseiDataId(fop, i, pokemonDB_GetZokuseiDataId(static_cast<uint16_t>(form.row), i));
  if (form.kind != xd::forms::Kind::Battle) fightOutPokemonSetTokuseiDataId(fop, pokemonGetTokuseiDataId(p));
  void* oldSeq = fightOutPokemon_GetSequencePtr(fop);
  const uint16_t tenki = static_cast<uint16_t>(fightFloorGetNowTenkouDataId(nullptr, 1));
  void* newSeq = fightOutPokemonCreateSequence(fop, tenki, -1);
  if (oldSeq != nullptr && newSeq != nullptr) {
    Sequence_LoadWaza(oldSeq, 0x81, 4, 0);
    Sequence_LoadWaza(newSeq, 0x83, 4, 0);
    fightSeqFightOutPokemonEffect(0x81, fop, 1, 0);
    battleGridReplacePokemon(oldSeq, newSeq);
    battleGridUpdate();
    fightOutPokemon_SetSequencePtr(fop, newSeq);
    fightSeqFightOutPokemonEffect(0x83, fop, 0, 0);
    Sequence_Free(oldSeq);
  }
  Say(messageId, fop, GSmsgGetGSchar(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pokemonDB_GetName(static_cast<uint16_t>(form.row))))));
  if (runAppear) fightSeqFightOutPokemonTokuseiCheckAppear(fop);
  std::fprintf(stderr, "[forms] %s: species row %u -> %u, ability %u\n", form.key.c_str(), static_cast<unsigned>(form.base), form.row,
               static_cast<unsigned>(pokemonGetTokuseiDataId(p)));
  return true;
}

void Mark(void* fop, bool mine) {
  const int s = Slot(fop);
  if (s < 0) return;
  sPending[s] = fop;
  sChoiceMega[s] = mine;
}
}

extern "C" void* PortXD_RealActionWaza(void* fop, int32_t a, int32_t kind, int32_t c, void* data, int32_t waza, uint16_t target, int8_t banme, uint8_t autoMake);
extern "C" void* fightOutPokemonCreateFightActionAttackWaza(void* fop, int32_t a, int32_t kind, int32_t c, void* data, int32_t waza, uint16_t target,
                                                            int8_t banme, uint8_t autoMake) {
  void* r = PortXD_RealActionWaza(fop, a, kind, c, data, waza, target, banme, autoMake);
  if (xd::forms::MegaRule() && !sFriendApplying && MegaFormFor(fop) != nullptr && !sUsed[SideOf(fop)]) {
    if (sArmed || TestArm()) Mark(fop, true);
    sArmed = false;
  }
  return r;
}

namespace xd::forms {

bool MegaRule() { return Loaded() && xd::challenges::Active("mega_evolution"); }

void BattleEnded() {
  sArmed = false;
  sUsed[0] = sUsed[1] = false;
  for (int i = 0; i < 4; i++) { sPending[i] = nullptr; sChoiceMega[i] = false; }
  sSeenCount = 0;
  sGivenCount = 0;
  sLastTurn = -1;
  sFormedCount = 0;
}

void SetFriendApplying(bool on) { sFriendApplying = on; }

void FriendMega(void* fop) {
  if (MegaRule() && MegaFormFor(fop) != nullptr && !sUsed[SideOf(fop)]) Mark(fop, false);
}

bool TakeChoiceMega(void* fop) {
  const int s = Slot(fop);
  return s >= 0 && sPending[s] == fop && sChoiceMega[s];
}

void TurnStart() {
  if (!MegaRule()) return;
  const int turn = fightFloor_GetTurnCount(nullptr);
  if (turn == sLastTurn) return;
  sLastTurn = turn;
  if (!xd::online::Active()) {
    for (uint8_t b = 0; b < 4; b++) {
      void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
      if (fop == nullptr || sPending[b] != nullptr) continue;
      if (fightOutPokemonIsGcHeroFightOutPokemon(fop) || sUsed[SideOf(fop)]) continue;
      GiveBossStone(fop);
      if (MegaFormFor(fop) != nullptr) { sPending[b] = fop; sChoiceMega[b] = false; }
    }
  }
  for (uint8_t b = 0; b < 4; b++) {
    void* fop = sPending[b];
    if (fop == nullptr) continue;
    sPending[b] = nullptr;
    sChoiceMega[b] = false;
    const Form* f = MegaFormFor(fop);
    if (f == nullptr || sUsed[SideOf(fop)]) continue;
    sUsed[SideOf(fop)] = true;
    void* idata = itemDataBiosGetPtr(static_cast<uint16_t>(f->stone));
    Say(kTextReact, fop, idata != nullptr ? GSmsgGetGSchar(itemDataBiosGetName(idata)) : nullptr);
    SwapTo(fop, *f, kTextMega);
  }
}

void EntryPrimal(void* fop) {
  if (!Loaded() || fop == nullptr) return;
  if (fightOutPokemonIsUseHensinBuff(fop)) return;
  const uint32_t item = fightOutPokemonGetSoubiItemDataId(fop);
  if (item == 0) return;
  const uint32_t base = BaseOf(figthOutPokemonGetPokemonDataId(fop));
  for (const auto& f : All()) {
    if (f.kind == Kind::Primal && f.stone == item && f.base == base) {
      SwapTo(fop, f, kTextPrimal, false);
      return;
    }
  }
}

void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts) {
  if (!xd::gen57::Loaded()) return;
  texts.emplace_back((0x7D0u << 20) | kTextReact, engine::i18n::Tr("{10}'s {0E}{00}is reacting!"));
  texts.emplace_back((0x7D0u << 20) | kTextMega, engine::i18n::Tr("{10} has Mega Evolved{00}into {0E}!"));
  texts.emplace_back((0x7D0u << 20) | kTextPrimal, engine::i18n::Tr("{10} went back to its primal form!"));
  texts.emplace_back((0x7D0u << 20) | kTextZenOn, engine::i18n::Tr("{10} entered Zen Mode!"));
  texts.emplace_back((0x7D0u << 20) | kTextZenOff, engine::i18n::Tr("{10} left Zen Mode!"));
  texts.emplace_back((0x7D0u << 20) | kTextBlade, engine::i18n::Tr("{10} changed to Blade Forme!"));
  texts.emplace_back((0x7D0u << 20) | kTextShield, engine::i18n::Tr("{10} changed to Shield Forme!"));
  texts.emplace_back((0x7D0u << 20) | kTextSchoolOn, engine::i18n::Tr("{10} formed a school!"));
  texts.emplace_back((0x7D0u << 20) | kTextSchoolOff, engine::i18n::Tr("{10} stopped schooling!"));
  texts.emplace_back((0x7D0u << 20) | kTextShellOff, engine::i18n::Tr("{10}'s shell broke!"));
  texts.emplace_back((0x7D0u << 20) | kTextShellOn, engine::i18n::Tr("{10} is covered by its shell again!"));
  texts.emplace_back((0x7D0u << 20) | kTextBusted, engine::i18n::Tr("{10}'s disguise was busted!"));
  texts.emplace_back((0x7D0u << 20) | kTextPirouette, engine::i18n::Tr("{10} changed to Pirouette Forme!"));
  texts.emplace_back((0x7D0u << 20) | kTextAria, engine::i18n::Tr("{10} changed to Aria Forme!"));
  texts.emplace_back((0x7D0u << 20) | kTextAsh, engine::i18n::Tr("{10} became Ash-Greninja!"));
  texts.emplace_back((0x7D0u << 20) | kTextComplete, engine::i18n::Tr("{10} transformed into its Complete Forme!"));
  texts.emplace_back((0x7D0u << 20) | 0xBF1C, engine::i18n::Tr("The Pokemon now has its hidden ability!"));
  texts.emplace_back((0x7D0u << 20) | 0xBF1D, engine::i18n::Tr("This Pokemon has no hidden ability."));
  texts.emplace_back((0x7D0u << 20) | 0xBF1E, engine::i18n::Tr("This Pokemon already has its hidden ability."));
  texts.emplace_back((0x7D0u << 20) | 0xBF1F, engine::i18n::Tr("A Shadow Pokemon cannot use this yet."));
}

}

namespace {
const xd::forms::Form* AltForm(uint32_t base, const char* tag) {
  const std::string want = std::string("(") + tag + ")";
  for (const auto& f : xd::forms::All()) {
    if (f.kind == xd::forms::Kind::Battle && f.base == base && f.key.find(want) != std::string::npos) return &f;
  }
  return nullptr;
}

bool ChangeTo(void* fop, uint32_t row, uint32_t message) {
  if (figthOutPokemonGetPokemonDataId(fop) == row) return false;
  xd::forms::Form tmp;
  tmp.row = row;
  tmp.base = xd::forms::BaseOf(row);
  tmp.kind = xd::forms::Kind::Battle;
  tmp.key = "battle form";
  return SwapTo(fop, tmp, message, false);
}

bool Changeable(void* fop, const xd::forms::Form* alt, uint32_t base) {
  if (alt == nullptr) return false;
  const uint32_t sp = figthOutPokemonGetPokemonDataId(fop);
  return sp == base || sp == alt->row;
}

void Evaluate(void* fop, bool afterHit) {
  if (!xd::forms::Loaded() || fop == nullptr || !fightOutPokemonCheckFightOut(fop)) return;
  void* p = fightOutPokemonGetPokemonPtr(fop);
  if (p == nullptr) return;
  const uint32_t sp = figthOutPokemonGetPokemonDataId(fop);
  const uint32_t base = xd::forms::BaseOf(sp);
  const uint16_t ability = fightOutPokemonGetTokuseiDataId(fop);
  const int hp = pokemon_GetHp(p), mx = pokemon_GetMaxHp(p);
  if (mx <= 0 || hp <= 0) return;
  const char* tag = nullptr;
  bool want = false;
  uint32_t on = 0, off = 0;
  switch (ability) {
  case 161: tag = "Zen"; want = hp * 2 <= mx; on = kTextZenOn; off = kTextZenOff; if (afterHit) return; break;
  case 208: tag = "School"; want = pokemon_GetLevel(p) >= 20 && hp * 4 > mx; on = kTextSchoolOn; off = kTextSchoolOff; if (afterHit) return; break;
  case 211: tag = "Complete"; want = hp * 2 <= mx; on = kTextComplete; off = 0; if (afterHit || !want) return; break;
  case 197: tag = "Core"; want = hp * 2 <= mx; on = kTextShellOff; off = kTextShellOn; break;
  default: return;
  }
  const xd::forms::Form* alt = AltForm(base, tag);
  if (!Changeable(fop, alt, base)) return;
  const bool is = sp == alt->row;
  if (want && !is) ChangeTo(fop, alt->row, on);
  else if (!want && is) ChangeTo(fop, base, off);
}
}

namespace xd::forms {

void BattleFormEntry(void* fop) { Evaluate(fop, false); }

void BattleFormTurnEnd() {
  if (!Loaded()) return;
  for (uint8_t b = 0; b < 4; b++) Evaluate(fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b), false);
}

void BattleFormMove(void* attacker, uint16_t move, bool kingsShield, bool damaging) {
  (void)move;
  if (!Loaded() || attacker == nullptr || fightOutPokemonGetTokuseiDataId(attacker) != 176) return;
  const uint32_t base = BaseOf(figthOutPokemonGetPokemonDataId(attacker));
  const Form* blade = AltForm(base, "Blade");
  if (!Changeable(attacker, blade, base)) return;
  if (kingsShield) ChangeTo(attacker, base, kTextShield);
  else if (damaging) ChangeTo(attacker, blade->row, kTextBlade);
}

void BattleFormHit(void* attacker, void* defender, uint16_t move, bool ko) {
  if (!Loaded()) return;
  if (defender != nullptr && !ko) Evaluate(defender, true);
  if (attacker == nullptr || !fightOutPokemonCheckFightOut(attacker)) return;
  const uint16_t ability = fightOutPokemonGetTokuseiDataId(attacker);
  const uint32_t base = BaseOf(figthOutPokemonGetPokemonDataId(attacker));
  if (ko && ability == 210) {
    const Form* ash = AltForm(base, "Ash");
    if (Changeable(attacker, ash, base)) ChangeTo(attacker, ash->row, kTextAsh);
  }
  if (move == 567) {
    const Form* pir = AltForm(base, "Pirouette");
    if (Changeable(attacker, pir, base)) {
      if (figthOutPokemonGetPokemonDataId(attacker) == base) ChangeTo(attacker, pir->row, kTextPirouette);
      else ChangeTo(attacker, base, kTextAria);
    }
  }
}

bool DisguiseIntact(void* defender) {
  if (!Loaded() || defender == nullptr || fightOutPokemonGetTokuseiDataId(defender) != 209) return false;
  const uint32_t sp = figthOutPokemonGetPokemonDataId(defender);
  return AltForm(BaseOf(sp), "Busted") != nullptr && sp == BaseOf(sp);
}

void DisguiseBust(void* defender) {
  const uint32_t sp = figthOutPokemonGetPokemonDataId(defender);
  const Form* busted = AltForm(BaseOf(sp), "Busted");
  if (busted != nullptr) ChangeTo(defender, busted->row, kTextBusted);
}

}

namespace {
constexpr int kMenuMove = 0x39, kFirstItem = 249, kRowShift = 34;
struct ItemView {
  uint8_t flags, flags2, unk2, unk3;
  uint16_t image;
  int16_t x, y, w, h, spriteId;
  uint32_t msg, pad;
  void* hook;
  int16_t next, unk;
};
static_assert(sizeof(ItemView) == 0x28, "menu item layout");
std::atomic<bool> sShifted{false};
std::atomic<bool> sGrown{false};
std::atomic<int> sTick{0}, sSpriteTick{0};
std::vector<std::pair<int, int16_t>> sSavedY;
std::mutex sRowLock;
std::string sRowStone;
void* sRowFop = nullptr;

std::string Utf8Of(const uint16_t* t) {
  std::string out;
  for (int n = 0; t != nullptr && n < 40; n++) {
    const uint16_t c = t[n];
    if (c == 0 || c >= 0xFF00 || c < 0x20) break;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) out += static_cast<char>(0xC0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3F));
    else out += static_cast<char>(0xE0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
  }
  return out;
}

const xd::forms::Form* StoneForm(void* fop) {
  if (fop == nullptr) return nullptr;
  const uint32_t item = fightOutPokemonGetSoubiItemDataId(fop);
  if (item == 0) return nullptr;
  const uint32_t sp = figthOutPokemonGetPokemonDataId(fop);
  const uint32_t base = xd::forms::BaseOf(sp);
  for (const auto& f : xd::forms::All()) {
    if (f.kind == xd::forms::Kind::Mega && f.stone == item && (f.base == base || f.row == sp)) return &f;
  }
  return nullptr;
}

void ShiftItems(bool on) {
  if (on) {
    sSavedY.clear();
    int guard = 0;
    for (int i = kFirstItem; i >= 0 && guard++ < 200;) {
      auto* it = static_cast<ItemView*>(menuItemBiosGetPtr(kMenuMove, static_cast<uint32_t>(i)));
      if (it == nullptr) break;
      sSavedY.emplace_back(i, it->y);
      it->y = static_cast<int16_t>(it->y - kRowShift);
      i = it->next;
    }
  } else {
    for (const auto& s : sSavedY) {
      auto* it = static_cast<ItemView*>(menuItemBiosGetPtr(kMenuMove, static_cast<uint32_t>(s.first)));
      if (it != nullptr) it->y = s.second;
    }
    sSavedY.clear();
  }
}

void RowCleanup() {
  if (sShifted && !menuIsCheck(kMenuMove)) {
    ShiftItems(false);
    sShifted = false;
    std::lock_guard<std::mutex> l(sRowLock);
    sRowFop = nullptr;
  }
  if (sGrown && !sShifted && sTick - sSpriteTick > 3) sGrown = false;
}

int RowState() {
  if (!sShifted) return 0;
  return sUsed[0] ? 3 : (sArmed ? 2 : 1);
}
}

extern "C" int32_t PortXD_RealMenuFightOpenWaza(void* trainer, void* status, uint32_t b, int32_t c);
extern "C" int32_t menuFightOpenWaza(void* trainer, void* status, uint32_t b, int32_t c) {
  if (xd::forms::MegaRule() && status != nullptr && !sShifted) {
    void* fop = static_cast<MenuWazaStatus*>(status)->fightOutPokemon;
    const xd::forms::Form* f = fop != nullptr && fightOutPokemonIsGcHeroFightOutPokemon(fop) ? StoneForm(fop) : nullptr;
    if (f != nullptr) {
      std::string stone;
      void* d = itemDataBiosGetPtr(static_cast<uint16_t>(f->stone));
      if (d != nullptr) stone = Utf8Of(GSmsgGetGSchar(itemDataBiosGetName(d)));
      { std::lock_guard<std::mutex> l(sRowLock); sRowStone = stone; sRowFop = fop; }
      ShiftItems(true);
      sShifted = true;
      sGrown = true;
      sSpriteTick = sTick.load();
    }
  }
  return PortXD_RealMenuFightOpenWaza(trainer, status, b, c);
}

extern "C" bool PortXD_MegaRowSprite(void* window, void* sprite, void (*draw)(void*, void*)) {
  if (!sGrown) return false;
  auto* w = static_cast<WinWorkLayout*>(window);
  if (w->menuID != kMenuMove) return false;
  sSpriteTick = sTick.load();
  auto* sp = static_cast<SpriteWorkLayout*>(sprite);
  const int id = sp->id;
  if (id != 252 && id != 253 && id != 254 && id != 258 && id != 261) return false;
  if (sp->h >= 0) return false;
  const SpriteWorkLayout keep = *sp;
  auto put = [&](int x, int y, int ww, int hh, int u, int v, int uw, int uh) {
    sp->x = static_cast<s16>(x); sp->y = static_cast<s16>(y); sp->w = static_cast<s16>(ww); sp->h = static_cast<s16>(hh);
    sp->u = static_cast<s16>(u); sp->v = static_cast<s16>(v); sp->uw = static_cast<s16>(uw); sp->uh = static_cast<s16>(uh);
    draw(window, sprite);
    sp->x = keep.x; sp->y = keep.y; sp->w = keep.w; sp->h = keep.h; sp->u = keep.u; sp->v = keep.v; sp->uw = keep.uw; sp->uh = keep.uh;
  };
  const bool corner = id == 258 || id == 261;
  const int bottomY = keep.y + kRowShift, bandY = keep.y + 12, bandH = kRowShift - 12;
  if (corner) put(keep.x, keep.y, keep.w, keep.h, 2, 15, 32, 8);
  else draw(window, sprite);
  if (id == 254) put(48, bandY, 544, bandH, 35, 15, 8, 8);
  if (corner) put(keep.x, bandY, keep.w, bandH, 2, 15, 32, 8);
  if (id == 253) put(keep.x, bottomY, keep.w, keep.h, 35, 2, 8, 12);
  else put(keep.x, bottomY, keep.w, keep.h, keep.u, keep.v, keep.uw, keep.uh);
  return true;
}

namespace {
void DrawRow() {
  const int state = RowState();
  if (state == 0) return;
  std::string stone;
  { std::lock_guard<std::mutex> l(sRowLock); stone = sRowStone; }
  float x0, y0, s;
  PortMenu_PictureRect(&x0, &y0, &s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  auto P = [&](float x, float y) { return ImVec2(x0 + x * s, y0 + y * s); };
  const bool used = state == 3, chosen = state == 2;
  auto col = [&](int r, int g, int b) { return IM_COL32(r, g, b, 255); };
  const float cy = 417.5f;
  const ImU32 text = used ? col(96, 96, 96) : (chosen ? col(255, 255, 255) : col(150, 150, 150));
  const ImU32 tint = used ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE;
  if (chosen) {
    xd::kit2::DrawPiece(dl, P(39, cy - 13), P(240, cy + 13), xd::kit2::Piece{0x17BF, 1, 1, 148, 38}, false, false, IM_COL32(170, 170, 170, 255));
  }
  xd::kit2::DrawPiece(dl, P(46, cy - 8.f), P(46 + 34.f, cy + 8.f), xd::kit2::Piece{0x1867, 175, 35, 34, 16}, false, false, tint);
  const float scale = 0.88f;
  const char* label = engine::i18n::Tr("Mega Evolution");
  const uint8_t tg = used ? 96 : (chosen ? 255 : 200);
  PortXD_GameText(88.f, cy - 10.f, label, tg, tg, tg, 255, scale);
  const float sw = PortXD_GameTextWidth(stone.c_str(), scale);
  const float rightU = 560.f;
  PortXD_GameText(rightU - sw, cy - 10.f, stone.c_str(), used ? 96 : 235, used ? 96 : 235, used ? 96 : 235, 255, scale);
  xd::kit2::DrawPiece(dl, P(rightU - sw - 24.f, cy - 9.f), P(rightU - sw - 4.f, cy + 9.f),
                      xd::kit2::Piece{0x216F, 56, 36, 20, 18}, false, false, tint);
}
}

static void PadSeen(const PADStatus* status) {
  static bool was = false;
  sTick++;
  RowCleanup();
  bool down = (status[0].button & PAD_TRIGGER_Z) != 0 && status[0].err == PAD_ERR_NONE;
  static int openFrames = 0, zAt = -1;
  if (zAt < 0) { const char* e = std::getenv("XD_MEGA_Z"); zAt = e != nullptr ? std::atoi(e) : 0; }
  openFrames = menuIsCheck(0x39) ? openFrames + 1 : 0;
  if (zAt > 0 && openFrames >= zAt && openFrames < zAt + 3) down = true;
  const bool edge = down && !was;
  was = down;
  if (!edge || sFriendApplying || !xd::forms::MegaRule()) return;
  if (!menuIsCheck(0x39)) return;
  sArmed = !sArmed;
}
namespace {
struct Register {
  Register() { PortInput_SetPadObserver(&PadSeen); PortVideo_SetFrameDrawer(&DrawRow); }
} sRegister;
}
