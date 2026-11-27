#include "xd_gen57.h"
#include "xd_gen_rules.h"
#include "xd_move_targets.h"
#include "xd_spare.h"

#include "xd_forms.h"

#include "i18n.h"
#include "pack_match.h"
#include "pack_tables.h"
#include "xd_gen4.h"
#include "xd_gen4_xg.h"
#include "../../../engine/mods/json.h"
#include "xd_languages.h"
#include "xd_user_folder.h"
#include "xd_xg.h"

#include "data/common_rel.h"

#include <port_message.h>
#include <sdk_port.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

extern "C" {
struct Gen57PkxEntry {
  uint32_t a, b;
};
extern Gen57PkxEntry pokemon_pkx_data[];
extern uint32_t pokemon_pkx_data_number;
extern void* pokemon_nakigoe_data[];
extern uint32_t pokemon_nakigoe_data_number;
extern uint8_t pokemon_tokusei_data[];
extern uint32_t pokemon_tokusei_data_number;
}

namespace {
using engine::i18n::Tr;
using xd::pack::ReadText;
std::string Fmt(const char* format, ...) {
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

constexpr int kPackVersion = 1;
constexpr uint32_t kPkxSpare = XD_PC_SPARE_SPECIES;
constexpr uint32_t kPkxDisc = 419;
constexpr uint32_t kWaveDisc = 389;
constexpr uint32_t kGroup = 0x7D0;
constexpr uint32_t kFirstText = 0x8000;
constexpr uint32_t kNoEffectText = 0x9100;
constexpr uint32_t kDoublesText = 0x9102;
constexpr uint32_t kNotYetText = 0x9101;

bool sLoaded = false;
bool sSpots = false;
bool sRowsInstalled = false;
bool sOwnCries = false;
uint32_t sReserved = 0, sPadSndFile = 0, sPadSndData = 0, sPadWaves = 0;
xd::pack::PackTables sTables;
std::vector<xd::pack::RowSpec> sRowsAll;

struct AbilityRow {
  uint32_t id = 0;
  std::string name[6], text[6];
};
std::vector<AbilityRow> sAbilities;

std::set<uint32_t> sEffectIds;
bool Implemented(uint32_t id) { return sEffectIds.count(id) != 0; }

void ReadEffectIds(const std::filesystem::path& pack);

int LanguageColumn() {
  const char* id = xd::languages::Active() ? xd::languages::Active()->id : "us";
  static const char* kIds[] = {"us", "ge", "fr", "it", "sp", "jp"};
  for (int i = 0; i < 6; i++) {
    if (std::strcmp(id, kIds[i]) == 0) return i;
  }
  return 0;
}

void ParseAbilities(const std::string& text) {
  sAbilities.clear();
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#' || line.rfind("format", 0) == 0) continue;
    std::vector<std::string> cols;
    for (size_t at = 0;;) {
      const size_t t = line.find('\t', at);
      cols.push_back(line.substr(at, t == std::string::npos ? t : t - at));
      if (t == std::string::npos) break;
      at = t + 1;
    }
    if (cols.size() < 3) continue;
    AbilityRow a;
    a.id = static_cast<uint32_t>(std::atoi(cols[0].c_str()));
    if (a.id < 124 || a.id > 255) continue;
    for (size_t l = 0; l < 6; l++) {
      a.name[l] = 1 + 2 * l < cols.size() ? cols[1 + 2 * l] : cols[1];
      a.text[l] = 2 + 2 * l < cols.size() ? cols[2 + 2 * l] : cols[2];
      if (a.name[l].empty()) a.name[l] = cols[1];
      if (a.text[l].empty()) a.text[l] = cols[2];
    }
    sAbilities.push_back(std::move(a));
  }
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

std::string NormKey(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}
bool NumberKey(const std::string& s, uint32_t& out) {
  if (s.empty()) return false;
  const char* p = s.c_str() + (s[0] == '#' ? 1 : 0);
  char* end = nullptr;
  const unsigned long v = std::strtoul(p, &end, 10);
  if (end == p || *end != 0) return false;
  out = static_cast<uint32_t>(v);
  return true;
}
uint32_t sPrismScale = 0;
std::vector<uint16_t> sEvoShop;

void ReadEvolutions(const engine::mods::LoadResult& result) {
  sEvoShop.clear();
  std::string text;
  if (!ReadText(xd::gen57::PackFolder() / L"evolutions.txt", text)) return;
  std::map<std::string, uint32_t> species, items;
  for (const auto& r : result.addedRows) {
    if (r.mod == "gen57" && r.table == "pokemon_data") species[NormKey(r.key)] = r.id;
    if (r.table == "item_data_prime") items[NormKey(r.key)] = r.id;
  }
  auto find = [&](const std::string& key, bool isItem, uint32_t& id) {
    if (NumberKey(key, id)) return true;
    const auto& m = isItem ? items : species;
    const auto it = m.find(NormKey(key));
    if (it == m.end()) return false;
    id = it->second;
    return true;
  };
  size_t added = 0, refused = 0;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "evo") {
      std::string sp, kind, param, to, fallback;
      ls >> sp >> kind >> param >> to >> fallback;
      uint32_t s = 0, k = 0, p = 0, t = 0;
      const bool itemKind = kind == "6" || kind == "7";
      if (!find(sp, false, s) || !NumberKey(kind, k) || !find(to, false, t) || !find(param, itemKind, p)) {
        std::fprintf(stderr, "[gen57] evolution \"%s\" left out (a name has no row in this run)\n", line.c_str());
        refused++;
        continue;
      }
      if (k == 22 && xd::xg::On() && xd::gen4::xg::Active() != nullptr) {
        uint32_t level[20] = {1}, move[20] = {p};
        xd::gen4::xg::Active()->MapLearnset(level, move);
        uint32_t fb = 0;
        if (move[0] != 0) {
          p = move[0];
        } else if (NumberKey(fallback, fb) && fb > 0) {
          k = 23;
          p = fb;
          std::fprintf(stderr, "[gen57] \"%s\": XG has no such move, the evolution is a level %u one here\n", line.c_str(), fb);
        }
      }
      xd::gen4::AddExtraEvolution({static_cast<uint16_t>(s), static_cast<uint8_t>(k), static_cast<uint16_t>(p), static_cast<uint16_t>(t)});
      added++;
    } else if (key == "shop") {
      std::string item;
      ls >> item;
      uint32_t i = 0;
      if (find(item, true, i)) sEvoShop.push_back(static_cast<uint16_t>(i));
      else std::fprintf(stderr, "[gen57] shop item \"%s\" is not in this run\n", item.c_str());
    }
  }
  std::string shop;
  for (uint16_t i : sEvoShop) shop += " " + std::to_string(i);
  std::fprintf(stderr, "[gen57] evolutions.txt: %zu in the list (%zu left out), %zu items for the prize desk:%s\n", added, refused, sEvoShop.size(), shop.c_str());
}

struct SpotEntry { int nat; const char* form; };
struct SpotList {
  const char* table;
  SpotEntry gen[3][5];
  SpotEntry alola[3];
  int alolas;
};
const SpotList kSpotLists[3] = {
    {"esabapokemondata_E1",
     {{{524, nullptr}, {529, nullptr}, {551, nullptr}, {610, nullptr}, {498, nullptr}},
      {{659, nullptr}, {667, nullptr}, {688, nullptr}, {696, nullptr}, {653, nullptr}},
      {{744, nullptr}, {749, nullptr}, {757, nullptr}, {782, nullptr}, {725, nullptr}}},
     {{0, "Geodude (Alola)"}, {0, "Diglett (Alola)"}, {0, "Sandshrew (Alola)"}},
     3},
    {"esabapokemondata_E2",
     {{{540, nullptr}, {548, nullptr}, {506, nullptr}, {636, nullptr}, {495, nullptr}},
      {{664, nullptr}, {661, nullptr}, {669, nullptr}, {705, nullptr}, {650, nullptr}},
      {{742, nullptr}, {753, nullptr}, {751, nullptr}, {759, nullptr}, {722, nullptr}}},
     {{0, "Vulpix (Alola)"}, {0, "Meowth (Alola)"}, {0, nullptr}},
     2},
    {"esabapokemondata_E3",
     {{{527, nullptr}, {532, nullptr}, {595, nullptr}, {633, nullptr}, {501, nullptr}},
      {{679, nullptr}, {708, nullptr}, {703, nullptr}, {714, nullptr}, {656, nullptr}},
      {{739, nullptr}, {755, nullptr}, {767, nullptr}, {778, nullptr}, {728, nullptr}}},
     {{0, "Rattata (Alola)"}, {0, "Grimer (Alola)"}, {0, nullptr}},
     2},
};

struct SpotPlan {
  bool on = false;
  int maxGen = 7;
  bool alola = false;
  int xd[3] = {0, 0, 0};
  int gen4[5] = {0, 0, 0, 0, 0};
  int later[3][5] = {};
  uint32_t top[3] = {0, 0, 0}, rate[3][3] = {};
};
SpotPlan sSpotPlan;
int sSpotsMaxGen = 7;

void Split5(int share, int out[5]) {
  static const int k25[5] = {6, 6, 6, 4, 3}, k20[5] = {5, 5, 5, 3, 2}, k15[5] = {4, 4, 3, 2, 2};
  const int* p = share == 25 ? k25 : share == 20 ? k20 : k15;
  for (int i = 0; i < 5; i++) out[i] = p[i];
}
void SplitXd(int share, int out[3]) {
  static const int k50[3] = {27, 16, 7}, k40[3] = {22, 13, 5}, k30[3] = {16, 10, 4};
  const int* p = share == 50 ? k50 : share == 40 ? k40 : k30;
  for (int i = 0; i < 3; i++) out[i] = p[i];
}

bool PackHasSpotForms(const std::filesystem::path& pack) {
  std::string text;
  if (!ReadText(pack / L"forms.txt", text)) return false;
  for (const SpotList& l : kSpotLists) {
    for (int i = 0; i < l.alolas; i++) {
      if (text.find(std::string("form ") + l.alola[i].form + "|") != std::string::npos) return true;
    }
  }
  return false;
}

bool PlanSpots(const std::filesystem::path& pack, std::string& dataText) {
  SpotPlan p;
  p.maxGen = std::max(5, std::min(7, sSpotsMaxGen));
  p.alola = p.maxGen >= 7 && PackHasSpotForms(pack);
  const int gens = p.maxGen - 3;
  const int share = p.alola ? 15 : (p.maxGen == 5 ? 25 : p.maxGen == 6 ? 20 : 15);
  const int xdShare = 100 - gens * share - (p.alola ? 10 : 0);
  for (int t = 0; t < 3; t++) {
    for (int i = 0; i < 3; i++) {
      if (!xd::gen4::SpotRowValues(kSpotLists[t].table, 3 + i, p.top[t], p.rate[t][i])) return false;
    }
  }
  SplitXd(xdShare, p.xd);
  Split5(share, p.gen4);
  for (int g = 0; g < p.maxGen - 4; g++) Split5(share, p.later[g]);
  p.on = true;
  std::string json = "{\n \"_note\": \"Poke Spot odds with the Gen 4 and Gen 5-7 sets (made by the game, gen57_pack.cpp)\"";
  for (int t = 0; t < 3; t++) {
    json += std::string(",\n \"") + kSpotLists[t].table + "\": {";
    for (int i = 0; i < 3; i++) {
      json += std::string(i ? "," : "") + "\n  \"#" + std::to_string(i) + "\": {\"prob, rate\": [" + std::to_string(p.xd[i]) + ", " + std::to_string(p.rate[t][i]) + "]}";
    }
    json += "\n }";
    for (int k = 0; k < 5; k++) {
      uint32_t top = 0, rate = 0;
      xd::gen4::SpotRowValues(kSpotLists[t].table, 3 + k, top, rate);
      xd::gen4::SetSpotRow(kSpotLists[t].table, 3 + k, static_cast<uint32_t>(p.gen4[k]), rate);
    }
  }
  json += "\n}\n";
  dataText = json;
  std::fprintf(stderr, "[gen57] Poke Spots: Gen 5-%d%s; the disc's three %d%%, Gen 4 %d%%, %d later generation(s) %d%% each%s\n", p.maxGen, p.alola ? " with the Alolan forms" : "",
               xdShare, share, p.maxGen - 4, share, p.alola ? ", Alolan 10%" : "");
  sSpotPlan = p;
  return true;
}

void AppendSpotRows(const engine::mods::LoadResult& result) {
  SpotPlan& p = sSpotPlan;
  if (!p.on) return;
  std::map<std::string, uint32_t> byName;
  for (const auto& r : result.addedRows) {
    if (r.mod == "gen57" && r.table == "pokemon_data") byName[NormKey(r.key)] = r.id;
  }
  std::map<int, std::string> nameOf;
  for (const auto& s : sTables.species) nameOf[static_cast<int>(s.dex)] = s.key;
  std::map<std::string, uint32_t> formRow;
  for (const auto& f : xd::forms::All()) formRow[f.key] = f.row;
  const auto find = [&](const SpotEntry& e) -> uint32_t {
    if (e.form != nullptr) {
      const auto it = formRow.find(e.form);
      return it == formRow.end() ? 0 : it->second;
    }
    const auto n = nameOf.find(e.nat);
    if (n == nameOf.end()) return 0;
    const auto it = byName.find(NormKey(n->second));
    return it == byName.end() ? 0 : it->second;
  };
  for (int t = 0; t < 3; t++) {
    const SpotList& list = kSpotLists[t];
    struct Row { uint32_t id; int prob; int rateOf; };
    std::vector<Row> rows;
    int toGen4 = 0;
    const auto block = [&](const SpotEntry* entries, const int* probs, int n, const char* what) {
      std::vector<Row> mine;
      int left = 0;
      for (int i = 0; i < n; i++) {
        const uint32_t id = find(entries[i]);
        if (id == 0) {
          std::fprintf(stderr, "[gen57] Poke Spots: %s entry %d of %s is not in this run (%s %d): its share goes to the block's first\n", what, i + 1, list.table,
                       entries[i].form ? entries[i].form : "species", entries[i].nat);
          left += probs[i];
          continue;
        }
        mine.push_back({id, probs[i], i < 3 ? i : 2});
      }
      if (left != 0) {
        if (mine.empty()) toGen4 += left;
        else mine[0].prob += left;
      }
      for (const Row& r : mine) rows.push_back(r);
    };
    for (int g = 0; g < p.maxGen - 4; g++) block(list.gen[g], p.later[g], 5, g == 0 ? "Gen 5" : g == 1 ? "Gen 6" : "Gen 7");
    if (p.alola) {
      int probs[3] = {0, 0, 0};
      if (list.alolas == 3) { probs[0] = 4; probs[1] = 3; probs[2] = 3; } else { probs[0] = 5; probs[1] = 5; }
      block(list.alola, probs, list.alolas, "Alolan");
    }
    if (toGen4 != 0) {
      uint32_t top = 0, rate = 0;
      xd::gen4::SpotRowValues(list.table, 3, top, rate);
      xd::gen4::SetSpotRow(list.table, 3, static_cast<uint32_t>(p.gen4[0] + toGen4), rate);
    }
    std::string log;
    for (const Row& r : rows) {
      sRowsAll.push_back({list.table, 0xFFFFFFFFu, 1, r.rateOf, {0, 1, 10, 1, 1, p.top[t], 2, 2, r.id, 4, 4, static_cast<uint32_t>(r.prob), 8, 4, p.rate[t][r.rateOf]}});
      log += " " + std::to_string(r.id) + ":" + std::to_string(r.prob);
    }
    std::fprintf(stderr, "[gen57] Poke Spots: %s gets %zu Gen 5-7 entries (species:percent)%s\n", list.table, rows.size(), log.c_str());
  }
}

void ReadEffectIds(const std::filesystem::path& pack) {
  namespace J = engine::mods::json;
  sEffectIds.clear();
  std::string text;
  if (!ReadText(pack / L"battle" / L"abilities.json", text)) return;
  J::Value root;
  J::Reader rd(text);
  if (!rd.Document(root) || root.kind != J::Value::Kind::Object) return;
  const auto list = root.fields.find("abilities");
  if (list == root.fields.end()) return;
  for (const std::string& k : list->second.order) {
    const J::Value& a = list->second.fields.at(k);
    if (a.kind != J::Value::Kind::Object || a.fields.size() < 2) continue;
    const auto id = a.fields.find("id");
    if (id != a.fields.end() && id->second.kind == J::Value::Kind::Number && id->second.number > 0) sEffectIds.insert(static_cast<uint32_t>(id->second.number));
  }
}

}

namespace xd::gen57 {

const std::vector<uint16_t>& EvolutionShopItems() { return sEvoShop; }
uint32_t PrismScaleItem() { return sLoaded ? sPrismScale : 0; }

std::filesystem::path PackFolder() { return xd::SettingsFolder() / L"extra_assets" / L"usum_gen57"; }

bool PackPresent(std::string* why) {
  std::string text;
  std::error_code ec;
  const std::filesystem::path info = PackFolder() / L"gen57.json";
  if (!std::filesystem::exists(info, ec) || !ReadText(info, text)) {
    if (why) *why = Fmt(Tr("the Gen 5-7 files are not made yet (no %s)"), PackFolder().string().c_str());
    return false;
  }
  const size_t at = text.find("\"version\"");
  const int version = at == std::string::npos ? 0 : std::atoi(text.c_str() + text.find(':', at) + 1);
  if (version != kPackVersion) {
    if (why) *why = std::string(Tr("the Gen 5-7 files were made by another version, make them again"));
    return false;
  }
  switch (xd::packmatch::CheckGen57(xd::SettingsFolder() / L"extra_assets")) {
    case xd::packmatch::Problem::LegacyGen4Gone:
      if (why) *why = std::string(Tr("the Gen 5-7 files were made together with Gen 4 files that are not installed any more, make the Ultra Sun / Ultra Moon files again"));
      return false;
    case xd::packmatch::Problem::TocDiffers:
    case xd::packmatch::Problem::TooManyCries:
      if (why) *why = std::string(Tr("the Gen 5-7 files do not fit the Gen 4 files that are installed now, make the Ultra Sun / Ultra Moon files again"));
      return false;
    default: break;
  }
  return true;
}

bool Loaded() { return sLoaded; }
bool SpotsLoaded() { return sLoaded && sSpots; }

std::vector<std::string> FairyMoveNames() {
  std::vector<std::string> out;
  std::string text;
  if (!ReadText(PackFolder() / L"gen57.json", text)) return out;
  size_t at = text.find("\"fairy_moves\"");
  if (at == std::string::npos) return out;
  const size_t end = text.find(']', at);
  for (size_t p = text.find('[', at); p != std::string::npos && p < end;) {
    const size_t a = text.find('"', p + 1);
    if (a == std::string::npos || a > end) break;
    const size_t b = text.find('"', a + 1);
    if (b == std::string::npos) break;
    out.push_back(text.substr(a + 1, b - a - 1));
    p = b;
  }
  return out;
}

uint32_t ExtraRowCount(const char* table) {
  uint32_t n = 0;
  if (sLoaded) {
    for (const auto& r : sTables.rows) n += r.table == table ? r.count : 0;
    if (std::strcmp(table, "snd_file") == 0) n += sPadSndFile;
    if (std::strcmp(table, "snd_data") == 0) n += sPadSndData;
  }
  return n;
}

uint32_t ModelRowsTaken() {
  uint32_t n = 0;
  if (sLoaded) {
    for (const auto& r : sTables.pkx) n = std::max(n, r.row + 1 > kPkxDisc ? r.row + 1 - kPkxDisc : 0u);
  }
  const uint32_t gen4 = xd::gen4::ModelRowsTaken();
  return n > gen4 ? n - gen4 : 0;
}
uint32_t WaveRowsTaken() { return sLoaded ? sPadWaves + static_cast<uint32_t>(sTables.waves.size()) : 0; }

void SetSpotsMaxGen(int gen) { sSpotsMaxGen = gen; }

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles) {
  sLoaded = false;
  sSpots = false;
  sSpotPlan = SpotPlan();
  sTables = {};
  sRowsAll.clear();
  sAbilities.clear();
  sEvoShop.clear();
  sOwnCries = false;
  sReserved = sPadSndFile = sPadSndData = sPadWaves = 0;
  if (!wanted) return;
  std::string why;
  if (!xd::gen4::Loaded()) {
    std::fprintf(stderr, "[gen57] Gen 5-7 Pokemon are on, but the Gen 4 files they continue are not loaded: the game starts without them\n");
    return;
  }
  if (!PackPresent(&why)) {
    std::fprintf(stderr, "[gen57] Gen 5-7 Pokemon are on, but %s: the game starts without the Gen 5-7 Pokemon\n", why.c_str());
    PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - Gen 5-7"),
                     Fmt(Tr("The Gen 5-7 Pokemon can't be used: %s.\n\nThey are made from your own Ultra Sun / Ultra Moon dump on the "
                            "launcher's \"Extra assets\" page.\nThe game starts without them."),
                         why.c_str())
                         .c_str());
    return;
  }
  const std::filesystem::path pack = PackFolder();
  std::string text, error;
  if (!ReadText(pack / L"tables.txt", text) || !xd::pack::ParseTables(text, sTables, error)) {
    std::fprintf(stderr, "[gen57] the pack's tables.txt can't be used (%s): the game starts without the Gen 5-7 Pokemon\n",
                 error.empty() ? "unreadable" : error.c_str());
    sTables = {};
    return;
  }
  const xd::packmatch::Gen57Pack info = xd::packmatch::ReadGen57(pack);
  sOwnCries = info.ownCries;
  sReserved = info.reserved != 0 ? info.reserved : xd::packmatch::kReservedCries;
  {
    const uint32_t n4 = xd::gen4::ExtraRowCount("snd_file"), d4 = xd::gen4::ExtraRowCount("snd_data");
    uint32_t firstFile = 0, firstData = 0;
    for (const auto& r : sTables.rows) {
      if (r.table == "snd_file" && firstFile == 0) firstFile = r.row;
      if (r.table == "snd_data" && firstData == 0) firstData = r.row;
    }
    const uint32_t wantFile = xd::packmatch::kDiscSndFile + (sOwnCries ? sReserved : n4);
    const uint32_t wantData = xd::packmatch::kDiscSndData + (sOwnCries ? sReserved : d4);
    if ((firstFile != 0 && firstFile != wantFile) || (firstData != 0 && firstData != wantData) || (sOwnCries && (n4 > sReserved || d4 > sReserved))) {
      std::fprintf(stderr, "[gen57] the pack's cry rows start at %u / %u, the Gen 4 pack leaves %u / %u: the game starts without the Gen 5-7 Pokemon\n", firstFile,
                   firstData, wantFile, wantData);
      PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - Gen 5-7"),
                       Fmt("%s\n\n%s",
                           Tr("The Gen 5-7 Pokemon can't be used: the Gen 5-7 files do not fit the Gen 4 files that are installed."),
                           Tr("Make the Ultra Sun / Ultra Moon files again on the launcher's \"Extra assets\" page.\nThe game starts without them."))
                           .c_str());
      sTables = {};
      return;
    }
    if (sOwnCries && firstFile != 0) sPadSndFile = sReserved - n4;
    if (sOwnCries && firstData != 0) sPadSndData = sReserved - d4;
    if (sOwnCries && !sTables.waves.empty()) sPadWaves = sReserved - xd::gen4::WaveRowsTaken();
  }
  std::filesystem::path dataDir = pack / L"data";
  const char* lang = xd::languages::Active() ? xd::languages::Active()->id : "us";
  std::error_code ec;
  if (std::strcmp(lang, "us") != 0) {
    const std::filesystem::path alt = pack / (std::wstring(L"data_") + std::wstring(lang, lang + std::strlen(lang)));
    if (std::filesystem::is_directory(alt, ec)) dataDir = alt;
  }
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator(dataDir, ec)) {
    if (e.path().extension() == ".json") files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  std::vector<engine::mods::DataModFile> mine;
  for (const auto& f : files) {
    engine::mods::DataModFile d;
    d.mod = "gen57";
    d.builtIn = true;
    d.modName = "Gen 5-7 Pokemon and moves";
    d.label = "gen57/" + f.filename().string();
    if (!ReadText(f, d.text)) {
      std::fprintf(stderr, "[gen57] could not read %s: the game starts without the Gen 5-7 Pokemon\n", f.string().c_str());
      sTables = {};
      return;
    }
    if (f.filename() == "01_moves.json") {
      const int n = xd::movetargets::PatchPackMoves(d.text, true);
      if (n > 0) std::fprintf(stderr, "[gen57] move targets: %d field(s) of the pack's moves put right (a pack made before 0.7.5)\n", n);
    }
    mine.push_back(std::move(d));
  }
  if (mine.empty()) {
    std::fprintf(stderr, "[gen57] no data files in %s: the game starts without the Gen 5-7 Pokemon\n", dataDir.string().c_str());
    sTables = {};
    return;
  }
  {
    bool packHas = false;
    for (const auto& d : mine) if (d.text.find("\"Prism Scale\"") != std::string::npos) packHas = true;
    if (!packHas && !xd::xg::On()) {
      engine::mods::DataModFile d;
      d.mod = "gen57";
      d.builtIn = true;
      d.modName = "Gen 5-7 Pokemon and moves";
      d.label = "gen57/90_prism_scale.json";
      d.text = std::string("{\n \"_note\": \"the Prism Scale (Feebas -> Milotic with the Gen 5-7 rules), made by the game, gen57_pack.cpp\",\n") +
               " \"add\": {\"items\": {\"Prism Scale\": {\"copy\": \"Red Scarf\", \"price\": 100, \"coupon\": 6000,\n" +
               "  \"text\": {\"name\": \"" + JsonEscape(Tr("PRISM SCALE")) + "\", \"doc\": \"" + JsonEscape(Tr("Held by Feebas, it{00}evolves when it{00}levels up.")) +
               "\"}}}}\n}\n";
      mine.push_back(std::move(d));
    }
  }
  for (auto& d : mine) dataFiles.push_back(std::move(d));
  std::string abilities;
  if (ReadText(pack / L"abilities.txt", abilities)) ParseAbilities(abilities);
  ReadEffectIds(pack);
  options.extraLayers.push_back({"usum_gen57", "Gen 5-7 Pokemon", pack});
  options.builtInFolders["gen57"] = pack;
  sRowsAll.clear();
  if (sPadSndFile != 0) sRowsAll.push_back({"snd_file", xd::packmatch::kDiscSndFile + xd::gen4::ExtraRowCount("snd_file"), sPadSndFile, 0, {}});
  if (sPadSndData != 0) sRowsAll.push_back({"snd_data", xd::packmatch::kDiscSndData + xd::gen4::ExtraRowCount("snd_data"), sPadSndData, 0, {}});
  for (const auto& r : sTables.rows) sRowsAll.push_back(r);
  if (spots && xd::gen4::SpotsLoaded()) {
    engine::mods::DataModFile d;
    d.mod = "gen57";
    d.builtIn = true;
    d.modName = "Gen 5-7 Pokemon and moves";
    d.label = "gen57/optional/spots.json";
    if (PlanSpots(pack, d.text)) {
      dataFiles.push_back(std::move(d));
      sSpots = true;
    } else {
      std::fprintf(stderr, "[gen57] the Poke Spot set could not be made (the Gen 4 entries' rows are missing): the spots keep the Gen 4 set\n");
    }
  }
  xd::gen4::SetMoreRows(sRowsAll);
  sLoaded = true;
  std::fprintf(stderr, "[gen57] pack %s: %zu data file(s) (%s), %zu table row group(s), %zu model rows, %zu abilities\n",
               pack.string().c_str(), files.size(), dataDir.filename().string().c_str(), sRowsAll.size(), sTables.pkx.size(),
               sAbilities.size());
}

bool FairySlots(std::map<uint32_t, std::array<bool, 2>>& species, std::map<std::string, std::array<bool, 2>>& forms) {
  namespace J = engine::mods::json;
  const std::filesystem::path pack = PackFolder();
  const auto readJson = [&](const std::filesystem::path& file, J::Value& root) {
    std::string text;
    if (!ReadText(file, text)) return false;
    J::Reader rd(text);
    return rd.Document(root) && root.kind == J::Value::Kind::Array;
  };
  const auto num = [](const J::Value& v) { return v.kind == J::Value::Kind::Number ? static_cast<int>(v.number) : -1; };
  species.clear();
  forms.clear();
  J::Value root;
  if (!readJson(pack / L"report.json", root)) return false;
  for (const J::Value& e : root.items) {
    if (e.kind != J::Value::Kind::Object) continue;
    const auto nat = e.fields.find("nat"), types = e.fields.find("types");
    if (nat == e.fields.end() || types == e.fields.end() || types->second.items.size() != 2) continue;
    const std::array<bool, 2> s = {num(types->second.items[0]) == 17, num(types->second.items[1]) == 17};
    if (s[0] || s[1]) species[static_cast<uint32_t>(num(nat->second))] = s;
  }
  root = J::Value();
  if (readJson(pack / L"forms_report.json", root)) {
    for (const J::Value& e : root.items) {
      if (e.kind != J::Value::Kind::Object) continue;
      const auto key = e.fields.find("key"), types = e.fields.find("types");
      if (key == e.fields.end() || types == e.fields.end() || types->second.items.size() != 2) continue;
      const std::array<bool, 2> s = {num(types->second.items[0]) == 17, num(types->second.items[1]) == 17};
      if (s[0] || s[1]) forms[key->second.text] = s;
    }
  }
  return true;
}

bool XgInfo(xd::gen4::xg::Gen57Info& out) {
  namespace J = engine::mods::json;
  const std::filesystem::path pack = PackFolder();
  const auto readJson = [&](const std::filesystem::path& file, J::Value& root) {
    std::string text;
    if (!ReadText(file, text)) return false;
    J::Reader rd(text);
    return rd.Document(root);
  };
  const auto num = [](const J::Value& v) { return v.kind == J::Value::Kind::Number ? static_cast<int>(v.number) : -1; };
  J::Value root;
  if (readJson(pack / L"battle" / L"moves.json", root) && root.kind == J::Value::Kind::Object) {
    if (const auto it = root.fields.find("moves"); it != root.fields.end()) {
      for (const std::string& k : it->second.order) out.portCodeMoves.insert(k);
    }
  }
  root = J::Value();
  if (readJson(pack / L"battle" / L"abilities.json", root) && root.kind == J::Value::Kind::Object) {
    if (const auto it = root.fields.find("abilities"); it != root.fields.end()) {
      for (const std::string& k : it->second.order) {
        const J::Value& a = it->second.fields.at(k);
        if (a.kind != J::Value::Kind::Object) continue;
        const auto id = a.fields.find("id");
        if (id != a.fields.end() && num(id->second) > 0) out.portCodeAbilities.insert(static_cast<uint32_t>(num(id->second)));
      }
    }
  }
  for (const std::string& n : FairyMoveNames()) out.fairyMoves.insert(n);
  root = J::Value();
  if (readJson(pack / L"report.json", root) && root.kind == J::Value::Kind::Array) {
    for (const J::Value& e : root.items) {
      if (e.kind != J::Value::Kind::Object) continue;
      const auto nat = e.fields.find("nat"), types = e.fields.find("types");
      if (nat == e.fields.end() || types == e.fields.end() || types->second.items.size() != 2) continue;
      out.types[static_cast<uint32_t>(num(nat->second))] = {num(types->second.items[0]), num(types->second.items[1])};
    }
  }
  root = J::Value();
  if (readJson(pack / L"forms_report.json", root) && root.kind == J::Value::Kind::Array) {
    for (const J::Value& e : root.items) {
      if (e.kind != J::Value::Kind::Object) continue;
      const auto key = e.fields.find("key"), types = e.fields.find("types");
      if (key == e.fields.end() || types == e.fields.end() || types->second.items.size() != 2) continue;
      out.formTypes[key->second.text] = {num(types->second.items[0]), num(types->second.items[1])};
    }
  }
  for (const AbilityRow& a : sAbilities) out.abilityIds.insert(a.id);
  return !out.abilityIds.empty();
}

std::vector<std::string> AbilityNamesForXG() {
  std::vector<std::string> names(256);
  for (const AbilityRow& a : sAbilities) {
    if (a.id < 256) names[a.id] = a.name[0];
  }
  return names;
}

void AddPrismScale(const engine::mods::LoadResult& result) {
  sPrismScale = 0;
  if (xd::xg::On()) return;
  for (const auto& r : result.addedRows) {
    if (r.table == "item_data_prime" && r.mod == "gen57" && NormKey(r.key) == "prismscale") sPrismScale = r.id;
  }
  if (sPrismScale == 0) return;
  xd::gen4::AddExtraEvolution({328, 24, static_cast<uint16_t>(sPrismScale), 329});
  std::fprintf(stderr, "[gen57] Feebas + Prism Scale (item %u) -> Milotic with a Gen 5-7 rule set\n", sPrismScale);
}

void AfterLoad(const engine::mods::LoadResult& result) {
  if (!sLoaded) return;
  xd::forms::Load(result);
  std::vector<uint32_t> all;
  for (const auto& r : result.addedRows) {
    if (r.mod == "gen57" && r.table == "pokemon_data" && !xd::forms::IsForm(r.id)) all.push_back(r.id);
  }
  {
    uint32_t sylveon = 0, shiny = 0;
    for (const auto& r : result.addedRows) {
      if (r.mod == "gen57" && r.table == "pokemon_data" && r.key == "Sylveon") sylveon = r.id;
      if (r.table == "item_data_prime" && (r.key == "Shiny Stone" || r.key == "Shiny_Stone")) shiny = r.id;
    }
    if (sylveon != 0 && shiny != 0) {
      xd::gen4::AddExtraEvolution({133, 7, static_cast<uint16_t>(shiny), static_cast<uint16_t>(sylveon)});
      xd::gen4::SetEeveeSylveon(sylveon, shiny);
      std::fprintf(stderr, "[gen57] Eevee + Shiny Stone -> Sylveon (species %u, item %u)\n", sylveon, shiny);
    } else {
      std::fprintf(stderr, "[gen57] Eevee does not evolve into Sylveon here (species %u, Shiny Stone %u: a row is missing)\n", sylveon, shiny);
    }
  }
  AddPrismScale(result);
  ReadEvolutions(result);
  AppendSpotRows(result);
  for (uint32_t id : all) sRowsAll.push_back({"pdapokemonsortdata", 0xFFFFFFFFu, 1, -1, {0, 2, id}});
  if (xd::forms::Loaded() && !xd::xg::On()) {
    for (uint32_t i = 0; i < xd::forms::ShopSpareRows(); i++) sRowsAll.push_back({"shopGoods", 0xFFFFFFFFu, 1, -1, {0, 2, 0}});
  }
  xd::gen4::SetMoreRows(sRowsAll);
  std::fprintf(stderr, "[gen57] %zu species numbered, added to the PDA's sort list\n", all.size());
}

void ApplyMainTables() {
  if (!sLoaded) return;
  if (!sTables.waves.empty()) {
    const uint32_t first = sOwnCries ? kWaveDisc + sReserved : pokemon_nakigoe_data_number;
    if (first < pokemon_nakigoe_data_number || first + sTables.waves.size() > kWaveDisc + kPkxSpare) {
      std::fprintf(stderr, "[gen57] the PDA's cry waveforms don't fit the game's table: left out\n");
    } else {
      for (uint32_t k = pokemon_nakigoe_data_number; k < first; k++) pokemon_nakigoe_data[k] = pokemon_nakigoe_data[0];
      for (size_t k = 0; k < sTables.waves.size(); k++) {
        auto* p = static_cast<uint8_t*>(PortMem_LowAlloc(sTables.waves[k].size(), 16));
        std::copy(sTables.waves[k].begin(), sTables.waves[k].end(), p);
        pokemon_nakigoe_data[first + k] = p;
      }
      pokemon_nakigoe_data_number = first + static_cast<uint32_t>(sTables.waves.size());
      std::fprintf(stderr, "[gen57] pokemon_nakigoe_data grown: %u -> %u rows\n", first, pokemon_nakigoe_data_number);
    }
  }
  if (sTables.pkx.empty()) return;
  const uint32_t old = pokemon_pkx_data_number;
  uint32_t last = old;
  for (const auto& r : sTables.pkx) {
    if (r.row < old || r.row >= kPkxDisc + kPkxSpare) {
      std::fprintf(stderr,
                   "[gen57] the model row %u doesn't fit after the Gen 4 pack's (the table has %u rows): left out (make the Gen 5-7 files again)\n",
                   r.row, old);
      continue;
    }
    const auto moved = xd::pack::TocGroupRemap().find(r.group);
    pokemon_pkx_data[r.row].a = moved != xd::pack::TocGroupRemap().end() ? moved->second : r.group;
    pokemon_pkx_data[r.row].b = r.res;
    last = std::max(last, r.row + 1);
  }
  pokemon_pkx_data_number = last;
  std::fprintf(stderr, "[gen57] pokemon_pkx_data grown: %u -> %u rows\n", old, last);
}

const char* AbilityName(uint32_t id) {
  for (const AbilityRow& a : sAbilities) {
    if (a.id == id) return a.name[0].c_str();
  }
  return nullptr;
}

void AppendAbilityTexts(std::vector<std::pair<uint32_t, std::string>>& texts) {
  if (!sLoaded) return;
  const int col = LanguageColumn();
  for (const AbilityRow& a : sAbilities) {
    std::string name = a.name[col];
    if (col != 5) {
      for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    texts.emplace_back((kGroup << 20) | (kFirstText + a.id * 2), name);
    texts.emplace_back((kGroup << 20) | (kFirstText + a.id * 2 + 1), a.text[col]);
  }
  texts.emplace_back((kGroup << 20) | kNotYetText, Tr("Its effect isn't in the game yet."));
  texts.emplace_back((kGroup << 20) | kDoublesText, Tr("Works in double battles only."));
}

bool AllyOnly(uint32_t id) {
  static const char* const kNames[] = {"HEALER", "FRIEND GUARD", "TELEPATHY", "AROMA VEIL", "FLOWER VEIL", "SYMBIOSIS", "BATTERY", "RECEIVER", "POWER OF ALCHEMY"};
  for (const AbilityRow& a : sAbilities) {
    if (a.id != id) continue;
    std::string n = a.name[0];
    for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const char* k : kNames) if (n == k) return true;
  }
  return false;
}

uint32_t DescriptionText(uint32_t id) {
  if (xd::xg::On() || Implemented(id)) return kFirstText + id * 2 + 1;
  if (AllyOnly(id)) return kDoublesText;
  return xd::rulegen::AtLeast(5) ? kNotYetText : kNoEffectText;
}

bool AbilityImplemented(uint32_t id) { return Implemented(id); }

void SyncAbilityDescriptions() {
  if (!sRowsInstalled || xd::xg::On()) return;
  auto* rows = reinterpret_cast<uint32_t*>(pokemon_tokusei_data);
  for (const AbilityRow& a : sAbilities) rows[3 * a.id + 2] = (kGroup << 20) | DescriptionText(a.id);

}

void InstallAbilityRows() {
  if (!sLoaded || sAbilities.empty()) return;
  static bool done = false;
  if (done) return;
  if (!xd::xg::On() && pokemon_tokusei_data_number < 124) {
    std::fprintf(stderr, "[gen57] ability table has %u rows, the Gen 4 rows are missing: Gen 5-7 ability names not added\n",
                 pokemon_tokusei_data_number);
    return;
  }
  done = true;
  uint32_t top = pokemon_tokusei_data_number;
  for (const AbilityRow& a : sAbilities) {
    xd::xg::SetAbilityRow(pokemon_tokusei_data, a.id, (kGroup << 20) | (kFirstText + a.id * 2),
                          (kGroup << 20) | DescriptionText(a.id));
    top = std::max(top, a.id + 1);
  }
  pokemon_tokusei_data_number = top;
  sRowsInstalled = true;
  std::fprintf(stderr, "[gen57] ability table grown to %u rows (%zu Gen 5-7 abilities named)\n", top, sAbilities.size());
}

}
