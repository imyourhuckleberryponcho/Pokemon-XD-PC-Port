#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <settings.h>

#include <game/pxdvs/GSAPI/GSflag/flagData.h>
#include <game/pxdvs/app/floor/floorData.h>

struct GSthread;
struct FightEncountData;

extern "C" {
GSthread* GSthreadCreate(u8 affinity, u32 group, u32 stackSize, u8 usesFPU, u8 autoStart, void* entry);
GSthread* GSthreadGetCurrentThread(void);
void GSthreadSetName(GSthread* thread, const char* name);
u8 GSthreadIsRunning(GSthread* thread);
s32 GSfloorGetCurrent(void);
s32 GSfloorGetCurrentFloorGroupID(void);
bool GSfloorIsFloorPushed(void);
void floorLink(u32 floorID, u32 posIndex);
u32 GSflagGet(u32 id);
void GSflagSet(u32 id, u32 value);
u16* GSmsgGetGSchar(u32 key);
u8 fightIsFight(void);
FightEncountData* fightEncountDataBiosGetPtr(u16 id);
u32 fightEncountDataBiosGetFightName(FightEncountData* data);
u32 fightEncountDataBiosGetFightKind(FightEncountData* data);
u32 fightEncountDataBiosGetTrainer(FightEncountData* data);
u32 fightEncountDataBiosGetPokemon(FightEncountData* data);
u32 fightEncountDataBiosGetEntry(FightEncountData* data);
u32 fightEncountDataBiosGetFightType(FightEncountData* data);
u16 fightEncountDataBiosGetFightFloorDataId(FightEncountData* data);
u16 fightEncountDataBiosGetDiskId(FightEncountData* data);
u16 fightEncountDataBiosGetFightTrainerDataId(FightEncountData* data, u8 index);
u16 fightEncountDataBiosGetFightDeckGrpId(FightEncountData* data, u8 index);
void* fightKindDataBiosGetPtr(u16 id);
u32 fightKindDataBiosGetName(void* data);
void* fightTypeDataBiosGetPtr(u16 id);
u32 fightTypeDataBiosGetName(void* data);
u8* itemDataBiosGetPtr(u16 index);
u32 itemDataBiosGetName(u8* p);
bool itemDataCheckValid(u16 id);
void heroAddPokedoru(void* hero, u32 amount);
u32 floorGetNextFloorID(void);
void gamedatasaveSetStatus(void* save, u32 kind, u32 value);
s32 memcardTaskCall(s32 kind, s32 type, s32 arg);
void heroAddPokecoupon(void* hero, s32 amount);
uintptr_t heroGetStatus(void* hero, u32 selector, u16 index);
u8 pokemonIsDarkPokemon(void* pokemon);
f32 pokemonGetDp(void* pokemon);
void pokemonSetDp(void* pokemon, f32 value);
s32 heroItemAddItemDataId(void* hero, u32 itemID, u32 amount, u32 index);
void pcboxDebugPokemonSetupFull(void);
void pokemonCreate(void* pokemon, u16 species, u8 level, void* attest);
uintptr_t savedataGetStatus(u32 save, u32 kind);
uintptr_t gamedataGetStatus(void* gamedata, u32 kind);
s32 heroCatchPokemon(void* hero, void* pokemon, u32 arg3, u32 arg4, u8 allowPCBox);
void memoDataSet(u32 kind, void* pokemon);
void pokemonBiosSetItemDataId(void* pokemon, u16 item);
void pokemonBiosSetTokuseiFlag(void* pokemon, bool second);
void pcboxAddItem(void* pcbox, u16 id, u16 n);
void pokemonSetOnZukanFlag(void* pokemon, u8 caught);

void* GSgfxLayerFindByID(s32 id);
s32 GSgfxLayerFindByName_C(const char* name);

s32 dbgMenuGetLayerID(void);
void dbgMenuSetLayerID(s32 id);
void dbgMenuFlagSetEtc(s32 index);
s32 dbgMenuFightEncount(u16 id);
s32 dbgMenuFightStopHostWin(void);
s32 dbgMenuFightStopHostLose(void);
s32 dbgMenuFightStopTimeOut(void);
s32 dbgMenuDbgPokemonGet(void);
s32 dbgMenuFlagCheck(void);
s32 dbgMenuMenuTestEeveeMenu(void);
s32 dbgMenuFightEncountSelect(void);
s32 dbgMenuHeroCreateItem(void);
s32 dbgMenuHeroIncMoney(void);
s32 dbgMenuHeroIncCoupon(void);
s32 dbgMenuHeroPokemonAdd(void);
s32 dbgMenuHeroPokemonSelect0(void);
s32 dbgMenuHeroPokemonSelect1(void);
s32 dbgMenuHeroPokemonSelect2(void);
s32 dbgMenuHeroPokemonSelect3(void);
s32 dbgMenuHeroPokemonSelect4(void);
s32 dbgMenuHeroPokemonSelect5(void);
s32 dbgMenuSoundTestBGM(void);
s32 dbgMenuSoundTestSE(void);
s32 dbgMenuSoundTestENV(void);
s32 dbgMenuSoundTestNAKIGOE(void);
s32 dbgMenuMemcardSave(void);
s32 dbgMenuMemcardLoad(void);
s32 dbgMenuRelive(void);
s32 dbgMenuMsgCheck(void);
s32 dbgMenuCameraChangeDisp(void);
s32 dbgMenuCameraResetFloor(void);
s32 dbgMenuFieldCameraChangeDisp(void);
s32 dbgMenuPeopleOpenEdit(void);
s32 dbgMenuDispChangePartyInfo(void);
s32 dbgMenuLogChangeDisp(void);
s32 dbgMenuGSgfxChangeDisp(void);
s32 debugMenuChangePerfBar(void);
s32 debugMenuChangeMemInfo(void);
s32 dbgMenuScriptDispLine(void);
s32 dbgMenuScriptHumanColision(void);
s32 dbgMenuSoundVolumeDisp(void);
s32 dbgMenuSetHeroStyleNormal(void);
s32 dbgMenuSetHeroStyleVisor(void);
s32 dbgMenuSetHeroStyleSnatch(void);
s32 dbgMenuPTout(void);
void menuShopOpen(s32 shop);

extern void* floordata;
extern s32* floordata_number;
extern void* flag_etc;
extern u32* flag_etc_number;
extern u32* flag_data_number;
extern u32* fight_encount_data_number;

u32 fightGetFightResultId(void);

void PortInput_StopRecording(void);
void PortLog_Flush(void);
uint32_t PortVI_GetFlipCount(void);
void PortInput_DevMenuNote(const char* action);
const char* PortInput_ReplayDevMenuAt(size_t index, uint32_t* frame);
}

namespace {

bool sEnabled = false;
bool sShown = false;

struct Screen {
  const char* name;
  const char* label;
  s32 (*func)(void);
};
const Screen kScreens[] = {
    {"dbgMenuFlagCheck", "Flag: story list (hold R: flag editor)", dbgMenuFlagCheck},
    {"dbgMenuMenuTestEeveeMenu", "Menu: the Eevee offer (floor 0x97 only)", dbgMenuMenuTestEeveeMenu},
    {"dbgMenuFightEncountSelect", "Fight: choose a battle", dbgMenuFightEncountSelect},
    {"dbgMenuHeroCreateItem", "ItemCreate: item and count", dbgMenuHeroCreateItem},
    {"dbgMenuHeroIncMoney", "Hero: add money", dbgMenuHeroIncMoney},
    {"dbgMenuHeroIncCoupon", "Hero: add coupons", dbgMenuHeroIncCoupon},
    {"dbgMenuHeroPokemonAdd", "Pokemon: make a new one", dbgMenuHeroPokemonAdd},
    {"dbgMenuHeroPokemonSelect0", "Pokemon: edit party 1", dbgMenuHeroPokemonSelect0},
    {"dbgMenuHeroPokemonSelect1", "Pokemon: edit party 2", dbgMenuHeroPokemonSelect1},
    {"dbgMenuHeroPokemonSelect2", "Pokemon: edit party 3", dbgMenuHeroPokemonSelect2},
    {"dbgMenuHeroPokemonSelect3", "Pokemon: edit party 4", dbgMenuHeroPokemonSelect3},
    {"dbgMenuHeroPokemonSelect4", "Pokemon: edit party 5", dbgMenuHeroPokemonSelect4},
    {"dbgMenuHeroPokemonSelect5", "Pokemon: edit party 6", dbgMenuHeroPokemonSelect5},
    {"dbgMenuSoundTestBGM", "SoundTest: music", dbgMenuSoundTestBGM},
    {"dbgMenuSoundTestSE", "SoundTest: sound effects", dbgMenuSoundTestSE},
    {"dbgMenuSoundTestENV", "SoundTest: ambience", dbgMenuSoundTestENV},
    {"dbgMenuSoundTestNAKIGOE", "SoundTest: Pokemon cries", dbgMenuSoundTestNAKIGOE},
    {"dbgMenuMemcardSave", "Memcard: save", dbgMenuMemcardSave},
    {"dbgMenuMemcardLoad", "Memcard: load", dbgMenuMemcardLoad},
    {"dbgMenuRelive", "Relive (purify)", dbgMenuRelive},
    {"dbgMenuMsgCheck", "Msg: message test", dbgMenuMsgCheck},
    {"dbgMenuCameraChangeDisp", "Camera: info on/off", dbgMenuCameraChangeDisp},
    {"dbgMenuCameraResetFloor", "Camera: reset", dbgMenuCameraResetFloor},
    {"dbgMenuFieldCameraChangeDisp", "FieldCamera: info on/off", dbgMenuFieldCameraChangeDisp},
    {"dbgMenuPeopleOpenEdit", "People: editor", dbgMenuPeopleOpenEdit},
    {"dbgMenuDispChangePartyInfo", "People: party info on/off", dbgMenuDispChangePartyInfo},
    {"dbgMenuLogChangeDisp", "Log: on/off", dbgMenuLogChangeDisp},
    {"dbgMenuGSgfxChangeDisp", "GSgfx: info on/off", dbgMenuGSgfxChangeDisp},
    {"debugMenuChangePerfBar", "Performance bar", debugMenuChangePerfBar},
    {"debugMenuChangeMemInfo", "Memory info", debugMenuChangeMemInfo},
    {"dbgMenuScriptDispLine", "Script: show lines", dbgMenuScriptDispLine},
    {"dbgMenuScriptHumanColision", "Walk through people on/off", dbgMenuScriptHumanColision},
    {"dbgMenuSoundVolumeDisp", "Sound volume info on/off", dbgMenuSoundVolumeDisp},
    {"dbgMenuSetHeroStyleNormal", "Hero style: normal", dbgMenuSetHeroStyleNormal},
    {"dbgMenuSetHeroStyleVisor", "Hero style: visor", dbgMenuSetHeroStyleVisor},
    {"dbgMenuSetHeroStyleSnatch", "Hero style: Snag Machine", dbgMenuSetHeroStyleSnatch},
    {"dbgMenuPTout", "Partner leaves", dbgMenuPTout},
};

struct Scheduled {
  uint32_t frame;
  std::string action;
};
std::vector<Scheduled> sScheduled;
size_t sNextScheduled = 0;
std::vector<std::string> sQueue;
std::string sRunning;
GSthread* sThread = nullptr;
bool sThreadDone = true;
uint32_t sThreadStart = 0;
std::vector<std::string> sUsed;
std::string sStatus;
bool sReplayChecked = false;

std::vector<std::string> Words(const std::string& s) {
  std::vector<std::string> w;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') i++;
    size_t j = i;
    while (j < s.size() && s[j] != ' ') j++;
    if (j > i) w.push_back(s.substr(i, j - i));
    i = j;
  }
  return w;
}
long Num(const std::vector<std::string>& w, size_t i, long fallback = 0) {
  return i < w.size() ? std::strtol(w[i].c_str(), nullptr, 0) : fallback;
}

std::string GameText(u32 key) {
  const u16* t = key != 0 ? GSmsgGetGSchar(key) : nullptr;
  std::string out;
  if (t == nullptr) {
    return out;
  }
  for (int n = 0; n < 80; n++) {
    const uint16_t c = t[n];
    if (c == 0 || c == 0xFFFF) {
      break;
    }
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | c >> 6);
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | c >> 12);
      out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

void Perform(const std::string& action) {
  const std::vector<std::string> w = Words(action);
  const std::string& k = w.empty() ? action : w[0];
  if (k == "warp") {
    if (GSfloorIsFloorPushed()) {
      sStatus = "can't warp from this place (the game refuses: floor pushed)";
      return;
    }
    floorLink(static_cast<u32>(Num(w, 1)), static_cast<u32>(Num(w, 2)));
  } else if (k == "story") {
    if (GSfloorIsFloorPushed()) {
      sStatus = "can't change the story point from this place (floor pushed)";
      return;
    }
    dbgMenuFlagSetEtc(static_cast<s32>(Num(w, 1)));
  } else if (k == "flag") {
    GSflagSet(static_cast<u32>(Num(w, 1)), static_cast<u32>(Num(w, 2)));
  } else if (k == "battle") {
    if (fightIsFight() != 0) {
      sStatus = "already in a battle";
      return;
    }
    dbgMenuFightEncount(static_cast<u16>(Num(w, 1)));
  } else if (k == "fight") {
    const std::string how = w.size() > 1 ? w[1] : "";
    const s32 r = how == "win" ? dbgMenuFightStopHostWin() : how == "lose" ? dbgMenuFightStopHostLose()
                                                                            : dbgMenuFightStopTimeOut();
    if (r < 0) {
      sStatus = "not in a battle";
      return;
    }
  } else if (k == "money") {
    heroAddPokedoru(nullptr, static_cast<u32>(Num(w, 1)));
  } else if (k == "coupons") {
    heroAddPokecoupon(nullptr, static_cast<s32>(Num(w, 1)));
  } else if (k == "item") {
    const u16 id = static_cast<u16>(Num(w, 1));
    const long count = Num(w, 2, 1);
    if (!itemDataCheckValid(id) || count < 1 || count > 999) {
      sStatus = "no such item / count";
      return;
    }
    heroItemAddItemDataId(nullptr, id, static_cast<u32>(count), static_cast<u32>(-1));
  } else if (k == "pcitem") {
    const u16 id = static_cast<u16>(Num(w, 1));
    const long count = Num(w, 2, 1);
    if (!itemDataCheckValid(id) || count < 1 || count > 999) {
      sStatus = "pcitem: <item number> <count 1..999>";
      return;
    }
    pcboxAddItem(nullptr, id, static_cast<u16>(count));
    sStatus = "pcitem: " + std::to_string(count) + " x item " + std::to_string(id) + " into the PC item storage";
  } else if (k == "lead") {
    const long slot = Num(w, 1, 2);
    const uintptr_t a = heroGetStatus(nullptr, 3, 0);
    const uintptr_t b = slot >= 2 && slot <= 6 ? heroGetStatus(nullptr, 3, static_cast<u16>(slot - 1)) : 0;
    const uintptr_t c = heroGetStatus(nullptr, 3, 1);
    if (a == 0 || b == 0 || c <= a) {
      sStatus = "lead: <party slot 2-6>";
      return;
    }
    const size_t stride = c - a;
    std::vector<uint8_t> tmp(stride);
    std::memcpy(tmp.data(), reinterpret_cast<void*>(a), stride);
    std::memcpy(reinterpret_cast<void*>(a), reinterpret_cast<void*>(b), stride);
    std::memcpy(reinterpret_cast<void*>(b), tmp.data(), stride);
    sStatus = "lead: party slot " + std::to_string(slot) + " swapped with slot 1";
  } else if (k == "ability") {
    const long slot = Num(w, 1, 1);
    const long which = Num(w, 2, 0);
    void* p = slot >= 1 && slot <= 6 ? reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(slot - 1))) : nullptr;
    if (p == nullptr || which < 0 || which > 1) {
      sStatus = "ability: <party slot 1-6> <0|1>";
      return;
    }
    pokemonBiosSetTokuseiFlag(p, which != 0);
    sStatus = "ability: party slot " + std::to_string(slot) + " ability flag " + std::to_string(which);
  } else if (k == "kit") {
    dbgMenuDbgPokemonGet();
  } else if (k == "give") {
    const long species = Num(w, 1, 0);
    const long level = Num(w, 2, 5);
    if (species < 1 || species > 0x3FFF || level < 1 || level > 100) {
      sStatus = "give: <species number 1..16383> [level 1..100]";
      return;
    }
    alignas(16) static uint8_t pokemon[1024];
    std::memset(pokemon, 0, sizeof pokemon);
    const uintptr_t hero = savedataGetStatus(0, 2);
    pokemonCreate(pokemon, static_cast<u16>(species), static_cast<u8>(level), reinterpret_cast<void*>(gamedataGetStatus(nullptr, 1)));
    heroCatchPokemon(reinterpret_cast<void*>(hero), pokemon, 0, 4, 1);
    memoDataSet(0, pokemon);
    sStatus = "give: species " + std::to_string(species) + " level " + std::to_string(level);
  } else if (k == "hold") {
    const long slot = Num(w, 1, 1);
    const long item = Num(w, 2, 0);
    void* p = slot >= 1 && slot <= 6 ? reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(slot - 1))) : nullptr;
    if (p == nullptr || item < 0 || (item != 0 && !itemDataCheckValid(static_cast<u16>(item)))) {
      sStatus = "hold: <party slot 1-6> <item number>";
      return;
    }
    pokemonBiosSetItemDataId(p, static_cast<u16>(item));
    sStatus = "hold: party slot " + std::to_string(slot) + " holds item " + std::to_string(item);
  } else if (k == "zukan") {
    const long species = Num(w, 1, 0);
    const long caught = Num(w, 2, 1);
    if (species < 1 || species > 0x3FFF) {
      sStatus = "zukan: <species number> [caught 0/1]";
      return;
    }
    alignas(16) static uint8_t pokemon[1024];
    std::memset(pokemon, 0, sizeof pokemon);
    pokemonCreate(pokemon, static_cast<u16>(species), 5, reinterpret_cast<void*>(gamedataGetStatus(nullptr, 1)));
    pokemonSetOnZukanFlag(pokemon, static_cast<u8>(caught != 0));
    sStatus = "zukan: species " + std::to_string(species) + (caught != 0 ? " seen + caught" : " seen");
  } else if (k == "pcfill") {
    pcboxDebugPokemonSetupFull();
  } else if (k == "heart") {
    const long slot = Num(w, 1, 1);
    void* p = slot >= 1 && slot <= 6 ? reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(slot - 1)))
                                     : nullptr;
    if (p == nullptr || !pokemonIsDarkPokemon(p)) {
      sStatus = "heart: party slot " + std::to_string(slot) + " is not a Shadow Pokemon";
      return;
    }
    const f32 before = pokemonGetDp(p);
    pokemonSetDp(p, static_cast<f32>(Num(w, 2, 0)));
    sStatus = "heart: party slot " + std::to_string(slot) + " gauge " + std::to_string(static_cast<long>(before)) +
              " -> " + std::to_string(static_cast<long>(pokemonGetDp(p)));
  } else if (k == "save") {
    if (fightIsFight() != 0) {
      sStatus = "save: not during a battle";
      return;
    }
    if (GSfloorIsFloorPushed()) {
      sStatus = "save: not from this place (floor pushed)";
      return;
    }
    gamedatasaveSetStatus(nullptr, 5, floorGetNextFloorID());
    const s32 r = memcardTaskCall(4, 2, 0);
    sStatus = r == 4 ? "save: saved (memory card result 4)" : "save: FAILED, memory card result " + std::to_string(r);
  } else if (k == "shop") {
    const int shop = w.size() > 1 ? std::atoi(w[1].c_str()) : -1;
    if (shop < 0 || shop > 20) {
      sStatus = "shop: a number from 0 to 20 is needed";
      return;
    }
    menuShopOpen(shop);
  } else if (k == "screen") {
    const std::string name = w.size() > 1 ? w[1] : "";
    for (const Screen& s : kScreens) {
      if (name == s.name) {
        if (dbgMenuGetLayerID() < 0 || GSgfxLayerFindByID(dbgMenuGetLayerID()) == nullptr) {
          dbgMenuSetLayerID(GSgfxLayerFindByName_C("menu"));
        }
        if (dbgMenuGetLayerID() < 0) {
          sStatus = "no layer for the game's debug screens here";
          return;
        }
        s.func();
        return;
      }
    }
    sStatus = "no game screen " + name;
  } else {
    sStatus = "unknown action " + action;
  }
}

void ThreadEntry(void) {
  const std::string action = sRunning;
  Perform(action);
  if (sStatus.empty()) {
    sStatus = "done: " + action;
  }
  std::fprintf(stderr, "[dev menu] %s\n", sStatus.c_str());
  sThreadDone = true;
}

struct BattleWatch {
  bool on;
  uint32_t winAfter;
  uint32_t exitAfter;
  uint32_t start;
  uint32_t end;
  bool winAsked;
};
BattleWatch sWatch = {};

std::string PlaceName(u32 floorID);
std::string sTabRequest;
char sFilter[64] = "";

void Request(const std::string& action) {
  if (action == "show" || action == "hide") {
    sShown = action == "show";
    return;
  }
  if (action.rfind("tab ", 0) == 0) {
    sTabRequest = action.substr(4);
    return;
  }
  if (action.rfind("search", 0) == 0) {
    std::snprintf(sFilter, sizeof sFilter, "%s", action.size() > 7 ? action.c_str() + 7 : "");
    return;
  }
  if (action == "list story") {
    if (flag_etc == nullptr || flag_etc_number == nullptr) {
      std::fprintf(stderr, "[dev menu] list story: not loaded yet\n");
      return;
    }
    const FlagEtc* etc = static_cast<const FlagEtc*>(flag_etc);
    for (u32 i = 1; i < *flag_etc_number; i++) {
      if (etc[i].msg != 0) {
        std::fprintf(stderr, "[dev menu] story %u flag %u floor 0x%X partner %u place \"%s\" name \"%s\"\n", i,
                     etc[i].flagValue, etc[i].floor, etc[i].type, PlaceName(etc[i].floor).c_str(),
                     GameText(etc[i].msg).c_str());
        float pos[3];
        std::memcpy(&pos[0], &etc[i].x, 4);
        std::memcpy(&pos[1], &etc[i].y, 4);
        std::memcpy(&pos[2], &etc[i].z, 4);
        std::fprintf(stderr, "[dev menu] story %u position %.2f %.2f %.2f script 0x%08X\n", i, pos[0], pos[1],
                     pos[2], static_cast<unsigned>(etc[i].script));
      }
    }
    std::fprintf(stderr, "[dev menu] list story: end\n");
    return;
  }
  if (action == "list battles") {
    if (fight_encount_data_number == nullptr) {
      std::fprintf(stderr, "[dev menu] list battles: not loaded yet\n");
      return;
    }
    for (u32 i = 1; i < *fight_encount_data_number; i++) {
      FightEncountData* d = fightEncountDataBiosGetPtr(static_cast<u16>(i));
      const u32 kind = fightEncountDataBiosGetFightKind(d);
      const u32 type = fightEncountDataBiosGetFightType(d);
      std::fprintf(stderr,
                   "[dev menu] battle %u kind %u kindname \"%s\" type %u typename \"%s\" trainers %u pokemon %u "
                   "entry %u floor %u disk %u deck %u/%u trainer %u/%u name \"%s\"\n",
                   i, kind, GameText(fightKindDataBiosGetName(fightKindDataBiosGetPtr(static_cast<u16>(kind)))).c_str(),
                   type, GameText(fightTypeDataBiosGetName(fightTypeDataBiosGetPtr(static_cast<u16>(type)))).c_str(),
                   fightEncountDataBiosGetTrainer(d), fightEncountDataBiosGetPokemon(d), fightEncountDataBiosGetEntry(d),
                   fightEncountDataBiosGetFightFloorDataId(d), fightEncountDataBiosGetDiskId(d),
                   fightEncountDataBiosGetFightDeckGrpId(d, 0), fightEncountDataBiosGetFightDeckGrpId(d, 1),
                   fightEncountDataBiosGetFightTrainerDataId(d, 0), fightEncountDataBiosGetFightTrainerDataId(d, 1),
                   GameText(fightEncountDataBiosGetFightName(d)).c_str());
    }
    std::fprintf(stderr, "[dev menu] list battles: end\n");
    return;
  }
  if (action.rfind("battlewatch", 0) == 0) {
    const std::vector<std::string> w = Words(action);
    sWatch = {true, static_cast<uint32_t>(Num(w, 1)), static_cast<uint32_t>(Num(w, 2, 600)), 0, 0, false};
    std::fprintf(stderr, "[dev menu] battlewatch: win after %u frames, exit %u frames after the battle\n",
                 sWatch.winAfter, sWatch.exitAfter);
    return;
  }
  if (action == "status") {
    if (floordata == nullptr || flag_data_number == nullptr || GSfloorGetCurrent() == 0) {
      std::fprintf(stderr, "[dev menu] status game frame %u floor 0x0\n", PortVI_GetFlipCount());
      return;
    }
    std::fprintf(stderr, "[dev menu] status game frame %u floor 0x%X story %u battle %u floor_pushed %u busy %u\n",
                 PortVI_GetFlipCount(), GSfloorGetCurrent(), GSflagGet(0x3C4), fightIsFight(),
                 GSfloorIsFloorPushed() ? 1u : 0u, sThreadDone && sQueue.empty() ? 0u : 1u);
    return;
  }
  sQueue.push_back(action);
}

void StartNext() {
  const uint32_t frame = PortVI_GetFlipCount();
  if (!sThreadDone && sThread != nullptr && frame > sThreadStart + 1 && !GSthreadIsRunning(sThread)) {
    sThreadDone = true;
  }
  if (sQueue.empty() || !sThreadDone) {
    return;
  }
  if (GSthreadGetCurrentThread() != nullptr || floordata == nullptr || GSfloorGetCurrent() == 0) {
    return;
  }
  sRunning = sQueue.front();
  sQueue.erase(sQueue.begin());
  sStatus.clear();
  sThreadDone = false;
  sThreadStart = frame;
  sThread = GSthreadCreate(1, static_cast<u32>(GSfloorGetCurrentFloorGroupID()), 0x4000, 1, 1,
                           reinterpret_cast<void*>(&ThreadEntry));
  if (sThread == nullptr) {
    sThreadDone = true;
    sStatus = "the game has no free task for it";
    std::fprintf(stderr, "[dev menu] %s: %s\n", sRunning.c_str(), sStatus.c_str());
    return;
  }
  GSthreadSetName(sThread, "devMenu(port)");
  PortInput_DevMenuNote(sRunning.c_str());
  sUsed.push_back("game frame " + std::to_string(frame) + ": " + sRunning);
}

void WatchBattle() {
  if (!sWatch.on || floordata == nullptr || GSfloorGetCurrent() == 0) {
    return;
  }
  const uint32_t frame = PortVI_GetFlipCount();
  const bool inFight = fightIsFight() != 0;
  if (sWatch.start == 0) {
    if (inFight) {
      sWatch.start = frame;
      std::fprintf(stderr, "[dev menu] battlewatch: battle started at game frame %u\n", frame);
    }
    return;
  }
  if (sWatch.end == 0) {
    if (inFight) {
      if (sWatch.winAfter != 0 && !sWatch.winAsked && frame >= sWatch.start + sWatch.winAfter) {
        sWatch.winAsked = true;
        std::fprintf(stderr, "[dev menu] battlewatch: fight win at game frame %u\n", frame);
        sQueue.push_back("fight win");
      }
      return;
    }
    sWatch.end = frame;
    std::fprintf(stderr, "[dev menu] battlewatch: battle ended at game frame %u, result %u, %u frames\n", frame,
                 fightGetFightResultId() & 0xFFFFu, frame - sWatch.start);
    return;
  }
  if (frame >= sWatch.end + sWatch.exitAfter) {
    std::fprintf(stderr, "[dev menu] battlewatch: exit at game frame %u\n", frame);
    PortInput_StopRecording();
    std::fflush(stdout);
    std::fflush(stderr);
    PortLog_Flush();
    std::_Exit(0);
  }
}

void LoadSchedule() {
  const char* list = std::getenv("XD_DEV_ACTIONS");
  for (const char* p = list != nullptr ? list : ""; *p != '\0';) {
    const char* end = std::strchr(p, ',');
    const std::string item(p, end != nullptr ? static_cast<size_t>(end - p) : std::strlen(p));
    const size_t colon = item.find(':');
    if (colon != std::string::npos) {
      sScheduled.push_back({static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)), item.substr(colon + 1)});
    } else if (!item.empty()) {
      std::fprintf(stderr, "[dev menu] XD_DEV_ACTIONS: skipped '%s' (FRAME:ACTION)\n", item.c_str());
    }
    p = end != nullptr ? end + 1 : p + item.size();
  }
}

void CheckReplay() {
  sReplayChecked = true;
  const char* untilText = std::getenv("XD_REPLAY_UNTIL");
  const uint32_t until = untilText != nullptr && untilText[0] != '\0' ? std::strtoul(untilText, nullptr, 10) : UINT32_MAX;
  uint32_t frame = 0;
  size_t count = 0;
  for (size_t i = 0; const char* action = PortInput_ReplayDevMenuAt(i, &frame); i++) {
    if (frame >= until) {
      break;
    }
    count++;
    if (sEnabled) {
      sScheduled.push_back({frame, action});
    } else {
      std::fprintf(stderr, "[dev menu] the replayed recording used the developer menu at game frame %u (%s): "
                           "set XD_DEV_MENU=1 to do that again, else the replay goes its own way from there\n",
                   frame, action);
    }
  }
  if (count > 0 && sEnabled) {
    std::fprintf(stderr, "[dev menu] replaying %zu developer-menu actions from the recording\n", count);
  }
  std::stable_sort(sScheduled.begin(), sScheduled.end(),
                   [](const Scheduled& a, const Scheduled& b) { return a.frame < b.frame; });
}

bool Matches(const std::string& text) {
  if (sFilter[0] == '\0') return true;
  std::string a = text, b = sFilter;
  std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return a.find(b) != std::string::npos;
}

std::string PlaceName(u32 floorID) {
  if (floordata == nullptr || floordata_number == nullptr) return "";
  if (floorID == 0) return "(here)";
  const FloorData* floors = static_cast<const FloorData*>(floordata);
  for (s32 i = 0; i < *floordata_number; i++) {
    if (floors[i].floorID == floorID) {
      return GameText(floors[i].placeName);
    }
  }
  return "";
}

void StoryTab() {
  ImGui::TextWrapped("The developers' list of story points (the Flag screen): sets the story flag, partner, "
                     "money and map of that point. Story flag now: %u", GSflagGet(0x3C4));
  if (flag_etc == nullptr || flag_etc_number == nullptr) {
    ImGui::TextUnformatted("(not loaded yet)");
    return;
  }
  const FlagEtc* etc = static_cast<const FlagEtc*>(flag_etc);
  if (ImGui::BeginTable("story", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("go", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("flag", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("place");
    ImGui::TableSetupColumn("story point");
    for (u32 i = 1; i < *flag_etc_number; i++) {
      if (etc[i].msg == 0) continue;
      const std::string name = GameText(etc[i].msg);
      const std::string place = PlaceName(etc[i].floor);
      if (!Matches(name) && !Matches(place)) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::SmallButton("Go")) Request("story " + std::to_string(i));
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::Text("%u", i);
      ImGui::TableNextColumn();
      ImGui::Text("%u", etc[i].flagValue);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(place.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.empty() ? "?" : name.c_str());
    }
    ImGui::EndTable();
  }
}

void PlacesTab() {
  static int pos = 0;
  ImGui::TextWrapped("Every map (the Floor screen's warp). Current map: 0x%X. Entrance number:", GSfloorGetCurrent());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("##pos", &pos);
  if (floordata == nullptr || floordata_number == nullptr) {
    ImGui::TextUnformatted("(not loaded yet)");
    return;
  }
  const FloorData* floors = static_cast<const FloorData*>(floordata);
  if (ImGui::BeginTable("floors", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("go", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("map", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("place");
    for (s32 i = 0; i < *floordata_number; i++) {
      const FloorData& f = floors[i];
      if (f.floorID == 0) continue;
      const std::string name = GameText(f.placeName);
      char id[16];
      std::snprintf(id, sizeof id, "0x%X", f.floorID);
      if (!Matches(name) && !Matches(id)) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (ImGui::SmallButton("Go")) Request(std::string("warp ") + id + " " + std::to_string(pos));
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(id);
      ImGui::TableNextColumn();
      ImGui::Text("%u/%u", f.area, f.kind);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.empty() ? "-" : name.c_str());
    }
    ImGui::EndTable();
  }
}

void FlagsTab() {
  static int id = 0x3C4, value = 0;
  const u32 count = flag_data_number != nullptr ? *flag_data_number : 0;
  ImGui::TextWrapped("Any game flag (the Flag screen's editor). 0x3C4 (964) is the story progress. %u flags.", count);
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("flag", &id);
  id = std::clamp(id, 0, count > 0 ? static_cast<int>(count) - 1 : 0);
  ImGui::SameLine();
  ImGui::Text("= %u now", GSflagGet(static_cast<u32>(id)));
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("new value", &value);
  ImGui::SameLine();
  if (ImGui::Button("Set")) Request("flag " + std::to_string(id) + " " + std::to_string(value));
}

void BattleTab() {
  const bool inFight = fightIsFight() != 0;
  ImGui::TextWrapped(inFight ? "In a battle now. End it (the game ends it when this turn is over: pick any move):"
                             : "Not in a battle.");
  ImGui::BeginDisabled(!inFight);
  if (ImGui::Button("Win")) Request("fight win");
  ImGui::SameLine();
  if (ImGui::Button("Lose")) Request("fight lose");
  ImGui::SameLine();
  if (ImGui::Button("Time out")) Request("fight timeout");
  ImGui::EndDisabled();
  ImGui::Separator();
  ImGui::TextUnformatted("Start a battle (fight_encount_data, the Fight screen's list):");
  if (fight_encount_data_number == nullptr) {
    ImGui::TextUnformatted("(not loaded yet)");
    return;
  }
  ImGui::BeginDisabled(inFight);
  if (ImGui::BeginTable("battles", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("go", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("battle");
    for (u32 i = 1; i < *fight_encount_data_number; i++) {
      const std::string name = GameText(fightEncountDataBiosGetFightName(fightEncountDataBiosGetPtr(static_cast<u16>(i))));
      if (!Matches(name) && !Matches(std::to_string(i))) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::SmallButton("Go")) Request("battle " + std::to_string(i));
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::Text("%u", i);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.empty() ? "-" : name.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::EndDisabled();
}

void ItemsTab() {
  static int money = 10000, coupons = 1000, count = 1;
  if (ImGui::Button("Developers' kit (Pokemon and items)")) Request("kit");
  ImGui::SameLine();
  if (ImGui::Button("Save to the memory card")) Request("save");
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("##money", &money);
  ImGui::SameLine();
  if (ImGui::Button("Add money")) Request("money " + std::to_string(money));
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("##coupons", &coupons);
  ImGui::SameLine();
  if (ImGui::Button("Add coupons")) Request("coupons " + std::to_string(coupons));
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("count", &count);
  count = std::clamp(count, 1, 999);
  if (ImGui::BeginTable("items", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("get", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("item");
    for (int i = 1; i < 0x400; i++) {
      if (!itemDataCheckValid(static_cast<u16>(i))) continue;
      const std::string name = GameText(itemDataBiosGetName(itemDataBiosGetPtr(static_cast<u16>(i))));
      if (name.empty() || !Matches(name)) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (ImGui::SmallButton("Get")) Request("item " + std::to_string(i) + " " + std::to_string(count));
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::Text("%d", i);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.c_str());
    }
    ImGui::EndTable();
  }
}

void ScreensTab() {
  ImGui::TextWrapped("Experimental: the game's own debug screens, as the developers had them. They open in "
                     "the game and take the controller (B goes back), but the retail game lacks their texts, "
                     "so most windows are empty and some close at once. The other tabs do the same jobs.");
  for (const Screen& s : kScreens) {
    if (!Matches(s.label) && !Matches(s.name)) continue;
    if (ImGui::Button(s.label)) Request(std::string("screen ") + s.name);
  }
}

void DrawWindow() {
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Developer menu (cheats) - F9", &sShown)) {
    ImGui::End();
    return;
  }
  ImGui::Text("map 0x%X  story %u  %s", GSfloorGetCurrent(), GSflagGet(0x3C4), fightIsFight() ? "battle" : "");
  if (!sThreadDone) {
    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "running: %s", sRunning.c_str());
  } else if (!sStatus.empty()) {
    ImGui::TextWrapped("%s", sStatus.c_str());
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "search", sFilter, sizeof sFilter);
  if (ImGui::BeginTabBar("tabs")) {
    static const struct {
      const char* name;
      void (*draw)();
    } kTabs[] = {{"Story", StoryTab},   {"Places", PlacesTab}, {"Flags", FlagsTab},
                 {"Battle", BattleTab}, {"Items", ItemsTab},   {"Game screens", ScreensTab}};
    for (const auto& tab : kTabs) {
      const ImGuiTabItemFlags flags = sTabRequest == tab.name ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(tab.name, nullptr, flags)) {
        tab.draw();
        ImGui::EndTabItem();
      }
    }
    sTabRequest.clear();
    ImGui::EndTabBar();
  }
  ImGui::End();
}

}

extern "C" {

bool PortXD_DevMenuInit(void) {
  sEnabled = engine::settings::Global().Bool("system.dev_menu").Get();
  if (sEnabled) {
    LoadSchedule();
    std::fprintf(stderr, "[dev menu] on: F9 in the game window opens it (cheats; runs are marked as such)\n");
  }
  return sEnabled;
}

void PortXD_DevMenuToggle(void) {
  if (sEnabled) {
    sShown = !sShown;
  }
}

void PortXD_DevMenuFrame(void) {
  if (!sReplayChecked) {
    CheckReplay();
  }
  if (!sEnabled) {
    return;
  }
  const uint32_t frame = PortVI_GetFlipCount();
  while (sNextScheduled < sScheduled.size() && sScheduled[sNextScheduled].frame <= frame) {
    Request(sScheduled[sNextScheduled++].action);
  }
  WatchBattle();
  StartNext();
  if (sShown && ImGui::GetCurrentContext() != nullptr) {
    DrawWindow();
  }
}

const char* PortXD_DevMenuReport(void) {
  static std::string text;
  if (!sEnabled) {
    return "";
  }
  text = sUsed.empty() ? "developer menu: on, not used\n"
                       : "developer menu: USED (cheats), " + std::to_string(sUsed.size()) +
                             (sUsed.size() == 1 ? " action:\n" : " actions:\n");
  for (const std::string& line : sUsed) {
    text += "  " + line + "\n";
  }
  return text.c_str();
}

}
