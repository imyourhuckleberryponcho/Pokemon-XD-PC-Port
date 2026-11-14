#include "xd_gen4.h"
#include "xd_gen4_machines.h"

#include "data/common_rel.h"
#include "xd_user_folder.h"

#include <port_message.h>
#include <sdk_port.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

extern "C" {
struct Gen4PkxEntry {
  uint32_t a, b;
};
extern Gen4PkxEntry pokemon_pkx_data[];
extern uint32_t pokemon_pkx_data_number;
extern void* pokemon_nakigoe_data[];
extern uint32_t pokemon_nakigoe_data_number;
}

namespace {

constexpr int kPackVersion = 2;
constexpr uint32_t kPkxSpare = 256;
constexpr uint32_t kWaveOld = 389, kWaveSpare = 256;

bool sLoaded = false;
bool sSpots = false;

struct PkxRow {
  uint32_t row, group, res;
};
std::vector<PkxRow> sPkx;
std::vector<std::vector<uint8_t>> sWaves;
uint32_t sPkxExpect = 0;
struct RowSpec {
  std::string table;
  uint32_t row = 0, count = 1;
  int32_t copyOf = -1;
  std::vector<uint32_t> sets;
};
std::vector<RowSpec> sRows;
struct SpeciesRef {
  uint32_t dex = 0, id = 0;
  std::string key;
};
std::vector<SpeciesRef> sSpecies;
struct GoodsRow {
  int32_t copyOf = -1;
  std::string item;
};
std::vector<GoodsRow> sGoods;
std::string sEvoText;

struct ParsedTables {
  std::vector<RowSpec> rows;
};

std::string Norm(std::string k) {
  for (char& c : k) c = c == ' ' ? '_' : c;
  return k;
}

void Register() {
  std::vector<XDExtraRow> rows;
  rows.reserve(sRows.size());
  for (const RowSpec& r : sRows) rows.push_back({r.table.c_str(), r.row, r.count, r.copyOf, r.sets.data(), r.sets.size() / 3});
  PortXD_SetExtraRows(rows.data(), rows.size());
}

bool ReadText(const std::filesystem::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool ParseTables(const std::string& text, ParsedTables& out, std::string& error) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "format") {
      int v = 0;
      ls >> v;
      if (v != 1) {
        error = "tables.txt has format " + std::to_string(v) + ", this game reads 1";
        return false;
      }
    } else if (key == "pkx") {
      PkxRow r{};
      ls >> r.row >> r.group >> r.res;
      sPkx.push_back(r);
    } else if (key == "pkxcount") {
      ls >> sPkxExpect;
    } else if (key == "row") {
      RowSpec r;
      ls >> r.table >> r.row >> r.count >> r.copyOf;
      std::string s;
      while (ls >> s) {
        unsigned a = 0, b = 0, c = 0;
        if (std::sscanf(s.c_str(), "%u:%u:%u", &a, &b, &c) != 3) {
          error = "tables.txt: \"" + s + "\" is not offset:size:value";
          return false;
        }
        r.sets.push_back(a), r.sets.push_back(b), r.sets.push_back(c);
      }
      out.rows.push_back(std::move(r));
    } else if (key == "wave") {
      std::string hex;
      unsigned dex = 0;
      ls >> dex >> hex;
      std::vector<uint8_t> w;
      for (size_t k = 0; k + 1 < hex.size(); k += 2) w.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(k, 2).c_str(), nullptr, 16)));
      if (w.empty() || w.size() != static_cast<size_t>(w[0]) + 1) {
        error = "tables.txt: the wave of species " + std::to_string(dex) + " has the wrong length";
        return false;
      }
      sWaves.push_back(std::move(w));
    } else if (key == "face") {
    } else if (key == "goods") {
      GoodsRow g;
      ls >> g.item;
      sGoods.push_back(std::move(g));
    } else if (key == "goodscopy") {
      GoodsRow g;
      ls >> g.copyOf;
      sGoods.push_back(std::move(g));
    } else if (key == "species") {
      SpeciesRef r;
      ls >> r.dex >> r.id >> r.key;
      sSpecies.push_back(r);
    } else {
      error = "tables.txt: unknown line \"" + key + "\"";
      return false;
    }
  }
  return true;
}

bool Number(const std::string& s, uint32_t& out) {
  if (s.empty()) return false;
  const char* p = s.c_str() + (s[0] == '#' ? 1 : 0);
  char* end = nullptr;
  const unsigned long v = std::strtoul(p, &end, 10);
  if (end == p || *end != 0) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

void ResolveEvolutions(const engine::mods::LoadResult& result, const std::map<std::string, uint32_t>& species) {
  std::map<std::string, uint32_t> items;
  for (const auto& r : result.addedRows) {
    if (r.table == "item_data_prime") items[Norm(r.key)] = r.id;
  }
  auto find = [&](const std::string& key, bool isItem, uint32_t& id) {
    if (Number(key, id)) return true;
    const auto& m = isItem ? items : species;
    const auto it = m.find(Norm(key));
    if (it == m.end()) return false;
    id = it->second;
    return true;
  };
  std::vector<xd::gen4::ExtraEvolution> list;
  int32_t cord = -1;
  uint32_t eevee[4] = {0, 0, 0, 0};
  bool eeveeOk = false;
  int refused = 0;
  std::istringstream in(sEvoText);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "evo") {
      std::string sp, kind, item, to;
      ls >> sp >> kind >> item >> to;
      uint32_t s = 0, k = 0, i = 0, t = 0;
      if (!find(sp, false, s) || !Number(kind, k) || !find(to, false, t) || (item != "-" && !find(item, true, i))) {
        std::fprintf(stderr, "[gen4] evolution \"%s\" left out (a name has no row: its item or species is not in this run)\n", line.c_str());
        refused++;
        continue;
      }
      list.push_back({static_cast<uint16_t>(s), static_cast<uint8_t>(k), static_cast<uint16_t>(i), static_cast<uint16_t>(t)});
    } else if (key == "cord") {
      std::string item;
      ls >> item;
      uint32_t i = 0;
      if (find(item, true, i)) {
        cord = static_cast<int32_t>(i);
      } else {
        std::fprintf(stderr, "[gen4] the Linking Cord is not in this run (no item \"%s\"): trade evolutions by item stay out\n", item.c_str());
      }
    } else if (key == "eevee") {
      std::string a, b, c, d;
      ls >> a >> b >> c >> d;
      eeveeOk = find(a, false, eevee[0]) && find(b, true, eevee[1]) && find(c, false, eevee[2]) && find(d, true, eevee[3]);
      if (!eeveeOk) std::fprintf(stderr, "[gen4] the Eevee offer keeps its five choices (\"%s\": a name has no row in this run)\n", line.c_str());
    }
  }
  xd::gen4::SetExtraEvolutions(list, cord);
  xd::gen4::SetEeveeChoices(eeveeOk, eevee[0], eevee[1], eevee[2], eevee[3]);
  std::fprintf(stderr, "[gen4] evolutions: %zu in the PC-only list (%d left out), Linking Cord %s, Eevee offer %s\n", list.size(), refused,
               cord >= 0 ? ("item " + std::to_string(cord)).c_str() : "not in this run", eeveeOk ? "7 choices" : "5 choices");
}

}

namespace xd::gen4 {

std::filesystem::path PackFolder() { return xd::SettingsFolder() / L"extra_assets" / L"pbr_gen4"; }

bool PackPresent(std::string* why) {
  std::string text;
  std::error_code ec;
  const std::filesystem::path info = PackFolder() / L"gen4.json";
  if (!std::filesystem::exists(info, ec) || !ReadText(info, text)) {
    if (why) *why = "the Gen 4 files are not made yet (no " + PackFolder().string() + ")";
    return false;
  }
  const size_t at = text.find("\"version\"");
  const int version = at == std::string::npos ? 0 : std::atoi(text.c_str() + text.find(':', at) + 1);
  if (version != kPackVersion) {
    if (why) *why = "the Gen 4 files were made by another version (" + std::to_string(version) + ", this game needs " +
                    std::to_string(kPackVersion) + "): make them again";
    return false;
  }
  return true;
}

bool Loaded() { return sLoaded; }
bool SpotsLoaded() { return sLoaded && sSpots; }

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles) {
  sLoaded = false;
  sSpots = false;
  sPkx.clear();
  sWaves.clear();
  sRows.clear();
  sSpecies.clear();
  sGoods.clear();
  machines::Clear();
  sEvoText.clear();
  xd::gen4::SetExtraEvolutions({}, -1);
  xd::gen4::SetEeveeChoices(false, 0, 0, 0, 0);
  if (!wanted) return;
  std::string why;
  if (!PackPresent(&why)) {
    std::fprintf(stderr, "[gen4] Gen 4 rules are on, but %s: the game starts without the Gen 4 Pokemon\n", why.c_str());
    PortMessage_Show(PORT_MESSAGE_WARNING, "Pokemon XD - Gen 4",
                     ("The Gen 4 Pokemon can't be used: " + why +
                      ".\n\nThey are made from your own Pokemon Battle Revolution disc on the launcher's \"Extra assets\" page.\n"
                      "The game starts without them.")
                         .c_str());
    return;
  }
  const std::filesystem::path pack = PackFolder();
  ParsedTables tables;
  std::string text, error;
  if (!ReadText(pack / L"tables.txt", text) || !ParseTables(text, tables, error)) {
    std::fprintf(stderr, "[gen4] the pack's tables.txt can't be used (%s): the game starts without the Gen 4 Pokemon\n",
                 error.empty() ? "unreadable" : error.c_str());
    return;
  }
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(pack / L"data", ec)) {
    if (e.path().extension() == ".json") files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    engine::mods::DataModFile d;
    d.mod = "gen4";
    d.builtIn = true;
    d.modName = "Gen 4 Pokemon and moves";
    d.label = "gen4/" + f.filename().string();
    if (!ReadText(f, d.text)) {
      std::fprintf(stderr, "[gen4] could not read %s: the game starts without the Gen 4 Pokemon\n", f.string().c_str());
      return;
    }
    dataFiles.push_back(std::move(d));
  }
  if (spots) {
    engine::mods::DataModFile d;
    d.mod = "gen4";
    d.builtIn = true;
    d.modName = "Gen 4 Pokemon and moves";
    d.label = "gen4/optional/spots.json";
    std::string spotText;
    ParsedTables spotTables;
    if (ReadText(pack / L"optional" / L"spots.json", d.text) && ReadText(pack / L"optional" / L"spots_tables.txt", spotText) &&
        ParseTables(spotText, spotTables, error)) {
      for (auto& r : spotTables.rows) tables.rows.push_back(std::move(r));
      dataFiles.push_back(std::move(d));
      sSpots = true;
    } else {
      std::fprintf(stderr, "[gen4] the Poke Spot set is not in the pack: the spots keep their own Pokemon\n");
    }
  }
  machines::Load(pack);
  ReadText(pack / L"evolutions.txt", sEvoText);
  options.extraLayers.push_back({"pbr_gen4", "Gen 4 Pokemon", pack});
  sRows = tables.rows;
  Register();
  sLoaded = true;
  std::fprintf(stderr, "[gen4] pack %s: %zu data file(s), %zu table row group(s), %zu model rows\n", pack.string().c_str(),
               dataFiles.size(), sRows.size(), sPkx.size());
}

void AfterLoad(const engine::mods::LoadResult& result) {
  if (!sLoaded) return;
  machines::Resolve(result);
  std::map<std::string, uint32_t> ids;
  for (const auto& r : result.addedRows) {
    if (r.table == "pokemon_data") ids[Norm(r.key)] = r.id;
  }
  std::vector<uint32_t> all, legend;
  for (SpeciesRef& s : sSpecies) {
    uint32_t id = 0;
    if (s.key[0] == '#') {
      id = static_cast<uint32_t>(std::atoi(s.key.c_str() + 1));
    } else if (auto it = ids.find(s.key); it != ids.end()) {
      id = it->second;
    } else {
      std::fprintf(stderr, "[gen4] the species \"%s\" got no number (its data was refused: see the mods messages)\n", s.key.c_str());
      continue;
    }
    s.id = id;
    all.push_back(id);
    static const uint32_t kLegend[] = {480, 481, 482, 483, 484, 485, 486, 487, 488, 490, 491, 492, 493};
    if (std::find(std::begin(kLegend), std::end(kLegend), s.dex) != std::end(kLegend)) legend.push_back(id);
  }
  std::map<std::string, uint32_t> itemIds;
  for (const auto& r : result.addedRows) {
    if (r.table == "item_data_prime") itemIds[Norm(r.key)] = r.id;
  }
  size_t goodsMissing = 0;
  for (const GoodsRow& g : sGoods) {
    if (g.copyOf >= 0) {
      sRows.push_back({"shopGoods", 0xFFFFFFFFu, 1, g.copyOf, {}});
      continue;
    }
    uint32_t id = 0;
    if (!g.item.empty() && std::isdigit(static_cast<unsigned char>(g.item[0]))) {
      id = static_cast<uint32_t>(std::atoi(g.item.c_str()));
    } else if (auto it = itemIds.find(g.item); it != itemIds.end()) {
      id = it->second;
    } else {
      goodsMissing++;
    }
    sRows.push_back({"shopGoods", 0xFFFFFFFFu, 1, -1, {0, 2, id}});
  }
  if (goodsMissing != 0) std::fprintf(stderr, "[gen4] %zu shop item(s) got no number (their data was refused: see the mods messages)\n", goodsMissing);
  ResolveEvolutions(result, ids);
  for (uint32_t id : legend) sRows.push_back({"legendPokemonData", 0xFFFFFFFFu, 1, -1, {0, 2, id}});
  for (uint32_t id : all) sRows.push_back({"pdapokemonsortdata", 0xFFFFFFFFu, 1, -1, {0, 2, id}});
  Register();
  std::fprintf(stderr, "[gen4] %zu species numbered (%zu legendary), added to the PDA's sort list\n", all.size(), legend.size());
}

void ApplyWaves() {
  if (sWaves.empty()) return;
  if (pokemon_nakigoe_data_number != kWaveOld || sWaves.size() > kWaveSpare) {
    std::fprintf(stderr, "[gen4] the PDA's cry waveforms don't fit the game's table (%u rows, %zu waves): left out\n",
                 pokemon_nakigoe_data_number, sWaves.size());
    return;
  }
  for (size_t k = 0; k < sWaves.size(); k++) {
    auto* p = static_cast<uint8_t*>(PortMem_LowAlloc(sWaves[k].size(), 16));
    std::copy(sWaves[k].begin(), sWaves[k].end(), p);
    pokemon_nakigoe_data[kWaveOld + k] = p;
  }
  pokemon_nakigoe_data_number = kWaveOld + static_cast<uint32_t>(sWaves.size());
  std::fprintf(stderr, "[gen4] pokemon_nakigoe_data grown: %u -> %u rows\n", kWaveOld, pokemon_nakigoe_data_number);
}

void ApplyMainTables() {
  if (!sLoaded) return;
  ApplyWaves();
  if (sPkx.empty()) return;
  const uint32_t old = pokemon_pkx_data_number;
  uint32_t last = old;
  for (const PkxRow& r : sPkx) {
    if (r.row < old || r.row >= old + kPkxSpare || (sPkxExpect != 0 && sPkxExpect != old)) {
      std::fprintf(stderr, "[gen4] the model row %u doesn't fit the game's model table (%u rows, room for %u more): left out\n", r.row,
                   old, kPkxSpare);
      continue;
    }
    pokemon_pkx_data[r.row].a = r.group;
    pokemon_pkx_data[r.row].b = r.res;
    last = std::max(last, r.row + 1);
  }
  pokemon_pkx_data_number = last;
  std::fprintf(stderr, "[gen4] pokemon_pkx_data grown: %u -> %u rows\n", old, last);
}

}

extern "C" void PortXD_Gen4MainTables(void) { xd::gen4::ApplyMainTables(); }
