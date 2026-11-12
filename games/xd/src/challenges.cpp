#include "xd_battles_won.h"
#include "xd_challenges.h"
#include "xd_playthrough.h"

#include <game/winwork.h>

#include <dolphin/pad.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>

extern "C" {
u16 menuPocket2Open(s32 mode, s32* slot, void* trainer);
void* windowSearchID(s32 id);
void fightOutPokemonKizetuEffect(void* fightOutPokemon, u8 mode);
u8 fightOutPokemonIsGcHeroFightOutPokemon(void* fightOutPokemon);
u8 fightFloor_GetFightKindBackSaveDataFlag(void* floor);
void* fightOutPokemon_GetMotoFightPokemonPtr(void* fightOutPokemon);
void* fightPokemon_GetMotoPokemonPtr(void* fightPokemon);
void* fightFloorGetFightOutPokemonPtrToFightTrainerPtr(void* floor, void* fightOutPokemon);
s16 fightTrainerGetFightOutPokemonToTemotiBanme(void* trainer, void* fightOutPokemon);
u8 fightIsFight(void);
u32 fightGetFightResultId(void);
bool GSfloorIsFloorPushed(void);
uintptr_t heroGetStatus(void* hero, u32 selector, u16 index);
u32 pokemonBiosGetPokemonDataId(void* pokemon);
s32 pcboxAddPokemon(void* pcbox, void* pokemon, s8 box);
void pokemonInit(void* pokemon);
u32 pokemon_GetRnd(void* pokemon);
u32 pokemon_GetCatchTrainerRnd(void* pokemon);
u8 pokemonIsDarkPokemon(void* pokemon);
s8 pcboxGetPokemonBoxNbEmptySlot(void* pcbox, s8 box);
s32 GSfloorGetCurrent(void);
extern void* floordata;
void fightPokemon_SetHokakuFlag(void* fightPokemon, u8 value);
void fightOutPokemonHokakuEffect(void* fightOutPokemon, u8 shakes, u8 ball, u8 mode, u8* work);
u32 fightFloor_GetEncountFloorId(void* floor);
u32 floorGetPlaceName(u32 floorID);
u16* GSmsgGetGSchar(u32 key);
uint32_t PortVI_GetFlipCount(void);
extern int (*menuPcBoxActionFilter)(u32 msg, int box, int place, void* under, void* held);
int menuPcBoxPeekCursor(int* box, int* place, int* state, int* holding, int* quick, void** under);
int menuPcBoxPeekMore(int* stateNext, void** held);
s32 windowGetActiveID(void);
extern unsigned char xdExpLevelLimit;
u32 GSflagGet(u32 id);
extern u32* flag_data_number;
}

namespace xd::challenges {
namespace {

constexpr s32 kPocketMenu = 0x2C;
constexpr s32 kActionMenu = 0x2D;
constexpr int kItemPocket = 0, kBerryPocket = 3;

std::vector<std::string> sActive;
bool sBattleBag = false;
constexpr u32 kUseMessage = 0x4292;

struct ActionMenuItemView {
  u32 msg;
  void (*func)();
  u16 flags;
  u16 pad;
};
struct ActionMenuParamView {
  const ActionMenuItemView* menu;
  int n;
};

bool CursorOnUse(const WinWorkLayout* w) {
  const auto* param = reinterpret_cast<const ActionMenuParamView*>(w->params[0]);
  if (param == nullptr || param->menu == nullptr || w->cursor < 0 || w->cursor >= param->n || param->n > 4) return false;
  return param->menu[w->cursor].msg == kUseMessage;
}
bool sLog = false;

constexpr s8 kGraveyardBox = 7;
constexpr size_t kPokemonBytes = 0xC4;
constexpr int kCalmFrames = 30;
constexpr int kPartySweepFrames = 300;
struct Faint {
  u32 species, rnd, trainerRnd;
};
std::vector<Faint> sFaints;
std::vector<std::string> sRuleLog;
bool sWasFight = false, sInFight = false, sPushedSeen = false, sPartyCheck = false;
int sCalm = 0;

struct Lost {
  u32 species, rnd, trainerRnd;
};
std::vector<Lost> sLost;

enum class AreaBy {
  PlaceName,
  Map,
};
constexpr AreaBy kAreaBy = AreaBy::PlaceName;
constexpr bool kFailedThrowUsesArea = false;
constexpr int kBallPocket = 1;
std::vector<u32> sAreas;
int sOwnedAtStart = -1;
int sCaughtInBattle = 0;
int sThrownInBattle = 0;
u32 sCatchArea = 0;

std::vector<std::string> sMessages;
void Say(const std::string& text) {
  xd::pt::RuleNote(text);
  sMessages.push_back(text);
}

constexpr u32 kStoryFlag = 0x3C4;
bool sCapWrote = false;
int sCapTold = -1;
int sCap = 100;
u32 sCapStory = 0xFFFFFFFFu;
unsigned sCapBoss = 0;
unsigned sCapWonChanges = 0;
bool sCapInStory = false;

void SetLimit(int level, const char* why) {
  if (xdExpLevelLimit == level) return;
  std::fprintf(stderr, "[challenge] level_cap: the game's experience limit %d -> %d (%s)\n", xdExpLevelLimit, level, why);
  xdExpLevelLimit = static_cast<unsigned char>(level);
  sCapWrote = true;
}

void LevelCapFrame() {
  if (floordata == nullptr || flag_data_number == nullptr) return;
  if (!sCapInStory) {
    const s32 map = GSfloorGetCurrent();
    if (map == 0 || fightIsFight() != 0 || floorGetPlaceName(static_cast<u32>(map)) == 0) {
      if (sCapWrote) SetLimit(100, "not on a map of the story");
      return;
    }
    sCapInStory = true;
    std::fprintf(stderr, "[challenge] level_cap: the story's first map of this playthrough (0x%X)\n", map);
  }
  const u32 story = GSflagGet(kStoryFlag);
  if (story != sCapStory || xd::battles::Changes() != sCapWonChanges) {
    static bool logged = false;
    if (!logged && (sLog || std::getenv("XD_LEVEL_CAP_LOG") != nullptr)) LogLevelCaps();
    logged = true;
    const bool same = story == sCapStory;
    const int capBefore = sCap;
    const unsigned bossBefore = sCapBoss;
    sCapStory = story;
    sCapWonChanges = xd::battles::Changes();
    sCap = LevelCapAt(story, &sCapBoss);
    if (!same || sCap != capBefore || sCapBoss != bossBefore) {
      std::fprintf(stderr, "[challenge] level_cap: story %u: cap %d (the next boss: battle %u)\n", story, sCap, sCapBoss);
    }
  }
  if (fightIsFight() != 0 && fightFloor_GetFightKindBackSaveDataFlag(nullptr) == 0) {
    SetLimit(100, "a battle kind that doesn't reach the save");
  } else {
    SetLimit(sCap, "the story's cap");
  }
}

void LevelCapTell() {
  if (sCapStory == 0xFFFFFFFFu || sCapTold == sCap) return;
  const bool first = sCapTold < 0;
  sCapTold = sCap;
  if (sCap >= 100) {
    if (first) return;
    sRuleLog.push_back("level cap: none from here on (story point " + std::to_string(sCapStory) + ")");
    Say("Level cap: none from here on.");
    return;
  }
  sRuleLog.push_back("level cap " + std::to_string(sCap) + " (story point " + std::to_string(sCapStory) + ")");
  Say("Level cap: " + std::to_string(sCap) + ". Pokemon at this level gain no more experience.");
}

void* PartyRecord(int slot) { return reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(slot))); }
int PartyCount() {
  int n = 0;
  while (n < 6 && PartyRecord(n) != nullptr && pokemonBiosGetPokemonDataId(PartyRecord(n)) != 0) n++;
  return n;
}
int OwnedCount() {
  int n = PartyCount();
  for (s8 box = 0; box < 8; box++) {
    const int empty = pcboxGetPokemonBoxNbEmptySlot(nullptr, box);
    if (empty >= 0 && empty <= 30) n += 30 - empty;
  }
  return n;
}

bool IsLost(void* record) {
  if (record == nullptr || pokemonBiosGetPokemonDataId(record) == 0) return false;
  const u32 rnd = pokemon_GetRnd(record), trainerRnd = pokemon_GetCatchTrainerRnd(record);
  for (const Lost& l : sLost) {
    if (l.rnd == rnd && l.trainerRnd == trainerRnd) return true;
  }
  return false;
}

bool MoveToPc(int slot) {
  void* p = PartyRecord(slot);
  if (pcboxAddPokemon(nullptr, p, kGraveyardBox) != 1 && pcboxAddPokemon(nullptr, p, -1) != 1) return false;
  const int n = PartyCount();
  for (int s = slot; s + 1 < n; s++) std::memcpy(PartyRecord(s), PartyRecord(s + 1), kPokemonBytes);
  pokemonInit(PartyRecord(n - 1));
  return true;
}

std::string GameText(u32 key) {
  const u16* t = key != 0 ? GSmsgGetGSchar(key) : nullptr;
  std::string out;
  for (int n = 0; t != nullptr && n < 40 && t[n] != 0 && t[n] != 0xFFFF; n++) {
    out += t[n] >= 0x20 && t[n] < 0x7F ? static_cast<char>(t[n]) : t[n] == 0xE9 ? 'e' : '?';
  }
  return out;
}

u32 BattleArea() {
  const u32 map = fightFloor_GetEncountFloorId(nullptr) & 0xFFFFu;
  const u32 place = kAreaBy == AreaBy::PlaceName ? floorGetPlaceName(map) : 0;
  return place != 0 ? place : 0x10000000u + map;
}
std::string AreaName(u32 area) { return area >= 0x10000000u ? "map " + std::to_string(area - 0x10000000u) : GameText(area); }
bool AreaUsed(u32 area) { return std::find(sAreas.begin(), sAreas.end(), area) != sAreas.end(); }
void UseArea(u32 area, const char* why) {
  if (AreaUsed(area)) return;
  sAreas.push_back(area);
  sRuleLog.push_back("area used: " + AreaName(area) + " (" + why + ")");
  std::fprintf(stderr, "[challenge] nuzlocke_one_per_area: area %u \"%s\" is used up (%s)\n", area, AreaName(area).c_str(), why);
}

void NoteFaint(void* fightOutPokemon) {
  if (!sLog && !Active("nuzlocke_faint")) return;
  if (fightOutPokemon == nullptr || fightOutPokemonIsGcHeroFightOutPokemon(fightOutPokemon) != 1) return;
  if (fightFloor_GetFightKindBackSaveDataFlag(nullptr) == 0) {
    std::fprintf(stderr, "[challenge] faint in a battle kind that doesn't reach the save: not counted\n");
    return;
  }
  void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fightOutPokemon);
  const int place = trainer != nullptr ? fightTrainerGetFightOutPokemonToTemotiBanme(trainer, fightOutPokemon) : -1;
  void* fp = fightOutPokemon_GetMotoFightPokemonPtr(fightOutPokemon);
  void* moto = fp != nullptr ? fightPokemon_GetMotoPokemonPtr(fp) : nullptr;
  const u32 species = moto != nullptr ? pokemonBiosGetPokemonDataId(moto) : 0;
  if (species == 0) {
    std::fprintf(stderr, "[challenge] faint: the player's Pokemon, but its record can't be told (battle place %d): not counted\n", place + 1);
    return;
  }
  const Faint f = {species, pokemon_GetRnd(moto), pokemon_GetCatchTrainerRnd(moto)};
  int slot = -1;
  for (int s = 0; s < 6 && slot < 0; s++) {
    void* p = PartyRecord(s);
    if (p != nullptr && pokemonBiosGetPokemonDataId(p) != 0 && pokemon_GetRnd(p) == f.rnd && pokemon_GetCatchTrainerRnd(p) == f.trainerRnd) slot = s;
  }
  std::fprintf(stderr, "[challenge] faint: the player's Pokemon, party slot %d, species %u (battle place %d)%s\n", slot + 1, species, place + 1,
               slot < 0 ? ": not in the saved party now; counts if it is there after the battle" : "");
  for (const Faint& seen : sFaints) {
    if (seen.rnd == f.rnd && seen.trainerRnd == f.trainerRnd) return;
  }
  sFaints.push_back(f);
}

struct Release {
  bool on, held;
  void* record;
  u32 species, rnd, trainerRnd;
};
Release sRelease = {};
void ReleaseCheck(bool last) {
  if (!sRelease.on) return;
  void* now = sRelease.record;
  int next = 0;
  void* held = nullptr;
  const bool open = menuPcBoxPeekMore(&next, &held) != 0;
  if (sRelease.held) now = open ? held : nullptr;
  if (sRelease.held && !open) {
    sRelease.on = false;
    return;
  }
  const bool there = now != nullptr && pokemonBiosGetPokemonDataId(now) != 0 && pokemon_GetRnd(now) == sRelease.rnd &&
                     pokemon_GetCatchTrainerRnd(now) == sRelease.trainerRnd;
  if (there) {
    if (last || !open) sRelease.on = false;
    return;
  }
  sRelease.on = false;
  for (size_t i = 0; i < sLost.size(); i++) {
    if (sLost[i].rnd != sRelease.rnd || sLost[i].trainerRnd != sRelease.trainerRnd) continue;
    sLost.erase(sLost.begin() + static_cast<std::ptrdiff_t>(i));
    sRuleLog.push_back("released: lost species " + std::to_string(sRelease.species) + " was released at the PC (Nuzlocke)");
    std::fprintf(stderr, "[challenge] nuzlocke_faint: lost species %u was released at the PC: off the lost list (%zu lost now)\n",
                 sRelease.species, sLost.size());
    break;
  }
}

constexpr int kPcFirstPartyPlace = 4, kPcFirstBoxPlace = 10;
constexpr u32 kPcMove = 0x3BC8, kPcSummary = 0x3BC9, kPcWithdraw = 0x3BCA, kPcMarkings = 0x3BCB, kPcDeposit = 0x3BCD,
              kPcPutDown = 0x3BCE, kPcSwap = 0x3BCF, kPcRelease = 0x3BE8;
const char* PcActionName(u32 msg) {
  switch (msg) {
  case kPcMove: return "MOVE";
  case kPcSummary: return "SUMMARY";
  case kPcWithdraw: return "WITHDRAW";
  case kPcMarkings: return "MARKINGS";
  case kPcDeposit: return "DEPOSIT";
  case kPcPutDown: return "put down";
  case kPcSwap: return "swap";
  case kPcRelease: return "RELEASE";
  default: return "?";
  }
}
int PcAction(u32 msg, int box, int place, void* under, void* held) {
  ReleaseCheck(true);
  if (!Active("nuzlocke_faint") || sLost.empty()) return 0;
  const bool heldLost = IsLost(held), underLost = IsLost(under);
  if (!heldLost && !underLost) return 0;
  const bool party = place >= kPcFirstPartyPlace && place < kPcFirstBoxPlace;
  const bool graveyard = place >= kPcFirstBoxPlace && box == kGraveyardBox;
  void* current = held != nullptr ? held : under;
  const bool currentLost = held != nullptr ? heldLost : underLost;
  const char* refused = nullptr;
  switch (msg) {
  case kPcWithdraw:
    if (currentLost) refused = "Nuzlocke: this Pokemon is lost. It can't rejoin your party.";
    break;
  case kPcDeposit:
    if (heldLost) refused = "Nuzlocke: this Pokemon is lost. It can only be put down in the last PC box.";
    break;
  case kPcPutDown:
  case kPcSwap:
    if (heldLost && !graveyard) {
      refused = party ? "Nuzlocke: this Pokemon is lost. It can't rejoin your party."
                      : "Nuzlocke: this Pokemon is lost. It can only be put down in the last PC box.";
      break;
    }
    if (msg == kPcPutDown) break;
    [[fallthrough]];
  case kPcMove:
    if (underLost && !graveyard && pcboxGetPokemonBoxNbEmptySlot(nullptr, kGraveyardBox) <= 0) {
      refused = "Nuzlocke: this Pokemon is lost, and the last PC box has no room for it. It stays here.";
    }
    break;
  default:
    break;
  }
  const u32 species = pokemonBiosGetPokemonDataId(currentLost ? current : heldLost ? held : under);
  if (refused != nullptr || sLog) {
    std::fprintf(stderr, "[challenge] nuzlocke_faint: PC %s %s (%s %d place %d, lost species %u, %s)\n", PcActionName(msg),
                 refused != nullptr ? "refused" : "allowed", party ? "party, box shown" : "box", box + 1,
                 party ? place - kPcFirstPartyPlace + 1 : place - kPcFirstBoxPlace + 1, species,
                 heldLost ? "the lost one in the hand" : held != nullptr ? "another Pokemon in the hand" : "empty hand");
  }
  if (refused != nullptr) {
    Say(refused);
    return 1;
  }
  if (msg == kPcRelease && currentLost) {
    sRelease = {true, held != nullptr, held != nullptr ? nullptr : under, species, pokemon_GetRnd(current), pokemon_GetCatchTrainerRnd(current)};
  }
  return 0;
}

void CheckParty(bool announce) {
  ReleaseCheck(true);
  if (!Active("nuzlocke_faint") || sLost.empty() || GSfloorGetCurrent() == 0) return;
  const int count = PartyCount();
  std::vector<int> lost;
  for (int s = count - 1; s >= 0; s--) {
    if (IsLost(PartyRecord(s))) lost.push_back(s);
  }
  if (lost.empty()) return;
  const bool onlyLost = static_cast<int>(lost.size()) >= count;
  if (onlyLost) lost.pop_back();
  int moved = 0, kept = 0;
  for (int slot : lost) {
    const u32 species = pokemonBiosGetPokemonDataId(PartyRecord(slot));
    if (MoveToPc(slot)) {
      moved++;
      sRuleLog.push_back("lost Pokemon (species " + std::to_string(species) + ") was in the party: moved back to the PC");
      std::fprintf(stderr, "[challenge] nuzlocke_faint: lost species %u was in the party (slot %d): moved back to the PC; party now %d\n",
                   species, slot + 1, PartyCount());
    } else {
      kept++;
    }
  }
  if (moved != 0) {
    Say(moved == 1 ? "Nuzlocke: a lost Pokemon can't rejoin your party. It was moved back to the PC."
                   : "Nuzlocke: lost Pokemon can't rejoin your party. They were moved back to the PC.");
  }
  if (!announce && moved == 0) return;
  if (kept != 0) {
    std::fprintf(stderr, "[challenge] nuzlocke_faint: every PC box is full: %d lost Pokemon stay in the party for now\n", kept);
    Say("Nuzlocke: the PC boxes are full, so a lost Pokemon stays with you. Please make room.");
  } else if (onlyLost) {
    std::fprintf(stderr, "[challenge] nuzlocke_faint: the party holds only lost Pokemon: the first one stays for now\n");
    Say("Nuzlocke: your party holds only lost Pokemon. Please withdraw another Pokemon.");
  }
}

void BattleBegins() {
  sCaughtInBattle = sThrownInBattle = 0;
  sCatchArea = 0;
  sOwnedAtStart = Active("nuzlocke_one_per_area") && GSfloorGetCurrent() != 0 ? OwnedCount() : -1;
}

void CatchesOver() {
  const int caught = sCaughtInBattle, thrown = sThrownInBattle;
  sCaughtInBattle = sThrownInBattle = 0;
  if (!Active("nuzlocke_one_per_area") || thrown == 0 || sOwnedAtStart < 0) return;
  const int owned = OwnedCount();
  std::fprintf(stderr, "[challenge] nuzlocke_one_per_area: battle over: %d ball(s) thrown, %d caught; Pokemon owned %d -> %d\n", thrown,
               caught, sOwnedAtStart, owned);
  if (AreaUsed(sCatchArea)) return;
  if (owned > sOwnedAtStart) {
    UseArea(sCatchArea, "a Pokemon caught there joined you");
    Say("Nuzlocke: that was your one catch in this area (" + AreaName(sCatchArea) + ").");
  } else if (kFailedThrowUsesArea) {
    UseArea(sCatchArea, "a Poke Ball was thrown there");
    Say("Nuzlocke: that was your one try in this area (" + AreaName(sCatchArea) + ").");
  }
}

bool BattleOver() {
  const u32 result = fightGetFightResultId() & 0xFFFFu;
  CatchesOver();
  std::vector<int> lost;
  for (int s = 0; s < 6; s++) {
    void* p = PartyRecord(s);
    if (p == nullptr || pokemonBiosGetPokemonDataId(p) == 0) continue;
    for (const Faint& f : sFaints) {
      if (pokemon_GetRnd(p) == f.rnd && pokemon_GetCatchTrainerRnd(p) == f.trainerRnd) {
        lost.push_back(s);
        break;
      }
    }
  }
  sFaints.clear();
  const int count = PartyCount();
  if (sLog) std::fprintf(stderr, "[challenge] battle over: result %u, party %d, fainted in it %zu\n", result, count, lost.size());
  if (!Active("nuzlocke_faint") || lost.empty()) return false;
  if (static_cast<int>(lost.size()) >= count) {
    std::fprintf(stderr, "[challenge] nuzlocke_faint: every party Pokemon fainted: run failed, party kept\n");
    sRuleLog.push_back("failed: every party Pokemon fainted (Nuzlocke)");
    Say("Nuzlocke: all your Pokemon fainted. This run has failed.");
    return false;
  }
  std::sort(lost.rbegin(), lost.rend());
  int moved = 0, kept = 0;
  for (int slot : lost) {
    void* p = PartyRecord(slot);
    const u32 species = pokemonBiosGetPokemonDataId(p);
    const bool shadow = pokemonIsDarkPokemon(p) != 0;
    if (!IsLost(p)) sLost.push_back({species, pokemon_GetRnd(p), pokemon_GetCatchTrainerRnd(p)});
    if (!MoveToPc(slot)) {
      std::fprintf(stderr, "[challenge] nuzlocke_faint: every PC box is full: species %u stays in the party\n", species);
      sRuleLog.push_back("lost: species " + std::to_string(species) + " fainted (Nuzlocke); the PC boxes were full, it stayed in the party");
      kept++;
      continue;
    }
    moved++;
    sRuleLog.push_back("lost: species " + std::to_string(species) + " fainted (Nuzlocke), moved to the PC");
    std::fprintf(stderr, "[challenge] nuzlocke_faint: species %u%s (party slot %d) moved to the PC; party now %d\n", species,
                 shadow ? " (a Shadow Pokemon)" : "", slot + 1, PartyCount());
  }
  if (moved != 0) {
    Say("Nuzlocke: " + std::to_string(moved) + (moved == 1 ? " Pokemon fainted and was" : " Pokemon fainted and were") +
        " moved to the last PC box.");
  }
  if (kept != 0) {
    sRuleLog.push_back("failed: the PC boxes were full, a fainted Pokemon could not be put away (Nuzlocke)");
    Say("Nuzlocke: the PC boxes are full, so a fainted Pokemon stays with you. This run has failed.");
  }
  return kept != 0;
}

}

const std::vector<Rule>& Rules() {
  static const std::vector<Rule> rules = {
      {"no_battle_items", "No items in battle",
       "In battles the bag only gives Poke Balls: no potions, no status healers, no X items. Snagging stays possible."},
      {"nuzlocke_faint", "Nuzlocke: fainted is lost",
       "A Pokemon of yours that faints is moved to the last PC box when the battle is over, even if it was revived, and "
       "can't join your party again. If all of them faint, the run has failed."},
      {"nuzlocke_one_per_area", "Nuzlocke: one catch per area",
       "In each area you may catch one Pokemon of your choice, snagged or wild. After that, Poke Balls can't be used "
       "in that area's battles."},
      {"level_cap", "Level caps",
       "Your Pokemon can't grow past the level of the next story boss's strongest Pokemon: at the cap they gain no "
       "experience, and Rare Candy has no effect. The cap rises when the boss is beaten."},
  };
  return rules;
}

const Rule* Find(const std::string& id) {
  for (const Rule& r : Rules()) {
    if (id == r.id) return &r;
  }
  return nullptr;
}

std::vector<std::string> TestSwitch() {
  std::vector<std::string> ids;
  const char* env = std::getenv("XD_CHALLENGES");
  if (env == nullptr || env[0] == '\0') return ids;
  if (const char* o = std::getenv("XD_REQUIRE_ORIGINAL"); o != nullptr && std::strcmp(o, "1") == 0) return ids;
  std::string text = env;
  for (size_t at = 0; at <= text.size();) {
    const size_t end = std::min(text.find(',', at), text.size());
    const std::string id = text.substr(at, end - at);
    if (Find(id) != nullptr) ids.push_back(id);
    else if (!id.empty()) std::fprintf(stderr, "[challenge] XD_CHALLENGES: no rule \"%s\"; left out\n", id.c_str());
    at = end + 1;
  }
  return ids;
}

void SetActive(const std::vector<std::string>& ids, const char* why) {
  sLog = std::getenv("XD_CHALLENGE_LOG") != nullptr;
  std::vector<std::string> known = TestSwitch();
  for (const std::string& id : ids) {
    if (std::find(known.begin(), known.end(), id) != known.end()) continue;
    if (Find(id) != nullptr) known.push_back(id);
    else std::fprintf(stderr, "[challenge] this playthrough has a rule this version doesn't know (\"%s\"): not enforced\n", id.c_str());
  }
  sRuleLog.clear();
  sFaints.clear();
  sLost.clear();
  sAreas.clear();
  sCaughtInBattle = sThrownInBattle = 0;
  sRelease = {};
  sCapTold = -1;
  sCapStory = 0xFFFFFFFFu;
  sCapInStory = false;
  if (sCapWrote && std::find(known.begin(), known.end(), "level_cap") == known.end()) SetLimit(100, "the rule is off");
  const bool changed = known != sActive;
  sActive = known;
  static const bool pcOff = [] {
    const char* e = std::getenv("XD_CHALLENGE_PC_REFUSAL");
    return e != nullptr && std::strcmp(e, "0") == 0;
  }();
  menuPcBoxActionFilter = Active("nuzlocke_faint") && !pcOff ? &PcAction : nullptr;
  if (!changed) return;
  std::string list;
  for (const std::string& id : sActive) list += (list.empty() ? "" : ", ") + id;
  std::fprintf(stderr, "[challenge] rules of this playthrough (%s): %s\n", why, list.empty() ? "none" : list.c_str());
}

bool Active(const char* id) { return std::find(sActive.begin(), sActive.end(), id) != sActive.end(); }
bool AnyActive() { return !sActive.empty(); }

bool RefusePress(const void* activeWindow, unsigned newButtons, std::string& message) {
  if (sActive.empty()) return false;
  if (activeWindow == nullptr) return false;
  const auto* w = static_cast<const WinWorkLayout*>(activeWindow);
  if (sLog && (newButtons & PAD_BUTTON_A) != 0) {
    std::fprintf(stderr, "[challenge] A pressed: active menu 0x%X state %d cursor %d,%d busy %d, battle bag %d, items %d\n", w->menuID,
                 w->state, w->cursorX, w->cursor, w->pad_a4, sBattleBag ? 1 : 0, w->itemNum);
    int box = 0, place = 0, state = 0, holding = 0, quick = 0;
    void* under = nullptr;
    if (menuPcBoxPeekCursor(&box, &place, &state, &holding, &quick, &under) != 0) {
      std::fprintf(stderr, "[challenge] A pressed: the PC's Pokemon screen: box %d place %d state %d, hand %s, %s mode, species under the cursor %u\n",
                   box + 1, place, state, holding != 0 ? "full" : "empty", quick != 0 ? "quick" : "list",
                   under != nullptr ? pokemonBiosGetPokemonDataId(under) : 0u);
    }
  }
  if (sBattleBag && Active("no_battle_items") && (newButtons & PAD_BUTTON_A) != 0 && w->menuID == kActionMenu &&
      w->state == 2 && CursorOnUse(w)) {
    const auto* bag = static_cast<const WinWorkLayout*>(windowSearchID(kPocketMenu));
    if (bag != nullptr && (bag->cursor == kItemPocket || bag->cursor == kBerryPocket)) {
      message = "No items in battle: only Poke Balls can be used.";
      std::fprintf(stderr, "[challenge] no_battle_items: an item of pocket %d refused\n", bag->cursor);
      return true;
    }
  }
  if (sBattleBag && Active("nuzlocke_one_per_area") && (newButtons & PAD_BUTTON_A) != 0 && w->menuID == kActionMenu &&
      w->state == 2 && CursorOnUse(w) && fightIsFight() != 0 && fightFloor_GetFightKindBackSaveDataFlag(nullptr) != 0) {
    const auto* bag = static_cast<const WinWorkLayout*>(windowSearchID(kPocketMenu));
    if (bag != nullptr && bag->cursor == kBallPocket) {
      const u32 area = BattleArea();
      const bool inBattle = sCaughtInBattle != 0 || (kFailedThrowUsesArea && sThrownInBattle != 0);
      if (inBattle || AreaUsed(area)) {
        message = inBattle ? "Nuzlocke: you already had your catch in this battle. One catch per area."
                           : "Nuzlocke: you already caught a Pokemon in this area (" + AreaName(area) + ").";
        std::fprintf(stderr, "[challenge] nuzlocke_one_per_area: a Poke Ball refused (area %u \"%s\", caught in this battle %d)\n", area,
                     AreaName(area).c_str(), sCaughtInBattle);
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> State() {
  ReleaseCheck(false);
  std::vector<std::string> lines;
  char text[96];
  for (const Lost& l : sLost) {
    std::snprintf(text, sizeof text, "lost %u %08x %08x", l.species, l.rnd, l.trainerRnd);
    lines.push_back(text);
  }
  for (u32 a : sAreas) lines.push_back("area " + std::to_string(a) + " " + AreaName(a));
  return lines;
}

void SetState(const std::vector<std::string>& lines, const char* why) {
  sLost.clear();
  sAreas.clear();
  for (const std::string& line : lines) {
    unsigned a = 0, b = 0, c = 0;
    if (std::sscanf(line.c_str(), "lost %u %x %x", &a, &b, &c) == 3) sLost.push_back({a, b, c});
    else if (std::sscanf(line.c_str(), "area %u", &a) == 1) sAreas.push_back(a);
    else std::fprintf(stderr, "[challenge] the save's challenge state has a line this version doesn't know: \"%s\"\n", line.c_str());
  }
  sPartyCheck = true;
  if (!lines.empty() || sLog) {
    std::fprintf(stderr, "[challenge] state of this playthrough (%s): %zu lost Pokemon, %zu areas used\n", why, sLost.size(), sAreas.size());
  }
}

std::vector<std::string> TakeLog() {
  std::vector<std::string> log;
  log.swap(sRuleLog);
  return log;
}

void Frame() {
  if (sActive.empty() && !sLog) return;
  if (!sMessages.empty() && PortXD_ShowRuleMessage(sMessages.front())) sMessages.erase(sMessages.begin());
  ReleaseCheck(false);
  if (Active("level_cap")) LevelCapFrame();
  if (fightIsFight() != 0) {
    if (!sInFight) BattleBegins();
    sInFight = sWasFight = true;
    sCalm = 0;
    return;
  }
  sInFight = false;
  if (floordata == nullptr || GSfloorGetCurrent() == 0) return;
  if (GSfloorIsFloorPushed()) {
    sPushedSeen = true;
    sCalm = 0;
    return;
  }
  if (++sCalm < kCalmFrames) return;
  if (Active("level_cap")) LevelCapTell();
  if (sPushedSeen) {
    sPushedSeen = false;
    sPartyCheck = true;
  }
  if (sWasFight) {
    sWasFight = false;
    sPartyCheck = !BattleOver();
  }
  if (sPartyCheck || sCalm % kPartySweepFrames == 0) {
    const bool announce = sPartyCheck;
    sPartyCheck = false;
    CheckParty(announce);
  }
  if (sCalm >= (1 << 30)) sCalm = kCalmFrames;
}

}

extern "C" void PortXD_KizetuEffect(void* fightOutPokemon, u8 mode) {
  if (mode == 0) xd::challenges::NoteFaint(fightOutPokemon);
  fightOutPokemonKizetuEffect(fightOutPokemon, mode);
}

extern "C" void PortXD_HokakuEffect(void* fightOutPokemon, u8 shakes, u16 ball, u8 mode, u8* work) {
  using namespace xd::challenges;
  if (mode == 0 && (Active("nuzlocke_one_per_area") || sLog)) {
    sThrownInBattle++;
    sCatchArea = BattleArea();
    std::fprintf(stderr, "[challenge] ball thrown (%d of this battle, %u shakes, area %u \"%s\")\n", sThrownInBattle, shakes, sCatchArea,
                 AreaName(sCatchArea).c_str());
  }
  fightOutPokemonHokakuEffect(fightOutPokemon, shakes, static_cast<u8>(ball), mode, work);
}
extern "C" void PortXD_HokakuFlag(void* fightPokemon, u8 value) {
  using namespace xd::challenges;
  if (value == 1 && (Active("nuzlocke_one_per_area") || sLog)) {
    sCaughtInBattle++;
    sCatchArea = BattleArea();
    std::fprintf(stderr, "[challenge] catch: a ball caught its target (catch %d of this battle, area %u \"%s\")\n", sCaughtInBattle, sCatchArea,
                 AreaName(sCatchArea).c_str());
  }
  fightPokemon_SetHokakuFlag(fightPokemon, value);
}

extern "C" u16 PortXD_FightPocketOpen(s32 mode, s32* slot, void* trainer) {
  xd::challenges::sBattleBag = true;
  const u16 item = menuPocket2Open(mode, slot, trainer);
  xd::challenges::sBattleBag = false;
  return item;
}
