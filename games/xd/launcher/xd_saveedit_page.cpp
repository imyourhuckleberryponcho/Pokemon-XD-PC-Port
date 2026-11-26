#include "xd_saveedit_page.h"

#include "xd_playthrough_files.h"

#include <i18n.h>
#include <playthrough.h>
#include <xd_legality_disc.h>
#include <xd_legality_tables.h>
#include <xd_save_region.h>
#include <xd_saveedit.h>
#include <xd_saveedit_write.h>

#include <gc_memcard.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>

namespace xd::saveeditpage {

namespace fs = std::filesystem;
namespace xs = xd::save;
namespace se = xd::saveedit;
using engine::i18n::Tr;
using launcher::GameInfo;
using EditRow = GameInfo::Saves::EditRow;
using EditSection = GameInfo::Saves::EditSection;
using EditTab = GameInfo::Saves::EditTab;
using EditView = GameInfo::Saves::EditView;
using Kind = GameInfo::Saves::EditRow::Kind;

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

std::u16string FromUtf8(const std::string& s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xF0 && i + 3 < s.size()) {
      cp = (c & 7u) << 18 | (s[i + 1] & 0x3Fu) << 12 | (s[i + 2] & 0x3Fu) << 6 | (s[i + 3] & 0x3Fu);
      n = 4;
    } else if (c >= 0xE0 && i + 2 < s.size()) {
      cp = (c & 15u) << 12 | (s[i + 1] & 0x3Fu) << 6 | (s[i + 2] & 0x3Fu);
      n = 3;
    } else if (c >= 0xC0 && i + 1 < s.size()) {
      cp = (c & 31u) << 6 | (s[i + 1] & 0x3Fu);
      n = 2;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out += static_cast<char16_t>(0xD800 + (cp >> 10));
      out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
    } else {
      out += static_cast<char16_t>(cp);
    }
    i += n;
  }
  return out;
}

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool ReadBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

using Choices = std::vector<std::pair<std::string, std::string>>;

struct Session {
  fs::path cardRoot, gci, setupFolder;
  std::shared_ptr<xd::legality::TablesFile> rules;
  bool fromFile = false;
  se::Tables tables;
  se::Editor ed;
  std::string key;
  bool readOnly = false;
  std::string readOnlyWhy;
  se::Spot sel{se::Area::Party, 0, 0};
  int box = 0, pocket = 0;
  bool moving = false;
  se::Spot moveFrom;
  bool learnOnly = true;
  int addSpecies = 0, addLevel = 5, bagItem = 0, bagCount = 1;
  bool addParty = false;
  bool careful = false;
  bool japaneseText = false;
  int keyItem = 0, memoAdd = 0, memoRem = 0;
  Choices speciesList, itemList, natureList;
};

std::unique_ptr<Session> sSession;
std::mutex sMutex;
std::shared_ptr<xd::legality::TablesFile> sRules;
std::string sRulesDisc;
std::function<fs::path(const fs::path&)> sSaveFileOf;
std::function<bool(const fs::path&)> sCardFree;

std::shared_ptr<xd::legality::TablesFile> RulesFor(const std::string& disc, std::string& error) {
  const std::lock_guard<std::mutex> lock(sMutex);
  if (sRules && sRulesDisc == disc) return sRules;
  if (disc.empty()) {
    error = Tr("Choose your Pokemon XD disc on the Disc page first: the editor takes the game's lists and names from it.");
    return nullptr;
  }
  auto r = std::make_shared<xd::legality::DiscRules>();
  if (!xd::legality::LoadRulesFromDisc(disc, *r, error)) {
    error = Fmt(Tr("The disc can't be read: %s"), error.c_str());
    return nullptr;
  }
  sRules = std::make_shared<xd::legality::TablesFile>(xd::legality::TablesOf(*r, ""));
  sRulesDisc = disc;
  return sRules;
}

std::string SpeciesName(const Session& s, uint16_t id) {
  const std::string n = s.rules->Species(id);
  return n.empty() ? Fmt(Tr("Pokemon #%d"), id) : n;
}

std::string PlayTimeText(double seconds) {
  const long total = static_cast<long>(seconds);
  return Fmt(Tr("%ld hours %02ld minutes"), total / 3600, total / 60 % 60);
}

std::set<uint16_t> Learnable(const Session& s, uint16_t species) {
  std::set<uint16_t> out;
  const auto& r = s.rules->rules;
  for (int depth = 0; depth < 4 && species != 0 && species < r.species.size(); depth++) {
    const auto& sp = r.species[species];
    for (const auto& lm : sp.levelUp) out.insert(lm.move);
    for (uint16_t m : sp.eggMoves) out.insert(m);
    for (size_t i = 0; i < sp.machine.size(); i++) {
      if (sp.machine[i] && r.machineMove[i] != 0) out.insert(r.machineMove[i]);
    }
    for (size_t i = 0; i < sp.tutor.size(); i++) {
      if (sp.tutor[i] && r.tutorMove[i] != 0) out.insert(r.tutorMove[i]);
    }
    for (const auto& g : r.giftMoves) {
      if (g.first == species) out.insert(g.second);
    }
    species = sp.evolvesFrom;
  }
  out.erase(0);
  return out;
}

std::string Spot(const se::Spot& s) { return s.area == se::Area::Party ? "party." + std::to_string(s.slot) : "box." + std::to_string(s.box) + "." + std::to_string(s.slot); }

bool ParseSpot(const std::string& text, se::Spot& out) {
  int a = 0, b = 0;
  if (std::sscanf(text.c_str(), "party.%d", &a) == 1 && text.rfind("party.", 0) == 0) {
    out = {se::Area::Party, 0, a};
    return true;
  }
  if (std::sscanf(text.c_str(), "box.%d.%d", &a, &b) == 2) {
    out = {se::Area::Box, a, b};
    return true;
  }
  return false;
}

const xs::Pokemon* OrigAt(const Session& s, const se::Spot& sp) {
  const xs::Save& o = s.ed.originalSave();
  if (sp.area == se::Area::Party) return sp.slot >= 0 && sp.slot < xs::kPartySize ? &o.party[static_cast<size_t>(sp.slot)] : nullptr;
  return sp.box >= 0 && sp.box < xs::kBoxCount && sp.slot >= 0 && sp.slot < xs::kBoxSlots ? &o.boxes[static_cast<size_t>(sp.box)].slots[static_cast<size_t>(sp.slot)] : nullptr;
}

EditRow Info(const std::string& label, const std::string& value) {
  EditRow r;
  r.kind = Kind::Info;
  r.label = label;
  r.value = value;
  return r;
}
EditRow Num(const std::string& id, const std::string& label, long value, long mn, long mx, bool changed = false) {
  EditRow r;
  r.kind = Kind::Number;
  r.id = id;
  r.label = label;
  r.value = std::to_string(value);
  r.min = mn;
  r.max = mx;
  r.changed = changed;
  return r;
}
EditRow Txt(const std::string& id, const std::string& label, const std::string& value, int maxLen, bool changed = false) {
  EditRow r;
  r.kind = Kind::Text;
  r.id = id;
  r.label = label;
  r.value = value;
  r.maxLen = maxLen;
  r.changed = changed;
  return r;
}
EditRow Btn(const std::string& id, const std::string& label, const std::string& text) {
  EditRow r;
  r.kind = Kind::Button;
  r.id = id;
  r.label = label;
  r.value = text;
  return r;
}
EditRow Tog(const std::string& id, const std::string& label, bool on, bool changed = false) {
  EditRow r;
  r.kind = Kind::Toggle;
  r.id = id;
  r.label = label;
  r.value = on ? "1" : "0";
  r.changed = changed;
  return r;
}
EditRow Pick(const std::string& id, const std::string& label, const std::string& text, bool selected, bool changed) {
  EditRow r;
  r.kind = Kind::Pick;
  r.id = id;
  r.label = label;
  r.value = text;
  r.selected = selected;
  r.changed = changed;
  return r;
}
std::string NatureName(const Session& s, uint8_t id) {
  static const char* const kStandard[25] = {"Hardy",   "Lonely", "Brave",  "Adamant", "Naughty", "Bold",    "Docile",  "Relaxed", "Impish",
                                            "Lax",     "Timid",  "Hasty",  "Serious", "Jolly",   "Naive",   "Modest",  "Mild",    "Quiet",
                                            "Bashful", "Rash",   "Calm",   "Gentle",  "Sassy",   "Careful", "Quirky"};
  const std::string n = s.rules ? s.rules->Nature(id) : std::string();
  return !n.empty() ? n : std::string(id < 25 ? kStandard[id] : "?");
}

std::string NatureLabel(const Session& s, uint8_t id) {
  if (id >= 25) return NatureName(s, id);
  const int up = id / 5, down = id % 5;
  const std::string name = NatureName(s, id);
  if (up == down) return Fmt(Tr("%s (no change)"), name.c_str());
  const char* words[5] = {Tr("Attack"), Tr("Defense"), Tr("Speed"), Tr("Sp. Atk"), Tr("Sp. Def")};
  return Fmt(Tr("%s (+%s, -%s)"), name.c_str(), words[up], words[down]);
}

EditRow Cho(const std::string& id, const std::string& label, const std::string& shown, const Choices& choices, bool changed = false) {
  EditRow r;
  r.kind = Kind::Choice;
  r.id = id;
  r.label = label;
  r.value = shown;
  r.choices = choices;
  r.changed = changed;
  return r;
}

std::string MonText(const Session& s, const xs::Pokemon& p) {
  if (p.IsEmpty()) return Tr("(empty)");
  std::string name = xs::DisplayUtf8(p.nickname);
  const std::string species = SpeciesName(s, p.species);
  if (name.empty() || Upper(name) == Upper(species)) name = species;
  else name += " (" + species + ")";
  return Fmt(Tr("%s  Lv%d"), name.c_str(), static_cast<int>(p.level));
}

void BuildPokemon(Session& s, EditTab& tab) {
  const se::Spot sel = s.sel;
  const xs::Save& sv = s.ed.save();
  EditSection party;
  party.title = Tr("Party");
  party.column = 0;
  for (int i = 0; i < xs::kPartySize; i++) {
    const se::Spot sp{se::Area::Party, 0, i};
    const xs::Pokemon& p = sv.party[static_cast<size_t>(i)];
    const xs::Pokemon* o = OrigAt(s, sp);
    party.rows.push_back(Pick("pick." + Spot(sp), std::to_string(i + 1), MonText(s, p), sp == sel, o != nullptr && o->raw != p.raw));
  }
  EditSection pc;
  pc.title = Fmt(Tr("PC box %d of %d"), s.box + 1, xs::kBoxCount);
  pc.column = 0;
  {
    EditRow prev = Btn("box.prev", "", Tr("< Previous box"));
    EditRow next = Btn("box.next", "", Tr("Next box >"));
    pc.rows.push_back(prev);
    pc.rows.push_back(next);
    const bool nameChanged = sv.boxes[static_cast<size_t>(s.box)].name != s.ed.originalSave().boxes[static_cast<size_t>(s.box)].name;
    pc.rows.push_back(Txt("box.name", Tr("Box name"), xs::DisplayUtf8(sv.boxes[static_cast<size_t>(s.box)].name), se::MaxBoxName({}), nameChanged));
  }
  for (int i = 0; i < xs::kBoxSlots; i++) {
    const se::Spot sp{se::Area::Box, s.box, i};
    const xs::Pokemon& p = sv.boxes[static_cast<size_t>(s.box)].slots[static_cast<size_t>(i)];
    const xs::Pokemon* o = OrigAt(s, sp);
    pc.rows.push_back(Pick("pick." + Spot(sp), std::to_string(i + 1), MonText(s, p), sp == sel, o != nullptr && o->raw != p.raw));
  }
  tab.sections.push_back(party);
  tab.sections.push_back(pc);

  EditSection one;
  one.column = 1;
  const xs::Pokemon* p = s.ed.At(sel);
  const xs::Pokemon* o = OrigAt(s, sel);
  if (s.moving) {
    one.title = Tr("Moving a Pokemon");
    one.hint = Tr("Pick the place it goes to (a Pokemon there swaps). Nothing here changes the game, so moving does not mark the save.");
    one.rows.push_back(Btn("move.cancel", "", Tr("Cancel the move")));
    tab.sections.push_back(one);
    return;
  }
  if (p == nullptr || p->IsEmpty()) {
    one.title = Fmt(Tr("Empty place (%s)"), sel.area == se::Area::Party ? Fmt(Tr("party %d"), sel.slot + 1).c_str() : Fmt(Tr("box %d, place %d"), sel.box + 1, sel.slot + 1).c_str());
    one.hint = Tr("Add a new Pokemon: it goes to the first free place of the PC boxes (or the party).");
    one.rows.push_back(Cho("add.species", Tr("Species"), s.addSpecies != 0 ? SpeciesName(s, static_cast<uint16_t>(s.addSpecies)) : Tr("(choose)"), s.speciesList));
    one.rows.push_back(Num("add.level", Tr("Level"), s.addLevel, 1, 100));
    one.rows.push_back(Tog("add.party", Tr("Into the party"), s.addParty));
    one.rows.push_back(Btn("add.do", "", Tr("Add this Pokemon")));
    tab.sections.push_back(one);
    return;
  }
  const bool shadow = s.ed.IsShadow(sel);
  const bool diffAny = o != nullptr && o->raw != p->raw;
  one.title = Upper(MonText(s, *p));
  one.hint = shadow ? Tr("A Shadow Pokemon: its species, nature, gender and shininess stay as they are until it is purified.") : std::string();
  const auto lockShadow = [&](EditRow r) {
    if (shadow) {
      r.locked = true;
      r.note = Tr("Locked on a Shadow Pokemon.");
    }
    return r;
  };
  (void)diffAny;
  one.rows.push_back(lockShadow(Cho("sel.species", Tr("Species"), SpeciesName(s, p->species), s.speciesList, o && o->species != p->species)));
  one.rows.push_back(Txt("sel.nick", Tr("Nickname"), xs::DisplayUtf8(p->nickname), se::MaxNickname({}), o && o->nickname != p->nickname));
  one.rows.push_back(Num("sel.level", Tr("Level"), p->level, 1, 100, o && o->level != p->level));
  {
    EditRow it = Cho("sel.item", Tr("Held item"), p->heldItem == 0 ? Tr("(nothing)") : s.rules->Item(p->heldItem), s.itemList, o && o->heldItem != p->heldItem);
    one.rows.push_back(it);
  }
  one.rows.push_back(Num("sel.friend", Tr("Friendship"), p->friendship, 0, 255, o && o->friendship != p->friendship));
  {
    one.rows.push_back(lockShadow(Cho("sel.nature", Tr("Nature"), NatureLabel(s, p->Nature()), s.natureList, o && o->Nature() != p->Nature())));
    const int ratio = p->species < s.rules->rules.species.size() ? s.rules->rules.species[p->species].sexRatio : 255;
    if (ratio >= 1 && ratio <= 253) {
      const bool female = ratio > static_cast<int>(p->pid & 0xFF);
      const bool wasFemale = o != nullptr && ratio > static_cast<int>(o->pid & 0xFF);
      one.rows.push_back(lockShadow(Cho("sel.gender", Tr("Gender"), female ? Tr("Female") : Tr("Male"), {{"0", Tr("Male")}, {"1", Tr("Female")}}, o && female != wasFemale)));
    } else {
      one.rows.push_back(Info(Tr("Gender"), ratio == 0 ? Tr("Male (this species has no choice)") : ratio == 254 ? Tr("Female (this species has no choice)") : Tr("none")));
    }
    one.rows.push_back(lockShadow(Tog("sel.shiny", Tr("Shiny"), p->IsShiny(), o && o->IsShiny() != p->IsShiny())));
  }
  {
    const auto& sp = p->species < s.rules->rules.species.size() ? s.rules->rules.species[p->species] : xd::legality::SpeciesRules();
    Choices abilities;
    abilities.push_back({"0", s.rules->Ability(sp.abilities[0])});
    if (sp.abilities[1] != 0) abilities.push_back({"1", s.rules->Ability(sp.abilities[1])});
    one.rows.push_back(Cho("sel.ability", Tr("Ability"), s.rules->Ability(sp.abilities[p->SecondAbility() && sp.abilities[1] ? 1 : 0]), abilities,
                           o && o->SecondAbility() != p->SecondAbility()));
  }
  const char* stats[6] = {Tr("HP"), Tr("Attack"), Tr("Defense"), Tr("Sp. Atk"), Tr("Sp. Def"), Tr("Speed")};
  for (int i = 0; i < 6; i++) {
    one.rows.push_back(Num("sel.iv." + std::to_string(i), Fmt(Tr("IV %s"), stats[i]), p->ivs[static_cast<size_t>(i)], 0, 31, o && o->ivs[static_cast<size_t>(i)] != p->ivs[static_cast<size_t>(i)]));
  }
  for (int i = 0; i < 6; i++) {
    one.rows.push_back(Num("sel.ev." + std::to_string(i), Fmt(Tr("EV %s"), stats[i]), p->evs[static_cast<size_t>(i)], 0, 255, o && o->evs[static_cast<size_t>(i)] != p->evs[static_cast<size_t>(i)]));
  }
  Choices moves;
  moves.push_back({"0", Tr("(no move)")});
  {
    const std::set<uint16_t> ok = s.learnOnly ? Learnable(s, p->species) : std::set<uint16_t>();
    std::set<uint16_t> known;
    for (const xs::Move& m : p->moves) known.insert(m.id);
    for (size_t m = 1; m < s.rules->rules.moves.size(); m++) {
      const auto& mr = s.rules->rules.moves[m];
      if (!mr.exists || (mr.shadow && !shadow)) continue;
      if (s.learnOnly && !ok.count(static_cast<uint16_t>(m)) && !known.count(static_cast<uint16_t>(m))) continue;
      const std::string n = s.rules->Move(static_cast<uint16_t>(m));
      if (!n.empty()) moves.push_back({std::to_string(m), n});
    }
  }
  one.rows.push_back(Tog("sel.learnonly", Tr("Only moves this species can learn"), s.learnOnly));
  for (int i = 0; i < 4; i++) {
    const xs::Move& m = p->moves[static_cast<size_t>(i)];
    const bool mc = o && (o->moves[static_cast<size_t>(i)].id != m.id || o->moves[static_cast<size_t>(i)].ppUps != m.ppUps || o->moves[static_cast<size_t>(i)].pp != m.pp);
    one.rows.push_back(Cho("sel.move." + std::to_string(i), Fmt(Tr("Move %d"), i + 1), m.id == 0 ? Tr("(no move)") : s.rules->Move(m.id), moves, mc));
    if (m.id != 0) one.rows.push_back(Num("sel.pp." + std::to_string(i), Fmt(Tr("PP Ups, move %d"), i + 1), m.ppUps, 0, 3, o && o->moves[static_cast<size_t>(i)].ppUps != m.ppUps));
  }
  one.rows.push_back(Info(Tr("HP"), Fmt("%d / %d", static_cast<int>(p->hp), static_cast<int>(p->stats[0]))));
  one.rows.push_back(Btn("sel.heal", "", Tr("Heal (HP, PP, status)")));
  one.rows.push_back(Btn("sel.moveto", "", Tr("Move this Pokemon...")));
  {
    EditRow rel = Btn("sel.release", "", Tr("Release this Pokemon"));
    if (shadow) {
      rel.locked = true;
      rel.note = Tr("A Shadow Pokemon can't be released.");
    }
    one.rows.push_back(rel);
  }
  one.rows.push_back(Info(Tr("Good to know"), Tr("Everything above except nickname and place changes the game: the save is then marked as cheats used.")));
  tab.sections.push_back(one);
}

void BuildTrainer(Session& s, EditTab& tab) {
  const xs::Save& sv = s.ed.save();
  const xs::Save& o = s.ed.originalSave();
  EditSection sec;
  sec.title = Tr("Trainer");
  sec.rows.push_back(Txt("trainer.name", Tr("Name"), xs::DisplayUtf8(sv.trainerName), se::MaxTrainerName({}), sv.trainerName != o.trainerName));
  sec.rows.back().note = Tr("Your Pokemon keep counting as yours, and the playthrough's files follow the new name.");
  sec.rows.push_back(Info(Tr("Trainer ID"), std::to_string(sv.TrainerId())));
  sec.rows.push_back(Num("trainer.money", Tr("Money"), sv.money, 0, se::kMaxMoney, sv.money != o.money));
  sec.rows.push_back(Num("trainer.coupons", Tr("Poke Coupons"), sv.coupons, 0, se::kMaxCoupons, sv.coupons != o.coupons));
  const long total = static_cast<long>(sv.playTimeSeconds);
  const bool pt = sv.playTimeSeconds != o.playTimeSeconds;
  sec.rows.push_back(Num("trainer.hours", Tr("Play time, hours"), total / 3600, 0, 999, pt));
  sec.rows.push_back(Num("trainer.minutes", Tr("Play time, minutes"), total / 60 % 60, 0, 59, pt));
  tab.sections.push_back(sec);
}

void BuildBag(Session& s, EditTab& tab) {
  const xs::Save& sv = s.ed.save();
  const xs::Save& o = s.ed.originalSave();
  EditSection pockets;
  pockets.title = Tr("Pockets");
  pockets.column = 0;
  static const se::Pocket order[] = {se::Pocket::Items, se::Pocket::Balls, se::Pocket::TmHm, se::Pocket::Berries, se::Pocket::Colognes, se::Pocket::BattleCds};
  const char* names[] = {Tr("Items"), Tr("Poke Balls"), Tr("TMs & HMs"), Tr("Berries"), Tr("Colognes"), Tr("Battle CDs")};
  for (int i = 0; i < 6; i++) {
    pockets.rows.push_back(Pick("bag.pocket." + std::to_string(static_cast<int>(order[i])), "", names[i], s.pocket == static_cast<int>(order[i]), false));
  }
  tab.sections.push_back(pockets);
  const se::Pocket p = static_cast<se::Pocket>(s.pocket);
  const auto& slots = [&](const xs::Save& v) -> const std::vector<xs::ItemSlot>& {
    switch (p) {
      case se::Pocket::Items: return v.items;
      case se::Pocket::KeyItems: return v.keyItems;
      case se::Pocket::Balls: return v.balls;
      case se::Pocket::TmHm: return v.tmHm;
      case se::Pocket::Berries: return v.berries;
      case se::Pocket::Colognes: return v.colognes;
      default: return v.battleCds;
    }
  };
  EditSection list;
  list.column = 1;
  list.title = names[0];
  for (int i = 0; i < 6; i++) {
    if (order[i] == p) list.title = names[i];
  }
  const auto& now = slots(sv);
  const auto& before = slots(o);
  int used = 0;
  for (size_t i = 0; i < now.size(); i++) {
    if (now[i].item == 0 || now[i].count == 0) continue;
    used++;
    const std::string name = s.rules->Item(now[i].item);
    const bool changed = i >= before.size() || before[i].item != now[i].item || before[i].count != now[i].count;
    list.rows.push_back(Num("bag.count." + std::to_string(i), name.empty() ? Fmt(Tr("Item #%d"), now[i].item) : name, now[i].count, 1, se::kMaxBagCount, changed));
    list.rows.push_back(Btn("bag.remove." + std::to_string(i), "", Fmt(Tr("Remove %s"), name.c_str())));
  }
  if (used == 0) list.rows.push_back(Info("", Tr("Nothing in this pocket.")));
  Choices add;
  static const int kindOf[7] = {2, 5, 1, 4, 3, 6, 7};
  for (size_t i = 1; i < s.rules->rules.items.size(); i++) {
    const auto& it = s.rules->rules.items[i];
    if (!it.exists || it.bagKind != kindOf[s.pocket]) continue;
    const std::string n = s.rules->Item(static_cast<uint16_t>(i));
    if (!n.empty()) add.push_back({std::to_string(i), n});
  }
  list.rows.push_back(Cho("bag.item", Tr("Add an item"), s.bagItem != 0 ? s.rules->Item(static_cast<uint16_t>(s.bagItem)) : Tr("(choose)"), add));
  list.rows.push_back(Num("bag.newcount", Tr("How many"), s.bagCount, 1, se::kMaxBagCount));
  list.rows.push_back(Btn("bag.add", "", Tr("Add to the bag")));
  tab.sections.push_back(list);
}

void BuildCareful(Session& s, EditTab& tab) {
  EditSection sec;
  sec.title = Tr("Careful");
  sec.hint = Tr("These parts are tied to the story. Changing them can make events skip or repeat, leave a playthrough stuck, or make the Strategy Memo and the story disagree. Make sure you have a backup you trust (one is made when you save) and only go on if you know what you are doing.");
  sec.rows.push_back(Tog("careful.show", Tr("I understand: show the risky changes"), s.careful));
  tab.sections.push_back(sec);
  if (!s.careful) return;
  const xs::Save& sv = s.ed.save();
  const xs::Save& o = s.ed.originalSave();
  EditSection key;
  key.title = Tr("Key items");
  key.hint = Tr("Story items (Disc Case, Mask of Ice...). The game may check for them to start or finish an event: removing one can block the story, adding one can skip a step.");
  for (size_t i = 0; i < sv.keyItems.size(); i++) {
    const xs::ItemSlot& it = sv.keyItems[i];
    if (it.item == 0 || it.count == 0) continue;
    const std::string name = s.rules->Item(it.item);
    key.rows.push_back(Btn("key.remove." + std::to_string(i), name.empty() ? Fmt(Tr("Item #%d"), it.item) : name, Tr("Remove")));
    if (i >= o.keyItems.size() || o.keyItems[i].item != it.item) key.rows.back().changed = true;
  }
  Choices keys;
  for (size_t i = 1; i < s.rules->rules.items.size(); i++) {
    const auto& it = s.rules->rules.items[i];
    if (!it.exists || it.bagKind != 5) continue;
    const std::string n = s.rules->Item(static_cast<uint16_t>(i));
    if (!n.empty()) keys.push_back({std::to_string(i), n});
  }
  key.rows.push_back(Cho("key.item", Tr("Add a key item"), s.keyItem != 0 ? s.rules->Item(static_cast<uint16_t>(s.keyItem)) : Tr("(choose)"), keys));
  key.rows.push_back(Btn("key.add", "", Tr("Add this key item")));
  tab.sections.push_back(key);
  EditSection memo;
  memo.title = Tr("Strategy Memo");
  memo.hint = Tr("The game's list of Pokemon you have met. Adding a species lists it as met; removing one takes it out of the list. Entries past 500 are kept elsewhere and are not changed.");
  const std::vector<uint16_t> listed = s.ed.MemoSpecies();
  memo.rows.push_back(Info(Tr("Species listed"), std::to_string(listed.size())));
  memo.rows.push_back(Cho("memo.add.species", Tr("Add a species"), s.memoAdd != 0 ? SpeciesName(s, static_cast<uint16_t>(s.memoAdd)) : Tr("(choose)"), s.speciesList));
  memo.rows.push_back(Btn("memo.add", "", Tr("Add to the Strategy Memo")));
  Choices present;
  for (uint16_t sp : listed) present.push_back({std::to_string(sp), SpeciesName(s, sp)});
  memo.rows.push_back(Cho("memo.rem.species", Tr("Remove a species"), s.memoRem != 0 ? SpeciesName(s, static_cast<uint16_t>(s.memoRem)) : Tr("(choose)"), present));
  memo.rows.push_back(Btn("memo.remove", "", Tr("Remove from the Strategy Memo")));
  tab.sections.push_back(memo);
  EditSection hearts;
  hearts.title = Tr("Shadow Pokemon hearts");
  hearts.hint = Tr("Opens the heart of a Shadow Pokemon you have caught: it is then ready to be purified. You still purify it yourself at the Relic Stone or in the Purification Chamber, so the game teaches the real moves and gives the stored experience. Nothing else about a Shadow Pokemon is changed.");
  const auto scan = [&](const se::Spot& sp) {
    const xs::Pokemon* p = s.ed.At(sp);
    if (p == nullptr || p->IsEmpty() || p->shadowId == 0 || p->shadowId >= xs::kShadowEntries) return;
    const xs::ShadowEntry& e = sv.shadows[p->shadowId];
    if (!e.Snagged() || e.Purified()) return;
    const std::string label = Fmt("%s (%s)", MonText(s, *p).c_str(), sp.area == se::Area::Party ? Fmt(Tr("party %d"), sp.slot + 1).c_str() : Fmt(Tr("box %d, place %d"), sp.box + 1, sp.slot + 1).c_str());
    if (e.heartGauge == 0) {
      hearts.rows.push_back(Info(label, Tr("heart open")));
    } else {
      hearts.rows.push_back(Btn("heart." + Spot(sp), label, Fmt(Tr("Open the heart (gauge %d)"), e.heartGauge)));
    }
  };
  for (int i = 0; i < xs::kPartySize; i++) scan({se::Area::Party, 0, i});
  for (int b = 0; b < xs::kBoxCount; b++) {
    for (int i = 0; i < xs::kBoxSlots; i++) scan({se::Area::Box, b, i});
  }
  if (hearts.rows.empty()) hearts.rows.push_back(Info("", Tr("No caught Shadow Pokemon with a closed heart.")));
  tab.sections.push_back(hearts);
}

void BuildView(Session& s, EditView& v) {
  const int tab = v.tab;
  const xs::Save& sv = s.ed.save();
  v = EditView();
  v.tab = tab;
  v.who = Fmt(Tr("%s, %s"), xs::DisplayUtf8(sv.trainerName).c_str(), PlayTimeText(sv.playTimeSeconds).c_str());
  v.thumbnail = s.setupFolder / (s.key + ".png");
  v.readOnly = s.readOnly;
  v.readOnlyWhy = s.readOnlyWhy;
  EditTab pokemon, trainer, bag, careful;
  pokemon.name = Tr("Pokemon");
  trainer.name = Tr("Trainer");
  bag.name = Tr("Bag");
  careful.name = Tr("Careful");
  if (s.readOnly) {
    EditSection sec;
    sec.title = Tr("This save can only be looked at");
    sec.hint = s.readOnlyWhy;
    pokemon.sections.push_back(sec);
    v.tabs = {pokemon};
    return;
  }
  BuildPokemon(s, pokemon);
  BuildTrainer(s, trainer);
  BuildBag(s, bag);
  BuildCareful(s, careful);
  v.tabs = {pokemon, trainer, bag, careful};
  if (v.tab < 0 || v.tab >= static_cast<int>(v.tabs.size())) v.tab = 0;
  for (const se::Change& c : s.ed.changes()) {
    v.changes.push_back(c.text);
    v.changeMarks.push_back(c.marks ? 1 : 0);
  }
  v.willMark = s.ed.WillMark();
  v.markText = v.changes.empty() ? std::string() : v.willMark ? Tr("This changes your game: the save will be marked as cheats used.") : Tr("These changes don't change the game: no mark.");
  v.problems = s.ed.Problems(s.tables);
  v.notes = s.ed.Notes(s.tables);
}

bool HasKana(const std::u16string& n) {
  for (char16_t c : n) {
    if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x4E00 && c <= 0x9FFF)) return true;
  }
  return false;
}

bool Load(Session& s, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!ReadBytes(s.gci, bytes)) {
    error = Tr("The save's card file can't be read.");
    return false;
  }
  engine::memcard::CardFile card;
  if (!engine::memcard::ReadGci(bytes, card, error) || card.data.size() < xs::kFileSize) {
    if (error.empty()) error = Tr("The save's card file is too short.");
    return false;
  }
  const std::vector<uint8_t> data(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
  se::Options opt;
  std::string why;
  opt.japanese = xs::LayoutOf(data, why) == xs::SaveKind::JapaneseLayout || s.japaneseText;
  if (!s.ed.Open(data, opt, error)) return false;
  if (HasKana(s.ed.save().trainerName)) {
    opt.japanese = true;
    if (!s.ed.Open(data, opt, error)) return false;
  }
  s.key = se::PlaythroughKey(s.ed.save().trainerOtId, s.ed.save().trainerName);
  return true;
}

bool OpenImpl(const fs::path& cardRoot, const std::string& disc, EditView& out, std::string& error) {
  auto s = std::make_unique<Session>();
  s->cardRoot = cardRoot;
  s->setupFolder = cardRoot / L"playthroughs" / L"Card A";
  s->gci = sSaveFileOf ? sSaveFileOf(cardRoot) : fs::path();
  if (s->gci.empty()) {
    error = Tr("This save is empty: there is nothing to edit yet. Play and save once first.");
    return false;
  }
  s->rules = RulesFor(disc, error);
  if (!s->rules) return false;
  for (size_t i = 1; i < s->rules->names->species.size() && !s->japaneseText; i++) s->japaneseText = HasKana(FromUtf8(s->rules->names->species[i]));
  if (!Load(*s, error)) return false;
  {
    namespace ep = engine::playthrough;
    ep::Setup setup;
    std::string e2;
    const fs::path file = ep::FileFor(s->setupFolder, s->key);
    const bool haveSetup = fs::exists(file) && ep::ReadFile(file, setup, e2);
    const bool plain = !haveSetup || (setup.WorldMods().empty() && setup.randomizer.empty() && setup.addedRows.empty() &&
                                      !(setup.rules.present && std::any_of(setup.rules.gens.begin(), setup.rules.gens.end(), [](int g) { return g != 3; })));
    if (haveSetup && setup.edition == "xg") {
      s->readOnly = true;
      s->readOnlyWhy = Tr("This is a Pokemon XG save: the editor does not handle those yet.");
    } else if (haveSetup) {
      std::vector<uint8_t> tb;
      auto tf = std::make_shared<xd::legality::TablesFile>();
      std::string perr;
      if (ReadBytes(s->setupFolder / (s->key + ".tables"), tb) && xd::legality::Parse(tb, *tf, perr) && tf->stamp == ep::TablesStamp(setup)) {
        s->rules = tf;
        s->fromFile = true;
      } else if (!plain) {
        s->readOnly = true;
        s->readOnlyWhy = Tr("This playthrough plays with other lists than the original game's (mods, a randomizer or Pokemon of other generations). The game writes them next to the save while you play: start the game with this save once with this version, then edit it here. Until then it can only be looked at.");
      }
    }
    if (s->japaneseText) {
      auto copy = std::make_shared<xd::legality::TablesFile>(*s->rules);
      copy->names = std::make_shared<xd::legality::TableNames>(*s->rules->names);
      for (auto* list : {&copy->names->species, &copy->names->moves, &copy->names->items, &copy->names->abilities, &copy->names->natures}) {
        for (std::string& n : *list) n = xs::DisplayUtf8(FromUtf8(n));
      }
      s->rules = copy;
    }
    s->tables.rules = &s->rules->rules;
  }
  if (xs::LayoutOf(s->ed.original(), error) == xs::SaveKind::JapaneseLayout) {
    s->readOnly = true;
    s->readOnlyWhy = Tr("This save is in the Japanese layout: the editor does not write those yet.");
  }
  error.clear();
  for (size_t i = 1; i < s->rules->rules.species.size(); i++) {
    if (!s->rules->rules.species[i].exists) continue;
    const std::string n = s->rules->Species(static_cast<uint16_t>(i));
    if (!n.empty()) s->speciesList.push_back({std::to_string(i), n});
  }
  s->itemList.push_back({"0", Tr("(nothing)")});
  for (size_t i = 1; i < s->rules->rules.items.size(); i++) {
    if (!s->rules->rules.items[i].exists) continue;
    const std::string n = s->rules->Item(static_cast<uint16_t>(i));
    if (!n.empty()) s->itemList.push_back({std::to_string(i), n});
  }
  for (int i = 0; i < 25; i++) s->natureList.push_back({std::to_string(i), NatureLabel(*s, static_cast<uint8_t>(i))});
  s->ed.Seed(static_cast<uint64_t>(std::time(nullptr)) * 2654435761u);
  {
    const auto tf = s->rules;
    s->ed.SetSpeciesNames([tf](uint16_t id) { return tf->Species(id); });
  }
  out = EditView();
  BuildView(*s, out);
  sSession = std::move(s);
  return true;
}

std::string SetImpl(const std::string& id, const std::string& value, EditView& view) {
  if (!sSession) return Tr("The editor is not open.");
  Session& s = *sSession;
  if (s.readOnly) return s.readOnlyWhy;
  const long n = std::strtol(value.c_str(), nullptr, 10);
  std::string why;
  const se::Tables& t = s.tables;
  const auto selected = [&]() -> const xs::Pokemon* { return s.ed.At(s.sel); };
  if (id.rfind("pick.", 0) == 0) {
    se::Spot sp;
    if (!ParseSpot(id.substr(5), sp)) return "?";
    if (s.moving) {
      why = s.ed.Swap(s.moveFrom, sp);
      if (why.empty()) {
        s.moving = false;
        s.sel = sp;
        if (sp.area == se::Area::Box) s.box = sp.box;
      }
    } else {
      s.sel = sp;
      if (sp.area == se::Area::Box) s.box = sp.box;
    }
  } else if (id == "box.prev" || id == "box.next") {
    s.box = (s.box + (id == "box.next" ? 1 : xs::kBoxCount - 1)) % xs::kBoxCount;
    if (s.sel.area == se::Area::Box) s.sel = {se::Area::Box, s.box, s.sel.slot};
  } else if (id == "box.name") {
    why = s.ed.SetBoxName(s.box, FromUtf8(value));
  } else if (id == "move.cancel") {
    s.moving = false;
  } else if (id == "sel.moveto") {
    s.moving = true;
    s.moveFrom = s.sel;
  } else if (id == "sel.species") {
    why = s.ed.SetSpecies(s.sel, static_cast<uint16_t>(n), t);
  } else if (id == "sel.nick") {
    why = s.ed.SetNickname(s.sel, FromUtf8(value));
  } else if (id == "sel.level") {
    why = s.ed.SetLevel(s.sel, static_cast<int>(n), t);
  } else if (id == "sel.item") {
    why = s.ed.SetHeldItem(s.sel, static_cast<uint16_t>(n), t);
  } else if (id == "sel.friend") {
    why = s.ed.SetFriendship(s.sel, static_cast<int>(n));
  } else if (id == "sel.nature" || id == "sel.gender" || id == "sel.shiny") {
    se::PidWish w;
    (id == "sel.nature" ? w.nature : id == "sel.gender" ? w.female : w.shiny) = static_cast<int>(n);
    why = s.ed.SetPid(s.sel, w, t);
  } else if (id == "sel.ability") {
    why = s.ed.SetAbilitySlot(s.sel, static_cast<int>(n), t);
  } else if (id.rfind("sel.iv.", 0) == 0) {
    why = s.ed.SetIv(s.sel, std::atoi(id.c_str() + 7), static_cast<int>(n), t);
  } else if (id.rfind("sel.ev.", 0) == 0) {
    why = s.ed.SetEv(s.sel, std::atoi(id.c_str() + 7), static_cast<int>(n), t);
  } else if (id == "sel.learnonly") {
    s.learnOnly = n != 0;
  } else if (id.rfind("sel.move.", 0) == 0) {
    const int i = std::atoi(id.c_str() + 9);
    const xs::Pokemon* p = selected();
    why = s.ed.SetMove(s.sel, i, static_cast<uint16_t>(n), p != nullptr && static_cast<uint16_t>(n) == p->moves[static_cast<size_t>(i)].id ? p->moves[static_cast<size_t>(i)].ppUps : 0, t);
  } else if (id.rfind("sel.pp.", 0) == 0) {
    why = s.ed.SetPpUps(s.sel, std::atoi(id.c_str() + 7), static_cast<int>(n), t);
  } else if (id == "sel.heal") {
    why = s.ed.Heal(s.sel, t);
  } else if (id == "sel.release") {
    why = s.ed.Release(s.sel);
  } else if (id == "add.species") {
    s.addSpecies = static_cast<int>(n);
  } else if (id == "add.level") {
    s.addLevel = static_cast<int>(std::clamp<long>(n, 1, 100));
  } else if (id == "add.party") {
    s.addParty = n != 0;
  } else if (id == "add.do") {
    se::Spot where;
    if (s.addSpecies == 0) {
      why = Tr("Choose a species first.");
    } else {
      why = s.ed.Add(static_cast<uint16_t>(s.addSpecies), s.addLevel, s.addParty, t, where);
      if (why.empty()) {
        s.sel = where;
        if (where.area == se::Area::Box) s.box = where.box;
      }
    }
  } else if (id == "trainer.name") {
    why = s.ed.SetTrainerName(FromUtf8(value));
  } else if (id == "trainer.money") {
    why = s.ed.SetMoney(static_cast<uint32_t>(std::max(0L, n)));
  } else if (id == "trainer.coupons") {
    why = s.ed.SetCoupons(static_cast<uint32_t>(std::max(0L, n)));
  } else if (id == "trainer.hours" || id == "trainer.minutes") {
    const long total = static_cast<long>(s.ed.save().playTimeSeconds);
    long h = total / 3600, m = total / 60 % 60;
    (id == "trainer.hours" ? h : m) = n;
    if (m > 59) m = 59;
    why = s.ed.SetPlayTime(static_cast<double>(h * 3600 + m * 60 + total % 60));
  } else if (id == "careful.show") {
    s.careful = n != 0;
  } else if (id.rfind("key.remove.", 0) == 0) {
    why = s.ed.RemoveBagSlot(se::Pocket::KeyItems, std::atoi(id.c_str() + 11));
  } else if (id == "key.item") {
    s.keyItem = static_cast<int>(n);
  } else if (id == "key.add") {
    why = s.keyItem == 0 ? std::string(Tr("Choose an item first.")) : s.ed.AddBagItem(se::Pocket::KeyItems, static_cast<uint16_t>(s.keyItem), 1, t);
  } else if (id == "memo.add.species") {
    s.memoAdd = static_cast<int>(n);
  } else if (id == "memo.rem.species") {
    s.memoRem = static_cast<int>(n);
  } else if (id == "memo.add") {
    why = s.memoAdd == 0 ? std::string(Tr("Choose a species first.")) : s.ed.AddMemoSpecies(static_cast<uint16_t>(s.memoAdd), t);
  } else if (id == "memo.remove") {
    why = s.memoRem == 0 ? std::string(Tr("Choose a species first.")) : s.ed.RemoveMemoSpecies(static_cast<uint16_t>(s.memoRem), t);
    if (why.empty()) s.memoRem = 0;
  } else if (id.rfind("heart.", 0) == 0) {
    se::Spot sp;
    why = ParseSpot(id.substr(6), sp) ? s.ed.OpenHeart(sp) : std::string("?");
  } else if (id.rfind("bag.pocket.", 0) == 0) {
    s.pocket = std::atoi(id.c_str() + 11);
    s.bagItem = 0;
  } else if (id.rfind("bag.count.", 0) == 0) {
    const int i = std::atoi(id.c_str() + 10);
    const auto p = static_cast<se::Pocket>(s.pocket);
    const std::vector<xs::ItemSlot>* sl = nullptr;
    const xs::Save& sv = s.ed.save();
    switch (p) {
      case se::Pocket::Items: sl = &sv.items; break;
      case se::Pocket::Balls: sl = &sv.balls; break;
      case se::Pocket::TmHm: sl = &sv.tmHm; break;
      case se::Pocket::Berries: sl = &sv.berries; break;
      case se::Pocket::Colognes: sl = &sv.colognes; break;
      default: sl = &sv.battleCds; break;
    }
    if (i >= 0 && i < static_cast<int>(sl->size())) why = s.ed.SetBagSlot(p, i, (*sl)[static_cast<size_t>(i)].item, static_cast<int>(n), t);
  } else if (id.rfind("bag.remove.", 0) == 0) {
    why = s.ed.RemoveBagSlot(static_cast<se::Pocket>(s.pocket), std::atoi(id.c_str() + 11));
  } else if (id == "bag.item") {
    s.bagItem = static_cast<int>(n);
  } else if (id == "bag.newcount") {
    s.bagCount = static_cast<int>(std::clamp<long>(n, 1, se::kMaxBagCount));
  } else if (id == "bag.add") {
    why = s.bagItem == 0 ? std::string(Tr("Choose an item first.")) : s.ed.AddBagItem(static_cast<se::Pocket>(s.pocket), static_cast<uint16_t>(s.bagItem), s.bagCount, t);
  } else {
    why = Fmt("unknown row %s", id.c_str());
  }
  BuildView(s, view);
  return why;
}

GameInfo::Saves::TradeResult SaveImpl(EditView& view) {
  GameInfo::Saves::TradeResult r;
  if (!sSession) {
    r.message = Tr("The editor is not open.");
    return r;
  }
  Session& s = *sSession;
  se::CommitRequest req;
  req.cardRoot = s.cardRoot;
  req.gciFile = s.gci;
  req.setupFolder = s.setupFolder;
  req.cardIsFree = [cardRoot = s.cardRoot] { return !sCardFree || sCardFree(cardRoot); };
  const se::CommitResult c = se::Commit(s.ed, s.tables, req);
  r.ok = c.ok;
  r.message = c.message;
  if (c.ok) {
    std::string error;
    const se::Spot sel = s.sel;
    if (Load(s, error)) {
      s.sel = sel;
      BuildView(s, view);
    }
  } else {
    BuildView(s, view);
  }
  return r;
}

}

void Install(GameInfo& game, std::function<fs::path(const fs::path&)> saveFileOf, std::function<bool(const fs::path&)> cardIsFree) {
  sSaveFileOf = std::move(saveFileOf);
  sCardFree = std::move(cardIsFree);
  game.saves.editOpen = OpenImpl;
  game.saves.editSet = SetImpl;
  game.saves.editRevert = [](EditView& view) {
    if (!sSession) return;
    std::string error;
    const se::Spot sel = sSession->sel;
    if (Load(*sSession, error)) {
      sSession->sel = sel;
      sSession->moving = false;
      BuildView(*sSession, view);
    }
  };
  game.saves.editSave = SaveImpl;
  game.saves.editClose = [] { sSession.reset(); };
}

}
