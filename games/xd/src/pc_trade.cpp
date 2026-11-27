#include "xd_pc_trade.h"
#include "xd_pc_boxes.h"
#include "i18n.h"

using engine::i18n::Tr;

#include <xd_save.h>
#include <xd_save_convert.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <game/winwork.h>

struct Pokemon;
enum darkPokemonWhere { place0, place1, place2, place3, place4, place5, place6 };
struct DarkPokemon {
  void setPlace(darkPokemonWhere, unsigned char);
  bool isRelive() const;
};
struct PCBOX {
  static PCBOX* getPointer();
  bool setPokemon(Pokemon*, signed char, signed char);
  Pokemon* getPokemon(signed char, signed char);
};
struct CReliveHall {
  static bool isOpen();
};
struct PcEvoInfo {
  uint8_t kind;
  uint8_t reserved;
  uint16_t parameter;
};
extern "C" {
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
uintptr_t savedataGetStatus(void* save, int32_t kind);
DarkPokemon* darkPokemonGetDarkPokemon(uint32_t id);
void pokemonBiosCopy(void* dst, const void* src);
void pokemonBiosSetFriend(void* pokemon, uint16_t value);
void memoDataSet(int32_t kind, void* pokemon);
uint16_t pokemonEvolutionCheck(void* pokemon, int32_t mode, uint16_t item, uint16_t* extra, PcEvoInfo* info);
int32_t PortXD_FieldEvolve(void* pokemon, int32_t kind, int32_t item);
int32_t PortXD_SaveForced(void);
int PortXD_CheatsUsedHere(void);
void PortXD_CheatsNoteReceived(const char* what);
int32_t pokemonEvolutionAll(void* pokemon, uint16_t species, uint16_t extra, PcEvoInfo* info, void* hero, int writeMemo, int mode, int fade);
void GSthreadSwitch(void);
void* windowSearchID(int32_t id);
void* GSthreadGetCurrentThread(void);
bool GSfloorIsFloorPushed(void);
void* GSthreadCreate(uint8_t affinity, uint32_t group, uint32_t stackSize, uint8_t usesFPU, uint8_t autoStart, void* entry);
int32_t GSfloorGetCurrentFloorGroupID(void);
void heroMoveGetResID(uint32_t* group, uint32_t* id, int32_t member);
int32_t heroMoveGetLeader(void);
void* GSresGetResource(uint32_t group, uint32_t id);
uint16_t* GSmsgGetGSchar(uint32_t key);
void* menuDataBiosGetPtr(int32_t menuID);
void* PortXD_RealMenuItemPtr(int32_t menuID, uint32_t index);
void winSpriteSetDisp(void* sprite, bool on);
int32_t windowGetValue(int32_t menuID);
void menuOpen(int32_t menuID, uint8_t check);
void menuClose(int32_t menuID);
void menuCloseSync(int32_t menuID, int32_t flag);
uint32_t floorGetNextFloorID(void);
void gamedatasaveSetStatus(void* save, int32_t kind, uint32_t value);
int32_t memcardTaskCall(int32_t kind, int32_t type, int32_t arg);
bool PortMenu_Active(void);
bool PortMenu_Pending(void);
}

namespace {

constexpr int32_t kRowMsg = 0x7D0FFE00;
constexpr size_t kRec = xd::save::kPokemonSize;
constexpr int kPartySlots = 6, kBoxSlots = 30;
int BoxCount() { return xd::pcboxes::Count(); }

using xd::trade::Bytes;

uint8_t* PartyPtr(int i) { return reinterpret_cast<uint8_t*>(heroGetStatus(nullptr, 3, static_cast<uint16_t>(i))); }
uint8_t* BoxPtr(int b, int s) {
  PCBOX* pc = PCBOX::getPointer();
  return pc != nullptr ? reinterpret_cast<uint8_t*>(pc->getPokemon(static_cast<signed char>(b), static_cast<signed char>(s))) : nullptr;
}
bool IsEmptyRec(const uint8_t* p) { return p == nullptr || (p[0] == 0 && p[1] == 0); }

Bytes WireOf(const uint8_t* live) {
  Bytes w(live, live + kRec);
  xd::save::SwapPokemonRecord(w.data());
  uint8_t state = 0;
  const xd::save::Pokemon p = xd::save::ParsePokemon(live, xd::save::ByteOrder::PC);
  if (p.shadowId != 0 && p.shadowId < xd::save::kShadowEntries) {
    DarkPokemon* d = darkPokemonGetDarkPokemon(p.shadowId);
    state = d != nullptr && d->isRelive() ? 1 : 2;
  }
  w.push_back(state);
  w.push_back(PortXD_CheatsUsedHere() != 0 ? 1 : 0);
  return w;
}

struct Place {
  int box = -2;
  int slot = -1;
  bool valid() const { return box != -2; }
};
uint8_t* At(const Place& pl) { return pl.box == -1 ? PartyPtr(pl.slot) : BoxPtr(pl.box, pl.slot); }

struct Staged {
  bool committed = false;
  Place place;
  Bytes give, get;
};
Staged gStaged;

class LiveTrader : public xd::trade::Trader {
 public:
  explicit LiveTrader(const xd::trade::Config& cfg) : Trader(cfg) {
    const uint8_t* hero = reinterpret_cast<const uint8_t*>(savedataGetStatus(nullptr, 2));
    trainer_ = hero != nullptr ? xd::save::ToUtf8(xd::save::ReadString(hero, 10, xd::save::ByteOrder::PC)) : std::string("your game");
    if (trainer_.empty()) trainer_ = "your game";
    gStaged = Staged{};
  }
  const std::string& trainerName() const override { return trainer_; }

  std::vector<xd::trade::Entry> List() const override {
    std::vector<xd::trade::Entry> out;
    if (cfg_.rules == nullptr) return out;
    auto add = [&](int box, int slot, const uint8_t* live) {
      if (IsEmptyRec(live)) return;
      xd::trade::Entry e;
      e.box = box;
      e.slot = slot;
      e.pokemon = xd::save::ParsePokemon(live, xd::save::ByteOrder::PC);
      e.wire = WireOf(live);
      xd::trade::Card c = Describe(e.pokemon);
      e.card = c;
      const xd::trade::Verdict v = JudgeFor(e.pokemon, static_cast<xd::trade::ShadowState>(e.wire[kRec]));
      e.ok = v.ok;
      e.reason = v.text;
      out.push_back(std::move(e));
    };
    for (int i = 0; i < kPartySlots; i++) add(-1, i, PartyPtr(i));
    for (int b = 0; b < BoxCount(); b++)
      for (int s = 0; s < kBoxSlots; s++) add(b, s, BoxPtr(b, s));
    return out;
  }

  xd::trade::Verdict Prepare(const Bytes& give, const Bytes& get) override {
    gStaged = Staged{};
    result_ = xd::trade::Result{};
    if (cfg_.rules == nullptr) return {false, "The game's tables are not ready."};
    if (const xd::trade::Verdict v = CheckIncoming(get); !v.ok) return v;
    xd::save::Pokemon incoming;
    xd::trade::ShadowState sh;
    Parse(get, incoming, sh);
    Place at;
    for (int i = 0; i < kPartySlots && !at.valid(); i++) {
      const uint8_t* p = PartyPtr(i);
      if (!IsEmptyRec(p) && WireOf(p) == give) at = {-1, i};
    }
    for (int b = 0; b < BoxCount() && !at.valid(); b++)
      for (int s = 0; s < kBoxSlots && !at.valid(); s++) {
        const uint8_t* p = BoxPtr(b, s);
        if (!IsEmptyRec(p) && WireOf(p) == give) at = {b, s};
      }
    if (!at.valid()) return {false, "The Pokemon you offered is no longer in your game."};
    bool able = false;
    for (int i = 0; i < kPartySlots; i++) {
      const uint8_t* p = PartyPtr(i);
      if (IsEmptyRec(p)) continue;
      if (at.box == -1 && at.slot == i) {
        if (incoming.hp != 0) able = true;
      } else if (xd::save::ParsePokemon(p, xd::save::ByteOrder::PC).hp != 0) {
        able = true;
      }
    }
    if (!able) return {false, "After this trade you would have no Pokemon in your party that can battle."};
    result_.received = Name(incoming.species);
    result_.receivedFromCheats = get.size() > kRec + 1 && get[kRec + 1] != 0;
    result_.where = at.box == -1 ? "party slot " + std::to_string(at.slot + 1)
                                 : "PC box " + std::to_string(at.box + 1) + ", place " + std::to_string(at.slot + 1);
    uint8_t kind = 0;
    xd::save::Pokemon asReceived = incoming;
    const uint16_t to = TradeEvolution(*cfg_.rules, asReceived, kind);
    if (to != 0 && to < cfg_.rules->species.size() && cfg_.rules->species[to].exists) {
      result_.evolved = true;
      result_.evolvedFrom = Name(incoming.species);
      result_.evolvedInto = Name(to);
    }
    staged_ = {false, at, give, get};
    return {true, ""};
  }

  xd::trade::Verdict Commit(const std::function<bool(const Bytes&, std::string&)>&) override {
    if (!staged_.place.valid()) return {false, "Nothing is prepared."};
    gStaged = staged_;
    gStaged.committed = true;
    return {true, ""};
  }

 private:
  std::string trainer_;
  Staged staged_;
};

bool ApplyStaged(std::string& error) {
  const Staged s = gStaged;
  gStaged = Staged{};
  uint8_t* mine = At(s.place);
  if (IsEmptyRec(mine) || WireOf(mine) != s.give) {
    error = Tr("The Pokemon you gave is no longer where it was, so nothing was changed.");
    return false;
  }
  alignas(16) uint8_t incoming[0xD0] = {};
  std::memcpy(incoming, s.get.data(), kRec);
  xd::save::SwapPokemonRecord(incoming);
  incoming[0xBA] = incoming[0xBB] = 0;
  uint16_t sid;
  std::memcpy(&sid, mine + 0xBA, 2);
  if (sid != 0 && sid < xd::save::kShadowEntries) {
    if (DarkPokemon* d = darkPokemonGetDarkPokemon(sid)) d->setPlace(place5, 0);
  }
  if (s.place.box == -1) {
    pokemonBiosCopy(mine, incoming);
  } else if (!PCBOX::getPointer()->setPokemon(reinterpret_cast<Pokemon*>(incoming), static_cast<signed char>(s.place.box),
                                               static_cast<signed char>(s.place.slot))) {
    error = Tr("The PC box would not take the Pokemon.");
    return false;
  }
  uint8_t* got = At(s.place);
  if (s.get.size() > kRec + 1 && s.get[kRec + 1] != 0) PortXD_CheatsNoteReceived("received a Pokemon from a save with cheats");
  pokemonBiosSetFriend(got, 0x46);
  memoDataSet(0, got);
  return true;
}

bool Evolve(const Place& pl) {
  uint8_t* p = At(pl);
  if (p == nullptr) return false;
  uint16_t extra = 0;
  PcEvoInfo info{};
  const uint16_t evo = pokemonEvolutionCheck(p, 2, 0, &extra, &info);
  if (evo == 0 || evo == 0xFFFF) return false;
  PortXD_FieldEvolve(p, 2, 0);
  return true;
}

bool ForcedSave(int32_t* resultOut) {
  const int32_t r = PortXD_SaveForced();
  if (resultOut != nullptr) *resultOut = r;
  return r == 4;
}

void SaveAndSay() {
  int32_t r = 0;
  if (!ForcedSave(&r)) {
    std::fprintf(stderr, "[pc trade] SAVE FAILED, memory card result %d\n", r);
    PortXD_ShowRuleMessage(Tr("The trade is done in your game, but the save FAILED. Your game was not saved: save it yourself before "
                              "you turn the game off. The copy of your save from before the trade is kept."));
    while (PortMenu_Pending() || PortMenu_Active()) GSthreadSwitch();
  } else {
    std::fprintf(stderr, "[pc trade] saved (memory card result 4)\n");
  }
}

Place sAfterPlace;
bool sAfterWanted = false;
bool sAfterRunning = false;
int sAfterQuiet = 0;
}
bool PortXD_PcTradeBusy(void) { return sAfterWanted || sAfterRunning; }
namespace {
void AfterThread(void) {
  for (int i = 0; i < 5; i++) GSthreadSwitch();
  const bool evolved = Evolve(sAfterPlace);
  std::fprintf(stderr, "[pc trade] evolution scene %s\n", evolved ? "run" : "not needed");
  SaveAndSay();
  sAfterRunning = false;
}

struct MenuDataView {
  uint8_t type, unk1[2], dispMax;
  uint8_t unk4[2];
  uint16_t itemIndex;
};
struct Item {
  uint8_t flags, flags2, unk2, unk3;
  uint16_t imageId;
  int16_t x, y, w, h;
  int16_t spriteId;
  uint32_t msgId;
  void* hook;
  int16_t next;
  int16_t unk1A;
};
static_assert(sizeof(Item) == 0x28, "the item table's entry");

constexpr int kExtBase = 31000, kExtCount = 9;
constexpr int kEdgeL = 887, kEdgeR = 888, kLast = 903, kFrame4 = 862, kRed4 = 866, kPlate0 = 867, kCursor4 = 899;
Item sExt[kExtCount];
Item sOrig[3];
bool sLayoutKnown = false, sFive = false;
bool sArmed = false;
uint16_t sOrigIndex7A = 0;
uint8_t sOrigDisp7A = 0;
bool sSaved7A = false;

Item* RealItem(int idx) { return static_cast<Item*>(PortXD_RealMenuItemPtr(0x7B, static_cast<uint32_t>(idx))); }

int32_t RedBar4(void* w, void* sprite) {
  winSpriteSetDisp(sprite, windowGetValue(static_cast<WinWorkLayout*>(w)->menuID) == 4);
  return 0;
}
int32_t Text4(void*, void* sprite) {
  static_cast<SpriteWorkLayout*>(sprite)->msg = 0x3BB4;
  return 0;
}

void LayoutFiveRows(bool five) {
  if (!sLayoutKnown) {
    sOrig[0] = *RealItem(kEdgeL), sOrig[1] = *RealItem(kEdgeR), sOrig[2] = *RealItem(kLast);
    sLayoutKnown = true;
  }
  *RealItem(kEdgeL) = sOrig[0], *RealItem(kEdgeR) = sOrig[1], *RealItem(kLast) = sOrig[2];
  sFive = five;
  if (!five) return;
  constexpr int16_t kStep = 48;
  int n = 0;
  auto add = [&](const Item& src, int dy) -> Item& {
    Item& e = sExt[n];
    e = src;
    e.y = static_cast<int16_t>(e.y + dy);
    e.flags &= static_cast<uint8_t>(~0x40);
    e.next = static_cast<int16_t>(kExtBase + n + 1);
    n++;
    return e;
  };
  add(*RealItem(kFrame4), kStep);
  add(*RealItem(kRed4), kStep).hook = reinterpret_cast<void*>(&RedBar4);
  for (int i = 0; i < 5; i++) add(*RealItem(kPlate0 + i), kStep);
  add(*RealItem(kCursor4), kStep);
  Item& text = add(sOrig[2], kStep);
  text.hook = reinterpret_cast<void*>(&Text4);
  text.flags |= 0x40;
  text.next = sOrig[2].next;
  RealItem(kLast)->flags &= static_cast<uint8_t>(~0x40);
  RealItem(kLast)->next = static_cast<int16_t>(kExtBase);
  RealItem(kEdgeL)->h = static_cast<int16_t>(RealItem(kEdgeL)->h + kStep);
  RealItem(kEdgeR)->h = static_cast<int16_t>(RealItem(kEdgeR)->h + kStep);
}

void PointWindow7A(bool at7B) {
  auto* a = static_cast<MenuDataView*>(menuDataBiosGetPtr(0x7A));
  auto* b = static_cast<MenuDataView*>(menuDataBiosGetPtr(0x7B));
  if (a == nullptr || b == nullptr) return;
  if (!sSaved7A) {
    sOrigIndex7A = a->itemIndex;
    sOrigDisp7A = a->dispMax;
    sSaved7A = true;
    std::fprintf(stderr, "[pc trade] PC menu data: window 0x7A items from %u (disp %u), window 0x7B from %u (disp %u)\n", a->itemIndex,
                 a->dispMax, b->itemIndex, b->dispMax);
  }
  a->itemIndex = at7B ? b->itemIndex : sOrigIndex7A;
  a->dispMax = at7B ? b->dispMax : sOrigDisp7A;
}

}

std::string PortXD_LiveMessage(uint32_t key) {
  const uint16_t* t = key != 0 ? GSmsgGetGSchar(key) : nullptr;
  std::u16string s;
  for (int n = 0; t != nullptr && n < 256 && t[n] != 0 && t[n] != 0xFFFF; n++) s.push_back(static_cast<char16_t>(t[n]));
  return xd::save::ToUtf8(s);
}

std::unique_ptr<xd::trade::Trader> PortXD_MakeLiveTrader(const xd::trade::Config& cfg) { return std::make_unique<LiveTrader>(cfg); }

extern "C" {

int32_t PortXD_PcMenuExtraRow(void) {
  sArmed = PortXD_PcTradeAvailable();
  return sArmed ? kRowMsg : 0;
}

int32_t menuOpenCustom(int32_t menu, int32_t parent, int32_t* cursor, uint32_t close, uint8_t check, int32_t param, ...);
int32_t PortXD_PcMenuOpenCustom(int32_t menu, int32_t parent, int32_t* cursor, uint32_t close, uint8_t check, int32_t param) {
  if (menu == 0x7A || menu == 0x7B) {
    const bool extra = sArmed;
    sArmed = false;
    if (menu == 0x7A) {
      LayoutFiveRows(false);
      PointWindow7A(extra);
    } else {
      LayoutFiveRows(extra && CReliveHall::isOpen());
    }
  }
  return menuOpenCustom(menu, parent, cursor, close, check, param);
}

void* PortXD_PcExtItem(uint32_t index) {
  if (sFive && index >= static_cast<uint32_t>(kExtBase) && index < static_cast<uint32_t>(kExtBase + kExtCount)) return &sExt[index - kExtBase];
  return nullptr;
}

int32_t PortXD_PcMenuExtraSelected(int32_t msgId) {
  if (msgId != kRowMsg) return 0;
  gStaged = Staged{};
  if (!PortXD_OpenOnlineTradeLive()) return 0;
  while (PortMenu_Pending() || PortMenu_Active()) GSthreadSwitch();
  if (!gStaged.committed) return 0;
  const Place pl = gStaged.place;
  std::string error;
  if (!ApplyStaged(error)) {
    std::fprintf(stderr, "[pc trade] %s\n", error.c_str());
    PortXD_ShowRuleMessage(error);
    while (PortMenu_Pending() || PortMenu_Active()) GSthreadSwitch();
    return 0;
  }
  std::fprintf(stderr, "[pc trade] swapped (%s %d/%d); evolution check\n", pl.box == -1 ? "party" : "box", pl.box, pl.slot);
  sAfterPlace = pl;
  sAfterWanted = true;
  sAfterQuiet = 0;
  return 1;
}

void PortXD_PcTradeFrame(void) {
  static int sWaited = 0;
  if (!sAfterWanted) { sWaited = 0; return; }
  if (sAfterRunning) return;
  if (++sWaited > 3600) {
    sAfterWanted = false;
    std::fprintf(stderr, "[pc trade] the PC never closed: evolution and save skipped\n");
    return;
  }
  if (GSthreadGetCurrentThread() != nullptr || windowSearchID(0x79) != nullptr || GSfloorIsFloorPushed()) { sAfterQuiet = 0; return; }
  if (++sAfterQuiet < 20) return;
  sAfterWanted = false;
  sAfterRunning = true;
  if (GSthreadCreate(1, static_cast<uint32_t>(GSfloorGetCurrentFloorGroupID()), 0x4000, 1, 1, reinterpret_cast<void*>(&AfterThread)) == nullptr) {
    sAfterRunning = false;
    std::fprintf(stderr, "[pc trade] no game thread for the evolution and the save\n");
  }
}

}

bool PortXD_PcTradeSwallowInput() { return sAfterWanted; }

std::vector<PortXD_Owned> PortXD_OwnedOf(uint16_t species) {
  std::vector<PortXD_Owned> out;
  if (species == 0) return out;
  auto take = [&](int box, int slot, const uint8_t* live) {
    if (IsEmptyRec(live)) return;
    const xd::save::Pokemon p = xd::save::ParsePokemon(live, xd::save::ByteOrder::PC);
    if (p.species != species || p.IsEgg()) return;
    PortXD_Owned o;
    o.nickname = xd::save::ToUtf8(p.nickname);
    o.level = p.level, o.box = box, o.slot = slot, o.nature = p.Nature(), o.stats = p.stats, o.evs = p.evs, o.ivs = p.ivs;
    out.push_back(std::move(o));
  };
  if (savedataGetStatus(nullptr, 2) == 0) return out;
  for (int i = 0; i < kPartySlots; i++) take(-1, i, PartyPtr(i));
  for (int b = 0; b < BoxCount(); b++)
    for (int s = 0; s < kBoxSlots; s++) take(b, s, BoxPtr(b, s));
  return out;
}
