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
      const bool byNumber = entry.size() >= 2 && entry[0] == '#';
      const unsigned long number = byNumber ? std::strtoul(entry.c_str() + 1, nullptr, 10) : 0;
      for (uint32_t i = 0; i < f.count && !ok; i++) {
        FsysEntry e;
        if (byNumber ? i == number
                     : Fsys_Entry(&f, i, &e) && (std::strcmp(e.fullName, entry.c_str()) == 0 || std::strcmp(e.name, entry.c_str()) == 0)) {
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
  {
    auto val = [&](const char* table, uint32_t entry, const char* name, uint32_t element = 0) {
      const em::DataTable* t = cat.FindTable(table);
      if (t == nullptr || entry >= t->count) return 0u;
      static std::map<std::string, std::vector<uint8_t>> files;
      const std::string key = t->places[0].archive + "/" + t->places[0].entry;
      if (files.count(key) == 0) read_disc_entry(t->places[0].archive, t->places[0].entry, files[key]);
      for (const auto& f : t->fields) {
        if (f.name == name) return em::ReadDataField(files[key].data() + t->places[0].offset + entry * t->entrySize, f, element);
      }
      return 0u;
    };
    auto with = [&](er::Settings s, uint64_t seed, std::initializer_list<std::pair<const char*, uint32_t>> sets) {
      s.seed = seed;
      for (const auto& [id, v] : sets) s.Set(sc, id, v);
      xr::Result r;
      if (!xr::Generate(cat, read_disc_entry, s, r, error)) std::printf("FAIL generate: %s\n", error.c_str());
      return r;
    };
    auto now = [&](const xr::Result& r, const char* table, uint32_t entry, const char* field) {
      const auto t = r.mod.tables.find(table);
      if (t != r.mod.tables.end()) {
        const auto e = t->second.find(entry);
        if (e != t->second.end() && e->second.count(field)) return e->second.at(field);
      }
      return val(table, entry, field);
    };
    auto game = [&](const xr::Result& r, size_t& texts, std::string& report) {
      em::DataModFile file{xr::kModId, "Randomizer", "data/randomizer.json", r.GameJson("check")};
      std::vector<em::Message> messages;
      const em::DataApplyResult applied = em::ApplyDataMods(cat, {file}, read_disc_entry, messages);
      texts = applied.texts.size();
      bool clean = true;
      for (const em::Message& m : messages) {
        if (m.level == em::Message::Level::Info) continue;
        report += m.text + "\n";
        clean = false;
      }
      return clean;
    };
    er::Settings classic;
    xr::ApplyStyle(classic, xr::Style::Classic);
    const uint32_t goodsCount = cat.FindTable("shopGoods") != nullptr ? cat.FindTable("shopGoods")->count : 0;
    check(goodsCount > 100 && cat.FindTable("shopData") != nullptr && cat.FindTable("shopGoods")->places.size() == 2,
          "the shops' tables are found, in both copies of the menu module (" + std::to_string(goodsCount) + " goods entries)");

    for (uint32_t mode : {1u, 2u, 3u}) {
      const std::string label = mode == 1 ? "shops shuffled" : mode == 2 ? "shops everyday goods" : "shops anything";
      const xr::Result a = with(off, 2024, {{"shops", mode}}), b = with(off, 2024, {{"shops", mode}}), c = with(off, 2025, {{"shops", mode}});
      check(a.code.rfind("XDR4-", 0) == 0 && xr::CodeVersionOf(a.code) == 4, label + ": a version-4 code (" + a.code + ")");
      const bool onlyGoods = a.mod.tables.size() == 1 && a.mod.tables.count("shopGoods") == 1;
      check(onlyGoods && a.mod.Count() > 40, label + ": only shop goods change (" + std::to_string(a.mod.Count()) + ")");
      check(a.mod.ToJson("x") == b.mod.ToJson("x") && a.mod.spoiler == b.mod.spoiler && a.mod.ToJson("x") != c.mod.ToJson("x"),
            label + ": same code = same stock, another seed = another");
      bool ends = true, kept = true;
      std::multiset<uint32_t> before, after;
      size_t balls = 0;
      for (uint32_t g = 0; g < goodsCount; g++) {
        const uint32_t was = val("shopGoods", g, "value"), is = now(a, "shopGoods", g, "value");
        ends &= (was == 0) == (is == 0);
        before.insert(was);
        after.insert(is);
        if (was == 0) continue;
        const uint32_t kind = val("item_data_prime", val("item_data_index", was, "value"), "kind");
        if (kind != 2 && kind != 3 && kind != 4) {
          balls += kind == 1 ? 1 : 0;
          kept &= was == is;
        }
      }
      check(ends, label + ": every list keeps its length (no end mark moved)");
      check(kept && balls >= 10, label + ": Poke Balls (" + std::to_string(balls) + " entries), Poke Snacks and all other special goods stay");
      if (mode == 1) check(before == after, "shops shuffled: the same items as on the disc, in other places");
      if (mode == 2) {
        bool everyday = true;
        std::string bad;
        for (const auto& [g, fields] : a.mod.tables.at("shopGoods")) {
          const uint32_t e = val("item_data_index", fields.at("value"), "value");
          const uint32_t kind = val("item_data_prime", e, "kind"), eff = val("item_data_prime", e, "itemEffectParam");
          const bool ok = kind == 3 || (kind == 2 && ((eff >= 1 && eff <= 31) || (eff >= 41 && eff <= 47) || e == 80 || e == 81 || (e >= 83 && e <= 86)));
          if (!ok && bad.empty()) bad = " (item entry " + std::to_string(e) + ")";
          everyday &= ok;
        }
        check(everyday, "shops everyday goods: only medicine, cures, repels, battle items and berries are sold" + bad);
      }
      if (mode == 3) {
        bool tm = false;
        for (uint64_t seed = 1; seed <= 10 && !tm; seed++) {
          const xr::Result any = with(off, seed, {{"shops", 3}});
          for (const auto& [g, fields] : any.mod.tables.at("shopGoods")) tm |=val("item_data_prime", val("item_data_index", fields.at("value"), "value"), "kind") == 4;
        }
        check(tm, "shops anything: TMs can turn up too (10 seeds)");
      }
      size_t changes = 0;
      std::string report;
      const bool loaded = load_back(cat, a, changes, report);
      check(loaded && changes == a.mod.Count(), label + ": taken by the data-mod layer (the menu module's tables)" + (report.empty() ? "" : ": " + report));
      const xr::Result plus = with(classic, 2024, {{"shops", mode}}), plain = with(classic, 2024, {});
      xr::Result rest = plus;
      rest.mod.tables.erase("shopGoods");
      check(plus.mod.tables.count("shopGoods") && plus.mod.tables.at("shopGoods") == a.mod.tables.at("shopGoods") &&
                rest.mod.ToJson("x") == plain.mod.ToJson("x"),
            label + ": on top of Classic the same stock, and Classic's own numbers untouched (own stream)");
    }
    {
      size_t changes = 0;
      std::string report;
      check(sc.Find("tutor_compat") < 0, "\"who can learn tutor moves\" is not offered (left out on purpose, 2026-11-12)");
      const uint32_t rows = cat.FindTable("wazaoshie_data") != nullptr ? cat.FindTable("wazaoshie_data")->count : 0;
      bool onlyIds = true, different = true, noTm = true, noOhko = true;
      for (uint64_t seed = 1; seed <= 20; seed++) {
        const xr::Result m = with(off, seed, {{"tutor_moves", seed % 2 ? 1u : 2u}, {"tm_moves", seed % 3 == 0 ? 1u : 0u}});
        onlyIds &= (seed % 3 == 0 || m.mod.tables.size() == 1) && m.mod.tables.count("wazaoshie_data") == 1;
        std::set<uint32_t> seen, tms;
        for (uint32_t k = 0; k < (cat.FindTable("item_waza_machine_no") ? cat.FindTable("item_waza_machine_no")->count : 0); k++) {
          tms.insert(now(m, "item_waza_machine_no", k, "wazaID"));
        }
        for (uint32_t i = 0; i < rows; i++) {
          const uint32_t mv = now(m, "wazaoshie_data", i, "id");
          different &= seen.insert(mv).second;
          noTm &= tms.count(mv) == 0;
          if (seed % 2 == 0) noOhko &= val("waza_data", mv, "seqId") != val("waza_data", 12, "seqId");
        }
        if (seed == 1) {
          for (const auto& [e, fields] : m.mod.tables.at("wazaoshie_data")) {
            for (const auto& [f, v] : fields) onlyIds &= f == "id";
          }
        }
      }
      const xr::Result t1 = with(off, 77, {{"tutor_moves", 1}}), t2 = with(off, 77, {{"tutor_moves", 1}});
      check(rows == 12 && onlyIds && t1.mod.ToJson("x") == t2.mod.ToJson("x") && load_back(cat, t1, changes, report),
            "tutor moves: only the 12 rows' move ids change, same code = same, taken by the layer" + report);
      check(different && noTm && noOhko, "tutor moves, 20 seeds: all different, none a TM teaches (also with random TMs), no one-hit-KO when ruled out");
    }
    {
      const char* spots[] = {"esabapokemondata_E1", "esabapokemondata_E2", "esabapokemondata_E3"};
      er::Settings wild = off;
      wild.Set(sc, "wild", 1);
      wild.Set(sc, "wild.similar", 1);
      bool oneToOne = true, themed = true, levelsOk = true, levelsOnly = true;
      size_t spotsChanged = 0;
      for (uint64_t seed = 1; seed <= 30; seed++) {
        const xr::Result g = with(wild, seed, {{"wild.scope", 1}}), ty = with(wild, seed, {{"wild.types", 1}});
        std::map<uint32_t, uint32_t> swap;
        std::set<uint32_t> news;
        for (const char* spot : spots) {
          for (uint32_t i = 0; i < cat.FindTable(spot)->count; i++) {
            const uint32_t was = val(spot, i, "id"), is = now(g, spot, i, "id");
            if (was == 0) continue;
            if (swap.count(was)) oneToOne &= swap[was] == is;
            else oneToOne &= news.insert(is).second;
            swap[was] = is;
            const uint32_t t2 = now(ty, spot, i, "id");
            bool shares = false;
            for (uint32_t x = 0; x < 2; x++) {
              for (uint32_t y = 0; y < 2; y++) shares |= val("pokemon_data", was, "zokuseiDataId", x) == val("pokemon_data", t2, "zokuseiDataId", y);
            }
            themed &= shares;
            spotsChanged += t2 != was ? 1 : 0;
          }
        }
      }
      check(oneToOne, "Poke Spots \"same swap everywhere\", 30 seeds: a species becomes the same new one in every spot, no two the same");
      check(themed && spotsChanged > 200, "Poke Spots \"similar types\", 30 seeds: every new Pokemon shares a type with the one it replaces");
      const xr::Result base = with(wild, 5, {});
      const xr::Result lv = with(off, 5, {{"wild_levels", 2}}), both = with(wild, 5, {{"wild_levels", 4}});
      for (const auto& [table, entries] : lv.mod.tables) {
        levelsOnly &= table.rfind("esabapokemondata_E", 0) == 0 && table != "esabapokemondata_EX";
        for (const auto& [e, fields] : entries) {
          for (const auto& [f, v] : fields) levelsOnly &= f == "minLevel" || f == "maxLevel";
        }
      }
      for (const char* spot : spots) {
        for (uint32_t i = 0; i < cat.FindTable(spot)->count; i++) {
          if (val(spot, i, "id") == 0) continue;
          const uint32_t lo = val(spot, i, "minLevel");
          levelsOk &= now(lv, spot, i, "minLevel") == er::ScaleLevel(lo, 20, 100) && now(lv, spot, i, "maxLevel") >= now(lv, spot, i, "minLevel") &&
                      now(lv, spot, i, "maxLevel") <= 100 && now(lv, spot, i, "minLevel") > lo;
          levelsOk &= now(both, spot, i, "id") == now(base, spot, i, "id") && now(both, spot, i, "minLevel") == er::ScaleLevel(lo, 50, 100);
        }
      }
      size_t changes = 0;
      std::string report;
      check(levelsOnly && levelsOk && lv.mod.Count() >= 9 && load_back(cat, both, changes, report),
            "Poke Spot levels: only the three spots' level numbers, each the disc's +N%; with random species the species are the same" + report);
      check(base.code.rfind("XDR1-", 0) == 0 && base.mod.ToJson("x") == with(wild, 5, {{"wild.scope", 0}, {"wild.types", 0}}).mod.ToJson("x"),
            "Poke Spots with the new options off: still a version-1 code and the same picks");
    }
    {
      size_t fitted = 0, givenUp = 0, longest = 0, lines = 0, textsTaken = 0;
      bool clean = true, sameNumbers = true;
      std::string firstReport;
      for (uint64_t seed = 1; seed <= 25; seed++) {
        const xr::Result r = with(off, seed, {{"tm_moves", seed % 2 ? 1u : 2u}});
        for (const auto& [id, words] : r.text.messages) {
          if (words.empty()) {
            givenUp++;
            continue;
          }
          fitted++;
          size_t line = 0, count = 1;
          for (size_t i = 0; i < words.size(); i++) {
            if (words.compare(i, 4, "{00}") == 0) {
              longest = std::max(longest, line);
              line = 0;
              count++;
              i += 3;
            } else if ((static_cast<unsigned char>(words[i]) & 0xC0) != 0x80) {
              line++;
            }
          }
          longest = std::max(longest, line);
          lines = std::max(lines, count);
        }
        size_t texts = 0;
        std::string report;
        const bool ok = game(r, texts, report);
        clean &= ok && texts == r.text.messages.size();
        textsTaken += texts;
        if (!ok && firstReport.empty()) firstReport = report;
        sameNumbers &= r.mod.tables.count("item_data_prime") == 1;
      }
      check(fitted > 1000 && lines <= 3, "TM descriptions, 25 seeds: " + std::to_string(fitted) + " TM texts rewritten for their new moves, none longer than 3 lines (longest line " +
                                             std::to_string(longest) + " characters; the disc's longest item line has 21); " + std::to_string(givenUp) +
                                             " left to the move's own text for lack of room");
      check(clean && sameNumbers && textsTaken > 1000, "and the data-mod layer puts every one of them into all the menus' message tables without a complaint" +
                                                         (firstReport.empty() ? "" : ": " + firstReport));
      const xr::Result plain = with(off, 3, {{"shops", 1}});
      check(plain.text.Empty() && plain.GameJson("x") == plain.mod.ToJson("x"), "without random TM moves: no text is touched (the game's data file is the numbers alone)");
    }
    {
      int good = 0, runs = 0;
      std::string firstError;
      for (xr::Style style : {xr::Style::Light, xr::Style::Classic, xr::Style::Chaos}) {
        for (uint32_t k = 0; k < 12; k++) {
          er::Settings s;
          xr::ApplyStyle(s, style);
          s.seed = 4000 + k * 13;
          s.Set(sc, "shops", k % 4);
          s.Set(sc, "tutor_moves", k % 3);
          s.Set(sc, "wild.scope", (k / 2) % 2);
          s.Set(sc, "wild.types", (k / 4) % 2);
          s.Set(sc, "wild_levels", k % 5);
          xr::Result r;
          runs++;
          size_t texts = 0;
          std::string report;
          if (xr::Generate(cat, read_disc_entry, s, r, error) && game(r, texts, report)) good++;
          else if (firstError.empty()) firstError = error + report;
        }
      }
      check(good == runs, "generator's own checks and the data-mod layer pass with the new switches on " + std::to_string(runs) + " runs" +
                              (firstError.empty() ? "" : ": " + firstError));
    }
    {
      er::Settings s3 = classic;
      s3.seed = 4242;
      s3.Set(sc, "trainer_levels", 2);
      const std::string code3 = er::EncodeCode(sc, s3);
      er::Settings back;
      bool seedOnly = true;
      check(code3.rfind("XDR3-", 0) == 0 && er::DecodeCode(sc, code3, back, seedOnly, error) && back.Get(sc, "shops") == 0 &&
                back.Get(sc, "tutor_moves") == 0 && back.Get(sc, "wild_levels") == 0 && er::EncodeCode(sc, back) == code3,
            "a version-3 code is still written as version 3 and read with the new switches off (" + code3 + ")");
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
  if (!r.text.Empty()) std::printf("%zu TM description(s) rewritten for the bag's box (game.json: what the game applies)\n", r.text.messages.size());
  std::printf("code: %s\n%zu numbers changed, written to %s (data/randomizer.json, spoiler.txt)\n", r.code.c_str(), r.mod.Count(), out.c_str());
  std::printf("%s the data-mod layer took %zu of them%s%s\n", ok ? "ok  " : "FAIL", changes, report.empty() ? "" : ":\n", report.c_str());
  return ok ? 0 : 1;
}
