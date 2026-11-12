#include "xd_randomizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <numeric>
#include <set>

namespace em = engine::mods;
namespace er = engine::randomizer;

namespace xd::randomizer {

namespace {

constexpr uint32_t kAbilityWonderGuard = 25, kAbilityForecast = 59;
constexpr uint32_t kNationalLugia = 249;
constexpr uint32_t kNationalLast = 386;
constexpr uint32_t kMoveStruggle = 165;
constexpr uint32_t kItemKindTm = 4;
constexpr uint32_t kMoveEffectOhko = 38;
constexpr uint32_t kMaxLevel = 100;
constexpr uint32_t kNatureCount = 25;
const char* const kNatureNames[kNatureCount] = {"Hardy",   "Lonely", "Brave", "Adamant", "Naughty", "Bold",    "Docile", "Relaxed", "Impish",
                                                "Lax",     "Timid",  "Hasty", "Serious", "Jolly",   "Naive",   "Modest", "Mild",    "Quiet",
                                                "Bashful", "Rash",   "Calm",  "Gentle",  "Sassy",   "Careful", "Quirky"};
constexpr uint32_t kLevelPercents[] = {0, 10, 20, 30, 50};
constexpr uint32_t kLevelsByStrength = 5;

er::Schema MakeSchema() {
  er::Schema s;
  s.prefix = "XDR";
  s.version = 3;
  const std::vector<std::string> onOff = {"off", "on"};
  s.switches = {
      {"wild", "Randomize Poke Spot Pokemon", onOff},
      {"wild.similar", "Poke Spots: similar strength (BST)", onOff},
      {"trainers", "Randomize trainers' Pokemon (story)", onOff},
      {"trainers.similar", "Trainers: similar strength (BST)", onOff},
      {"trainers.moves", "Trainers: moves", {"kept", "natural (by level)"}},
      {"trainers.no_dupes", "Trainers: no duplicate species in one team", onOff},
      {"shadow", "Randomize which Pokemon are Shadow", onOff},
      {"shadow.similar", "Shadows: similar strength (BST)", onOff},
      {"shadow.keep_lugia", "Keep Shadow Lugia", onOff},
      {"items.ground", "Items on the ground", {"off", "random", "shuffle"}},
      {"abilities", "Randomize abilities", onOff},
      {"abilities.family", "Abilities: same within an evolution family", onOff},
      {"tm_compat", "Who can learn each TM", {"off", "random"}},
      {"legendaries", "Legendaries allowed", {"never", "only where the disc had one", "any"}},
      {"tm_moves", "Which move each TM teaches", {"off", "random", "random, no one-hit-KO moves"}, 2},
      {"trainer_levels", "Trainers' Pokemon: levels", {"kept", "+10%", "+20%", "+30%", "+50%", "matched to the new Pokemon's strength"}, 3},
      {"trainer_items", "Trainers' Pokemon: held items", {"kept", "random", "none"}, 3},
      {"trainer_natures", "Trainers' Pokemon: natures", {"kept", "random"}, 3},
  };
  return s;
}

class Tables {
public:
  Tables(const em::DataCatalog& c, const em::EntryReader& r) : cat_(c), read_(r) {}
  const em::DataTable* T(const std::string& name) const { return cat_.FindTable(name); }
  const em::DataField* F(const em::DataTable& t, const std::string& field) const {
    for (const em::DataField& f : t.fields) {
      if (f.name == field) return &f;
    }
    return nullptr;
  }
  bool Need(const std::string& table, std::initializer_list<const char*> fields, std::string& error) const {
    const em::DataTable* t = T(table);
    if (t == nullptr || t->places.empty()) {
      error = "table " + table + " not found on the disc";
      return false;
    }
    for (const char* f : fields) {
      if (F(*t, f) == nullptr) {
        error = "table " + table + " has no field " + f;
        return false;
      }
    }
    return true;
  }
  uint32_t Count(const std::string& table) const {
    const em::DataTable* t = T(table);
    return t != nullptr ? t->count : 0;
  }
  uint32_t Get(const std::string& table, uint32_t entry, const std::string& field, uint32_t element = 0) const {
    const em::DataTable* t = T(table);
    if (t == nullptr || t->places.empty() || entry >= t->count) return 0;
    const em::DataField* f = F(*t, field);
    if (f == nullptr || element >= f->count) return 0;
    const em::DataPlace& p = t->places[0];
    auto key = std::make_pair(p.archive, p.entry);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      std::vector<uint8_t> bytes;
      if (!read_(p.archive, p.entry, bytes)) bytes.clear();
      it = cache_.emplace(key, std::move(bytes)).first;
    }
    const size_t at = p.offset + static_cast<size_t>(entry) * t->entrySize;
    if (at + t->entrySize > it->second.size()) return 0;
    return em::ReadDataField(it->second.data() + at, *f, element);
  }
  std::string Name(const std::string& list, uint32_t v) const {
    if (const em::NameList* l = cat_.FindList(list)) {
      for (const auto& [n, num] : l->names) {
        if (num == v) return n;
      }
    }
    return "#" + std::to_string(v);
  }

private:
  const em::DataCatalog& cat_;
  const em::EntryReader& read_;
  mutable std::map<std::pair<std::string, std::string>, std::vector<uint8_t>> cache_;
};

std::string Field(const char* name, uint32_t element) { return std::string(name) + "[" + std::to_string(element) + "]"; }

struct Species {
  std::vector<uint32_t> pool;
  std::vector<uint32_t> bst;
  std::vector<bool> legendary;
  std::vector<uint32_t> family;
  std::vector<uint32_t> national;
  uint32_t lugia = 0;
};

bool ReadSpecies(const Tables& t, Species& sp, std::string& error) {
  if (!t.Need("pokemon_data", {"numZukan", "basisStatus", "sinka.pokemonDataId", "tokuseiDataId"}, error)) return false;
  const uint32_t n = t.Count("pokemon_data");
  sp.bst.assign(n, 0);
  sp.legendary.assign(n, false);
  sp.national.assign(n, 0);
  sp.family.resize(n);
  std::iota(sp.family.begin(), sp.family.end(), 0u);
  std::set<uint32_t> visitors;
  for (uint32_t i = 0; i < t.Count("esabapokemondata_EX"); i++) visitors.insert(t.Get("esabapokemondata_EX", i, "id"));
  if (const em::DataTable* lt = t.T("legendPokemonData"); lt != nullptr && !lt->fields.empty()) {
    for (uint32_t i = 0; i < lt->count; i++) {
      const uint32_t s = t.Get("legendPokemonData", i, lt->fields[0].name);
      if (s < n) sp.legendary[s] = true;
    }
  }
  std::set<uint32_t> seenNational;
  for (uint32_t s = 1; s < n; s++) {
    uint32_t total = 0;
    for (uint32_t k = 0; k < 6; k++) total += t.Get("pokemon_data", s, "basisStatus", k);
    sp.bst[s] = total;
    const uint32_t nat = t.Get("pokemon_data", s, "numZukan");
    sp.national[s] = nat;
    if (nat == kNationalLugia && sp.lugia == 0) sp.lugia = s;
    if (nat == 0 || nat > kNationalLast || total == 0 || visitors.count(s) != 0 || !seenNational.insert(nat).second) continue;
    sp.pool.push_back(s);
  }
  auto root = [&](uint32_t x) {
    while (sp.family[x] != x) x = sp.family[x] = sp.family[sp.family[x]];
    return x;
  };
  for (uint32_t s = 1; s < n; s++) {
    for (uint32_t k = 0; k < 5; k++) {
      const uint32_t to = t.Get("pokemon_data", s, "sinka.pokemonDataId", k);
      if (to == 0 || to >= n) continue;
      const uint32_t a = root(s), b = root(to);
      if (a != b) sp.family[std::max(a, b)] = std::min(a, b);
    }
  }
  for (uint32_t s = 0; s < n; s++) sp.family[s] = root(s);
  if (sp.pool.size() < 100) {
    error = "too few species found on the disc (" + std::to_string(sp.pool.size()) + ")";
    return false;
  }
  return true;
}

std::vector<uint32_t> NaturalMoves(const Tables& t, uint32_t species, uint32_t level) {
  std::vector<uint32_t> moves;
  for (uint32_t k = 0; k < 20; k++) {
    const uint32_t lv = t.Get("pokemon_data", species, "waza.level", k);
    const uint32_t mv = t.Get("pokemon_data", species, "waza.dataId", k);
    if (mv == 0) break;
    if (lv > level) break;
    moves.erase(std::remove(moves.begin(), moves.end(), mv), moves.end());
    moves.push_back(mv);
  }
  if (moves.size() > 4) moves.erase(moves.begin(), moves.end() - 4);
  moves.resize(4, 0);
  return moves;
}

std::vector<uint32_t> PoolFor(const Species& sp, bool allowLegendary) {
  std::vector<uint32_t> p;
  for (uint32_t s : sp.pool) {
    if (allowLegendary || !sp.legendary[s]) p.push_back(s);
  }
  return p;
}

uint32_t Pick(er::Rng& rng, const std::vector<uint32_t>& pool, const Species& sp, uint32_t old, bool similar,
              const std::vector<bool>* used) {
  if (similar && old < sp.bst.size() && sp.bst[old] != 0) return er::PickSimilar(rng, pool, sp.bst, sp.bst[old], 10, used);
  std::vector<uint32_t> free;
  for (uint32_t x : pool) {
    if (used == nullptr || !(*used)[x]) free.push_back(x);
  }
  const std::vector<uint32_t>& from = free.empty() ? pool : free;
  return from[rng.Below(static_cast<uint32_t>(from.size()))];
}

void Wild(const Tables& t, const Species& sp, const er::Settings& s, Result& r) {
  const auto& sc = GetSchema();
  er::Rng rng = er::Rng::Stream(s.seed, "wild");
  const auto pool = PoolFor(sp, s.Get(sc, "legendaries") == 2);
  const bool similar = s.Get(sc, "wild.similar") != 0;
  for (const char* spot : {"esabapokemondata_E1", "esabapokemondata_E2", "esabapokemondata_E3"}) {
    std::vector<bool> used(sp.bst.size(), false);
    for (uint32_t i = 0; i < t.Count(spot); i++) {
      const uint32_t old = t.Get(spot, i, "id");
      if (old == 0) continue;
      const uint32_t now = Pick(rng, pool, sp, old, similar, &used);
      used[now] = true;
      r.mod.Set(spot, i, "id", now);
      r.mod.spoiler.push_back(std::string("Poke Spot ") + spot + " #" + std::to_string(i) + ": " + t.Name("species", old) +
                              " -> " + t.Name("species", now));
    }
  }
}

struct StoryTeams {
  std::set<uint32_t> shadowEntries;
  std::vector<uint32_t> shadows;
  std::vector<std::vector<uint32_t>> normalTeams;
};

StoryTeams ReadTeams(const Tables& t) {
  StoryTeams st;
  std::set<uint32_t> seenShadow;
  const uint32_t pokemonCount = t.Count("story_pokemon");
  for (uint32_t tr = 0; tr < t.Count("story_trainers"); tr++) {
    const uint32_t flags = t.Get("story_trainers", tr, "darkPokemonFlags");
    std::vector<uint32_t> team;
    for (uint32_t k = 0; k < 6; k++) {
      const uint32_t id = t.Get("story_trainers", tr, "pokemonDataID", k);
      if (id == 0) continue;
      if (flags & (1u << k)) {
        if (id < t.Count("darkpokemon_shadow") && seenShadow.insert(id).second) {
          st.shadows.push_back(id);
          const uint32_t deck = t.Get("darkpokemon_shadow", id, "deckPokemonID");
          if (deck != 0 && deck < pokemonCount) st.shadowEntries.insert(deck);
        }
      } else if (id < pokemonCount) {
        team.push_back(id);
      }
    }
    st.normalTeams.push_back(team);
  }
  return st;
}

void SetMoves(const Tables& t, Result& r, uint32_t entry, uint32_t species, uint32_t level) {
  const auto moves = NaturalMoves(t, species, level);
  for (uint32_t k = 0; k < 4; k++) r.mod.Set("story_pokemon", entry, Field("wazaID", k), moves[k]);
}

void Trainers(const Tables& t, const Species& sp, const StoryTeams& st, const er::Settings& s, Result& r) {
  const auto& sc = GetSchema();
  er::Rng rng = er::Rng::Stream(s.seed, "trainers");
  const auto pool = PoolFor(sp, s.Get(sc, "legendaries") == 2);
  const bool similar = s.Get(sc, "trainers.similar") != 0, natural = s.Get(sc, "trainers.moves") == 1,
             noDupes = s.Get(sc, "trainers.no_dupes") != 0;
  std::set<uint32_t> done;
  for (size_t tr = 0; tr < st.normalTeams.size(); tr++) {
    std::vector<bool> used(sp.bst.size(), false);
    for (uint32_t e : st.normalTeams[tr]) {
      if (done.count(e)) used[t.Get("story_pokemon", e, "pokemonID")] = true;
    }
    for (uint32_t e : st.normalTeams[tr]) {
      if (done.count(e) != 0 || st.shadowEntries.count(e) != 0) continue;
      done.insert(e);
      const uint32_t old = t.Get("story_pokemon", e, "pokemonID");
      if (old == 0 || old >= sp.bst.size()) continue;
      const uint32_t now = Pick(rng, pool, sp, old, similar, noDupes ? &used : nullptr);
      used[now] = true;
      r.mod.Set("story_pokemon", e, "pokemonID", now);
      const uint32_t level = t.Get("story_pokemon", e, "level");
      if (natural) SetMoves(t, r, e, now, level);
      r.mod.spoiler.push_back("Trainer " + t.Name("story_trainer", static_cast<uint32_t>(tr)) + ": " + t.Name("species", old) +
                              " -> " + t.Name("species", now) + " (level " + std::to_string(level) + ")");
    }
  }
}

struct TeamEntry {
  uint32_t entry;
  uint32_t trainer;
};
std::vector<TeamEntry> NormalEntries(const Tables& t, const StoryTeams& st) {
  std::vector<TeamEntry> out;
  std::set<uint32_t> done;
  for (size_t tr = 0; tr < st.normalTeams.size(); tr++) {
    for (uint32_t e : st.normalTeams[tr]) {
      if (st.shadowEntries.count(e) != 0 || !done.insert(e).second) continue;
      if (t.Get("story_pokemon", e, "pokemonID") == 0) continue;
      out.push_back({e, static_cast<uint32_t>(tr)});
    }
  }
  return out;
}

uint32_t NowValue(const Tables& t, const Result& r, const std::string& table, uint32_t entry, const std::string& field) {
  const auto ti = r.mod.tables.find(table);
  if (ti != r.mod.tables.end()) {
    const auto ei = ti->second.find(entry);
    if (ei != ti->second.end()) {
      const auto fi = ei->second.find(field);
      if (fi != ei->second.end()) return fi->second;
    }
  }
  return t.Get(table, entry, field);
}

std::string TeamEntryName(const Tables& t, const Result& r, const TeamEntry& e) {
  return "Trainer " + t.Name("story_trainer", e.trainer) + ": " + t.Name("species", NowValue(t, r, "story_pokemon", e.entry, "pokemonID"));
}

void TrainerLevels(const Tables& t, const Species& sp, const StoryTeams& st, const er::Settings& s, Result& r) {
  const auto& sc = GetSchema();
  const uint32_t mode = s.Get(sc, "trainer_levels");
  const bool natural = s.Get(sc, "trainers") != 0 && s.Get(sc, "trainers.moves") == 1;
  for (const TeamEntry& e : NormalEntries(t, st)) {
    const uint32_t old = t.Get("story_pokemon", e.entry, "level");
    if (old == 0) continue;
    const uint32_t discSpecies = t.Get("story_pokemon", e.entry, "pokemonID");
    const uint32_t species = NowValue(t, r, "story_pokemon", e.entry, "pokemonID");
    uint32_t now = old;
    if (mode == kLevelsByStrength) {
      if (discSpecies < sp.bst.size() && species < sp.bst.size()) {
        now = er::LevelForStrength(old, sp.bst[discSpecies], sp.bst[species], kMaxLevel);
      }
    } else if (mode < std::size(kLevelPercents)) {
      now = er::ScaleLevel(old, kLevelPercents[mode], kMaxLevel);
    }
    if (now == old) continue;
    r.mod.Set("story_pokemon", e.entry, "level", now);
    if (natural && r.mod.tables["story_pokemon"][e.entry].count("pokemonID") != 0) SetMoves(t, r, e.entry, species, now);
    r.mod.spoiler.push_back(TeamEntryName(t, r, e) + " level " + std::to_string(old) + " -> " + std::to_string(now));
  }
}

std::vector<uint32_t> HeldItemPool(const Tables& t) {
  std::set<uint32_t> held;
  for (const char* deck : {"story_pokemon", "colosseum_pokemon", "hundred_pokemon", "imasugu_pokemon", "virtual_pokemon",
                           "bingo_pokemon", "sample_pokemon"}) {
    const em::DataTable* table = t.T(deck);
    if (table == nullptr || table->places.empty() || t.F(*table, "holdItem") == nullptr) continue;
    for (uint32_t i = 1; i < table->count; i++) {
      if (t.Get(deck, i, "pokemonID") != 0) held.insert(t.Get(deck, i, "holdItem"));
    }
  }
  std::vector<uint32_t> pool;
  const uint32_t itemCount = t.Count("item_data_prime");
  for (uint32_t item : held) {
    if (item == 0 || item >= itemCount) continue;
    if (t.Get("item_data_prime", item, "important") != 0 || t.Get("item_data_prime", item, "itemSoubiDataId") == 0) continue;
    if (t.Get("item_data_prime", item, "name") == 0) continue;
    pool.push_back(item);
  }
  return pool;
}

bool TrainerItems(const Tables& t, const StoryTeams& st, const er::Settings& s, Result& r, std::string& error) {
  const uint32_t mode = s.Get(GetSchema(), "trainer_items");
  er::Rng rng = er::Rng::Stream(s.seed, "trainer_items");
  const std::vector<uint32_t> pool = mode == 1 ? HeldItemPool(t) : std::vector<uint32_t>{};
  if (mode == 1 && pool.size() < 5) {
    error = "too few held items found on the disc (" + std::to_string(pool.size()) + ")";
    return false;
  }
  for (const TeamEntry& e : NormalEntries(t, st)) {
    const uint32_t old = t.Get("story_pokemon", e.entry, "holdItem");
    const uint32_t now = mode == 1 ? pool[rng.Below(static_cast<uint32_t>(pool.size()))] : 0;
    r.mod.Set("story_pokemon", e.entry, "holdItem", now);
    if (now == old) continue;
    r.mod.spoiler.push_back(TeamEntryName(t, r, e) + " holds " + (now != 0 ? t.Name("item", now) : std::string("nothing")) +
                            " (was " + (old != 0 ? t.Name("item", old) : std::string("nothing")) + ")");
  }
  return true;
}

void TrainerNatures(const Tables& t, const StoryTeams& st, const er::Settings& s, Result& r) {
  er::Rng rng = er::Rng::Stream(s.seed, "trainer_natures");
  for (const TeamEntry& e : NormalEntries(t, st)) {
    if (t.Get("story_pokemon", e.entry, "randomSetting") != 0) continue;
    const uint32_t old = t.Get("story_pokemon", e.entry, "seikakuID");
    const uint32_t now = rng.Below(kNatureCount);
    r.mod.Set("story_pokemon", e.entry, "seikakuID", now);
    if (now == old) continue;
    r.mod.spoiler.push_back(TeamEntryName(t, r, e) + " nature " + kNatureNames[now] + " (was " +
                            (old < kNatureCount ? kNatureNames[old] : "?") + ")");
  }
}

void Shadows(const Tables& t, const Species& sp, const StoryTeams& st, const er::Settings& s, Result& r) {
  const auto& sc = GetSchema();
  er::Rng rng = er::Rng::Stream(s.seed, "shadow");
  const uint32_t legend = s.Get(sc, "legendaries");
  const bool similar = s.Get(sc, "shadow.similar") != 0, keepLugia = s.Get(sc, "shadow.keep_lugia") != 0;
  const auto normalPool = PoolFor(sp, legend == 2);
  const auto anyPool = PoolFor(sp, true);
  std::vector<bool> used(sp.bst.size(), false);
  std::vector<uint32_t> order;
  for (uint32_t id : st.shadows) {
    const uint32_t deck = t.Get("darkpokemon_shadow", id, "deckPokemonID");
    const uint32_t old = t.Get("story_pokemon", deck, "pokemonID");
    if (keepLugia && old == sp.lugia && sp.lugia != 0) {
      used[old] = true;
      continue;
    }
    order.push_back(id);
  }
  std::set<uint32_t> randomizedDecks;
  for (uint32_t id : order) randomizedDecks.insert(t.Get("darkpokemon_shadow", id, "deckPokemonID"));
  const uint32_t listCount = std::min(t.Count("darkPokemonList"), t.Count("darkpokemon_shadow"));
  for (uint32_t i = 1; i < listCount; i++) {
    const uint32_t deck = t.Get("darkpokemon_shadow", i, "deckPokemonID");
    const uint32_t species = t.Get("darkPokemonList", i, "pokemonID");
    if (deck != 0 && randomizedDecks.count(deck) == 0 && species != 0 && species < used.size()) used[species] = true;
  }
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> changed;
  std::set<uint32_t> doneDeck;
  for (uint32_t id : order) {
    const uint32_t deck = t.Get("darkpokemon_shadow", id, "deckPokemonID");
    if (!doneDeck.insert(deck).second) continue;
    const uint32_t old = t.Get("story_pokemon", deck, "pokemonID");
    if (old == 0 || old >= sp.bst.size()) continue;
    std::vector<uint32_t> pool = normalPool;
    if (legend == 1 && sp.legendary[old]) {
      pool.clear();
      for (uint32_t x : anyPool) {
        if (sp.legendary[x]) pool.push_back(x);
      }
      if (pool.empty()) pool = normalPool;
    }
    const uint32_t now = Pick(rng, pool, sp, old, similar, &used);
    used[now] = true;
    r.mod.Set("story_pokemon", deck, "pokemonID", now);
    changed[deck] = {old, now};
    const uint32_t level = t.Get("story_pokemon", deck, "level");
    SetMoves(t, r, deck, now, level);
    r.mod.spoiler.push_back("Shadow #" + std::to_string(id) + ": " + t.Name("species", old) + " -> " + t.Name("species", now) +
                            " (level " + std::to_string(level) + ")");
  }
  for (uint32_t i = 0; i < listCount; i++) {
    const auto c = changed.find(t.Get("darkpokemon_shadow", i, "deckPokemonID"));
    if (c != changed.end() && t.Get("darkPokemonList", i, "pokemonID") == c->second.first) {
      r.mod.Set("darkPokemonList", i, "pokemonID", c->second.second);
    }
  }
}

void GroundItems(const Tables& t, const er::Settings& s, Result& r) {
  const uint32_t mode = s.Get(GetSchema(), "items.ground");
  er::Rng rng = er::Rng::Stream(s.seed, "items.ground");
  const uint32_t itemCount = t.Count("item_data_prime");
  auto movable = [&](uint32_t item) { return item != 0 && item < itemCount && t.Get("item_data_prime", item, "important") == 0; };
  std::vector<uint32_t> boxes, items;
  for (uint32_t i = 0; i < t.Count("floor_tresure_list"); i++) {
    const uint32_t item = t.Get("floor_tresure_list", i, "item");
    if (!movable(item)) continue;
    boxes.push_back(i);
    items.push_back(item);
  }
  if (mode == 2) {
    rng.Shuffle(items);
  } else {
    std::vector<uint32_t> any;
    for (uint32_t it = 1; it < itemCount; it++) {
      if (movable(it) && t.Get("item_data_prime", it, "price") != 0) any.push_back(it);
    }
    if (any.empty()) return;
    for (uint32_t& it : items) it = any[rng.Below(static_cast<uint32_t>(any.size()))];
  }
  for (size_t k = 0; k < boxes.size(); k++) {
    const uint32_t old = t.Get("floor_tresure_list", boxes[k], "item");
    r.mod.Set("floor_tresure_list", boxes[k], "item", items[k]);
    r.mod.spoiler.push_back("Item box #" + std::to_string(boxes[k]) + ": " + t.Name("item", old) + " -> " + t.Name("item", items[k]));
  }
}

void Abilities(const Tables& t, const Species& sp, const er::Settings& s, Result& r) {
  er::Rng rng = er::Rng::Stream(s.seed, "abilities");
  const bool family = s.Get(GetSchema(), "abilities.family") != 0;
  std::set<uint32_t> inUse;
  for (uint32_t x : sp.pool) {
    for (uint32_t k = 0; k < 2; k++) inUse.insert(t.Get("pokemon_data", x, "tokuseiDataId", k));
  }
  std::vector<uint32_t> abilities;
  for (uint32_t a : inUse) {
    if (a != 0 && a != kAbilityWonderGuard && a != kAbilityForecast) abilities.push_back(a);
  }
  if (abilities.empty()) return;
  std::map<uint32_t, std::array<uint32_t, 2>> byFamily;
  for (uint32_t x : sp.pool) {
    std::array<uint32_t, 2> pick{};
    auto it = family ? byFamily.find(sp.family[x]) : byFamily.end();
    if (it != byFamily.end()) {
      pick = it->second;
    } else {
      pick[0] = abilities[rng.Below(static_cast<uint32_t>(abilities.size()))];
      do pick[1] = abilities[rng.Below(static_cast<uint32_t>(abilities.size()))];
      while (pick[1] == pick[0] && abilities.size() > 1);
      if (family) byFamily[sp.family[x]] = pick;
    }
    for (uint32_t k = 0; k < 2; k++) {
      const uint32_t old = t.Get("pokemon_data", x, "tokuseiDataId", k);
      if (k == 1 && old == 0) continue;
      r.mod.Set("pokemon_data", x, Field("tokuseiDataId", k), pick[k]);
    }
    r.mod.spoiler.push_back("Abilities " + t.Name("species", x) + ": " + std::to_string(pick[0]) +
                            (t.Get("pokemon_data", x, "tokuseiDataId", 1) != 0 ? " / " + std::to_string(pick[1]) : ""));
  }
}

void TmCompat(const Tables& t, const Species& sp, const er::Settings& s, Result& r) {
  er::Rng rng = er::Rng::Stream(s.seed, "tm_compat");
  const em::DataTable* pd = t.T("pokemon_data");
  const em::DataField* f = pd != nullptr ? t.F(*pd, "wazaMcn") : nullptr;
  if (f == nullptr) return;
  for (uint32_t tm = 0; tm < f->count; tm++) {
    uint32_t learners = 0;
    for (uint32_t x : sp.pool) learners += t.Get("pokemon_data", x, "wazaMcn", tm) != 0 ? 1 : 0;
    const uint32_t percent = static_cast<uint32_t>(learners * 100ull / sp.pool.size());
    for (uint32_t x : sp.pool) r.mod.Set("pokemon_data", x, Field("wazaMcn", tm), rng.Chance(percent) ? 1 : 0);
    r.mod.spoiler.push_back("TM " + std::to_string(tm + 1) + ": learned by about " + std::to_string(percent) + "% of species");
  }
}

struct TmMoves {
  std::vector<uint32_t> machines;
  std::map<uint32_t, uint32_t> itemOf;
  std::vector<uint32_t> pool;
  std::set<uint32_t> taken;
  std::map<uint32_t, uint32_t> docOf;
};

uint32_t TmDescription(const Tables& t, const TmMoves& tm, uint32_t move) {
  const auto it = tm.docOf.find(move);
  return it != tm.docOf.end() ? it->second : t.Get("waza_data", move, "doc");
}

bool TmMoveUsable(const Tables& t, uint32_t move) {
  if (move == 0 || move >= t.Count("waza_data") || move == kMoveStruggle) return false;
  if (t.Get("waza_data", move, "darkWazaKind") != 0 || t.Get("waza_data", move, "hidenFlag") != 0) return false;
  if (t.Get("waza_data", move, "pp") == 0 || t.Get("waza_data", move, "name") == 0) return false;
  const std::string name = t.Name("move", move);
  return !name.empty() && name[0] != '#' && name.find_first_not_of('?') != std::string::npos;
}

bool OneHitKo(const Tables& t, uint32_t move) { return t.Get("waza_data", move, "seqId") == kMoveEffectOhko; }

TmMoves ReadTmMoves(const Tables& t, bool fair) {
  TmMoves tm;
  const uint32_t count = t.Count("item_waza_machine_no"), moveCount = t.Count("waza_data");
  for (uint32_t item = 1; item < t.Count("item_data_prime"); item++) {
    if (t.Get("item_data_prime", item, "kind") != kItemKindTm) continue;
    const uint32_t machine = t.Get("item_data_prime", item, "buff");
    if (machine < count && tm.itemOf.count(machine) == 0) tm.itemOf[machine] = item;
  }
  std::set<uint32_t> tutor;
  for (uint32_t i = 0; i < t.Count("wazaoshie_data"); i++) tutor.insert(t.Get("wazaoshie_data", i, "id"));
  for (uint32_t m = 0; m < count; m++) {
    const uint32_t move = t.Get("item_waza_machine_no", m, "wazaID");
    if (const auto item = tm.itemOf.find(m); item != tm.itemOf.end() && move != 0 && tm.docOf.count(move) == 0) {
      if (const uint32_t doc = t.Get("item_data_prime", item->second, "doc"); doc != 0) tm.docOf[move] = doc;
    }
    if (tm.itemOf.count(m) != 0 && move != 0 && move < moveCount && t.Get("waza_data", move, "hidenFlag") == 0) {
      tm.machines.push_back(m);
    } else {
      tm.taken.insert(move);
    }
  }
  for (uint32_t move = 1; move < moveCount; move++) {
    if (!TmMoveUsable(t, move) || tutor.count(move) != 0 || tm.taken.count(move) != 0) continue;
    if (fair && OneHitKo(t, move)) continue;
    tm.pool.push_back(move);
  }
  return tm;
}

std::string TmName(uint32_t machine) {
  char text[16];
  std::snprintf(text, sizeof text, "TM%02u", machine + 1);
  return text;
}

void TmMovesRandom(const Tables& t, const er::Settings& s, Result& r) {
  er::Rng rng = er::Rng::Stream(s.seed, "tm_moves");
  const TmMoves tm = ReadTmMoves(t, s.Get(GetSchema(), "tm_moves") == 2);
  if (tm.pool.size() < tm.machines.size()) return;
  std::vector<uint32_t> moves = tm.pool;
  rng.Shuffle(moves);
  for (size_t k = 0; k < tm.machines.size(); k++) {
    const uint32_t machine = tm.machines[k], old = t.Get("item_waza_machine_no", machine, "wazaID"), now = moves[k];
    r.mod.Set("item_waza_machine_no", machine, "wazaID", now);
    r.mod.Set("item_data_prime", tm.itemOf.at(machine), "doc", TmDescription(t, tm, now));
    r.mod.spoiler.push_back(TmName(machine) + ": " + t.Name("move", old) + " -> " + t.Name("move", now));
  }
}

bool Verify(const Tables& t, const Species& sp, const StoryTeams& st, const er::Settings& s, const Result& r, std::string& error) {
  const auto& sc = GetSchema();
  const uint32_t legend = s.Get(sc, "legendaries");
  std::set<uint32_t> pool(sp.pool.begin(), sp.pool.end());
  auto value = [&](const std::string& table, uint32_t entry, const std::string& field) {
    auto ti = r.mod.tables.find(table);
    if (ti != r.mod.tables.end()) {
      auto ei = ti->second.find(entry);
      if (ei != ti->second.end()) {
        auto fi = ei->second.find(field);
        if (fi != ei->second.end()) return fi->second;
      }
    }
    return t.Get(table, entry, field);
  };
  auto fail = [&](const std::string& what) {
    error = "generator check failed: " + what;
    return false;
  };
  std::set<uint32_t> shadowSpecies;
  for (uint32_t id : st.shadows) {
    const uint32_t deck = t.Get("darkpokemon_shadow", id, "deckPokemonID");
    const uint32_t before = t.Get("story_pokemon", deck, "pokemonID"), now = value("story_pokemon", deck, "pokemonID");
    if (before != now) {
      if (pool.count(now) == 0) return fail("Shadow #" + std::to_string(id) + " got a species outside the pool");
      if (sp.legendary[now] && legend == 0) return fail("a legendary Shadow with legendaries off");
      if (sp.legendary[now] && legend == 1 && !sp.legendary[before]) return fail("a legendary Shadow where the disc had none");
      if (before == sp.lugia && s.Get(sc, "shadow.keep_lugia")) return fail("Shadow Lugia changed");
    }
    shadowSpecies.insert(now);
  }
  if (s.Get(sc, "shadow") && shadowSpecies.size() != st.shadows.size()) return fail("two Shadows of the same species");
  std::set<uint32_t> listSpecies;
  uint32_t listReal = 0;
  for (uint32_t i = 1; i < std::min(t.Count("darkPokemonList"), t.Count("darkpokemon_shadow")); i++) {
    const uint32_t deck = t.Get("darkpokemon_shadow", i, "deckPokemonID");
    if (deck == 0) continue;
    const uint32_t listNow = value("darkPokemonList", i, "pokemonID");
    if (t.Get("darkPokemonList", i, "pokemonID") == t.Get("story_pokemon", deck, "pokemonID") &&
        listNow != value("story_pokemon", deck, "pokemonID")) {
      return fail("Shadow #" + std::to_string(i) + "'s species in the Shadow list differs from its battle Pokemon");
    }
    listReal++;
    listSpecies.insert(listNow);
  }
  if (s.Get(sc, "shadow") && listSpecies.size() != listReal) return fail("two Shadow list entries of the same species");
  if (r.mod.tables.count("item_waza_machine_no") != 0) {
    const bool fair = s.Get(sc, "tm_moves") == 2;
    const TmMoves tm = ReadTmMoves(t, fair);
    std::set<uint32_t> seen;
    for (uint32_t m = 0; m < t.Count("item_waza_machine_no"); m++) {
      const uint32_t before = t.Get("item_waza_machine_no", m, "wazaID"), now = value("item_waza_machine_no", m, "wazaID");
      if (now != 0 && !seen.insert(now).second) return fail("two TMs teach the same move");
      if (now == before) continue;
      if (!TmMoveUsable(t, now)) return fail(TmName(m) + " got a move that can't be used");
      if (fair && OneHitKo(t, now)) return fail(TmName(m) + " got a one-hit-KO move with those ruled out");
      const auto item = tm.itemOf.find(m);
      if (item == tm.itemOf.end() || t.Get("waza_data", before, "hidenFlag") != 0) return fail("a machine that is no TM changed");
      if (value("item_data_prime", item->second, "doc") != TmDescription(t, tm, now)) {
        return fail(TmName(m) + "'s description is not its new move's");
      }
    }
  }
  if (const auto pokemon = r.mod.tables.find("story_pokemon"); pokemon != r.mod.tables.end()) {
    std::set<uint32_t> normal;
    for (const TeamEntry& e : NormalEntries(t, st)) normal.insert(e.entry);
    const std::vector<uint32_t> itemPool = HeldItemPool(t);
    const std::set<uint32_t> items(itemPool.begin(), itemPool.end());
    for (const auto& [entry, fields] : pokemon->second) {
      for (const auto& [field, v] : fields) {
        if (field != "level" && field != "holdItem" && field != "seikakuID") continue;
        if (normal.count(entry) == 0) {
          return fail("story_pokemon #" + std::to_string(entry) + ": " + field + " changed outside a trainer's normal slot");
        }
        if (field == "level" && (v < 1 || v > kMaxLevel)) return fail("a level outside 1-100");
        if (field == "holdItem" && v != 0 && items.count(v) == 0) return fail("a held item outside the pool");
        if (field == "seikakuID" && v >= kNatureCount) return fail("a nature that doesn't exist");
      }
    }
  }
  for (const auto& [table, entries] : r.mod.tables) {
    for (const auto& [entry, fields] : entries) {
      for (const auto& [field, v] : fields) {
        const bool species = (table == "story_pokemon" && field == "pokemonID") || field == "id";
        if (species && pool.count(v) == 0) return fail(table + " #" + std::to_string(entry) + " got a species outside the pool");
        if (species && table != "story_pokemon" && sp.legendary[v] && legend != 2) return fail("a legendary in a Poke Spot");
        if (species && table == "story_pokemon" && st.shadowEntries.count(entry) == 0 && sp.legendary[v] && legend != 2) {
          return fail("a legendary trainer Pokemon with legendaries limited");
        }
        if (field.rfind("tokuseiDataId", 0) == 0 && (v == kAbilityWonderGuard || v == kAbilityForecast)) return fail("a banned ability");
        if (table == "floor_tresure_list" && field == "item" && t.Get("item_data_prime", t.Get(table, entry, "item"), "important") != 0) {
          return fail("a key item moved");
        }
      }
    }
  }
  return true;
}

}

const er::Schema& GetSchema() {
  static const er::Schema schema = MakeSchema();
  return schema;
}

bool StyleFromName(const std::string& name, Style& out) {
  if (name == "light") out = Style::Light;
  else if (name == "classic") out = Style::Classic;
  else if (name == "chaos") out = Style::Chaos;
  else if (name == "custom") out = Style::Custom;
  else return false;
  return true;
}

void ApplyStyle(er::Settings& s, Style style) {
  const auto& sc = GetSchema();
  if (style == Style::Custom) {
    s.values.resize(sc.switches.size(), 0);
    return;
  }
  s.values.assign(sc.switches.size(), 0);
  const bool chaos = style == Style::Chaos;
  const uint32_t fair = chaos ? 0 : 1;
  s.Set(sc, "wild", 1);
  s.Set(sc, "wild.similar", fair);
  s.Set(sc, "shadow", 1);
  s.Set(sc, "shadow.similar", fair);
  s.Set(sc, "shadow.keep_lugia", 1);
  s.Set(sc, "items.ground", chaos ? 1 : 2);
  s.Set(sc, "legendaries", chaos ? 2 : 1);
  if (style == Style::Light) return;
  s.Set(sc, "trainers", 1);
  s.Set(sc, "trainers.similar", fair);
  s.Set(sc, "trainers.moves", 1);
  s.Set(sc, "trainers.no_dupes", fair);
  s.Set(sc, "abilities", 1);
  s.Set(sc, "abilities.family", fair);
  s.Set(sc, "tm_compat", 1);
  s.Set(sc, "tm_moves", chaos ? 1 : 2);
}

bool Generate(const em::DataCatalog& catalog, const em::EntryReader& read, const er::Settings& s, Result& out, std::string& error) {
  const auto& sc = GetSchema();
  out = Result{};
  out.code = er::EncodeCode(sc, s);
  Tables t(catalog, read);
  Species sp;
  if (!ReadSpecies(t, sp, error)) return false;
  if (!t.Need("story_trainers", {"darkPokemonFlags", "pokemonDataID"}, error) ||
      !t.Need("story_pokemon", {"pokemonID", "level", "wazaID"}, error) ||
      !t.Need("darkpokemon_shadow", {"deckPokemonID"}, error) || !t.Need("darkPokemonList", {"pokemonID"}, error) || !t.Need("floor_tresure_list", {"item"}, error) ||
      !t.Need("item_data_prime", {"important", "price"}, error) || !t.Need("esabapokemondata_E1", {"id"}, error)) {
    return false;
  }
  const StoryTeams teams = ReadTeams(t);
  if (s.Get(sc, "wild")) Wild(t, sp, s, out);
  if (s.Get(sc, "trainers")) Trainers(t, sp, teams, s, out);
  if (s.Get(sc, "shadow")) Shadows(t, sp, teams, s, out);
  if (s.Get(sc, "trainer_levels") || s.Get(sc, "trainer_items") || s.Get(sc, "trainer_natures")) {
    if (!t.Need("story_pokemon", {"holdItem", "seikakuID", "randomSetting"}, error) ||
        !t.Need("item_data_prime", {"itemSoubiDataId", "name"}, error)) {
      return false;
    }
    if (s.Get(sc, "trainer_levels")) TrainerLevels(t, sp, teams, s, out);
    if (s.Get(sc, "trainer_items") && !TrainerItems(t, teams, s, out, error)) return false;
    if (s.Get(sc, "trainer_natures")) TrainerNatures(t, teams, s, out);
  }
  if (s.Get(sc, "items.ground")) GroundItems(t, s, out);
  if (s.Get(sc, "abilities")) Abilities(t, sp, s, out);
  if (s.Get(sc, "tm_compat")) TmCompat(t, sp, s, out);
  if (s.Get(sc, "tm_moves")) {
    if (!t.Need("item_waza_machine_no", {"wazaID"}, error) || !t.Need("item_data_prime", {"kind", "buff", "doc"}, error) ||
        !t.Need("waza_data", {"darkWazaKind", "hidenFlag", "pp", "name", "doc", "seqId"}, error)) {
      return false;
    }
    TmMovesRandom(t, s, out);
  }
  for (auto ti = out.mod.tables.begin(); ti != out.mod.tables.end();) {
    for (auto ei = ti->second.begin(); ei != ti->second.end();) {
      for (auto fi = ei->second.begin(); fi != ei->second.end();) {
        const std::string& name = fi->first;
        const size_t br = name.find('[');
        const uint32_t element = br == std::string::npos ? 0 : static_cast<uint32_t>(std::stoul(name.substr(br + 1)));
        const uint32_t disc = t.Get(ti->first, ei->first, name.substr(0, br), element);
        fi = disc == fi->second ? ei->second.erase(fi) : std::next(fi);
      }
      ei = ei->second.empty() ? ti->second.erase(ei) : std::next(ei);
    }
    ti = ti->second.empty() ? out.mod.tables.erase(ti) : std::next(ti);
  }
  return Verify(t, sp, teams, s, out, error);
}

bool WriteMod(const Result& r, const er::Settings& s, const std::filesystem::path& folder, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(folder / "data", ec);
  if (ec) {
    error = "can't make the folder " + folder.string() + ": " + ec.message();
    return false;
  }
  auto write = [&](const std::filesystem::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) error = "can't write " + p.string();
    return static_cast<bool>(f);
  };
  const std::string mod = "{\n  \"id\": \"" + std::string(kModId) + "\",\n  \"name\": \"Randomizer " + r.code +
                          "\",\n  \"version\": \"" + std::to_string(GetSchema().CodeVersion(s)) +
                          "\",\n  \"description\": \"Made by the randomizer from code " + r.code +
                          "\",\n  \"game\": \"GXXE01\",\n  \"applies\": \"new_playthrough\",\n  \"priority\": 1000\n}\n";
  std::string spoiler = "Randomizer spoiler log\ncode: " + r.code + "\nseed: " + std::to_string(s.seed) + "\n\nswitches:\n";
  const auto& sc = GetSchema();
  for (size_t i = 0; i < sc.switches.size(); i++) {
    if (sc.switches[i].since > sc.CodeVersion(s)) continue;
    const uint32_t v = i < s.values.size() ? s.values[i] : 0;
    spoiler += "  " + sc.switches[i].label + ": " + sc.switches[i].choiceNames[v] + "\n";
  }
  spoiler += "\n";
  for (const std::string& line : r.mod.spoiler) spoiler += line + "\n";
  return write(folder / "mod.json", mod) && write(folder / "data" / "randomizer.json", r.mod.ToJson("randomizer " + r.code)) &&
         write(folder / "spoiler.txt", spoiler);
}

Style StyleOf(const er::Settings& s) {
  for (Style style : {Style::Light, Style::Classic, Style::Chaos}) {
    er::Settings t;
    ApplyStyle(t, style);
    er::Settings u = s;
    u.values.resize(t.values.size(), 0);
    if (u.values == t.values) return style;
  }
  return Style::Custom;
}

uint32_t CodeVersionOf(const std::string& code) {
  er::Settings s;
  bool seedOnly = false;
  std::string error;
  if (!er::DecodeCode(GetSchema(), code, s, seedOnly, error) || seedOnly) return GetSchema().version;
  return GetSchema().CodeVersion(s);
}

std::string CodeFor(Style style, uint64_t seed) {
  er::Settings s;
  ApplyStyle(s, style);
  s.seed = seed;
  return er::EncodeCode(GetSchema(), s);
}

bool NormalizeCode(const std::string& text, std::string& code, std::string& error) {
  const size_t colon = text.find(':');
  if (colon != std::string::npos) {
    Style style;
    std::string name = text.substr(0, colon);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string seed = text.substr(colon + 1);
    if (!StyleFromName(name, style) || style == Style::Custom || seed.empty() ||
        seed.find_first_not_of("0123456789") != std::string::npos || seed.size() > 19) {
      error = "expected light|classic|chaos:<seed number> or a code";
      return false;
    }
    code = CodeFor(style, std::stoull(seed));
    return true;
  }
  er::Settings s;
  ApplyStyle(s, Style::Classic);
  bool seedOnly = false;
  if (!er::DecodeCode(GetSchema(), text, s, seedOnly, error)) return false;
  code = er::EncodeCode(GetSchema(), s);
  return true;
}

}
