#include "xd_pokedex.h"

#include "ui_kit2.h"

#include "data/xd_data_mods.h"

#include <data_mods.h>
#include <gs_formats.h>
#include <pokedex.h>

#include <dolphin/types.h>
#include <game/pxdvs/app/fight/fightData.h>
#include <game/relglobal.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
u32 getDeckResourceID(u8 kind);
u16* GSmsgGetGSchar(u32 msg);
}

namespace xd::pokedex {
namespace {
namespace em = engine::mods;
namespace ep = engine::pokedex;

const em::DataField* FieldOf(const em::DataTable& t, const char* name) {
  for (const em::DataField& f : t.fields)
    if (f.name == name) return &f;
  return nullptr;
}

struct Member {
  uint16_t species = 0;
  uint8_t level = 0;
  bool shadow = false;
};
struct TrainerRow {
  uint16_t nameMsg = 0;
  std::vector<Member> team;
};
std::map<uint16_t, std::vector<TrainerRow>> sDecks;
bool sDecksRead = false;

void ReadDecks() {
  sDecksRead = true;
  const std::vector<uint8_t> bytes = xd::kit2::DiscFile("deck_archive.fsys");
  FsysArchive fsys;
  if (bytes.empty() || !Fsys_Open(bytes.data(), bytes.size(), &fsys)) {
    std::fprintf(stderr, "[pokedex] the trainer decks (deck_archive.fsys) could not be read: no trainers on the Where page\n");
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
  const std::string suffix = "_trainers";
  for (const em::DataTable& trainers : catalog.tables) {
    if (trainers.name.size() <= suffix.size() || trainers.name.compare(trainers.name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
    const em::DataTable* pokemon = catalog.FindTable(trainers.name.substr(0, trainers.name.size() - suffix.size()) + "_pokemon");
    const em::DataField* team = FieldOf(trainers, "pokemonDataID");
    const em::DataField* flags = FieldOf(trainers, "darkPokemonFlags");
    const em::DataField* name = FieldOf(trainers, "trainerName");
    const em::DataField* level = pokemon != nullptr ? FieldOf(*pokemon, "level") : nullptr;
    const em::DataField* species = pokemon != nullptr ? FieldOf(*pokemon, "pokemonID") : nullptr;
    if (team == nullptr || flags == nullptr || name == nullptr || level == nullptr || species == nullptr || trainers.places.empty()) continue;
    const uint16_t resource = resources[trainers.places[0].entry];
    std::vector<TrainerRow>& rows = sDecks[resource];
    rows.resize(trainers.count);
    for (uint32_t t = 0; t < trainers.count; t++) {
      const uint8_t* tr = bytesOf(trainers, t);
      if (tr == nullptr) continue;
      rows[t].nameMsg = static_cast<uint16_t>(em::ReadDataField(tr, *name, 0));
      for (uint32_t k = 0; k < team->count; k++) {
        uint32_t id = em::ReadDataField(tr, *team, k);
        if (id == 0) continue;
        const bool dark = (em::ReadDataField(tr, *flags, 0) >> k & 1u) != 0;
        if (dark) {
          const uint8_t* s = shadows != nullptr && shadowDeck != nullptr ? bytesOf(*shadows, id) : nullptr;
          id = s != nullptr ? em::ReadDataField(s, *shadowDeck, 0) : 0;
        }
        const uint8_t* p = id != 0 ? bytesOf(*pokemon, id) : nullptr;
        if (p == nullptr) continue;
        Member m;
        m.species = static_cast<uint16_t>(em::ReadDataField(p, *species, 0));
        m.level = static_cast<uint8_t>(em::ReadDataField(p, *level, 0));
        m.shadow = dark;
        if (m.species != 0) rows[t].team.push_back(m);
      }
    }
  }
  size_t trainersRead = 0;
  for (const auto& d : sDecks) trainersRead += d.second.size();
  std::fprintf(stderr, "[pokedex] trainer decks read from the disc: %zu files, %zu trainers\n", sDecks.size(), trainersRead);
}

std::string Words(uint32_t msg) { return msg == 0 ? std::string() : Utf8(GSmsgGetGSchar(msg)); }

bool Plain(const std::string& s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

std::string PlaceOf(std::string arena) {
  const size_t bf = arena.rfind(" (BF)");
  if (bf != std::string::npos && bf + 5 == arena.size()) arena.erase(bf);
  for (size_t p; (p = arena.find(" : ")) != std::string::npos;) arena.replace(p, 3, ", ");
  return arena;
}

}

void AddTrainerPlaces(std::map<uint16_t, ep::Where>& where) {
  if (!sDecksRead) ReadDecks();
  if (sDecks.empty() || fight_encount_data == nullptr || fight_encount_data_number == nullptr) return;
  const auto* battles = static_cast<const FightEncountData*>(fight_encount_data);
  const auto* floors = static_cast<const FightFloorData*>(fight_floor_data);
  std::set<std::string> seen;
  for (u32 b = 0; b < *fight_encount_data_number; b++) {
    const FightEncountData& fe = battles[b];
    const std::string arena = floors != nullptr && fe.fightFloorDataId < *fight_floor_data_number ? Words(floors[fe.fightFloorDataId].name) : std::string();
    for (int slot = 0; slot < 4; slot++) {
      const uint32_t ids = fe.trainerInfo[slot].ids;
      const uint32_t grp = ids >> 16, trainerIndex = ids & 0xFFFF;
      if (grp == 0) continue;
      const auto deck = sDecks.find(static_cast<uint16_t>(getDeckResourceID(static_cast<u8>(grp)) >> 16));
      if (deck == sDecks.end() || trainerIndex >= deck->second.size()) continue;
      const TrainerRow& tr = deck->second[trainerIndex];
      const std::string who = Words(tr.nameMsg);
      if (who.empty() || arena.empty() || (Plain(who) && !Plain(arena))) continue;
      for (const Member& m : tr.team) {
        const std::string key = std::to_string(m.species) + "|" + who + "|" + arena + "|" + (m.shadow ? "s" : "n");
        if (!seen.insert(key).second) continue;
        ep::WhereLine l;
        l.who = who;
        l.place = PlaceOf(arena);
        l.level = m.level;
        l.battle = static_cast<uint32_t>(b);
        (m.shadow ? where[m.species].shadow : where[m.species].trainers).push_back(l);
      }
    }
  }
}

}
