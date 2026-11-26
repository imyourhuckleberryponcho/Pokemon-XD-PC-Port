#include "data/xd_data_mods.h"
#include "data/xd_faces.h"
#include "data/xd_species.h"
#include "data/xd_sounds.h"
#include "i18n.h"
#include "xd_battle_models.h"
#include "xd_rulesets.h"
#include "xd_user_folder.h"
#include "xd_xg.h"
#include "xd_playthrough_files.h"
#include "xd_randomizer.h"
#include "xd_starter.h"
#include "xd_added_rows.h"
#include "xd_gen4.h"
#include "xd_gen57.h"
#include "pack_tables.h"
#include "xd_gen_rules.h"
#include "xd_gen4_xg.h"
#include "xd_modern_types.h"
#include "xd_languages.h"
#include "xd_scripts.h"
#include "battle_replay.h"
#include "common_script.h"
#include "xd_mod_battle.h"

#include <mods.h>
#include <mod_layout.h>
#include <mods_disc.h>
#include <port_disc.h>
#include <port_message.h>
#include <sdk_port.h>
#include <settings.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <string>

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
struct XdPinPkx { uint32_t a, b; };
extern XdPinPkx pokemon_pkx_data[];
extern uint32_t pokemon_pkx_data_number;
}

#include "xd_hd_packs.h"
#include "xd_mods_reload.h"

namespace xd::pt {
bool BootWorldMods(std::vector<std::string>& ids);
std::string BootRandomizer();
bool BootGen4();
bool BootGen4Spots();
bool BootGen57();
int BootMaxGen();
int BootRuleGen();
bool BootModernTypes();
bool BootLooks(engine::rulesets::Looks& out);
void LooksMissing(const std::string& names);
void LooksLoaded(const std::string& key);
std::string LooksKeyOfBoot();
void RandomizerMade(const std::string& code);
std::string BootMoreShadows();
void MoreShadowsMade(const std::string& text);
void AfterModsLoaded(const engine::mods::LoadResult& result, const std::filesystem::path& modsFolder,
                     const std::filesystem::path& listFile, const engine::mods::DiscView& disc);
}

namespace {
bool MakeMoreShadows(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, const std::string& text, const xd::randomizer::Result* rando,
                     std::vector<engine::mods::DataModFile>& out, std::string& error) {
  if (text.empty()) return true;
  namespace xr = xd::randomizer;
  xr::MoreShadowsPlan plan;
  if (!xr::ParseMoreShadows(text, plan)) {
    error = "More Shadow Pokemon: \"" + text + "\" is not an amount and a seed";
    return false;
  }
  const bool gen4 = xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr && xd::gen4::Loaded();
  if (gen4 && !xr::LoadGen4Species(xd::gen4::PackFolder() / L"randomizer.txt", error)) return false;
  if (gen4) {
    if (const auto* tr = xd::gen4::xg::Active()) xr::TranslateGen4Species([tr](xr::Gen4Species& g) {
      tr->MapLearnset(g.level, g.move);
      for (uint32_t& a : g.abilities) a = tr->MapAbility(a);
      tr->MapTypes(g.name, g.types);
    });
  }
  const bool gen57 = gen4 && xd::gen57::Loaded();
  if (gen57 && !xr::LoadGen57Species(xd::gen57::PackFolder() / L"randomizer.txt", error)) return false;
  if (gen57) {
    if (const auto* tr = xd::gen4::xg::Active()) xr::TranslateGen57Species([tr](xr::Gen4Species& g) {
      tr->MapLearnset(g.level, g.move);
      for (uint32_t& a : g.abilities) a = tr->MapAbility(a);
      tr->MapTypes57(g.dex, g.types);
    });
  }
  engine::randomizer::ModOutput mod;
  if (!xr::GenerateMoreShadows(catalog, read, plan, rando, gen4, mod, error, gen57)) return false;
  out.push_back({"more_shadows", "More Shadow Pokemon", "data/more_shadows.json", mod.ToJson("more shadows " + text), true});
  const std::filesystem::path folder = xd::CardRoot() / L"playthroughs" / L"more_shadows" / std::filesystem::path(text);
  std::error_code ec;
  if (!std::filesystem::exists(folder / "spoiler.txt", ec)) {
    std::filesystem::create_directories(folder, ec);
    std::string spoiler = "More Shadow Pokemon spoiler log\n\n";
    for (const std::string& line : mod.spoiler) spoiler += line + "\n";
    std::FILE* f = std::fopen((folder / "spoiler.txt").string().c_str(), "wb");
    if (f != nullptr) {
      std::fwrite(spoiler.data(), 1, spoiler.size(), f);
      std::fclose(f);
    }
  }
  std::fprintf(stderr, "[more shadows] %s: %zu number(s) changed (%s)\n", text.c_str(), mod.Count(), mod.spoiler.empty() ? "" : mod.spoiler.front().c_str());
  xd::pt::MoreShadowsMade(text);
  return true;
}
}

namespace {
std::string sActive;
std::string sBattleMods;
std::string sBattleModsWhy;

bool BlocksReplay(const engine::mods::Manifest& m, const engine::mods::AppliesVerdict& v, std::string* why) {
  if (m.scripts) {
    if (why) *why = "its scripts run in battles";
    return true;
  }
  if (v.effective != engine::mods::Applies::AnyTime) {
    if (why) *why = v.reason.empty() ? std::string("it changes game data a battle reads (tables, rules, added rows)") : v.reason;
    return true;
  }
  return false;
}
std::string sActiveBase;
std::string sExtraText;

bool ApplyProgramChanges(const std::vector<uint8_t>& program) {
  size_t size = 0;
  const uint8_t* dol = PortDisc_Dol(&size);
  uint8_t* mem1 = PortMem_MEM1();
  if (dol == nullptr || mem1 == nullptr || program.size() != size) return false;
  size_t changed = 0;
  for (size_t i = 0; i < size; i++) {
    if (program[i] == dol[i]) continue;
    uint32_t address = 0;
    if (!xd::data::DolOffsetToAddress(dol, size, static_cast<uint32_t>(i), address) || address < 0x80000000u ||
        address - 0x80000000u >= 0x01800000u) {
      return false;
    }
    mem1[address - 0x80000000u] = program[i];
    changed++;
  }
  std::fprintf(stderr, "[mods] main.dol: %zu byte(s) of its tables changed by data mods\n", changed);
  return true;
}
}

namespace {
uint32_t TocBe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void TocPut32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24), p[1] = static_cast<uint8_t>(v >> 16), p[2] = static_cast<uint8_t>(v >> 8), p[3] = static_cast<uint8_t>(v);
}
bool TocValid(const std::vector<uint8_t>& d, uint32_t& count, uint32_t& groups) {
  if (d.size() < 0x20) return false;
  count = TocBe32(d.data() + 8);
  groups = TocBe32(d.data() + 0x10);
  return groups >= 0x20 && groups <= d.size() && static_cast<uint64_t>(groups) + 16ull * count <= d.size();
}
bool ReadAll(const std::filesystem::path& p, std::vector<uint8_t>& out) {
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (f == nullptr) return false;
  uint8_t buf[65536];
  out.clear();
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return true;
}
std::vector<uint8_t> MergeToc(const std::vector<uint8_t>& lang, const std::vector<uint8_t>& other) {
  uint32_t lc = 0, lg = 0, oc = 0, og = 0;
  if (!TocValid(lang, lc, lg) || !TocValid(other, oc, og)) return {};
  std::vector<uint32_t> have;
  for (uint32_t i = 0; i < lc; i++) have.push_back(TocBe32(lang.data() + lg + 16 * i));
  std::vector<uint8_t> names(lang.begin() + 0x20, lang.begin() + lg), rows;
  uint32_t added = 0;
  for (uint32_t i = 0; i < oc; i++) {
    const uint8_t* g = other.data() + og + 16 * i;
    const uint32_t id = TocBe32(g), at = TocBe32(g + 4);
    if (std::find(have.begin(), have.end(), id) != have.end() || at >= other.size()) continue;
    uint8_t row[16] = {0};
    TocPut32(row, id);
    TocPut32(row + 4, static_cast<uint32_t>(0x20 + names.size()));
    for (size_t k = at; k < other.size() && other[k] != 0; k++) names.push_back(other[k]);
    names.push_back(0);
    rows.insert(rows.end(), row, row + 16);
    added++;
  }
  if (added == 0) return {};
  while (names.size() % 16 != 0) names.push_back(0);
  std::vector<uint8_t> out(lang.begin(), lang.begin() + 0x20);
  TocPut32(out.data() + 8, lc + added);
  TocPut32(out.data() + 0x10, static_cast<uint32_t>(0x20 + names.size()));
  out.insert(out.end(), names.begin(), names.end());
  out.insert(out.end(), lang.begin() + lg, lang.begin() + lg + 16 * lc);
  out.insert(out.end(), rows.begin(), rows.end());
  out.insert(out.end(), lang.begin() + lg + 16 * lc, lang.end());
  return out;
}
void MergeLanguageToc(engine::mods::LoadResult& result) {
  const xd::languages::Language* l = xd::languages::Active();
  if (std::strcmp(l->id, "us") == 0 || std::strcmp(l->id, "jp") == 0) return;
  if (const char* off = std::getenv("XD_LANGUAGE_TOC_MERGE"); off != nullptr && std::strcmp(off, "0") == 0) return;
  std::vector<uint8_t> lang;
  if (!ReadAll(xd::languages::LoadedPack().folder / (std::string("lang_") + l->id) / "files" / "GSfsys.toc", lang)) return;
  for (engine::mods::Replacement& r : result.files) {
    std::string lower = r.discPath;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower != "gsfsys.toc") continue;
    std::vector<uint8_t> other;
    if (r.rebuilt ? (other = r.data, true) : ReadAll(r.file, other)) {
      if (other == lang) return;
      std::vector<uint8_t> merged = MergeToc(lang, other);
      if (merged.empty()) merged = lang;
      r.data = std::move(merged);
      r.rebuilt = true;
      r.file.clear();
      r.entries.clear();
      r.generated.clear();
      std::fprintf(stderr, "[languages] archive list: the language's, with the rows of %s added (%zu bytes)\n", r.from.empty() ? "the mods" : r.from.c_str(), r.data.size());
    }
    return;
  }
}

void RepairDuplicateTocIds(engine::mods::LoadResult& result) {
  xd::pack::TocGroupRemap().clear();
  for (engine::mods::Replacement& r : result.files) {
    std::string lower = r.discPath;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower != "gsfsys.toc") continue;
    std::vector<uint8_t> d;
    if (r.rebuilt ? (d = r.data, true) : ReadAll(r.file, d)) {
      uint32_t count = 0, groups = 0;
      if (!TocValid(d, count, groups)) return;
      auto nameOf = [&](uint32_t i) {
        const uint32_t at = TocBe32(d.data() + groups + 16 * i + 4);
        std::string n;
        for (size_t k = at; at < d.size() && k < d.size() && d[k] != 0; k++) n.push_back(static_cast<char>(std::tolower(d[k])));
        return n;
      };
      auto isModel = [](const std::string& n) { return n.rfind("pkx_", 0) == 0; };
      uint32_t highest = 0;
      for (uint32_t i = 0; i < count; i++) highest = std::max(highest, TocBe32(d.data() + groups + 16 * i));
      std::map<uint32_t, uint32_t> firstOf;
      bool changed = false;
      for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = TocBe32(d.data() + groups + 16 * i);
        const auto it = firstOf.find(id);
        if (it == firstOf.end()) {
          firstOf[id] = i;
          continue;
        }
        if (!isModel(nameOf(i)) || isModel(nameOf(it->second)) || xd::pack::TocGroupRemap().count(id) != 0) continue;
        const uint32_t fresh = ++highest;
        TocPut32(d.data() + groups + 16 * i, fresh);
        xd::pack::TocGroupRemap()[id] = fresh;
        changed = true;
        std::fprintf(stderr, "[mods] archive list: number %u was there twice (%s hid %s); the model archive now has %u\n", id,
                     nameOf(it->second).c_str(), nameOf(i).c_str(), fresh);
      }
      if (!changed) return;
      r.data = std::move(d);
      r.rebuilt = true;
      r.file.clear();
      r.entries.clear();
      r.generated.clear();
    }
    return;
  }
}
}

extern "C" const char* PortXD_LanguageText(void);
extern "C" bool PortXD_LanguageMenuTable(void);

extern "C" bool PortXD_LoadMods(void) {
  namespace em = engine::mods;
  const char* off = std::getenv("XD_MODS");
  if (off != nullptr && std::strcmp(off, "0") == 0) {
    std::fprintf(stderr, "[mods] XD_MODS=0: no mods\n");
    return true;
  }
  if (const char* why = xd::xg::Off(xd::xg::Feature::Mods)) {
    std::fprintf(stderr, "[mods] XG mode: no mods (%s)\n", why);
    return true;
  }
  const std::filesystem::path user = xd::SettingsFolder();
  em::LoadOptions options;
  options.modsFolder = user / L"mods";
  options.listFile = user / em::kListFile;
  const bool playerFilesReadOnly = xd::PlayerFilesReadOnly();
  options.listReadOnly = playerFilesReadOnly;
  options.disc = em::OpenDiscView();
  options.disc.gameId = em::MakeTarget(options.disc.gameId, xd::xg::Edition());
  std::vector<em::LoadOptions::ExtraLayer> lookLayers;
  const std::filesystem::path modelsFolder = user / L"extra_assets" / xd::battle_models::kLayerId;
  bool modelsOn = false;
  std::vector<std::string> modelSources;
  {
    const engine::settings::BoolSetting models = engine::settings::Global().Bool(xd::battle_models::kSetting);
    engine::rulesets::Looks looks;
    const bool says = xd::pt::BootLooks(looks);
    modelSources = xd::rulesets::LayerOrderOf(looks, models.Valid() && models.Get());
    xd::pt::LooksLoaded(xd::pt::LooksKeyOfBoot());
    std::fprintf(stderr, "[looks] %s: %s%s\n", says ? "this playthrough" : "the player's setting", xd::rulesets::RankedText(looks, models.Valid() && models.Get()).c_str(),
                 looks.overrides.empty() ? "" : " (with pins for some species)");
    modelsOn = !modelSources.empty();
    {
      std::map<std::string, std::string> winner;
      std::map<std::string, size_t> perSource;
      for (const std::string& id : modelSources) {
        const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(id);
        if (f == nullptr) continue;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(user / L"extra_assets" / f->layerId / L"fsys", ec)) {
          if (e.is_directory(ec)) winner[e.path().filename().string()] = id;
        }
      }
      for (const auto& w : winner) perSource[w.second]++;
      std::string counts;
      for (const std::string& id : modelSources) counts += (counts.empty() ? "" : ", ") + id + " " + std::to_string(perSource[id]);
      if (!modelSources.empty()) std::fprintf(stderr, "[looks] model archives by source: %s (every other model: xd's own)\n", counts.c_str());
      if (std::getenv("XD_LOOKS_LOG") != nullptr) {
        for (const auto& w : winner) std::fprintf(stderr, "[looks] %s -> %s\n", w.first.c_str(), w.second.c_str());
      }
    }
    std::set<std::string> pinned;
    std::map<std::string, std::set<std::string>> pinBySource;
    if (!looks.overrides.empty()) {
      std::map<uint32_t, std::string> toc;
      size_t tocSize = 0;
      if (uint8_t* data = PortDisc_ReadOriginalFile("GSfsys.toc", &tocSize)) {
        auto be32 = [&](size_t o) { return o + 4 <= tocSize ? (uint32_t(data[o]) << 24 | uint32_t(data[o + 1]) << 16 | uint32_t(data[o + 2]) << 8 | data[o + 3]) : 0u; };
        const uint32_t n = be32(8), go = be32(0x10);
        for (uint32_t k = 0; k < n && go + 16 * (k + 1) <= tocSize; k++) {
          const uint32_t no = be32(go + 16 * k + 4);
          if (no < tocSize) toc[be32(go + 16 * k)] = std::string(reinterpret_cast<const char*>(data + no), strnlen(reinterpret_cast<const char*>(data + no), tocSize - no));
        }
        PortDisc_Free(data);
      }
      size_t dolSize = 0;
      const uint8_t* dol = PortDisc_Dol(&dolSize);
      for (const auto& pin : looks.overrides) {
        const int nat = pin.first;
        std::string archive;
        uint32_t group = 0;
        uint8_t row[8] = {};
        if (nat >= 1 && nat <= 386 && dol != nullptr && PortDol_Read(dol, dolSize, 0x8040D0A8u + 8u * static_cast<uint32_t>(nat), row, 8)) {
          group = uint32_t(row[0]) << 24 | uint32_t(row[1]) << 16 | uint32_t(row[2]) << 8 | row[3];
          const auto it = toc.find(group);
          if (it != toc.end()) { archive = it->second; for (char& c : archive) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); if (archive.size() < 5 || archive.compare(archive.size() - 5, 5, ".fsys") != 0) archive += ".fsys"; }
        }
        if (archive.empty()) { std::fprintf(stderr, "[looks] pin for species %d: no model archive known (group %u, toc entries %zu); not applied\n", nat, group, toc.size()); continue; }
        pinned.insert(archive);
        if (pin.second != "xd") pinBySource[pin.second].insert(archive);
        std::fprintf(stderr, "[looks] pin: species %d -> %s from %s\n", nat, archive.c_str(), pin.second.c_str());
      }
    }
    for (const std::string& id : modelSources) {
      const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(id);
      if (f == nullptr) continue;
      em::LoadOptions::ExtraLayer layer{f->layerId, f->name, user / L"extra_assets" / f->layerId, {}, pinned};
      lookLayers.push_back(layer);
    }
    for (const auto& pb : pinBySource) {
      const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(pb.first);
      if (f == nullptr) continue;
      em::LoadOptions::ExtraLayer layer{std::string("pins-") + f->layerId, std::string(f->name) + " (pinned species)", user / L"extra_assets" / f->layerId, pb.second, {}};
      lookLayers.push_back(layer);
      modelsOn = true;
    }
  }
  if (std::strcmp(xd::languages::Active()->id, "us") != 0) {
    const std::string name = std::string("Languages (") + xd::languages::Active()->name + ")";
    const std::string id = xd::languages::Active()->id;
    options.extraLayers.push_back({"languages", name, xd::languages::LoadedPack().FolderOf(id)});
    if (id != xd::languages::kJpId) {
      options.extraLayers.push_back({"languages-list", name + ", archive list",
                                     xd::languages::LoadedPack().folder / (std::string("lang_") + id)});
    }
    if (!PortXD_LanguageMenuTable()) {
      std::fprintf(stderr, "[languages] problem: the menu picture table of the pack could not be used\n");
    }
  }
  options.catalogForScripts = true;
  options.checkEntry = xd::data::CheckModEntry;
  options.finishRows = xd::data::BuildRooms;
  options.dataCatalog = [](const em::EntryReader& read, em::DataCatalog& out, std::string& error) {
    if (!xd::data::BuildDataCatalog(read, out, error)) return false;
    if (const char* why = xd::xg::Off(xd::xg::Feature::DataAddRows)) {
      for (em::DataTable& t : out.tables) {
        if (t.name == "pokemon_data" || t.name == "waza_data" || t.name == "item_data_prime") {
          t.addableBuiltIn = t.addable;
          if (t.name == "pokemon_data") {
            std::vector<std::string> kept;
            for (const std::string& k : t.assetKeys) {
              if (k == "face" || k == "icon") kept.push_back(k);
            }
            t.assetKeys = std::move(kept);
          } else {
            t.assetKeys.clear();
          }
        } else {
          t.addableBuiltIn = false;
          t.addable = false;
        }
      }
      std::fprintf(stderr, "[mods] data mods: in this edition only new Pokemon, moves and items can be added (%s)\n", why);
    }
    return true;
  };
  options.looksOrSound = xd::playthrough::LooksOrSound;
  options.usePlaythroughMods = xd::pt::BootWorldMods(options.playthroughMods);
  const std::string randomizerCode = xd::pt::BootRandomizer();
  const std::string moreShadowsText = xd::pt::BootMoreShadows();
  xd::starter::SetPlan({});
  std::vector<em::DataModFile> gen4Files;
  xd::gen4::SetGen57Wanted(xd::pt::BootGen57());
  xd::gen4::Prepare(xd::pt::BootGen4(), xd::pt::BootGen4Spots(), options, gen4Files);
  xd::gen57::SetSpotsMaxGen(xd::pt::BootMaxGen());
  xd::gen57::Prepare(xd::pt::BootGen57(), xd::pt::BootGen4Spots(), options, gen4Files);
  for (em::LoadOptions::ExtraLayer& layer : lookLayers) options.extraLayers.push_back(std::move(layer));
  xd::faces::SetFaceRowsTaken(xd::gen4::ExtraRowCount("poke_face") + xd::gen57::ExtraRowCount("poke_face"));
  xd::data::SetFlagRowsTaken(xd::gen4::ExtraRowCount("flag_data") + xd::gen57::ExtraRowCount("flag_data"));
  {
    xd::species::Taken taken;
    taken.models = xd::gen4::ModelRowsTaken() + xd::gen57::ModelRowsTaken(), taken.waves = xd::gen4::WaveRowsTaken() + xd::gen57::WaveRowsTaken();
    taken.sndFiles = xd::gen4::ExtraRowCount("snd_file") + xd::gen57::ExtraRowCount("snd_file"), taken.sndData = xd::gen4::ExtraRowCount("snd_data") + xd::gen57::ExtraRowCount("snd_data");
    taken.flags = xd::gen4::ExtraRowCount("flag_data") + xd::gen57::ExtraRowCount("flag_data");
    xd::species::SetTaken(taken);
    xd::sound::SetTaken(taken.sndFiles, taken.sndData);
  }
  xd::modern::Prepare(xd::pt::BootModernTypes(), xd::gen4::Loaded(), gen4Files);
  xd::rulegen::PrepareNumbers(xd::gen4::Loaded() ? xd::pt::BootRuleGen() : 3, gen4Files);
  {
    std::string layers = "base disc";
    if (std::strcmp(xd::languages::Active()->id, "us") != 0) layers += std::string(" > Languages (") + xd::languages::Active()->name + ")";
    if (xd::gen4::Loaded()) layers += " > Gen 4 pack";
    if (xd::gen4::SpotsLoaded()) layers += " > Gen 4 Poke Spots";
    if (xd::gen57::SpotsLoaded()) layers += " > Gen 5-7 Poke Spots";
    if (xd::gen57::Loaded()) layers += " > Gen 5-7 pack";
    for (const std::string& id : modelSources) {
      if (const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(id)) layers += std::string(" > ") + f->name;
    }
    if (xd::modern::Loaded()) layers += " > Modern types (Fairy)";
    if (!randomizerCode.empty()) layers += " > Randomizer " + randomizerCode;
    std::fprintf(stderr, "[mods] built-in layers: %s (user mods are applied before them; conflicts are reported only for user mods)\n",
                 layers.c_str());
  }
  if (!randomizerCode.empty() || !gen4Files.empty() || !moreShadowsText.empty()) {
    options.generateData = [randomizerCode, gen4Files, moreShadowsText](const em::DataCatalog& catalog, const em::EntryReader& read,
                                                       std::vector<em::DataModFile>& out, std::string& error) {
      namespace xr = xd::randomizer;
      if (xd::xg::On() && xd::gen4::Loaded()) {
        std::vector<em::DataModFile> packFiles = gen4Files;
        if (!xd::gen4::TranslateForXG(catalog, read, packFiles, error)) {
          error = "the Gen 4 Pokemon cannot be used on this XG disc: " + error;
          return false;
        }
        for (em::DataModFile& f : packFiles) out.push_back(std::move(f));
      } else {
        for (const em::DataModFile& f : gen4Files) out.push_back(f);
      }
      if (randomizerCode.empty()) return MakeMoreShadows(catalog, read, moreShadowsText, nullptr, out, error);
      engine::randomizer::Settings s;
      xr::ApplyStyle(s, xr::Style::Classic);
      bool seedOnly = false;
      if (!engine::randomizer::DecodeCode(xr::GetSchema(), randomizerCode, s, seedOnly, error)) return false;
      if (xd::xg::Off(xd::xg::Feature::Gen4Pokemon) != nullptr) { s.Set(xr::GetSchema(), "gen4", 0); s.Set(xr::GetSchema(), "gen57", 0); }
      if (xr::CodeIncludesGen4(randomizerCode) && xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr) {
        if (!xd::gen4::Loaded()) {
          error = "this randomizer code includes the Gen 4 Pokemon, but their files are not available (made from your Pokemon "
                  "Battle Revolution disc on the launcher's Extra assets page)";
          return false;
        }
        if (!xr::LoadGen4Species(xd::gen4::PackFolder() / L"randomizer.txt", error)) return false;
        if (const auto* tr = xd::gen4::xg::Active()) xr::TranslateGen4Species([tr](xr::Gen4Species& g) {
          tr->MapLearnset(g.level, g.move);
          for (uint32_t& a : g.abilities) a = tr->MapAbility(a);
          tr->MapTypes(g.name, g.types);
        });
      }
      if (xr::CodeIncludesGen57(randomizerCode) && xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr) {
        if (!xd::gen57::Loaded()) {
          error = "this randomizer code includes the Gen 5-7 Pokemon, but their files are not available (made from your Ultra Sun / Ultra "
                  "Moon dump on the launcher's Extra assets page)";
          return false;
        }
        if (!xr::LoadGen57Species(xd::gen57::PackFolder() / L"randomizer.txt", error)) return false;
        if (const auto* tr = xd::gen4::xg::Active()) xr::TranslateGen57Species([tr](xr::Gen4Species& g) {
          tr->MapLearnset(g.level, g.move);
          for (uint32_t& a : g.abilities) a = tr->MapAbility(a);
          tr->MapTypes57(g.dex, g.types);
        });
      }
      xr::Result r;
      if (!xr::Generate(catalog, read, s, r, error, xd::xg::On() ? &xr::kXGStarterBase : nullptr)) return false;
      xd::starter::SetPlan(r.starter);
      out.push_back({xr::kModId, "Randomizer", "data/randomizer.json", r.GameJson("randomizer " + r.code), true});
      const std::filesystem::path folder = xd::CardRoot() / L"playthroughs" / L"randomizer" / r.code;
      std::string writeError;
      std::error_code ec;
      if (!std::filesystem::exists(folder / "spoiler.txt", ec) && !xr::WriteMod(r, s, folder, writeError)) {
        std::fprintf(stderr, "[randomizer] could not write the spoiler log: %s\n", writeError.c_str());
      }
      std::fprintf(stderr, "[randomizer] %s: %zu number(s) changed (spoiler log: %ls)\n", r.code.c_str(), r.mod.Count(),
                   (folder / "spoiler.txt").wstring().c_str());
      xd::pt::RandomizerMade(randomizerCode);
      return MakeMoreShadows(catalog, read, moreShadowsText, &r, out, error);
    };
  }
  std::error_code ec;
  if (!playerFilesReadOnly) {
    std::filesystem::create_directories(options.modsFolder, ec);
  }

  em::LoadResult result = em::Load(options);
  MergeLanguageToc(result);
  RepairDuplicateTocIds(result);
  xd::pt::AfterModsLoaded(result, options.modsFolder, options.listFile, options.disc);
  for (const em::Message& m : result.messages) {
    static const char* const kLabel[] = {"", "warning: ", "problem: "};
    std::fprintf(stderr, "[mods] %s%s\n", kLabel[static_cast<int>(m.level)], m.text.c_str());
  }
  for (const em::DataChange& c : result.dataChanges) {
    if (c.mod == xd::randomizer::kModId) continue;
    std::fprintf(stderr, "[mods] data: %s #%u%s%s%s %s = %u (was %u; %s, %s)\n", c.table.c_str(), c.entry,
                 c.entryName.empty() ? "" : " (", c.entryName.c_str(), c.entryName.empty() ? "" : ")", c.field.c_str(), c.after,
                 c.before, c.mod.c_str(), c.where.c_str());
  }
  if (result.listWritten) {
    std::fprintf(stderr, "[mods] wrote %ls\n", options.listFile.wstring().c_str());
  }
  if (result.listWriteSkipped) {
    std::fprintf(stderr, "[mods] %s (mods.ini)\n", xd::kPlayerFilesReadOnlyNote);
  }
  if (result.HasWarnings()) {
    PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - mods"),
                     TrFmt(Tr("Please check your mods:\n\n%s\n\nThe game starts with the mods that can be used. Mods are in %s, the "
                              "list of mods that are on in mods.ini next to it."),
                           result.ReportCapped(10).c_str(), reinterpret_cast<const char*>(options.modsFolder.u8string().c_str()))
                         .c_str());
  }
  if (!result.program.empty() && !ApplyProgramChanges(result.program)) {
    std::fprintf(stderr, "[mods] problem: the data mods' changes to main.dol's tables could not be used\n");
    PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - mods"),
                     Tr("The mods' changes to the game program's tables could not be used. The game starts without mods."));
    PortDisc_SetOverlay(nullptr, 0);
    return false;
  }
  if (!em::InstallOverlay(result)) {
    PortMessage_Show(PORT_MESSAGE_WARNING, Tr("Pokemon XD - mods"),
                     Tr("The mods' files could not be prepared (was a mod changed while the game started?). "
                        "The game starts without mods."));
    return false;
  }
  sActive = result.ActiveText();
  sBattleMods.clear();
  sBattleModsWhy.clear();
  for (size_t i = 0; i < result.active.size(); i++) {
    std::string why;
    const em::AppliesVerdict v = i < result.activeApplies.size() ? result.activeApplies[i] : em::AppliesVerdict{};
    if (!BlocksReplay(result.active[i], v, &why)) continue;
    const std::string id = result.active[i].id + (result.active[i].version.empty() ? "" : " " + result.active[i].version);
    sBattleMods += (sBattleMods.empty() ? "" : ", ") + id;
    sBattleModsWhy += (sBattleModsWhy.empty() ? "" : "; ") + id + " (" + why + ")";
  }
  xd::hd::SetPacks(xd::modreload::TexturePacksOf(result, options));
  {
    std::vector<em::Message> ignored;
    std::vector<std::string> notes;
    xd::common_script::Install(result, em::FindMods(options.modsFolder, ignored), options.disc.gameId, notes);
    for (const std::string& n : notes) std::fprintf(stderr, "[mods] warning: %s\n", n.c_str());
  }
  std::fprintf(stderr, "[mods] in use: %s\n", sActive.empty() ? "none" : sActive.c_str());
  if (!modelsOn) {
    sExtraText = "Battle Revolution models: off";
  } else {
    sExtraText.clear();
    std::string missing;
    for (auto it = modelSources.rbegin(); it != modelSources.rend(); ++it) {
      const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(*it);
      if (f == nullptr) continue;
      const em::LoadResult::LayerResult* layer = nullptr;
      for (const auto& l : result.extraLayers) if (l.id == f->layerId) layer = &l;
      const std::string name = f->name;
      std::string one;
      if (layer == nullptr || !layer->found) {
        one = name + ": on, but not made yet (no folder " + (user / L"extra_assets" / f->layerId).string() + ")";
        missing += (missing.empty() ? "" : ", ") + name;
      } else if (layer->files == 0) {
        one = name + ": on, but its folder has no files";
        missing += (missing.empty() ? "" : ", ") + name;
      } else {
        one = name + ": on, " + std::to_string(layer->files) + " files";
      }
      sExtraText += (sExtraText.empty() ? "" : "; ") + one;
    }
    if (!missing.empty()) {
      std::fprintf(stderr, "[looks] not found, skipped: %s (the next source in this playthrough's list shows instead)\n", missing.c_str());
      xd::pt::LooksMissing(missing);
    }
  }
  sExtraText += std::string("; ") + PortXD_LanguageText();
  for (const auto& layer : result.extraLayers) {
    if (layer.id == "languages") sExtraText += " (" + std::to_string(layer.files) + " files from the pack)";
  }
  std::fprintf(stderr, "[extra assets] %s\n", sExtraText.c_str());
  xd::gen4::AfterLoad(result);
  xd::gen57::AfterLoad(result);
  xd::added::Install(result);
  {
    std::vector<em::Message> ignored;
    std::vector<std::string> notes;
    xd::modbattle::Load(result, em::FindMods(options.modsFolder, ignored), options.disc.gameId, notes);
    for (const std::string& n : notes) std::fprintf(stderr, "[mods] problem: battle effects: %s\n", n.c_str());
  }
  xd::scripts::Install(result);
  xd::replay::HookBattles();
  sActiveBase = sActive;
  if (const std::string scripts = xd::scripts::ActiveText(); !scripts.empty()) {
    sActive += (sActive.empty() ? "" : "; ") + scripts;
    sBattleMods += (sBattleMods.empty() ? "" : "; ") + scripts;
  }
  if (const std::string battle = xd::modbattle::ActiveText(); !battle.empty()) {
    sActive += (sActive.empty() ? "" : "; ") + battle;
    sActiveBase += (sActiveBase.empty() ? "" : "; ") + battle;
    sBattleMods += (sBattleMods.empty() ? "" : "; ") + battle;
    sBattleModsWhy += (sBattleModsWhy.empty() ? "" : "; ") + std::string("battle effects (") + battle + ": their files change what a battle computes)";
  }
  xd::modreload::Remember(options, result);
  return true;
}

namespace xd::modreload {
std::vector<xd::hd::Pack> TexturePacksOf(const engine::mods::LoadResult& result, const engine::mods::LoadOptions& options) {
  namespace em = engine::mods;
  std::vector<em::Message> ignored;
  const std::vector<em::ModFolder> found = em::FindMods(options.modsFolder, ignored);
  std::vector<xd::hd::Pack> packs;
  for (const em::Manifest& m : result.active) {
    for (const em::ModFolder& f : found) {
      if (f.manifest.id != m.id) continue;
      if (f.texturePack) {
        packs.push_back({f.folder, f.manifest.name, f.looseFiles});
      } else {
        for (const std::filesystem::path& t : em::LayoutFolders(f.folder, options.disc.gameId, "textures")) {
          packs.push_back({t, f.manifest.name + " (textures)", false});
        }
      }
    }
  }
  return packs;
}
}

extern "C" void PortXD_SetActiveMods(const char* scriptsText) {
  sActive = sActiveBase;
  if (scriptsText != nullptr && scriptsText[0] != '\0') sActive += (sActive.empty() ? "" : "; ") + std::string(scriptsText);
}

extern "C" bool PortXD_BattleModelsFound(void) {
  static int found = -1;
  if (found < 0) {
    std::error_code ec;
    found = std::filesystem::is_directory(xd::SettingsFolder() / L"extra_assets" / xd::battle_models::kLayerId, ec) ? 1 : 0;
  }
  return found == 1;
}

extern "C" const char* PortXD_ExtraAssetsText(void) { return sExtraText.c_str(); }

extern "C" const char* PortXD_ActiveMods(void) { return sActive.c_str(); }
extern "C" const char* PortXD_BattleMods(void) { return sBattleMods.c_str(); }
extern "C" const char* PortXD_BattleModsWhy(void) { return sBattleModsWhy.c_str(); }
