#include "xd_spare.h"
#include "i18n.h"
#include "xd_gen4.h"
#include "xd_challenges.h"
#include "xd_gen4_abilities.h"
#include "xd_gen4_machines.h"
#include "xd_gen4_xg.h"
#include "xd_gen4_abilities.h"
#include "xd_xg.h"
#include "xd_gen57.h"

#include "pack_tables.h"
#include "data/common_rel.h"
#include "xd_user_folder.h"

#include <port_message.h>
#include <sdk_port.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

namespace {
using engine::i18n::Tr;
std::string TrFmt(const char* format, ...) {
  va_list args, again;
  va_start(args, format);
  va_copy(again, args);
  const int n = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) std::vsnprintf(out.data(), out.size() + 1, format, again);
  va_end(again);
  return out;
}
}

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
constexpr uint32_t kPkxSpare = XD_PC_SPARE_SPECIES;
constexpr uint32_t kWaveOld = 389, kWaveSpare = XD_PC_SPARE_SPECIES;

bool sLoaded = false;
bool sSpots = false;

using xd::pack::PkxRow;
using xd::pack::RowSpec;
using xd::pack::SpeciesRef;
using xd::pack::GoodsRow;
using xd::pack::PackTables;
using xd::pack::ReadText;
std::vector<PkxRow> sPkx;
std::vector<std::vector<uint8_t>> sWaves;
uint32_t sPkxExpect = 0;
std::vector<RowSpec> sRows;
std::vector<SpeciesRef> sSpecies;
std::vector<GoodsRow> sGoods;
std::string sEvoText;

std::string Norm(std::string k) {
  for (char& c : k) c = c == ' ' ? '_' : c;
  return k;
}

std::vector<RowSpec> sMoreRows;

void Register() {
  std::vector<XDExtraRow> rows;
  rows.reserve(sRows.size() + sMoreRows.size());
  for (const RowSpec& r : sRows) rows.push_back({r.table.c_str(), r.row, r.count, r.copyOf, r.sets.data(), r.sets.size() / 3});
  for (const RowSpec& r : sMoreRows) rows.push_back({r.table.c_str(), r.row, r.count, r.copyOf, r.sets.data(), r.sets.size() / 3});
  PortXD_SetExtraRows(rows.data(), rows.size());
}

void Absorb(PackTables& t) {
  for (auto& p : t.pkx) sPkx.push_back(p);
  if (t.pkxExpect != 0) sPkxExpect = t.pkxExpect;
  for (auto& w : t.waves) sWaves.push_back(std::move(w));
  for (auto& r : t.species) sSpecies.push_back(r);
  for (auto& g : t.goods) sGoods.push_back(std::move(g));
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
  if (xd::xg::On()) eeveeOk = false;
  xd::gen4::SetEeveeChoices(eeveeOk, eevee[0], eevee[1], eevee[2], eevee[3]);
  std::fprintf(stderr, "[gen4] evolutions: %zu in the PC-only list (%d left out), Linking Cord %s, Eevee offer %s\n", list.size(), refused,
               cord >= 0 ? ("item " + std::to_string(cord)).c_str() : "not in this run", eeveeOk ? "7 choices" : "5 choices");
}

}

namespace xd::gen4 {

std::filesystem::path PackFolder() {
  const std::filesystem::path base = xd::SettingsFolder() / L"extra_assets";
  std::error_code ec;
  if (!std::filesystem::exists(base / L"pbr_gen4" / L"gen4.json", ec) && std::filesystem::exists(base / L"usum_gen4" / L"gen4.json", ec)) return base / L"usum_gen4";
  return base / L"pbr_gen4";
}

bool PackPresent(std::string* why) {
  std::string text;
  std::error_code ec;
  const std::filesystem::path info = PackFolder() / L"gen4.json";
  if (!std::filesystem::exists(info, ec) || !ReadText(info, text)) {
    if (why) *why = TrFmt(Tr("the Gen 4 files are not made yet (no %s)"), PackFolder().string().c_str());
    return false;
  }
  const size_t at = text.find("\"version\"");
  const int version = at == std::string::npos ? 0 : std::atoi(text.c_str() + text.find(':', at) + 1);
  if (version != kPackVersion) {
    if (why) *why = TrFmt(Tr("the Gen 4 files were made by another version (%d, this game needs %d): make them again"), version,
                          static_cast<int>(kPackVersion));
    return false;
  }
  return true;
}

bool Loaded() { return sLoaded; }
uint32_t ExtraRowCount(const char* table) {
  uint32_t n = 0;
  if (sLoaded) {
    for (const RowSpec& r : sRows) n += r.table == table ? r.count : 0;
  }
  return n;
}
bool SpotsLoaded() { return sLoaded && sSpots; }
void SetMoreRows(const std::vector<xd::pack::RowSpec>& rows) {
  sMoreRows = rows;
  Register();
}
uint32_t ModelRowsTaken() {
  uint32_t last = 0;
  if (sLoaded) {
    for (const PkxRow& r : sPkx) last = std::max(last, r.row + 1);
  }
  return last > 419 ? last - 419 : 0;
}
uint32_t WaveRowsTaken() { return sLoaded ? static_cast<uint32_t>(sWaves.size()) : 0; }

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles) {
  sLoaded = false;
  sSpots = false;
  sPkx.clear();
  sWaves.clear();
  sRows.clear();
  sMoreRows.clear();
  sSpecies.clear();
  sGoods.clear();
  machines::Clear();
  sEvoText.clear();
  xd::gen4::SetExtraEvolutions({}, -1);
  xd::gen4::SetEeveeChoices(false, 0, 0, 0, 0);
  if (!wanted) return;
  std::string why;
  if (!PackPresent(&why)) {
    std::fprintf(stderr, "[gen4] Gen 4 Pokemon are on, but %s: the game starts without the Gen 4 Pokemon\n", why.c_str());
    PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - Gen 4"),
                     TrFmt(Tr("The Gen 4 Pokemon can't be used: %s.\n\nThey are made from your own Pokemon Battle Revolution disc on "
                              "the launcher's \"Extra assets\" page.\nThe game starts without them."),
                           why.c_str())
                         .c_str());
    return;
  }
  const std::filesystem::path pack = PackFolder();
  PackTables tables;
  std::string text, error;
  if (!ReadText(pack / L"tables.txt", text) || !xd::pack::ParseTables(text, tables, error)) {
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
  if (xd::xg::On()) spots = false;
  if (spots) {
    engine::mods::DataModFile d;
    d.mod = "gen4";
    d.builtIn = true;
    d.modName = "Gen 4 Pokemon and moves";
    d.label = "gen4/optional/spots.json";
    std::string spotText;
    PackTables spotTables;
    if (ReadText(pack / L"optional" / L"spots.json", d.text) && ReadText(pack / L"optional" / L"spots_tables.txt", spotText) &&
        xd::pack::ParseTables(spotText, spotTables, error)) {
      for (auto& r : spotTables.rows) tables.rows.push_back(std::move(r));
      dataFiles.push_back(std::move(d));
      sSpots = true;
    } else {
      std::fprintf(stderr, "[gen4] the Poke Spot set is not in the pack: the spots keep their own Pokemon\n");
    }
  }
  if (!xd::xg::On()) machines::Load(pack);
  if (xd::xg::On()) sGoods.clear();
  ReadText(pack / L"evolutions.txt", sEvoText);
  options.extraLayers.push_back({"pbr_gen4", "Gen 4 Pokemon", pack});
  Absorb(tables);
  sRows = std::move(tables.rows);
  Register();
  sLoaded = true;
  std::fprintf(stderr, "[gen4] pack %s: %zu data file(s), %zu table row group(s), %zu model rows\n", pack.string().c_str(),
               dataFiles.size(), sRows.size(), sPkx.size());
}

bool TranslateForXG(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, std::vector<engine::mods::DataModFile>& files,
                    std::string& error) {
  static std::unique_ptr<xg::Translator> translator;
  xg::SetActive(nullptr);
  xg::Tables tables;
  if (!xg::BuildTables(catalog, read, tables, error)) return false;
  tables.packAbilities.assign(124, "");
  for (uint32_t id = 77; id <= 123; id++) {
    if (const char* n = xd::gen4::Gen4AbilityName(id)) tables.packAbilities[id] = n;
  }
  translator = std::make_unique<xg::Translator>(std::move(tables));
  std::string machines;
  ReadText(PackFolder() / L"machines.txt", machines);
  if (!translator->Run(files, machines, error)) return false;
  if (const char* dump = std::getenv("XD_GEN4_XG_DUMP")) {
    std::error_code ec;
    std::filesystem::create_directories(dump, ec);
    for (const engine::mods::DataModFile& f : files) {
      std::ofstream out(std::filesystem::path(dump) / std::filesystem::path(f.label).filename(), std::ios::binary);
      out << f.text;
    }
  }
  for (const std::string& line : translator->Report()) std::fprintf(stderr, "[gen4 xg] %s\n", line.c_str());
  std::fprintf(stderr, "[gen4 xg] %zu new move row(s) on XG, the rest of the Gen 4 moves use XG's own moves or a stand-in\n", translator->AddedMoves().size());
  xg::SetActive(translator.get());
  return true;
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

namespace {
struct StandIn {
  const char* species;
  uint8_t ability;
};
constexpr StandIn kStandIns[] = {
    {"Bidoof", 12}, {"Bibarel", 12},
    {"Cranidos", 69}, {"Rampardos", 69},
    {"Combee", 55},
    {"Cherrim", 34}, {"Leafeon", 34},
    {"Croagunk", 11}, {"Toxicroak", 11},
    {"Electivire", 72},
    {"Snover", 43}, {"Abomasnow", 43},
    {"Gallade", 39},
    {"Porygon-Z", 36},
    {"Phione", 44}, {"Manaphy", 44},
};
constexpr uint32_t kTokuseiOff = 0x32;
std::map<uint32_t, std::pair<uint8_t, uint8_t>> sOrigAbilities;
uint8_t* sSpeciesRows = nullptr;
uint32_t sSpeciesCount = 0;
constexpr uint32_t kRowSize = 0x124;

bool SameName(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size() && b[i] != 0; i++) {
    const char x = a[i] == '_' ? ' ' : a[i], y = b[i];
    if (std::tolower(static_cast<unsigned char>(x)) != std::tolower(static_cast<unsigned char>(y))) return false;
  }
  return i == a.size() && b[i] == 0;
}
}

namespace xd::gen4 {

std::pair<uint8_t, uint8_t> RulesOffAbilities(const std::string& species, uint8_t a, uint8_t b) {
  const bool g4a = a >= 77, g4b = b >= 77;
  if (!g4a && !g4b) return {a, b};
  const uint8_t gen3 = (a >= 1 && a <= 76) ? a : (b >= 1 && b <= 76) ? b : 0;
  if (gen3 != 0) return {gen3, gen3};
  uint8_t pick = 0;
  for (const StandIn& s : kStandIns) {
    if (SameName(species, s.species)) pick = s.ability;
  }
  return {pick, pick};
}

void SyncSpeciesAbilities() {
  if (xd::xg::On()) return;
  if (!sLoaded || sSpeciesRows == nullptr) return;
  const bool rules = xd::challenges::Active("gen4_rules");
  int changed = 0;
  for (const SpeciesRef& s : sSpecies) {
    if (s.id == 0) continue;
    if (s.id >= sSpeciesCount) continue;
    uint8_t* row = sSpeciesRows + static_cast<size_t>(s.id) * kRowSize;
    auto it = sOrigAbilities.find(s.id);
    if (it == sOrigAbilities.end()) it = sOrigAbilities.emplace(s.id, std::make_pair(row[kTokuseiOff], row[kTokuseiOff + 1])).first;
    const auto want = rules ? it->second : RulesOffAbilities(s.key, it->second.first, it->second.second);
    if (row[kTokuseiOff] != want.first || row[kTokuseiOff + 1] != want.second) changed++;
    row[kTokuseiOff] = want.first;
    row[kTokuseiOff + 1] = want.second;
  }
  static int last = -1;
  if (changed != 0 || last < 0) {
    std::fprintf(stderr, "[gen4] species abilities for %s: %d species row(s) changed\n",
                 rules ? "the Gen 4 rules (the pack's own)" : "the XD rules (Gen 3 abilities, stand-ins, none)", changed);
    last = changed;
  }
}

}

static void SpeciesRows(void* rows, uint32_t count) {
  sSpeciesRows = static_cast<uint8_t*>(rows);
  sSpeciesCount = count;
  sOrigAbilities.clear();
  xd::gen4::SyncSpeciesAbilities();
}

extern "C" void PortXD_SetSpeciesRowsHook(void (*f)(void*, uint32_t));
namespace { const bool sHooked = (PortXD_SetSpeciesRowsHook(&SpeciesRows), true); }

extern "C" void PortXD_Gen4MainTables(void) {
  xd::gen4::ApplyMainTables();
  xd::gen57::ApplyMainTables();
}
