#include "xd_battles_won.h"
#include "xd_challenges.h"

#include "data/xd_data_mods.h"

#include <data_mods.h>
#include <gs_formats.h>
#include <port_disc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

extern "C" {
void* fightEncountDataBiosGetPtr(u16 id);
u16 fightEncountDataBiosGetFightTrainerDataId(void* data, u8 index);
u16 fightEncountDataBiosGetFightDeckGrpId(void* data, u8 index);
u32 fightEncountDataBiosGetFightKind(void* data);
u32 getDeckResourceID(u8 kind);
u16* GSmsgGetGSchar(u32 key);
extern u32* fight_encount_data_number;
void PortXD_ReadTrainerLevels(void);
}

namespace {
namespace em = engine::mods;

struct Deck {
  uint16_t resource;
  std::string file;
  std::vector<uint8_t> maxLevel;
  std::vector<uint16_t> name;
};
std::vector<Deck> sDecks;
bool sRead = false;

constexpr int kCapOffset = 0;

struct Boss {
  u32 after;
  u16 battles[4];
};
const Boss kBosses[] = {
    {100, {224}},
    {270, {235}},
    {350, {268}},
    {480, {278}},
    {490, {289}},
    {580, {306}},
    {680, {326, 327, 329}},
    {730, {330}},
    {751, {349, 441}},
    {770, {336}},
    {800, {347}},
    {810, {349, 441}},
    {860, {367}},
    {900, {375}},
    {900, {393}},
    {940, {396}},
    {940, {395}},
    {940, {398}},
    {960, {401, 402}},
};
constexpr bool kCapNeverFalls = true;

const em::DataField* FieldOf(const em::DataTable& t, const char* name) {
  for (const em::DataField& f : t.fields) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

}

extern "C" void PortXD_ReadTrainerLevels(void) {
  sRead = true;
  sDecks.clear();
  size_t size = 0;
  uint8_t* archive = PortDisc_ReadFile("deck_archive.fsys", &size);
  FsysArchive fsys;
  if (archive == nullptr || !Fsys_Open(archive, size, &fsys)) {
    std::fprintf(stderr, "[level cap] the disc's trainer data (deck_archive.fsys) could not be read: no level caps this run\n");
    PortDisc_Free(archive);
    return;
  }
  std::map<std::string, std::vector<uint8_t>> files;
  std::map<std::string, uint16_t> resources;
  const em::EntryReader read = [&](const std::string& a, const std::string& entry, std::vector<uint8_t>& out) {
    if (a != "deck_archive.fsys") return false;
    if (const auto it = files.find(entry); it != files.end()) {
      out = it->second;
      return true;
    }
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e;
      if (!Fsys_Entry(&fsys, i, &e)) continue;
      if ((e.fullName == nullptr || entry != e.fullName) && (e.name == nullptr || entry != e.name)) continue;
      uint32_t n = 0;
      uint8_t* p = Fsys_Extract(&fsys, i, &n);
      if (p == nullptr) return false;
      out.assign(p, p + n);
      std::free(p);
      files[entry] = out;
      resources[entry] = e.id;
      return true;
    }
    return false;
  };
  em::DataCatalog catalog;
  xd::data::AddDeckTables(read, catalog);
  const em::DataTable* shadows = catalog.FindTable("darkpokemon_shadow");
  const em::DataField* shadowDeck = shadows != nullptr ? FieldOf(*shadows, "deckPokemonID") : nullptr;
  auto bytesOf = [&](const em::DataTable& t, uint32_t entry) -> const uint8_t* {
    if (t.places.empty() || entry >= t.count) return nullptr;
    const auto it = files.find(t.places[0].entry);
    const size_t at = t.places[0].offset + static_cast<size_t>(entry) * t.entrySize;
    return it != files.end() && at + t.entrySize <= it->second.size() ? it->second.data() + at : nullptr;
  };
  for (const em::DataTable& trainers : catalog.tables) {
    const std::string suffix = "_trainers";
    if (trainers.name.size() <= suffix.size() || trainers.name.compare(trainers.name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
    const em::DataTable* pokemon = catalog.FindTable(trainers.name.substr(0, trainers.name.size() - suffix.size()) + "_pokemon");
    const em::DataField* team = FieldOf(trainers, "pokemonDataID");
    const em::DataField* flags = FieldOf(trainers, "darkPokemonFlags");
    const em::DataField* name = FieldOf(trainers, "trainerName");
    const em::DataField* level = pokemon != nullptr ? FieldOf(*pokemon, "level") : nullptr;
    if (team == nullptr || flags == nullptr || name == nullptr || level == nullptr || trainers.places.empty()) continue;
    Deck deck;
    deck.file = trainers.places[0].entry;
    deck.resource = resources[deck.file];
    for (uint32_t t = 0; t < trainers.count; t++) {
      const uint8_t* tr = bytesOf(trainers, t);
      uint8_t best = 0;
      for (uint32_t k = 0; tr != nullptr && k < team->count; k++) {
        uint32_t id = em::ReadDataField(tr, *team, k);
        if (id == 0) continue;
        if ((em::ReadDataField(tr, *flags, 0) >> k & 1u) != 0) {
          const uint8_t* s = shadows != nullptr && shadowDeck != nullptr ? bytesOf(*shadows, id) : nullptr;
          id = s != nullptr ? em::ReadDataField(s, *shadowDeck, 0) : 0;
        }
        const uint8_t* p = id != 0 ? bytesOf(*pokemon, id) : nullptr;
        if (p != nullptr) best = std::max(best, static_cast<uint8_t>(em::ReadDataField(p, *level, 0)));
      }
      deck.maxLevel.push_back(best);
      deck.name.push_back(tr != nullptr ? static_cast<uint16_t>(em::ReadDataField(tr, *name, 0)) : 0);
    }
    sDecks.push_back(std::move(deck));
  }
  if (sDecks.empty() || std::getenv("XD_LEVEL_CAP_LOG") != nullptr) {
    FsysEntry e = {};
    std::fprintf(stderr, "[level cap] trainer data read: %u files in deck_archive.fsys (the first: %s / %s), %zu tables, %zu sets of trainers\n",
                 fsys.count, fsys.count != 0 && Fsys_Entry(&fsys, 0, &e) && e.name != nullptr ? e.name : "?",
                 e.fullName != nullptr ? e.fullName : "?", catalog.tables.size(), sDecks.size());
  }
  PortDisc_Free(archive);
}

namespace xd::challenges {

namespace {
const Deck* DeckOfGroup(u32 group) {
  const uint16_t resource = static_cast<uint16_t>(getDeckResourceID(static_cast<u8>(group)) >> 16);
  for (const Deck& d : sDecks) {
    if (d.resource == resource && resource != 0) return &d;
  }
  return nullptr;
}

std::string Text(u32 key) {
  const u16* t = key != 0 ? GSmsgGetGSchar(key) : nullptr;
  std::string out;
  for (int n = 0; t != nullptr && n < 40 && t[n] != 0 && t[n] != 0xFFFF; n++) {
    out += t[n] >= 0x20 && t[n] < 0x7F ? static_cast<char>(t[n]) : '?';
  }
  return out;
}
}

int BattleMaxLevel(unsigned battle, std::string* who) {
  if (fight_encount_data_number == nullptr || battle == 0 || battle >= *fight_encount_data_number) return 0;
  void* d = fightEncountDataBiosGetPtr(static_cast<u16>(battle));
  if (d == nullptr) return 0;
  const Deck* decks[4] = {};
  u32 trainers[4] = {};
  for (u8 i = 0; i < 4; i++) {
    const u32 group = fightEncountDataBiosGetFightDeckGrpId(d, i);
    trainers[i] = fightEncountDataBiosGetFightTrainerDataId(d, i);
    decks[i] = group != 0 && trainers[i] != 0 ? DeckOfGroup(group) : nullptr;
    if (decks[i] != nullptr && trainers[i] >= decks[i]->maxLevel.size()) decks[i] = nullptr;
  }
  const int perSide = trainers[2] != 0 || trainers[3] != 0 ? 2 : 1;
  int best = 0;
  for (int i = 0; i < 4; i++) {
    if (decks[i] == nullptr) continue;
    const bool opponent = i >= perSide && i < 2 * perSide;
    if (who != nullptr) {
      *who += (who->empty() ? "" : " + ") + Text(decks[i]->name[trainers[i]]) + " (place " + std::to_string(i) +
              (opponent ? ", opponent, " : ", player's side, ") + decks[i]->file + " #" + std::to_string(trainers[i]) + ", level " +
              std::to_string(decks[i]->maxLevel[trainers[i]]) + ")";
    }
    if (opponent) best = std::max<int>(best, decks[i]->maxLevel[trainers[i]]);
  }
  return best;
}

namespace {
int BossLevel(const Boss& b, unsigned* from) {
  int level = 0;
  for (u16 battle : b.battles) {
    const int l = battle != 0 ? BattleMaxLevel(battle, nullptr) : 0;
    if (l > level) {
      level = l;
      if (from != nullptr) *from = battle;
    }
  }
  return level;
}

bool AllWon(const Boss& b) {
  for (u16 battle : b.battles) {
    if (battle != 0 && !xd::battles::Won(battle)) return false;
  }
  return true;
}
}

int LevelCapAt(unsigned story, unsigned* bossBattle) {
  if (bossBattle != nullptr) *bossBattle = 0;
  const bool wonKnown = xd::battles::Complete();
  int before = 0;
  const size_t count = sizeof kBosses / sizeof kBosses[0];
  for (size_t i = 0; i < count; i++) {
    const Boss& b = kBosses[i];
    unsigned from = 0;
    int level = BossLevel(b, &from);
    if (story >= b.after || (wonKnown && AllWon(b))) {
      if (kCapNeverFalls) before = std::max(before, level);
      continue;
    }
    if (bossBattle != nullptr) *bossBattle = from;
    if (level <= 0) return 100;
    if (!wonKnown) {
      for (size_t k = i + 1; k < count && kBosses[k].after == b.after; k++) {
        const int l = BossLevel(kBosses[k], nullptr);
        if (l <= 0) return 100;
        level = std::max(level, l);
      }
    }
    return std::clamp(std::max(level, before) + kCapOffset, 1, 100);
  }
  return 100;
}

void LogLevelCaps() {
  if (!sRead) std::fprintf(stderr, "[level cap] the trainer data was not read at start-up\n");
  const char* env = std::getenv("XD_LEVEL_CAP_LOG");
  if (env != nullptr && std::strcmp(env, "battles") == 0 && fight_encount_data_number != nullptr) {
    for (const Deck& d : sDecks) {
      std::fprintf(stderr, "[level cap] trainer data %s (resource 0x%04X): %zu trainers\n", d.file.c_str(), d.resource, d.maxLevel.size());
    }
    for (u8 group = 1; group <= 7; group++) {
      std::fprintf(stderr, "[level cap] deck group %u is resource 0x%04X\n", group, getDeckResourceID(group) >> 16);
    }
    for (u32 i = 1; i < *fight_encount_data_number; i++) {
      std::string who;
      const int level = BattleMaxLevel(i, &who);
      if (!who.empty()) {
        std::fprintf(stderr, "[level cap] battle %u kind %u: highest level %d: %s\n", i,
                     fightEncountDataBiosGetFightKind(fightEncountDataBiosGetPtr(static_cast<u16>(i))), level, who.c_str());
      }
    }
  }
  u32 from = 0;
  int before = 0;
  const Boss* previous = nullptr;
  for (const Boss& b : kBosses) {
    std::string who;
    for (u16 battle : b.battles) {
      if (battle == 0) continue;
      std::string one;
      BattleMaxLevel(battle, &one);
      who += (who.empty() ? "battle " : "; battle ") + std::to_string(battle) + ": " + (one.empty() ? "trainers not known" : one);
    }
    const int level = BossLevel(b, nullptr);
    const int cap = level <= 0 ? 100 : std::clamp((kCapNeverFalls ? std::max(level, before) : level) + kCapOffset, 1, 100);
    if (previous != nullptr && previous->after == b.after) {
      std::fprintf(stderr, "[level cap] ... then, once that battle is won: cap %d (%s)\n", cap, who.c_str());
    } else {
      std::fprintf(stderr, "[level cap] story %u to %u: cap %d (%s)\n", from, b.after - 1, cap, who.c_str());
    }
    before = std::max(before, level);
    from = b.after;
    previous = &b;
  }
  std::fprintf(stderr, "[level cap] story %u on: no cap\n", from);
  if (!xd::battles::Complete()) {
    std::fprintf(stderr, "[level cap] this playthrough's won battles are not known from its start: bosses that share a "
                         "story point count together (the highest level among them)\n");
  }
}

}
