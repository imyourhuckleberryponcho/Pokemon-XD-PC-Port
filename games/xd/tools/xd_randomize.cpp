#include "../randomizer/xd_randomizer.h"
#include "../src/data/xd_data_mods.h"
#include "../src/dol_tables.h"

#include <gs_formats.h>
#include <mods.h>
#include <port_disc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace em = engine::mods;
namespace er = engine::randomizer;
namespace xr = xd::randomizer;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

bool read_disc_entry(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
  static std::map<std::pair<std::string, std::string>, std::pair<bool, std::vector<uint8_t>>> cache;
  auto it = cache.find({archive, entry});
  if (it != cache.end()) {
    out = it->second.second;
    return it->second.first;
  }
  bool ok = false;
  std::vector<uint8_t> bytes;
  size_t size = 0;
  if (archive == em::kProgramArchive) {
    const uint8_t* dol = PortDisc_Dol(&size);
    if (dol != nullptr && size != 0) {
      bytes.assign(dol, dol + size);
      ok = true;
    }
  } else if (uint8_t* file = PortDisc_ReadOriginalFile(archive.c_str(), &size)) {
    FsysArchive f{};
    if (Fsys_Open(file, size, &f)) {
      for (uint32_t i = 0; i < f.count && !ok; i++) {
        FsysEntry e;
        if (Fsys_Entry(&f, i, &e) && (std::strcmp(e.fullName, entry.c_str()) == 0 || std::strcmp(e.name, entry.c_str()) == 0)) {
          uint32_t n = 0;
          if (uint8_t* p = Fsys_Extract(&f, i, &n)) {
            bytes.assign(p, p + n);
            std::free(p);
            ok = true;
          }
        }
      }
    }
    PortDisc_Free(file);
  }
  cache[{archive, entry}] = {ok, bytes};
  out = bytes;
  return ok;
}

bool load_back(const em::DataCatalog& cat, const xr::Result& r, size_t& changes, std::string& report) {
  em::DataModFile file{xr::kModId, "Randomizer", "data/randomizer.json", r.mod.ToJson("check")};
  std::vector<em::Message> messages;
  const em::DataApplyResult applied = em::ApplyDataMods(cat, {file}, read_disc_entry, messages);
  changes = applied.changes.size();
  bool clean = true;
  for (const em::Message& m : messages) {
    if (m.level == em::Message::Level::Info) continue;
    report += m.text + "\n";
    clean = false;
  }
  return clean;
}

int run_selftest(const em::DataCatalog& cat) {
  const auto& sc = xr::GetSchema();
  std::string error;
  auto make = [&](xr::Style style, uint64_t seed) {
    er::Settings s;
    xr::ApplyStyle(s, style);
    s.seed = seed;
    xr::Result r;
    if (!xr::Generate(cat, read_disc_entry, s, r, error)) std::printf("FAIL generate: %s\n", error.c_str());
    return r;
  };
  const xr::Result c1 = make(xr::Style::Classic, 12345), c2 = make(xr::Style::Classic, 12345), c3 = make(xr::Style::Classic, 12346);
  check(c1.mod.Count() > 0, "Classic changes something (" + std::to_string(c1.mod.Count()) + " numbers)");
  check(c1.mod.ToJson("x") == c2.mod.ToJson("x") && c1.mod.spoiler == c2.mod.spoiler, "same code twice: byte-identical mod and spoiler");
  check(c1.mod.ToJson("x") != c3.mod.ToJson("x"), "another seed: another mod");

  er::Settings off;
  off.values.assign(sc.switches.size(), 0);
  off.seed = 777;
  xr::Result r0;
  check(xr::Generate(cat, read_disc_entry, off, r0, error) && r0.mod.Count() == 0, "every switch off: empty mod");

  const xr::Result light = make(xr::Style::Light, 99);
  bool onlyLight = true;
  for (const auto& [table, entries] : light.mod.tables) {
    onlyLight &= table == "esabapokemondata_E1" || table == "esabapokemondata_E2" || table == "esabapokemondata_E3" ||
                 table == "story_pokemon" || table == "darkPokemonList" || table == "floor_tresure_list";
  }
  check(onlyLight && light.mod.tables.count("esabapokemondata_E1") && light.mod.tables.count("floor_tresure_list"),
        "Light changes only Poke Spots, Shadow Pokemon and item boxes");

  er::Settings wildOnly = off, classic;
  wildOnly.seed = classic.seed = 4242;
  xr::ApplyStyle(classic, xr::Style::Classic);
  wildOnly.Set(sc, "wild", 1);
  wildOnly.Set(sc, "wild.similar", 1);
  xr::Result rw, rc;
  xr::Generate(cat, read_disc_entry, wildOnly, rw, error);
  xr::Generate(cat, read_disc_entry, classic, rc, error);
  check(rw.mod.tables.count("esabapokemondata_E1") && rw.mod.tables.size() <= 3 &&
            rw.mod.tables["esabapokemondata_E2"] == rc.mod.tables["esabapokemondata_E2"],
        "a category's picks don't depend on the other switches");

  er::Settings back;
  bool seedOnly = true;
  check(er::DecodeCode(sc, c1.code, back, seedOnly, error) && !seedOnly && back.seed == 12345 && make(xr::Style::Custom, 0).code != c1.code,
        "long code round-trips (" + c1.code + ")");
  {
    er::Settings fromCode;
    er::DecodeCode(sc, c1.code, fromCode, seedOnly, error);
    xr::Result again;
    xr::Generate(cat, read_disc_entry, fromCode, again, error);
    check(again.mod.ToJson("x") == c1.mod.ToJson("x"), "the code alone makes the same mod");
  }

  for (const auto* res : {&c1, &c3}) {
    size_t changes = 0;
    std::string report;
    const bool loaded = load_back(cat, *res, changes, report);
    check(loaded && changes == res->mod.Count(),
          "the data-mod layer takes every number (" + std::to_string(changes) + ")" + (report.empty() ? "" : ":\n" + report));
  }
  const xr::Result chaos = make(xr::Style::Chaos, 5);
  {
    size_t changes = 0;
    std::string report;
    const bool loaded = load_back(cat, chaos, changes, report);
    check(loaded && changes == chaos.mod.Count(), "Chaos too (" + std::to_string(changes) + ")" + report);
  }
  for (const auto* res : {&c1, &chaos}) {
    std::set<uint32_t> species;
    size_t shadows = 0;
    const em::DataTable* trainers = cat.FindTable("story_trainers");
    const em::DataTable* pok = cat.FindTable("story_pokemon");
    const em::DataTable* dark = cat.FindTable("darkpokemon_shadow");
    std::vector<uint8_t> deck, darkBytes;
    read_disc_entry(trainers->places[0].archive, trainers->places[0].entry, deck);
    read_disc_entry(dark->places[0].archive, dark->places[0].entry, darkBytes);
    auto field = [&](const em::DataTable* t, const char* name) -> const em::DataField& {
      for (const auto& f : t->fields) {
        if (f.name == name) return f;
      }
      return t->fields[0];
    };
    std::set<uint32_t> seen;
    for (uint32_t tr = 0; tr < trainers->count; tr++) {
      const uint8_t* e = deck.data() + trainers->places[0].offset + tr * trainers->entrySize;
      const uint32_t flags = em::ReadDataField(e, field(trainers, "darkPokemonFlags"), 0);
      for (uint32_t k = 0; k < 6; k++) {
        const uint32_t id = em::ReadDataField(e, field(trainers, "pokemonDataID"), k);
        if (!(flags & (1u << k)) || id == 0 || !seen.insert(id).second) continue;
        const uint32_t d = em::ReadDataField(darkBytes.data() + dark->places[0].offset + id * dark->entrySize, field(dark, "deckPokemonID"), 0);
        uint32_t sp = em::ReadDataField(deck.data() + pok->places[0].offset + d * pok->entrySize, field(pok, "pokemonID"), 0);
        auto t = res->mod.tables.find("story_pokemon");
        if (t != res->mod.tables.end()) {
          auto en = t->second.find(d);
          if (en != t->second.end() && en->second.count("pokemonID")) sp = en->second.at("pokemonID");
        }
        shadows++;
        species.insert(sp);
      }
    }
    check(shadows > 50 && species.size() == shadows, "every Shadow a different species (" + std::to_string(shadows) + " Shadows)");
    const em::DataTable* list = cat.FindTable("darkPokemonList");
    std::vector<uint8_t> dol;
    size_t listOk = 0, listChanged = 0, discOk = 0;
    if (list != nullptr && read_disc_entry(list->places[0].archive, list->places[0].entry, dol)) {
      seen.clear();
      for (uint32_t tr = 0; tr < trainers->count; tr++) {
        const uint8_t* e = deck.data() + trainers->places[0].offset + tr * trainers->entrySize;
        const uint32_t flags = em::ReadDataField(e, field(trainers, "darkPokemonFlags"), 0);
        for (uint32_t k = 0; k < 6; k++) {
          const uint32_t id = em::ReadDataField(e, field(trainers, "pokemonDataID"), k);
          if (!(flags & (1u << k)) || id == 0 || id >= list->count || !seen.insert(id).second) continue;
          const uint32_t d = em::ReadDataField(darkBytes.data() + dark->places[0].offset + id * dark->entrySize, field(dark, "deckPokemonID"), 0);
          const uint32_t discSp = em::ReadDataField(deck.data() + pok->places[0].offset + d * pok->entrySize, field(pok, "pokemonID"), 0);
          uint32_t sp = discSp;
          auto t = res->mod.tables.find("story_pokemon");
          if (t != res->mod.tables.end()) {
            auto en = t->second.find(d);
            if (en != t->second.end() && en->second.count("pokemonID")) sp = en->second.at("pokemonID");
          }
          const uint32_t discList = em::ReadDataField(dol.data() + list->places[0].offset + id * list->entrySize, list->fields[0], 0);
          uint32_t listSp = discList;
          auto l = res->mod.tables.find("darkPokemonList");
          if (l != res->mod.tables.end()) {
            auto en = l->second.find(id);
            if (en != l->second.end() && en->second.count("pokemonID")) listSp = en->second.at("pokemonID"), listChanged++;
          }
          discOk += discList == discSp;
          listOk += listSp == sp;
        }
      }
    }
    check(list != nullptr && discOk == shadows && listOk == shadows && listChanged > 0,
          "the Shadow species list (main.dol) matches every story Shadow: disc " + std::to_string(discOk) + ", randomized " +
              std::to_string(listOk) + " of " + std::to_string(shadows) + " (" + std::to_string(listChanged) + " rewritten)");
  }
  {
    int good = 0, runs = 0;
    std::string firstError;
    for (xr::Style style : {xr::Style::Light, xr::Style::Classic, xr::Style::Chaos}) {
      for (uint32_t legend = 0; legend < 3; legend++) {
        for (uint64_t seed = 1; seed <= 4; seed++) {
          er::Settings s;
          xr::ApplyStyle(s, style);
          s.Set(sc, "legendaries", legend);
          s.seed = seed * 1000003;
          xr::Result r;
          runs++;
          if (xr::Generate(cat, read_disc_entry, s, r, error)) good++;
          else if (firstError.empty()) firstError = error;
        }
      }
    }
    check(good == runs, "generator's own checks pass on " + std::to_string(runs) + " runs" + (firstError.empty() ? "" : ": " + firstError));
  }
  bool noBanned = true;
  for (const auto& [e, fields] : c1.mod.tables.count("pokemon_data") ? c1.mod.tables.at("pokemon_data") : decltype(c1.mod.tables)::mapped_type{}) {
    for (const auto& [f, v] : fields) {
      if (f.rfind("tokuseiDataId", 0) == 0) noBanned &= v != 25 && v != 59;
    }
  }
  check(noBanned, "no Wonder Guard / Forecast handed out");

  {
    er::Settings tmOnly = off;
    tmOnly.seed = 12345;
    tmOnly.Set(sc, "tm_moves", 1);
    xr::Result tm;
    const bool made = xr::Generate(cat, read_disc_entry, tmOnly, tm, error);
    check(made && tm.code.rfind("XDR2-", 0) == 0, "TM moves on: a version-2 code (" + tm.code + ")" + (made ? "" : ": " + error));
    bool onlyTm = tm.mod.tables.size() == 2 && tm.mod.tables.count("item_waza_machine_no") && tm.mod.tables.count("item_data_prime");
    std::set<uint32_t> moves;
    size_t machines = 0;
    bool movesOk = true;
    if (onlyTm) {
      for (const auto& [entry, fields] : tm.mod.tables.at("item_waza_machine_no")) {
        machines++;
        onlyTm &= fields.size() == 1 && fields.count("wazaID") == 1 && entry < 50;
        for (const auto& [f, v] : fields) movesOk &= v >= 1 && v <= 354 && v != 165 && moves.insert(v).second;
      }
      for (const auto& [entry, fields] : tm.mod.tables.at("item_data_prime")) onlyTm &= fields.size() == 1 && fields.count("doc") == 1;
    }
    check(onlyTm && machines >= 40 && machines <= 50,
          "TM moves alone change only TM01-TM50's moves and their items' descriptions (" + std::to_string(machines) + " TMs)");
    check(movesOk && moves.size() == machines, "every changed TM a different, real move (no Shadow move, no Struggle)");
    size_t changes = 0;
    std::string report;
    const bool loaded = load_back(cat, tm, changes, report);
    check(loaded && changes == tm.mod.Count() && changes == machines * 2,
          "the data-mod layer takes them, main.dol's table too (" + std::to_string(changes) + ")" + report);
    xr::Result tm2;
    tmOnly.seed = 12346;
    xr::Generate(cat, read_disc_entry, tmOnly, tm2, error);
    check(tm2.mod.tables["item_waza_machine_no"] != tm.mod.tables["item_waza_machine_no"], "another seed: other TM moves");
    auto ohkos = [](const xr::Result& r) {
      size_t n = 0;
      const auto t = r.mod.tables.find("item_waza_machine_no");
      if (t == r.mod.tables.end()) return n;
      for (const auto& [entry, fields] : t->second) {
        for (const auto& [f, v] : fields) n += v == 12 || v == 32 || v == 90 || v == 329;
      }
      return n;
    };
    size_t fairHits = 0, plainHits = 0, fairRuns = 0;
    xr::Result fair12345;
    for (uint64_t seed = 12345; seed < 12345 + 30; seed++) {
      er::Settings a = off, b = off;
      a.seed = b.seed = seed;
      a.Set(sc, "tm_moves", 1);
      b.Set(sc, "tm_moves", 2);
      xr::Result ra, rb;
      xr::Generate(cat, read_disc_entry, a, ra, error);
      fairRuns += xr::Generate(cat, read_disc_entry, b, rb, error) && rb.mod.tables.count("item_waza_machine_no") ? 1 : 0;
      plainHits += ohkos(ra);
      fairHits += ohkos(rb);
      if (seed == 12345) fair12345 = rb;
    }
    check(fairRuns == 30 && fairHits == 0 && plainHits > 0,
          "no one-hit-KO move on a TM when ruled out (30 seeds; plain random handed out " + std::to_string(plainHits) + ")");
    check(c1.mod.tables.count("item_waza_machine_no") &&
              c1.mod.tables.at("item_waza_machine_no") == fair12345.mod.tables["item_waza_machine_no"],
          "Classic with the same seed picks the same TM moves (own random stream)");
  }

  {
    struct Old {
      const char* code;
      xr::Style style;
      uint64_t seed;
    };
    const Old olds[] = {{"XDR1-2622-2222-2225-2GHZ-VPWR-4", xr::Style::Classic, 12345},
                        {"XDR1-2622-2222-2222-84FC-QUDQ-G", xr::Style::Chaos, 777}};
    for (const Old& o : olds) {
      er::Settings s, expect;
      bool seedOnly = true;
      xr::ApplyStyle(expect, o.style);
      expect.Set(sc, "tm_moves", 0);
      const bool decoded = er::DecodeCode(sc, o.code, s, seedOnly, error);
      check(decoded && !seedOnly && s.seed == o.seed && s.values == expect.values && s.Get(sc, "tm_moves") == 0,
            std::string("version-1 code ") + o.code + " still read: the old style's switches, TM moves off");
      check(er::EncodeCode(sc, s) == o.code && xr::CodeVersionOf(o.code) == 1, "and written back the same, as version 1");
      std::string normal;
      check(xr::NormalizeCode(o.code, normal, error) && normal == o.code, "a typed version-1 code stays that code");
      xr::Result r;
      const bool made = xr::Generate(cat, read_disc_entry, s, r, error);
      check(made && r.code == o.code && r.mod.tables.count("item_waza_machine_no") == 0 && r.mod.tables.count("item_data_prime") == 0,
            "its game has the disc's TM moves");
      xr::Result now = make(o.style, o.seed);
      now.mod.tables.erase("item_waza_machine_no");
      now.mod.tables.erase("item_data_prime");
      check(made && r.mod.ToJson("x") == now.mod.ToJson("x"), "and every other number as this build's style makes it");
    }
    check(xr::CodeFor(xr::Style::Light, 99) == light.code && light.code.rfind("XDR1-", 0) == 0,
          "Light (no new switch) still gives version-1 codes: " + light.code);
    check(c1.code.rfind("XDR2-", 0) == 0 && xr::CodeVersionOf(c1.code) == 2, "Classic now gives a version-2 code: " + c1.code);
  }
  {
    auto only = [&](uint64_t seed, uint32_t levels, uint32_t items, uint32_t natures) {
      er::Settings s = off;
      s.seed = seed;
      s.Set(sc, "trainer_levels", levels);
      s.Set(sc, "trainer_items", items);
      s.Set(sc, "trainer_natures", natures);
      xr::Result r;
      if (!xr::Generate(cat, read_disc_entry, s, r, error)) std::printf("FAIL generate: %s\n", error.c_str());
      return r;
    };
    auto count = [](const xr::Result& r, const char* field) {
      size_t n = 0;
      const auto t = r.mod.tables.find("story_pokemon");
      if (t == r.mod.tables.end()) return n;
      for (const auto& [entry, fields] : t->second) n += fields.count(field);
      return n;
    };
    const em::DataTable* pok = cat.FindTable("story_pokemon");
    std::vector<uint8_t> deck;
    read_disc_entry(pok->places[0].archive, pok->places[0].entry, deck);
    auto disc = [&](uint32_t entry, const char* name) {
      for (const auto& f : pok->fields) {
        if (f.name == name) return em::ReadDataField(deck.data() + pok->places[0].offset + entry * pok->entrySize, f, 0);
      }
      return 0u;
    };
    const xr::Result all = only(12345, 2, 1, 1), again = only(12345, 2, 1, 1), other = only(12346, 2, 1, 1);
    bool onlyThose = all.mod.tables.size() == 1 && all.mod.tables.count("story_pokemon") == 1;
    if (onlyThose) {
      for (const auto& [entry, fields] : all.mod.tables.at("story_pokemon")) {
        for (const auto& [f, v] : fields) onlyThose &= f == "level" || f == "holdItem" || f == "seikakuID";
      }
    }
    check(all.code.rfind("XDR3-", 0) == 0 && xr::CodeVersionOf(all.code) == 3, "trainer options on: a version-3 code (" + all.code + ")");
    check(onlyThose && count(all, "level") > 300 && count(all, "holdItem") > 300 && count(all, "seikakuID") > 300,
          "levels +20%, random held items and natures alone change only those numbers of trainers' Pokemon (" +
              std::to_string(count(all, "level")) + " levels, " + std::to_string(count(all, "holdItem")) + " items, " +
              std::to_string(count(all, "seikakuID")) + " natures)");
    check(all.mod.ToJson("x") == again.mod.ToJson("x") && all.mod.spoiler == again.mod.spoiler, "the same code twice: the same levels, items and natures");
    check(count(other, "level") == count(all, "level") && other.mod.tables.at("story_pokemon") != all.mod.tables.at("story_pokemon"),
          "another seed: the same levels (no chance in them), other items and natures");
    size_t changes = 0;
    std::string report;
    const bool loaded = load_back(cat, all, changes, report);
    check(loaded && changes == all.mod.Count(), "the data-mod layer takes them (" + std::to_string(changes) + ")" + report);
    bool levelsOk = true, naturesOk = true;
    std::set<uint32_t> items;
    for (const auto& [entry, fields] : all.mod.tables.at("story_pokemon")) {
      for (const auto& [f, v] : fields) {
        if (f == "level") levelsOk &= v == er::ScaleLevel(disc(entry, "level"), 20, 100) && v >= 1 && v <= 100 && v > disc(entry, "level");
        if (f == "seikakuID") naturesOk &= v < 25 && disc(entry, "randomSetting") == 0;
        if (f == "holdItem") items.insert(v);
      }
    }
    check(levelsOk, "every level is the disc's +20% (rounded, at least one up), within 1-100");
    check(naturesOk, "every nature is one of the 25, only on Pokemon whose nature the game takes from the record");
    check(items.size() >= 5 && items.size() <= 80 && items.count(0) == 0,
          "held items come from a pool of " + std::to_string(items.size()) + " items trainers' Pokemon hold on the disc");
    {
      const em::DataTable* dark = cat.FindTable("darkpokemon_shadow");
      std::vector<uint8_t> darkBytes;
      read_disc_entry(dark->places[0].archive, dark->places[0].entry, darkBytes);
      bool shadowKept = true;
      for (uint32_t i = 1; i < dark->count; i++) {
        uint32_t d = 0;
        for (const auto& f : dark->fields) {
          if (f.name == "deckPokemonID") d = em::ReadDataField(darkBytes.data() + dark->places[0].offset + i * dark->entrySize, f, 0);
        }
        shadowKept &= d == 0 || all.mod.tables.at("story_pokemon").count(d) == 0;
      }
      check(shadowKept, "no Shadow Pokemon's level, item or nature changed");
    }
    const xr::Result none = only(12345, 0, 2, 0);
    bool noneOk = none.mod.tables.size() == 1 && count(none, "holdItem") == none.mod.Count() && none.mod.Count() > 0;
    if (noneOk) {
      for (const auto& [entry, fields] : none.mod.tables.at("story_pokemon")) noneOk &= fields.at("holdItem") == 0 && disc(entry, "holdItem") != 0;
    }
    check(noneOk, "held items \"none\": the " + std::to_string(none.mod.Count()) + " held items of trainers' Pokemon removed, nothing else");
    const xr::Result naturesOnly = only(12345, 0, 0, 1), itemsOnly = only(12345, 0, 1, 0);
    bool sameNatures = count(naturesOnly, "seikakuID") == count(all, "seikakuID"), sameItems = count(itemsOnly, "holdItem") == count(all, "holdItem");
    for (const auto& [entry, fields] : all.mod.tables.at("story_pokemon")) {
      if (fields.count("seikakuID")) sameNatures = sameNatures && naturesOnly.mod.tables.at("story_pokemon").count(entry) && naturesOnly.mod.tables.at("story_pokemon").at(entry).at("seikakuID") == fields.at("seikakuID");
      if (fields.count("holdItem")) sameItems = sameItems && itemsOnly.mod.tables.at("story_pokemon").count(entry) && itemsOnly.mod.tables.at("story_pokemon").at(entry).at("holdItem") == fields.at("holdItem");
    }
    check(sameNatures && sameItems, "natures and items don't depend on each other or on the levels (own random streams)");
    er::Settings classicPlus;
    xr::ApplyStyle(classicPlus, xr::Style::Classic);
    classicPlus.seed = 12345;
    classicPlus.Set(sc, "trainer_levels", 4);
    classicPlus.Set(sc, "trainer_items", 1);
    classicPlus.Set(sc, "trainer_natures", 1);
    xr::Result plus;
    const bool madePlus = xr::Generate(cat, read_disc_entry, classicPlus, plus, error);
    bool sameRest = madePlus && plus.mod.tables.size() == c1.mod.tables.size();
    size_t movesFollow = 0;
    if (sameRest) {
      for (const auto& [table, entries] : c1.mod.tables) {
        if (table != "story_pokemon") {
          sameRest &= plus.mod.tables.count(table) && plus.mod.tables.at(table) == entries;
          continue;
        }
        for (const auto& [entry, fields] : entries) {
          const auto& now = plus.mod.tables.at(table).at(entry);
          for (const auto& [f, v] : fields) {
            if (f == "pokemonID") sameRest &= now.count(f) && now.at(f) == v;
            else if (f.rfind("wazaID", 0) == 0 && now.count("level") && (!now.count(f) || now.at(f) != v)) movesFollow++;
            else if (f.rfind("wazaID", 0) == 0 && !now.count("level")) sameRest &= now.count(f) && now.at(f) == v;
          }
        }
      }
    }
    check(sameRest, std::string("Classic + the trainer options: the same species and other tables as Classic alone") + (madePlus ? "" : ": " + error));
    check(movesFollow > 0, "\"by level\" moves are picked for the new level (" + std::to_string(movesFollow) + " moves differ at +50%)");
    {
      size_t n = 0;
      std::string rep2;
      const bool ok2 = load_back(cat, plus, n, rep2);
      check(ok2 && n == plus.mod.Count(), "the data-mod layer takes Classic + trainer options (" + std::to_string(n) + ")" + rep2);
    }
    const xr::Result strengthAlone = only(5, 5, 0, 0);
    check(strengthAlone.mod.Count() == 0, "levels \"matched to strength\" with the disc's own Pokemon: nothing changes");
    er::Settings chaosStrength;
    xr::ApplyStyle(chaosStrength, xr::Style::Chaos);
    chaosStrength.seed = 5;
    chaosStrength.Set(sc, "trainer_levels", 5);
    xr::Result cs;
    const bool madeCs = xr::Generate(cat, read_disc_entry, chaosStrength, cs, error);
    bool quarter = madeCs;
    size_t moved = 0;
    if (madeCs) {
      for (const auto& [entry, fields] : cs.mod.tables.at("story_pokemon")) {
        if (!fields.count("level")) continue;
        const uint32_t was = disc(entry, "level"), now = fields.at("level");
        moved++;
        quarter &= now >= 1 && now <= 100 && now * 4 >= was * 3 && now * 4 <= was * 5;
      }
    }
    check(quarter && moved > 50, "with Chaos's species: " + std::to_string(moved) + " levels move, each within a quarter of the disc's");
    int good = 0, runs = 0;
    std::string firstError;
    for (xr::Style style : {xr::Style::Light, xr::Style::Classic, xr::Style::Chaos}) {
      for (uint32_t levels = 0; levels < 6; levels++) {
        for (uint32_t items = 0; items < 3; items++) {
          er::Settings s;
          xr::ApplyStyle(s, style);
          s.seed = 31337 + levels * 7 + items;
          s.Set(sc, "trainer_levels", levels);
          s.Set(sc, "trainer_items", items);
          s.Set(sc, "trainer_natures", (levels + items) % 2);
          xr::Result r;
          runs++;
          if (xr::Generate(cat, read_disc_entry, s, r, error)) good++;
          else if (firstError.empty()) firstError = error;
        }
      }
    }
    check(good == runs, "generator's own checks pass with every level / item choice on " + std::to_string(runs) + " runs" +
                            (firstError.empty() ? "" : ": " + firstError));
  }
  {
    struct Old {
      const char* code;
      xr::Style style;
      uint64_t seed;
    };
    const Old olds[] = {{"XDR2-2A22-2222-2225-2GHZ-VQ2G-PB2", xr::Style::Classic, 12345},
                        {"XDR2-2A22-2222-2222-84FC-QT23-E6A", xr::Style::Chaos, 777}};
    for (const Old& o : olds) {
      er::Settings s;
      bool seedOnly = true;
      const bool decoded = er::DecodeCode(sc, o.code, s, seedOnly, error);
      check(decoded && !seedOnly && s.seed == o.seed && s.Get(sc, "trainer_levels") == 0 && s.Get(sc, "trainer_items") == 0 &&
                s.Get(sc, "trainer_natures") == 0 && xr::StyleOf(s) == o.style,
            std::string("version-2 code ") + o.code + " still read: the style, trainer levels / items / natures kept");
      check(er::EncodeCode(sc, s) == o.code && xr::CodeFor(o.style, o.seed) == o.code && xr::CodeVersionOf(o.code) == 2,
            "and written back the same; the style + seed still gives this code");
    }
  }
  std::printf(sOk ? "ALL OK\n" : "FAILED\n");
  return sOk ? 0 : 1;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "Usage: %s <disc> --code <code> --out <folder>\n"
                 "       %s <disc> --style light|classic|chaos|custom --seed <n> [--set id=value]... --out <folder>\n"
                 "       %s <disc> --switches | --selftest\n",
                 argv[0], argv[0], argv[0]);
    return 2;
  }
  const auto& sc = xr::GetSchema();
  if (std::strcmp(argv[2], "--switches") == 0) {
    for (const auto& sw : sc.switches) {
      std::printf("%-20s %s:", sw.id.c_str(), sw.label.c_str());
      for (size_t i = 0; i < sw.choiceNames.size(); i++) std::printf(" %zu=%s", i, sw.choiceNames[i].c_str());
      std::printf("\n");
    }
    return 0;
  }
  char err[512];
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Open(argv[1], err, sizeof(err)) ? PortDisc_Dol(&dolSize) : nullptr;
  if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, err, sizeof(err))) {
    std::fprintf(stderr, "FAILED: %s\n", err);
    return 1;
  }
  em::DataCatalog cat;
  std::string error;
  if (!xd::data::BuildDataCatalog(read_disc_entry, cat, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.c_str());
    return 1;
  }
  if (std::strcmp(argv[2], "--selftest") == 0) return run_selftest(cat);

  er::Settings s;
  xr::ApplyStyle(s, xr::Style::Custom);
  std::string out;
  for (int i = 2; i < argc; i++) {
    const std::string a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (a == "--code" && v) {
      bool seedOnly = false;
      if (!er::DecodeCode(sc, v, s, seedOnly, error)) {
        std::fprintf(stderr, "FAILED: %s\n", error.c_str());
        return 2;
      }
      i++;
    } else if (a == "--style" && v) {
      xr::Style st;
      if (!xr::StyleFromName(v, st)) {
        std::fprintf(stderr, "unknown style %s\n", v);
        return 2;
      }
      xr::ApplyStyle(s, st);
      i++;
    } else if (a == "--seed" && v) {
      s.seed = std::strtoull(v, nullptr, 10);
      i++;
    } else if (a == "--set" && v) {
      const std::string kv = v;
      const size_t eq = kv.find('=');
      if (eq == std::string::npos || sc.Find(kv.substr(0, eq)) < 0) {
        std::fprintf(stderr, "unknown switch in %s (see --switches)\n", v);
        return 2;
      }
      s.Set(sc, kv.substr(0, eq), static_cast<uint32_t>(std::strtoul(kv.c_str() + eq + 1, nullptr, 10)));
      i++;
    } else if (a == "--out" && v) {
      out = v;
      i++;
    } else {
      std::fprintf(stderr, "unknown or incomplete option %s\n", a.c_str());
      return 2;
    }
  }
  if (out.empty()) {
    std::fprintf(stderr, "--out <folder> is needed\n");
    return 2;
  }
  xr::Result r;
  if (!xr::Generate(cat, read_disc_entry, s, r, error) || !xr::WriteMod(r, s, out, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.c_str());
    return 1;
  }
  size_t changes = 0;
  std::string report;
  const bool ok = load_back(cat, r, changes, report) && changes == r.mod.Count();
  std::printf("code: %s\n%zu numbers changed, written to %s (data/randomizer.json, spoiler.txt)\n", r.code.c_str(), r.mod.Count(), out.c_str());
  std::printf("%s the data-mod layer took %zu of them%s%s\n", ok ? "ok  " : "FAIL", changes, report.empty() ? "" : ":\n", report.c_str());
  return ok ? 0 : 1;
}
