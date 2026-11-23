#include "xd_gen57.h"
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
#include <map>
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

bool sLoaded = false;
bool sSpots = false;
bool sOwnCries = false;
uint32_t sReserved = 0, sPadSndFile = 0, sPadSndData = 0, sPadWaves = 0;
xd::pack::PackTables sTables;
std::vector<xd::pack::RowSpec> sRowsAll;

struct AbilityRow {
  uint32_t id = 0;
  std::string name[6], text[6];
};
std::vector<AbilityRow> sAbilities;

bool Implemented(uint32_t) { return false; }

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

}

namespace xd::gen57 {

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

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles) {
  sLoaded = false;
  sSpots = false;
  sTables = {};
  sRowsAll.clear();
  sAbilities.clear();
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
    mine.push_back(std::move(d));
  }
  if (mine.empty()) {
    std::fprintf(stderr, "[gen57] no data files in %s: the game starts without the Gen 5-7 Pokemon\n", dataDir.string().c_str());
    sTables = {};
    return;
  }
  for (auto& d : mine) dataFiles.push_back(std::move(d));
  std::string abilities;
  if (ReadText(pack / L"abilities.txt", abilities)) ParseAbilities(abilities);
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
    std::string spotText;
    xd::pack::PackTables spotTables;
    if (ReadText(pack / L"optional" / L"spots.json", d.text) && ReadText(pack / L"optional" / L"spots_tables.txt", spotText) &&
        xd::pack::ParseTables(spotText, spotTables, error)) {
      for (auto& r : spotTables.rows) sRowsAll.push_back(std::move(r));
      dataFiles.push_back(std::move(d));
      sSpots = true;
    } else {
      std::fprintf(stderr, "[gen57] the Poke Spot set is not in the pack: the spots keep the Gen 4 set\n");
    }
  }
  xd::gen4::SetMoreRows(sRowsAll);
  sLoaded = true;
  std::fprintf(stderr, "[gen57] pack %s: %zu data file(s) (%s), %zu table row group(s), %zu model rows, %zu abilities\n",
               pack.string().c_str(), files.size(), dataDir.filename().string().c_str(), sRowsAll.size(), sTables.pkx.size(),
               sAbilities.size());
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

void AfterLoad(const engine::mods::LoadResult& result) {
  if (!sLoaded) return;
  xd::forms::Load(result);
  std::vector<uint32_t> all;
  for (const auto& r : result.addedRows) {
    if (r.mod == "gen57" && r.table == "pokemon_data" && !xd::forms::IsForm(r.id)) all.push_back(r.id);
  }
  for (uint32_t id : all) sRowsAll.push_back({"pdapokemonsortdata", 0xFFFFFFFFu, 1, -1, {0, 2, id}});
  if (xd::forms::Loaded()) {
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
    pokemon_pkx_data[r.row].a = r.group;
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
                          (kGroup << 20) | (xd::xg::On() || Implemented(a.id) ? kFirstText + a.id * 2 + 1 : kNoEffectText));
    top = std::max(top, a.id + 1);
  }
  pokemon_tokusei_data_number = top;
  std::fprintf(stderr, "[gen57] ability table grown to %u rows (%zu Gen 5-7 abilities named)\n", top, sAbilities.size());
}

}
