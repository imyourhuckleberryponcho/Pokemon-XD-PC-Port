#include <xd_saveedit.h>

#include <xd_save_write.h>

#include <i18n.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace xd::saveedit {

namespace xs = xd::save;
using engine::i18n::Tr;

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

constexpr size_t kHeroName = 0x00;
constexpr size_t kHeroParty = 0x30;
constexpr size_t kHeroMoney = 0x8E4;
constexpr size_t kHeroCoupons = 0x8E8;
constexpr size_t kBoxStride = 0x14 + xs::kBoxSlots * xs::kPokemonSize;
constexpr size_t kBoxPokemon = 0x14;
constexpr size_t kChamberStride = 0x3D8;
constexpr size_t kPlayTimeAt = 0x30;

struct PocketDesc {
  size_t offset;
  int count;
};
constexpr PocketDesc kPockets[] = {{0x4C8, 30}, {0x540, 43}, {0x5EC, 16}, {0x62C, 64}, {0x72C, 46}, {0x7E4, 3}, {0x7F0, 60}};

constexpr size_t kSpecies = 0x00, kHeld = 0x02, kHp = 0x04, kFriend = 0x06, kLevel = 0x11, kMarks = 0x14, kStatus = 0x16,
                 kFlags = 0x1D, kExp = 0x20, kOtId = 0x24, kPid = 0x28, kOtName = 0x38, kNick = 0x4E, kNickOrg = 0x64,
                 kMoves = 0x80, kStats = 0x90, kEvs = 0x9C, kIvs = 0xA8, kContest = 0xAE, kShadowId = 0xBA;
constexpr size_t kNameChars = 11;

void PutName(uint8_t* p, size_t chars, const std::u16string& s, xs::ByteOrder order) {
  std::memset(p, 0, chars * 2);
  for (size_t i = 0; i < s.size() && i < chars - 1; i++) xs::Write16(p + i * 2, static_cast<uint16_t>(s[i]), order);
}

void WriteF64(uint8_t* p, double d, xs::ByteOrder order) {
  uint64_t bits;
  std::memcpy(&bits, &d, sizeof bits);
  const uint32_t hi = static_cast<uint32_t>(bits >> 32), lo = static_cast<uint32_t>(bits);
  if (order == xs::ByteOrder::GameCube) {
    xs::Write32(p, hi, order);
    xs::Write32(p + 4, lo, order);
  } else {
    xs::Write32(p, lo, order);
    xs::Write32(p + 4, hi, order);
  }
}

std::string Upper(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return o;
}

std::string SpeciesText(const Tables* t, uint16_t id) {
  if (t != nullptr && t->rules != nullptr && t->rules->speciesName) {
    const std::string n = t->rules->speciesName(id);
    if (!n.empty()) return n;
  }
  return Fmt(Tr("Pokemon #%d"), static_cast<int>(id));
}
std::string MoveText(const Tables& t, uint16_t id) {
  if (t.rules != nullptr && t.rules->moveName) {
    const std::string n = t.rules->moveName(id);
    if (!n.empty()) return n;
  }
  return Fmt(Tr("move #%d"), static_cast<int>(id));
}
std::string ItemText(const Tables& t, uint16_t id) {
  if (id == 0) return Tr("nothing");
  if (t.rules != nullptr && t.rules->itemName) {
    const std::string n = t.rules->itemName(id);
    if (!n.empty()) return n;
  }
  return Fmt(Tr("item #%d"), static_cast<int>(id));
}

int SexRatioOf(const Tables& t, uint16_t species) {
  if (t.sexRatio) return t.sexRatio(species);
  if (t.rules == nullptr || species >= t.rules->species.size()) return -1;
  const int r = t.rules->species[species].sexRatio;
  return r >= 1 && r <= 253 ? r : -1;
}
int PocketOfItem(const Tables& t, uint16_t item) {
  if (t.pocketOf) return t.pocketOf(item);
  if (t.rules == nullptr || item >= t.rules->items.size()) return -1;
  static const int kindToPocket[8] = {-1, 2, 0, 4, 3, 1, 5, 6};
  const int k = t.rules->items[item].bagKind;
  return k >= 1 && k <= 7 ? kindToPocket[k] : -1;
}

const char* StatName(int i) {
  const char* names[6] = {Tr("HP"), Tr("Attack"), Tr("Defense"), Tr("Sp. Atk"), Tr("Sp. Def"), Tr("Speed")};
  return names[i];
}

bool SameBytes(const xs::Pokemon& a, const xs::Pokemon& b) { return a.raw == b.raw; }

}

std::string SpotText(const Spot& s) {
  return s.area == Area::Party ? Fmt(Tr("party %d"), s.slot + 1) : Fmt(Tr("box %d slot %d"), s.box + 1, s.slot + 1);
}

const char* PocketName(Pocket p) {
  const char* names[] = {Tr("Items"), Tr("Key Items"), Tr("Poke Balls"), Tr("TMs & HMs"), Tr("Berries"), Tr("Colognes"), Tr("Battle CDs")};
  return names[static_cast<int>(p)];
}
int PocketSize(Pocket p) { return kPockets[static_cast<int>(p)].count; }

int MaxTrainerName(const Options& o) { return o.japanese ? 5 : 7; }
int MaxNickname(const Options& o) { return o.japanese ? 5 : 10; }
int MaxBoxName(const Options&) { return 8; }

std::string CheckName(const std::u16string& name, int maxLen) {
  if (name.empty()) return Tr("A name can't be empty.");
  if (static_cast<int>(name.size()) > maxLen) return Fmt(Tr("A name can have at most %d letters here (the game's own name entry)."), maxLen);
  for (char16_t c : name) {
    if (c < 0x20 || (c >= 0x7F && c < 0xA0) || c == 0xFFFF) return Tr("That name has a letter the game can't show.");
  }
  return "";
}

std::string PlaythroughKey(uint32_t trainerOtId, const std::u16string& name) {
  const std::string utf8 = xs::ToUtf8(name);
  uint32_t hash = 2166136261u;
  for (unsigned char c : utf8) hash = (hash ^ c) * 16777619u;
  char key[40];
  std::snprintf(key, sizeof key, "pt_%08x_%08x", trainerOtId, hash);
  return key;
}

uint32_t Editor::Rand() {
  rng_ = rng_ * 6364136223846793005ull + 1442695040888963407ull;
  const uint64_t x = rng_;
  return static_cast<uint32_t>((x >> 22) ^ x >> 45);
}

bool Editor::Open(const std::vector<uint8_t>& file, const Options& options, std::string& error) {
  options_ = options;
  file_ = file;
  log_.clear();
  if (!xs::ReadSave(file_, orig_, report_, error)) return false;
  work_ = orig_;
  return true;
}

void Editor::Refresh() {
  xs::ReadReport scratch;
  xs::Reparse(work_, scratch);
}

void Editor::Log(const std::string& text, bool marks, const char* kind) { log_.push_back({text, marks, kind}); }

bool Editor::WillMark() const {
  for (const Change& c : log_) {
    if (c.marks) return true;
  }
  return false;
}

std::string Editor::MarkReason() const {
  std::string out;
  std::set<std::string> seen;
  for (const Change& c : log_) {
    if (!c.marks) continue;
    if (!seen.insert(c.kind).second) continue;
    out += (out.empty() ? "" : ", ") + c.kind;
  }
  return out;
}

bool Editor::Valid(const Spot& s) const {
  if (s.area == Area::Party) return s.slot >= 0 && s.slot < xs::kPartySize;
  return s.box >= 0 && s.box < xs::kBoxCount && s.slot >= 0 && s.slot < xs::kBoxSlots;
}

const uint8_t* Editor::Rec(const Spot& s) const {
  if (!Valid(s)) return nullptr;
  if (s.area == Area::Party) {
    const size_t h = xs::SectionOffsetInSlot(work_, 2);
    return h == 0 ? nullptr : work_.slotBytes.data() + h + kHeroParty + static_cast<size_t>(s.slot) * xs::kPokemonSize;
  }
  const size_t b = xs::SectionOffsetInSlot(work_, 3);
  return b == 0 ? nullptr
                : work_.slotBytes.data() + b + static_cast<size_t>(s.box) * kBoxStride + kBoxPokemon +
                      static_cast<size_t>(s.slot) * xs::kPokemonSize;
}
uint8_t* Editor::Rec(const Spot& s) { return const_cast<uint8_t*>(static_cast<const Editor*>(this)->Rec(s)); }

const xs::Pokemon* Editor::At(const Spot& s) const {
  if (!Valid(s)) return nullptr;
  return s.area == Area::Party ? &work_.party[static_cast<size_t>(s.slot)]
                               : &work_.boxes[static_cast<size_t>(s.box)].slots[static_cast<size_t>(s.slot)];
}

bool Editor::UnpurifiedShadow(const xs::Pokemon& p) const {
  if (p.IsEmpty() || p.shadowId == 0) return false;
  const xs::ShadowEntry* e = work_.ShadowOf(p);
  return e == nullptr || !e->Purified();
}

bool Editor::IsShadow(const Spot& s) const {
  const xs::Pokemon* p = At(s);
  return p != nullptr && UnpurifiedShadow(*p);
}

std::string Editor::Name(const Spot& s, const Tables* t) const {
  const xs::Pokemon* p = At(s);
  std::string who = std::string(Tr("empty place"));
  if (p != nullptr && !p->IsEmpty()) {
    who = SpeciesText(t, p->species);
    if (t == nullptr && speciesNames_) {
      const std::string n = speciesNames_(p->species);
      if (!n.empty()) who = n;
    }
  }
  return Upper(who) + " (" + SpotText(s) + ")";
}

std::string Editor::Refuse(const Spot& s) const {
  if (!Valid(s)) return Tr("That place does not exist.");
  if (Rec(s) == nullptr) return Tr("This save has no Pokemon section.");
  if (At(s)->IsEmpty()) return Tr("There is no Pokemon in that place.");
  return "";
}

bool Editor::FullRecompute(const Spot& s, const Tables& t, bool keepHpShare, std::string& why) {
  uint8_t* r = Rec(s);
  const xs::ByteOrder o = work_.order;
  xs::Pokemon p = xs::ParsePokemon(r, o);
  std::array<uint16_t, 6> stats{};
  if (!xd::legality::ExpectedStats(*t.rules, p, stats)) {
    why = "The game's tables don't have that species.";
    return false;
  }
  const uint16_t oldMax = p.stats[0];
  for (int i = 0; i < 6; i++) xs::Write16(r + kStats + i * 2, stats[static_cast<size_t>(i)], o);
  uint16_t hp = p.hp;
  if (keepHpShare && oldMax > 0 && hp > 0) {
    hp = static_cast<uint16_t>(std::max<long>(1, std::lround(static_cast<double>(hp) * stats[0] / oldMax)));
  }
  hp = std::min<uint16_t>(hp, stats[0]);
  xs::Write16(r + kHp, hp, o);
  return true;
}

std::string Editor::SetLevel(const Spot& s, int level, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (level < 1 || level > 100) return Tr("The level must be from 1 to 100.");
  const xs::Pokemon& p = *At(s);
  if (static_cast<int>(p.level) == level) return "";
  if (p.IsEgg()) return Tr("Eggs have no level to change.");
  if (t.rules == nullptr || p.species >= t.rules->species.size()) return Tr("The game's tables don't have that species.");
  uint32_t exp = 0;
  if (!xd::legality::ExpForLevel(*t.rules, t.rules->species[p.species].growth, level, exp)) return Tr("The game's tables have no experience curve for that species.");
  const std::string who = Name(s, &t);
  const int before = p.level;
  uint8_t* r = Rec(s);
  r[kLevel] = static_cast<uint8_t>(level);
  xs::Write32(r + kExp, exp, work_.order);
  Refresh();
  std::string why;
  if (!FullRecompute(s, t, true, why)) return why;
  Refresh();
  Log(Fmt(Tr("%s: level %d -> %d"), who.c_str(), before, level), true, "level");
  return "";
}

std::string Editor::SetSpecies(const Spot& s, uint16_t species, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  if (p.species == species) return "";
  if (t.rules == nullptr || species >= t.rules->species.size() || !t.rules->species[species].exists) return Tr("The game's tables don't have that species.");
  if (p.shadowId != 0) return Tr("A Shadow Pokemon (or one that was) keeps its species: the Shadow list in the save names it.");
  if (p.IsEgg()) return Tr("Eggs can't change species.");
  const std::string who = Name(s, &t);
  const uint16_t old = p.species;
  const std::string oldName = SpeciesText(&t, old);
  const bool nickWasDefault = p.nickname == std::u16string(oldName.begin(), oldName.end()) ||
                              Upper(xs::ToUtf8(p.nickname)) == Upper(oldName);
  uint8_t* r = Rec(s);
  const xs::ByteOrder o = work_.order;
  xs::Write16(r + kSpecies, species, o);
  uint32_t exp = 0;
  if (xd::legality::ExpForLevel(*t.rules, t.rules->species[species].growth, p.level, exp)) xs::Write32(r + kExp, exp, o);
  const auto& sp = t.rules->species[species];
  if (sp.abilities[1] == 0) r[kFlags] = static_cast<uint8_t>(r[kFlags] & ~0x40);
  if (nickWasDefault) {
    const std::string n = SpeciesText(&t, species);
    const std::u16string name(n.begin(), n.end());
    PutName(r + kNick, kNameChars, name, o);
    PutName(r + kNickOrg, kNameChars, name, o);
  }
  Refresh();
  std::string why;
  if (!FullRecompute(s, t, true, why)) return why;
  Refresh();
  Log(Fmt(Tr("%s: species -> %s"), who.c_str(), SpeciesText(&t, species).c_str()), true, "species");
  return "";
}

std::string Editor::SetNickname(const Spot& s, const std::u16string& name) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (auto why = CheckName(name, MaxNickname(options_)); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  if (p.nickname == name) return "";
  const std::string who = Name(s, nullptr);
  uint8_t* r = Rec(s);
  PutName(r + kNick, kNameChars, name, work_.order);
  Refresh();
  Log(Fmt(Tr("%s: nickname -> %s"), who.c_str(), xs::ToUtf8(name).c_str()), false, "nickname");
  return "";
}

std::string Editor::SetHeldItem(const Spot& s, uint16_t item, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  if (p.heldItem == item) return "";
  if (item != 0 && (t.rules == nullptr || item >= t.rules->items.size() || !t.rules->items[item].exists)) return Tr("The game's tables don't have that item.");
  const std::string who = Name(s, &t);
  xs::Write16(Rec(s) + kHeld, item, work_.order);
  Refresh();
  Log(Fmt(Tr("%s: held item %s -> %s"), who.c_str(), ItemText(t, p.heldItem).c_str(), ItemText(t, item).c_str()), true, "held item");
  return "";
}

std::string Editor::SetFriendship(const Spot& s, int value) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (value < 0 || value > 255) return Tr("Friendship goes from 0 to 255.");
  const xs::Pokemon& p = *At(s);
  if (p.friendship == value) return "";
  const std::string who = Name(s, nullptr);
  xs::Write16(Rec(s) + kFriend, static_cast<uint16_t>(value), work_.order);
  Refresh();
  Log(Fmt(Tr("%s: friendship %d -> %d"), who.c_str(), static_cast<int>(p.friendship), value), true, "friendship");
  return "";
}

std::string Editor::SetIv(const Spot& s, int stat, int value, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (stat < 0 || stat > 5) return Tr("That is not a stat.");
  if (value < 0 || value > 31) return Tr("An IV goes from 0 to 31.");
  const xs::Pokemon& p = *At(s);
  if (p.ivs[static_cast<size_t>(stat)] == value) return "";
  const std::string who = Name(s, &t);
  const int before = p.ivs[static_cast<size_t>(stat)];
  Rec(s)[kIvs + stat] = static_cast<uint8_t>(value);
  Refresh();
  std::string why;
  if (t.rules != nullptr && !FullRecompute(s, t, true, why)) return why;
  Refresh();
  Log(Fmt(Tr("%s: IV %s %d -> %d"), who.c_str(), StatName(stat), before, value), true, "IV");
  return "";
}

std::string Editor::SetEv(const Spot& s, int stat, int value, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (stat < 0 || stat > 5) return Tr("That is not a stat.");
  if (value < 0 || value > 255) return Tr("An EV goes from 0 to 255 here (the game's own highest is 100: the page says so when it is more).");
  const xs::Pokemon& p = *At(s);
  if (p.evs[static_cast<size_t>(stat)] == value) return "";
  const std::string who = Name(s, &t);
  const int before = p.evs[static_cast<size_t>(stat)];
  xs::Write16(Rec(s) + kEvs + stat * 2, static_cast<uint16_t>(value), work_.order);
  Refresh();
  std::string why;
  if (t.rules != nullptr && !FullRecompute(s, t, true, why)) return why;
  Refresh();
  Log(Fmt(Tr("%s: EV %s %d -> %d"), who.c_str(), StatName(stat), before, value), true, "EV");
  return "";
}

namespace {
int MaxPp(const Tables& t, uint16_t move, int ups) {
  if (t.rules == nullptr || move >= t.rules->moves.size()) return 0;
  return t.rules->moves[move].basePp * (5 + ups) / 5;
}
}

std::string Editor::SetMove(const Spot& s, int index, uint16_t move, int ppUps, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (index < 0 || index > 3) return Tr("A Pokemon has four moves.");
  if (ppUps < 0 || ppUps > 3) return Tr("PP Ups go from 0 to 3.");
  if (move != 0 && (t.rules == nullptr || move >= t.rules->moves.size() || !t.rules->moves[move].exists)) return Tr("The game's tables don't have that move.");
  const xs::Pokemon& p = *At(s);
  for (int i = 0; i < 4; i++) {
    if (move != 0 && i != index && p.moves[static_cast<size_t>(i)].id == move) return Tr("It already knows that move.");
  }
  if (p.moves[static_cast<size_t>(index)].id == move && p.moves[static_cast<size_t>(index)].ppUps == ppUps) return "";
  const std::string who = Name(s, &t);
  const uint16_t before = p.moves[static_cast<size_t>(index)].id;
  uint8_t* r = Rec(s);
  const xs::ByteOrder o = work_.order;
  uint8_t* w = r + kMoves + index * 4;
  xs::Write16(w, move, o);
  w[2] = move == 0 ? 0 : static_cast<uint8_t>(MaxPp(t, move, ppUps));
  w[3] = move == 0 ? 0 : static_cast<uint8_t>(ppUps);
  if (move == 0) {
    for (int i = index; i < 3; i++) std::memcpy(r + kMoves + i * 4, r + kMoves + (i + 1) * 4, 4);
    std::memset(r + kMoves + 3 * 4, 0, 4);
  }
  Refresh();
  {
    const std::string was = before == 0 ? std::string(Tr("(none)")) : MoveText(t, before);
    const std::string now = move == 0 ? std::string(Tr("(none)")) : MoveText(t, move);
    Log(ppUps != 0 ? Fmt(Tr("%s: move %s -> %s with %d PP Ups"), who.c_str(), was.c_str(), now.c_str(), ppUps)
                   : Fmt(Tr("%s: move %s -> %s"), who.c_str(), was.c_str(), now.c_str()),
        true, "move");
  }
  return "";
}

std::string Editor::SetPpUps(const Spot& s, int index, int ppUps, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (index < 0 || index > 3) return Tr("A Pokemon has four moves.");
  if (ppUps < 0 || ppUps > 3) return Tr("PP Ups go from 0 to 3.");
  const xs::Move m = At(s)->moves[static_cast<size_t>(index)];
  if (m.id == 0) return Tr("That move place is empty.");
  if (m.ppUps == ppUps) return "";
  const std::string who = Name(s, &t);
  const int oldMax = MaxPp(t, m.id, m.ppUps), newMax = MaxPp(t, m.id, ppUps);
  uint8_t* w = Rec(s) + kMoves + index * 4;
  w[3] = static_cast<uint8_t>(ppUps);
  int pp = m.pp + (newMax - oldMax);
  w[2] = static_cast<uint8_t>(std::clamp(pp, 0, std::max(newMax, 0)));
  Refresh();
  Log(Fmt(Tr("%s: %s PP Ups %d -> %d"), who.c_str(), MoveText(t, m.id).c_str(), static_cast<int>(m.ppUps), ppUps), true, "PP Ups");
  return "";
}

std::string Editor::SetAbilitySlot(const Spot& s, int slot, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (slot < 0 || slot > 1) return Tr("A Pokemon has a first and a second ability place.");
  const xs::Pokemon& p = *At(s);
  if ((p.SecondAbility() ? 1 : 0) == slot) return "";
  if (t.rules == nullptr || p.species >= t.rules->species.size()) return Tr("The game's tables don't have that species.");
  if (slot == 1 && t.rules->species[p.species].abilities[1] == 0) return Tr("This species has only one ability.");
  const std::string who = Name(s, &t);
  uint8_t* r = Rec(s);
  r[kFlags] = static_cast<uint8_t>(slot == 1 ? (r[kFlags] | 0x40) : (r[kFlags] & ~0x40));
  Refresh();
  Log(Fmt(Tr("%s: ability place %d -> %d"), who.c_str(), slot == 1 ? 1 : 2, slot == 1 ? 2 : 1), true, "ability");
  return "";
}

std::string Editor::Heal(const Spot& s, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  bool needs = p.hp != p.stats[0] || p.status != 0;
  for (const xs::Move& m : p.moves) needs |= m.id != 0 && m.pp != MaxPp(t, m.id, m.ppUps);
  if (!needs) return "";
  const std::string who = Name(s, &t);
  uint8_t* r = Rec(s);
  xs::Write16(r + kHp, p.stats[0], work_.order);
  r[kStatus] = 0;
  for (int i = 0; i < 4; i++) {
    const xs::Move& m = p.moves[static_cast<size_t>(i)];
    if (m.id != 0 && t.rules != nullptr) r[kMoves + i * 4 + 2] = static_cast<uint8_t>(MaxPp(t, m.id, m.ppUps));
  }
  Refresh();
  Log(Fmt(Tr("%s: healed (HP, PP, status)"), who.c_str()), true, "healing");
  return "";
}

std::string Editor::SetPcMarks(const Spot& s, uint8_t marks) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (marks > 0x0F) return Tr("There are four PC marks.");
  if (At(s)->marks == marks) return "";
  const std::string who = Name(s, nullptr);
  Rec(s)[kMarks] = marks;
  Refresh();
  Log(Fmt(Tr("%s: PC marks changed"), who.c_str()), false, "PC marks");
  return "";
}

std::string Editor::SetPid(const Spot& s, const PidWish& wish, const Tables& t) {
  if (auto why = Refuse(s); !why.empty()) return why;
  if (wish.nature < -1 || wish.nature > 24) return Tr("There are 25 natures.");
  const xs::Pokemon& p = *At(s);
  if (UnpurifiedShadow(p)) {
    return Tr("Nature, gender and shininess of a Shadow Pokemon can't be changed before it is purified: the Shadow list keeps the same number.");
  }
  const int ratio = SexRatioOf(t, p.species);
  if (wish.female >= 0 && ratio < 0) return Tr("This species has no choice of gender.");
  const auto fits = [&](uint32_t pid) {
    if (wish.nature >= 0 && static_cast<int>(pid % 25) != wish.nature) return false;
    if (wish.female >= 0 && ((ratio > static_cast<int>(pid & 0xFF)) ? 1 : 0) != wish.female) return false;
    if (wish.shiny >= 0) {
      const uint32_t x = (p.otId >> 16) ^ (p.otId & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF);
      if ((x < 8) != (wish.shiny == 1)) return false;
    }
    return true;
  };
  if (fits(p.pid)) return "";
  uint32_t pid = 0;
  bool found = false;
  for (int i = 0; i < 4000000 && !found; i++) {
    pid = Rand();
    found = fits(pid);
  }
  if (!found) return Tr("No personality value fits all of that.");
  const std::string who = Name(s, &t);
  const uint32_t before = p.pid;
  const int oldNature = static_cast<int>(before % 25);
  const bool oldShiny = p.IsShiny();
  uint8_t* r = Rec(s);
  xs::Write32(r + kPid, pid, work_.order);
  if (p.shadowId != 0 && p.shadowId < xs::kShadowEntries) {
    const size_t sh = xs::SectionOffsetInSlot(work_, 15);
    if (sh != 0) {
      uint8_t* e = work_.slotBytes.data() + sh + static_cast<size_t>(p.shadowId) * xs::kShadowEntrySize + 0x1C;
      if (xs::Read32(e, work_.order) == before) xs::Write32(e, pid, work_.order);
    }
  }
  Refresh();
  std::string why;
  if (t.rules != nullptr && !FullRecompute(s, t, true, why)) return why;
  Refresh();
  std::string what;
  const auto add = [&](const std::string& part) { what += (what.empty() ? "" : ", ") + part; };
  if (wish.nature >= 0 && wish.nature != oldNature) add(Fmt(Tr("nature %d -> %d"), oldNature, wish.nature));
  if (wish.female >= 0) add(wish.female ? Tr("gender -> female") : Tr("gender -> male"));
  if (wish.shiny >= 0 && (wish.shiny == 1) != oldShiny) add(wish.shiny ? Tr("now shiny") : Tr("no longer shiny"));
  Log(Fmt(Tr("%s: %s (new personality value)"), who.c_str(), what.c_str()), true, "personality");
  return "";
}

std::string Editor::Release(const Spot& s) {
  if (auto why = Refuse(s); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  if (UnpurifiedShadow(p)) return Tr("A Shadow Pokemon can't be released: it is not yours to let go before it is purified.");
  if (s.area == Area::Party) {
    int n = 0;
    for (const xs::Pokemon& q : work_.party) n += q.IsEmpty() ? 0 : 1;
    if (n <= 1) return Tr("The party can't be left empty.");
  }
  const std::string who = Name(s, nullptr);
  std::memset(Rec(s), 0, xs::kPokemonSize);
  if (s.area == Area::Party) PackParty();
  Refresh();
  Log(Fmt(Tr("%s: released"), who.c_str()), true, "release");
  return "";
}

void Editor::PackParty() {
  const size_t h = xs::SectionOffsetInSlot(work_, 2);
  if (h == 0) return;
  uint8_t* base = work_.slotBytes.data() + h + kHeroParty;
  const xs::ByteOrder o = work_.order;
  int to = 0;
  for (int from = 0; from < xs::kPartySize; from++) {
    uint8_t* a = base + static_cast<size_t>(from) * xs::kPokemonSize;
    if (xs::Read16(a, o) == 0) continue;
    if (from != to) {
      std::memcpy(base + static_cast<size_t>(to) * xs::kPokemonSize, a, xs::kPokemonSize);
      std::memset(a, 0, xs::kPokemonSize);
    }
    to++;
  }
}

std::string Editor::Swap(const Spot& a, const Spot& b) {
  if (!Valid(a) || !Valid(b)) return Tr("That place does not exist.");
  if (a == b) return "";
  uint8_t* ra = Rec(a);
  uint8_t* rb = Rec(b);
  if (ra == nullptr || rb == nullptr) return Tr("This save has no Pokemon section.");
  if (At(a)->IsEmpty() && At(b)->IsEmpty()) return "";
  const std::string wa = Name(a, nullptr), wb = Name(b, nullptr);
  if (a.area == Area::Party || b.area == Area::Party) {
    int n = 0;
    for (const xs::Pokemon& q : work_.party) n += q.IsEmpty() ? 0 : 1;
    const bool aIn = a.area == Area::Party, bIn = b.area == Area::Party;
    int after = n;
    if (aIn && !bIn) after += (At(b)->IsEmpty() ? 0 : 1) - (At(a)->IsEmpty() ? 0 : 1);
    if (bIn && !aIn) after += (At(a)->IsEmpty() ? 0 : 1) - (At(b)->IsEmpty() ? 0 : 1);
    if (after < 1) return Tr("The party can't be left empty.");
  }
  std::array<uint8_t, xs::kPokemonSize> tmp;
  std::memcpy(tmp.data(), ra, tmp.size());
  std::memcpy(ra, rb, tmp.size());
  std::memcpy(rb, tmp.data(), tmp.size());
  if (a.area == Area::Party || b.area == Area::Party) PackParty();
  Refresh();
  Log(Fmt(Tr("%s <-> %s: moved"), wa.c_str(), wb.c_str()), false, "move place");
  return "";
}

std::string Editor::Add(uint16_t species, int level, bool intoParty, const Tables& t, Spot& where) {
  if (t.rules == nullptr || species >= t.rules->species.size() || !t.rules->species[species].exists) return Tr("The game's tables don't have that species.");
  if (level < 1 || level > 100) return Tr("The level must be from 1 to 100.");
  if (xs::SectionOffsetInSlot(work_, 2) == 0 || xs::SectionOffsetInSlot(work_, 3) == 0) return Tr("This save has no Pokemon section.");
  Spot to;
  bool found = false;
  if (intoParty) {
    for (int i = 0; i < xs::kPartySize && !found; i++) {
      to = {Area::Party, 0, i};
      found = At(to)->IsEmpty();
    }
    if (!found) return Tr("The party is full.");
  } else {
    for (int b = 0; b < xs::kBoxCount && !found; b++) {
      for (int i = 0; i < xs::kBoxSlots && !found; i++) {
        to = {Area::Box, b, i};
        found = At(to)->IsEmpty();
      }
    }
    if (!found) return Tr("All the PC boxes are full.");
  }
  const xs::Pokemon* tmpl = nullptr;
  for (const xs::Pokemon& p : work_.party) {
    if (!p.IsEmpty() && p.shadowId == 0 && p.otId == work_.trainerOtId && p.otName == work_.trainerName) {
      tmpl = &p;
      break;
    }
  }
  if (tmpl == nullptr) {
    for (const xs::Pokemon& p : work_.party) {
      if (!p.IsEmpty() && p.shadowId == 0) {
        tmpl = &p;
        break;
      }
    }
  }
  if (tmpl == nullptr) return Tr("Nothing to copy the game's own record layout from: the save needs one Pokemon of its own that was not a Shadow Pokemon.");
  const xs::ByteOrder o = work_.order;
  const std::string who = Upper(SpeciesText(&t, species)) + " (" + SpotText(to) + ")";
  uint8_t* r = Rec(to);
  std::memcpy(r, tmpl->raw.data(), xs::kPokemonSize);
  const auto& sp = t.rules->species[species];
  xs::Write16(r + kSpecies, species, o);
  xs::Write16(r + kHeld, 0, o);
  xs::Write16(r + kFriend, static_cast<uint16_t>(t.baseFriendship ? t.baseFriendship(species) : sp.initFriend), o);
  r[0x0E] = static_cast<uint8_t>(level);
  r[kLevel] = static_cast<uint8_t>(level);
  r[0x12] = 0;
  r[0x13] = 0;
  r[kMarks] = 0;
  r[kStatus] = 0;
  r[kFlags] = static_cast<uint8_t>(r[kFlags] & 0x00);
  xs::Write32(r + 0x7C, 0, o);
  xs::Write16(r + kShadowId, 0, o);
  xs::Write32(r + kOtId, work_.trainerOtId, o);
  PutName(r + kOtName, kNameChars, work_.trainerName, o);
  const std::string nick = SpeciesText(&t, species);
  const std::u16string nick16(nick.begin(), nick.end());
  PutName(r + kNick, kNameChars, nick16, o);
  PutName(r + kNickOrg, kNameChars, nick16, o);
  xs::Write32(r + kPid, Rand(), o);
  uint32_t exp = 0;
  xd::legality::ExpForLevel(*t.rules, sp.growth, level, exp);
  xs::Write32(r + kExp, exp, o);
  for (int i = 0; i < 6; i++) {
    r[kIvs + i] = static_cast<uint8_t>(Rand() % 32);
    xs::Write16(r + kEvs + i * 2, 0, o);
  }
  std::memset(r + kContest, 0, 10);
  std::vector<uint16_t> moves;
  for (const auto& lm : sp.levelUp) {
    if (lm.level <= level && lm.move != 0 && std::find(moves.begin(), moves.end(), lm.move) == moves.end()) moves.push_back(lm.move);
  }
  if (moves.size() > 4) moves.erase(moves.begin(), moves.end() - 4);
  for (int i = 0; i < 4; i++) {
    uint8_t* w = r + kMoves + i * 4;
    const uint16_t m = i < static_cast<int>(moves.size()) ? moves[static_cast<size_t>(i)] : 0;
    xs::Write16(w, m, o);
    w[2] = m == 0 ? 0 : static_cast<uint8_t>(MaxPp(t, m, 0));
    w[3] = 0;
  }
  Refresh();
  std::string why;
  if (!FullRecompute(to, t, false, why)) {
    std::memset(r, 0, xs::kPokemonSize);
    Refresh();
    return why;
  }
  const xs::Pokemon made = xs::ParsePokemon(r, o);
  xs::Write16(r + kHp, made.stats[0], o);
  Refresh();
  where = to;
  Log(Fmt(Tr("%s: new Pokemon, level %d"), who.c_str(), level), true, "new Pokemon");
  return "";
}

std::string Editor::SetBoxName(int box, const std::u16string& name) {
  if (box < 0 || box >= xs::kBoxCount) return Tr("That box does not exist.");
  if (auto why = CheckName(name, MaxBoxName(options_)); !why.empty()) return why;
  if (work_.boxes[static_cast<size_t>(box)].name == name) return "";
  const size_t b = xs::SectionOffsetInSlot(work_, 3);
  if (b == 0) return Tr("This save has no PC section.");
  PutName(work_.slotBytes.data() + b + static_cast<size_t>(box) * kBoxStride, 10, name, work_.order);
  Refresh();
  Log(Fmt(Tr("Box %d: name -> %s"), box + 1, xs::ToUtf8(name).c_str()), false, "box name");
  return "";
}

std::string Editor::SetTrainerName(const std::u16string& name) {
  if (auto why = CheckName(name, MaxTrainerName(options_)); !why.empty()) return why;
  if (work_.trainerName == name) return "";
  const size_t h = xs::SectionOffsetInSlot(work_, 2);
  if (h == 0) return Tr("This save has no trainer section.");
  const xs::ByteOrder o = work_.order;
  const std::u16string old = work_.trainerName;
  const uint32_t id = work_.trainerOtId;
  PutName(work_.slotBytes.data() + h + kHeroName, kNameChars, name, o);
  const auto follow = [&](uint8_t* rec) {
    if (xs::Read16(rec, o) == 0) return;
    if (xs::Read32(rec + kOtId, o) != id) return;
    if (xs::ReadString(rec + kOtName, kNameChars, o) != old) return;
    PutName(rec + kOtName, kNameChars, name, o);
  };
  for (int i = 0; i < xs::kPartySize; i++) follow(work_.slotBytes.data() + h + kHeroParty + static_cast<size_t>(i) * xs::kPokemonSize);
  if (const size_t b = xs::SectionOffsetInSlot(work_, 3); b != 0) {
    for (int i = 0; i < xs::kBoxCount; i++) {
      for (int j = 0; j < xs::kBoxSlots; j++) {
        follow(work_.slotBytes.data() + b + static_cast<size_t>(i) * kBoxStride + kBoxPokemon + static_cast<size_t>(j) * xs::kPokemonSize);
      }
    }
  }
  if (const size_t d = xs::SectionOffsetInSlot(work_, 11); d != 0) follow(work_.slotBytes.data() + d + 8);
  if (const size_t c = xs::SectionOffsetInSlot(work_, 21); c != 0) {
    for (size_t i = 0; i < 9; i++) {
      for (size_t j = 0; j < 5; j++) follow(work_.slotBytes.data() + c + i * kChamberStride + j * xs::kPokemonSize);
    }
  }
  Refresh();
  Log(Fmt(Tr("Trainer name %s -> %s"), xs::ToUtf8(old).c_str(), xs::ToUtf8(name).c_str()), false, "trainer name");
  return "";
}

std::string Editor::SetMoney(uint32_t money) {
  if (money > kMaxMoney) return Tr("Money goes up to 999,999.");
  if (work_.money == money) return "";
  const size_t h = xs::SectionOffsetInSlot(work_, 2);
  if (h == 0) return Tr("This save has no trainer section.");
  const uint32_t before = work_.money;
  xs::Write32(work_.slotBytes.data() + h + kHeroMoney, money, work_.order);
  Refresh();
  Log(Fmt(Tr("Money %u -> %u"), before, money), true, "money");
  return "";
}

std::string Editor::SetCoupons(uint32_t coupons) {
  if (coupons > kMaxCoupons) return Tr("Poke Coupons go up to 99,999.");
  if (work_.coupons == coupons) return "";
  const size_t h = xs::SectionOffsetInSlot(work_, 2);
  if (h == 0) return Tr("This save has no trainer section.");
  const uint32_t before = work_.coupons;
  xs::Write32(work_.slotBytes.data() + h + kHeroCoupons, coupons, work_.order);
  Refresh();
  Log(Fmt(Tr("Poke Coupons %u -> %u"), before, coupons), true, "coupons");
  return "";
}

std::string Editor::SetPlayTime(double seconds) {
  if (!(seconds >= 0) || seconds > kMaxPlayTime) return Tr("Play time goes up to 999 hours 59 minutes 59 seconds.");
  seconds = std::floor(seconds);
  if (work_.playTimeSeconds == seconds) return "";
  const size_t g = xs::SectionOffsetInSlot(work_, 1);
  if (g == 0) return Tr("This save has no game-data section.");
  const double before = work_.playTimeSeconds;
  WriteF64(work_.slotBytes.data() + g + kPlayTimeAt, seconds, work_.order);
  Refresh();
  Log(Fmt(Tr("Play time %.0f:%02d -> %.0f:%02d hours"), std::floor(before / 3600), static_cast<int>(before) / 60 % 60, std::floor(seconds / 3600),
          static_cast<int>(seconds) / 60 % 60),
      true, "play time");
  return "";
}

size_t Editor::PocketOffset(Pocket p) const {
  const size_t h = xs::SectionOffsetInSlot(work_, 2);
  return h == 0 ? 0 : h + kPockets[static_cast<int>(p)].offset;
}

namespace {
const std::vector<xs::ItemSlot>& Slots(const xs::Save& s, Pocket p) {
  switch (p) {
    case Pocket::Items: return s.items;
    case Pocket::KeyItems: return s.keyItems;
    case Pocket::Balls: return s.balls;
    case Pocket::TmHm: return s.tmHm;
    case Pocket::Berries: return s.berries;
    case Pocket::Colognes: return s.colognes;
    default: return s.battleCds;
  }
}
}

std::string Editor::SetBagSlot(Pocket p, int index, uint16_t item, int count, const Tables& t) {
  const size_t base = PocketOffset(p);
  if (base == 0) return Tr("This save has no bag section.");
  if (index < 0 || index >= PocketSize(p)) return Tr("That bag place does not exist.");
  if (count < 0 || count > kMaxBagCount) return Tr("A stack holds 1 to 999.");
  if (item == 0 || count == 0) {
    item = 0;
    count = 0;
  }
  const xs::ItemSlot before = Slots(work_, p)[static_cast<size_t>(index)];
  if (before.item == item && before.count == count) return "";
  if (item != 0) {
    if (t.rules == nullptr || item >= t.rules->items.size() || !t.rules->items[item].exists) return Tr("The game's tables don't have that item.");
    if (PocketOfItem(t, item) != static_cast<int>(p)) return Fmt(Tr("That item doesn't go in the %s pocket."), PocketName(p));
    for (int i = 0; i < PocketSize(p); i++) {
      if (i != index && Slots(work_, p)[static_cast<size_t>(i)].item == item && Slots(work_, p)[static_cast<size_t>(i)].count != 0) return Tr("That item is in this pocket already: change its stack instead.");
    }
  }
  uint8_t* w = work_.slotBytes.data() + base + static_cast<size_t>(index) * 4;
  xs::Write16(w, item, work_.order);
  xs::Write16(w + 2, static_cast<uint16_t>(count), work_.order);
  Refresh();
  std::string what;
  if (before.count == 0) what = Fmt(Tr("added %s x%d"), ItemText(t, item).c_str(), count);
  else if (count == 0) what = Fmt(Tr("removed %s x%d"), ItemText(t, before.item).c_str(), static_cast<int>(before.count));
  else what = Fmt(Tr("%s x%d -> %s x%d"), ItemText(t, before.item).c_str(), static_cast<int>(before.count), ItemText(t, item).c_str(), count);
  Log(Fmt(Tr("Bag (%s): %s"), PocketName(p), what.c_str()), true, "bag");
  return "";
}

std::string Editor::RemoveBagSlot(Pocket p, int index) {
  const size_t base = PocketOffset(p);
  if (base == 0) return Tr("This save has no bag section.");
  const int n = PocketSize(p);
  if (index < 0 || index >= n) return Tr("That bag place does not exist.");
  const xs::ItemSlot before = Slots(work_, p)[static_cast<size_t>(index)];
  if (before.item == 0 && before.count == 0) return "";
  uint8_t* b = work_.slotBytes.data() + base;
  std::memmove(b + static_cast<size_t>(index) * 4, b + static_cast<size_t>(index + 1) * 4, static_cast<size_t>(n - index - 1) * 4);
  std::memset(b + static_cast<size_t>(n - 1) * 4, 0, 4);
  Refresh();
  Log(Fmt(Tr("Bag (%s): removed item #%d x%d"), PocketName(p), static_cast<int>(before.item), static_cast<int>(before.count)), true, "bag");
  return "";
}

std::string Editor::AddBagItem(Pocket p, uint16_t item, int count, const Tables& t) {
  if (count < 1 || count > kMaxBagCount) return Tr("A stack holds 1 to 999.");
  const int n = PocketSize(p);
  for (int i = 0; i < n; i++) {
    const xs::ItemSlot s = Slots(work_, p)[static_cast<size_t>(i)];
    if (s.item == item && s.count != 0) {
      if (s.count + count > kMaxBagCount) return Tr("A stack holds at most 999.");
      return SetBagSlot(p, i, item, s.count + count, t);
    }
  }
  for (int i = 0; i < n; i++) {
    const xs::ItemSlot s = Slots(work_, p)[static_cast<size_t>(i)];
    if (s.item == 0 || s.count == 0) return SetBagSlot(p, i, item, count, t);
  }
  return Tr("This pocket is full.");
}

std::string Editor::MoveBagSlot(Pocket p, int from, int to) {
  const size_t base = PocketOffset(p);
  if (base == 0) return Tr("This save has no bag section.");
  const int n = PocketSize(p);
  if (from < 0 || from >= n || to < 0 || to >= n) return Tr("That bag place does not exist.");
  if (from == to) return "";
  uint8_t* b = work_.slotBytes.data() + base;
  uint8_t tmp[4];
  std::memcpy(tmp, b + static_cast<size_t>(from) * 4, 4);
  if (from < to) std::memmove(b + static_cast<size_t>(from) * 4, b + static_cast<size_t>(from + 1) * 4, static_cast<size_t>(to - from) * 4);
  else std::memmove(b + static_cast<size_t>(to + 1) * 4, b + static_cast<size_t>(to) * 4, static_cast<size_t>(from - to) * 4);
  std::memcpy(b + static_cast<size_t>(to) * 4, tmp, 4);
  Refresh();
  Log(Fmt(Tr("Bag (%s): reordered"), PocketName(p)), false, "bag order");
  return "";
}

std::string Editor::OpenHeart(const Spot& s) {
  if (auto why = Refuse(s); !why.empty()) return why;
  const xs::Pokemon& p = *At(s);
  if (p.shadowId == 0) return Tr("That Pokemon is not a Shadow Pokemon.");
  if (p.shadowId >= xs::kShadowEntries) return Tr("This Shadow Pokemon's record is kept in the save extension (More Shadow Pokemon): its heart can't be opened here yet.");
  const xs::ShadowEntry& e = work_.shadows[p.shadowId];
  if (e.Purified()) return Tr("That Pokemon is purified already.");
  if (!e.Snagged()) return Tr("That Shadow Pokemon is not caught yet.");
  if (e.heartGauge == 0) return "";
  const size_t sh = xs::SectionOffsetInSlot(work_, 15);
  if (sh == 0) return Tr("This save has no Shadow list.");
  const std::string who = Name(s, nullptr);
  const int before = e.heartGauge;
  xs::Write32(work_.slotBytes.data() + sh + static_cast<size_t>(p.shadowId) * xs::kShadowEntrySize + 0x24, 0, work_.order);
  Refresh();
  Log(Fmt(Tr("%s: heart opened (gauge %d -> 0)"), who.c_str(), before), true, "shadow heart");
  return "";
}

std::vector<uint16_t> Editor::MemoSpecies() const {
  std::vector<uint16_t> out;
  const size_t m = xs::SectionOffsetInSlot(work_, 12);
  if (m == 0) return out;
  const uint8_t* mm = work_.slotBytes.data() + m;
  const uint16_t count = xs::Read16(mm, work_.order);
  for (uint16_t i = 0; i < count && i < 500; i++) out.push_back(static_cast<uint16_t>(xs::Read16(mm + 4 + i * 12, work_.order) & 0x3FFF));
  return out;
}

std::string Editor::AddMemoSpecies(uint16_t species, const Tables& t) {
  if (t.rules == nullptr || species == 0 || species >= t.rules->species.size() || !t.rules->species[species].exists) return Tr("The game's tables don't have that species.");
  const size_t m = xs::SectionOffsetInSlot(work_, 12);
  if (m == 0) return Tr("This save has no Strategy Memo.");
  uint8_t* mm = work_.slotBytes.data() + m;
  const xs::ByteOrder o = work_.order;
  const uint16_t count = xs::Read16(mm, o);
  for (uint16_t i = 0; i < count && i < 500; i++) {
    if ((xs::Read16(mm + 4 + i * 12, o) & 0x3FFF) == species) return "";
  }
  if (count >= 500) return Tr("The Strategy Memo is full (500 entries): the rest are kept in the save extension, which the editor does not change yet.");
  uint8_t* e = mm + 4 + static_cast<size_t>(count) * 12;
  xs::Write16(e, static_cast<uint16_t>(species | 0x8000), o);
  xs::Write32(e + 4, work_.trainerOtId, o);
  xs::Write32(e + 8, Rand(), o);
  xs::Write16(mm, static_cast<uint16_t>(count + 1), o);
  Refresh();
  Log(Fmt(Tr("Strategy Memo: %s added"), SpeciesText(&t, species).c_str()), true, "strategy memo");
  return "";
}

std::string Editor::RemoveMemoSpecies(uint16_t species, const Tables& t) {
  const size_t m = xs::SectionOffsetInSlot(work_, 12);
  if (m == 0) return Tr("This save has no Strategy Memo.");
  uint8_t* mm = work_.slotBytes.data() + m;
  const xs::ByteOrder o = work_.order;
  const uint16_t count = xs::Read16(mm, o);
  for (uint16_t i = 0; i < count && i < 500; i++) {
    if ((xs::Read16(mm + 4 + i * 12, o) & 0x3FFF) != species) continue;
    uint8_t* e = mm + 4 + static_cast<size_t>(i) * 12;
    std::memmove(e, e + 12, static_cast<size_t>(count - i - 1) * 12);
    std::memset(mm + 4 + static_cast<size_t>(count - 1) * 12, 0, 12);
    xs::Write16(mm, static_cast<uint16_t>(count - 1), o);
    Refresh();
    Log(Fmt(Tr("Strategy Memo: %s removed"), SpeciesText(&t, species).c_str()), true, "strategy memo");
    return "";
  }
  return "";
}

std::vector<std::pair<Spot, xs::Pokemon>> Editor::ChangedPokemon() const {
  std::multiset<std::array<uint8_t, xs::kPokemonSize>> before;
  for (const xs::Pokemon& p : orig_.party) before.insert(p.raw);
  for (const xs::Box& b : orig_.boxes) {
    for (const xs::Pokemon& p : b.slots) before.insert(p.raw);
  }
  std::vector<std::pair<Spot, xs::Pokemon>> out;
  const auto look = [&](const Spot& s, const xs::Pokemon& p) {
    if (p.IsEmpty()) return;
    const auto it = before.find(p.raw);
    if (it != before.end()) {
      before.erase(it);
      return;
    }
    out.push_back({s, p});
  };
  for (int i = 0; i < xs::kPartySize; i++) look({Area::Party, 0, i}, work_.party[static_cast<size_t>(i)]);
  for (int b = 0; b < xs::kBoxCount; b++) {
    for (int i = 0; i < xs::kBoxSlots; i++) look({Area::Box, b, i}, work_.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(i)]);
  }
  return out;
}

std::vector<std::string> Editor::Problems(const Tables& t) const {
  std::vector<std::string> out;
  if (t.rules == nullptr) return {"The playthrough's game tables are not known."};
  for (const auto& [spot, p] : ChangedPokemon()) {
    xd::legality::Context ctx;
    ctx.shadow = work_.ShadowOf(p);
    ctx.shadowStaysInItsSave = true;
    const xd::legality::Result r = xd::legality::Check(p, *t.rules, xd::legality::Level::Safe, ctx);
    for (const xd::legality::Finding& f : r.findings) out.push_back(Name(spot, &t) + ": " + f.text);
  }
  for (const std::string& u : xs::UnknownIds(work_, {static_cast<uint32_t>(t.rules->species.size()), static_cast<uint32_t>(t.rules->moves.size()),
                                                     static_cast<uint32_t>(t.rules->items.size())})) {
    bool inOriginal = false;
    for (const std::string& o : xs::UnknownIds(orig_, {static_cast<uint32_t>(t.rules->species.size()), static_cast<uint32_t>(t.rules->moves.size()),
                                                       static_cast<uint32_t>(t.rules->items.size())})) {
      inOriginal |= o == u;
    }
    if (!inOriginal) out.push_back(u);
  }
  return out;
}

std::vector<std::string> Editor::Notes(const Tables& t) const {
  std::vector<std::string> out;
  if (t.rules == nullptr) return out;
  for (const auto& [spot, p] : ChangedPokemon()) {
    xd::legality::Context ctx;
    ctx.shadow = work_.ShadowOf(p);
    ctx.shadowStaysInItsSave = true;
    const xd::legality::Result r = xd::legality::Check(p, *t.rules, xd::legality::Level::Legal, ctx);
    for (const xd::legality::Finding& f : r.findings) {
      if (f.level == xd::legality::Level::Legal) out.push_back(Name(spot, &t) + ": " + f.text);
    }
  }
  return out;
}

std::vector<uint8_t> Editor::Build(std::string& error) const {
  if (!Dirty()) {
    error = Tr("Nothing was changed.");
    return {};
  }
  const std::vector<uint8_t> out = xs::WriteSaveFile(file_, work_, report_, false, error);
  if (out.empty()) return {};
  xs::Save check;
  xs::ReadReport rep;
  std::string err;
  if (!xs::ReadSave(out, check, rep, err) || check.slot == orig_.slot) {
    error = Fmt(Tr("The new save did not pass the program's own check (%s)."), err.c_str());
    return {};
  }
  bool same = check.money == work_.money && check.coupons == work_.coupons && check.trainerName == work_.trainerName &&
              check.playTimeSeconds == work_.playTimeSeconds;
  for (size_t i = 0; i < work_.party.size(); i++) same &= SameBytes(check.party[i], work_.party[i]);
  for (size_t b = 0; b < work_.boxes.size(); b++) {
    same &= check.boxes[b].name == work_.boxes[b].name;
    for (size_t i = 0; i < work_.boxes[b].slots.size(); i++) same &= SameBytes(check.boxes[b].slots[i], work_.boxes[b].slots[i]);
  }
  for (Pocket p : {Pocket::Items, Pocket::KeyItems, Pocket::Balls, Pocket::TmHm, Pocket::Berries, Pocket::Colognes, Pocket::BattleCds}) {
    const auto& a = Slots(check, p);
    const auto& b = Slots(work_, p);
    same &= a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); i++) same &= a[i].item == b[i].item && a[i].count == b[i].count;
  }
  if (!same) {
    error = Tr("The new save does not read back the way it was edited.");
    return {};
  }
  return out;
}

}
