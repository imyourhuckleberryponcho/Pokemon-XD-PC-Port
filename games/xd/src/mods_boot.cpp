#include "data/xd_data_mods.h"
#include "i18n.h"
#include "xd_battle_models.h"
#include "xd_user_folder.h"
#include "xd_xg.h"
#include "xd_playthrough_files.h"
#include "xd_randomizer.h"
#include "xd_starter.h"
#include "xd_added_rows.h"
#include "xd_gen4.h"
#include "xd_modern_types.h"
#include "xd_languages.h"
#include "xd_scripts.h"

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

namespace xd::hd {
struct Pack {
  std::filesystem::path folder;
  std::string name;
  bool loose = false;
};
void SetPacks(std::vector<Pack> packs);
}

namespace xd::pt {
bool BootWorldMods(std::vector<std::string>& ids);
std::string BootRandomizer();
bool BootGen4();
bool BootGen4Spots();
bool BootModernTypes();
void RandomizerMade(const std::string& code);
void AfterModsLoaded(const engine::mods::LoadResult& result, const std::filesystem::path& modsFolder,
                     const std::filesystem::path& listFile, const engine::mods::DiscView& disc);
}

namespace {
std::string sActive;
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
  const std::filesystem::path modelsFolder = user / L"extra_assets" / xd::battle_models::kLayerId;
  bool modelsOn = false;
  {
    const engine::settings::BoolSetting models = engine::settings::Global().Bool(xd::battle_models::kSetting);
    modelsOn = models.Valid() && models.Get();
    if (modelsOn) {
      options.extraLayers.push_back({xd::battle_models::kLayerId, "Battle Revolution models", modelsFolder});
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
      for (em::DataTable& t : out.tables) t.addable = false;
      std::fprintf(stderr, "[mods] data mods: no added rows in this edition (%s)\n", why);
    }
    return true;
  };
  options.looksOrSound = xd::playthrough::LooksOrSound;
  options.usePlaythroughMods = xd::pt::BootWorldMods(options.playthroughMods);
  const std::string randomizerCode = xd::pt::BootRandomizer();
  xd::starter::SetPlan({});
  std::vector<em::DataModFile> gen4Files;
  xd::gen4::Prepare(xd::pt::BootGen4(), xd::pt::BootGen4Spots(), options, gen4Files);
  xd::data::SetFlagRowsTaken(xd::gen4::ExtraRowCount("flag_data"));
  xd::modern::Prepare(xd::pt::BootModernTypes(), xd::gen4::Loaded(), gen4Files);
  {
    std::string layers = "base disc";
    if (modelsOn) layers += " > Battle Revolution models";
    if (std::strcmp(xd::languages::Active()->id, "us") != 0) layers += std::string(" > Languages (") + xd::languages::Active()->name + ")";
    if (xd::gen4::Loaded()) layers += " > Gen 4 pack";
    if (xd::gen4::SpotsLoaded()) layers += " > Gen 4 Poke Spots";
    if (xd::modern::Loaded()) layers += " > Modern types (Fairy)";
    if (!randomizerCode.empty()) layers += " > Randomizer " + randomizerCode;
    std::fprintf(stderr, "[mods] built-in layers: %s (user mods are applied before them; conflicts are reported only for user mods)\n",
                 layers.c_str());
  }
  if (!randomizerCode.empty() || !gen4Files.empty()) {
    options.generateData = [randomizerCode, gen4Files](const em::DataCatalog& catalog, const em::EntryReader& read,
                                                       std::vector<em::DataModFile>& out, std::string& error) {
      namespace xr = xd::randomizer;
      for (const em::DataModFile& f : gen4Files) out.push_back(f);
      if (randomizerCode.empty()) return true;
      engine::randomizer::Settings s;
      xr::ApplyStyle(s, xr::Style::Classic);
      bool seedOnly = false;
      if (!engine::randomizer::DecodeCode(xr::GetSchema(), randomizerCode, s, seedOnly, error)) return false;
      if (xd::xg::Off(xd::xg::Feature::Gen4) != nullptr) s.Set(xr::GetSchema(), "gen4", 0);
      if (xr::CodeIncludesGen4(randomizerCode) && xd::xg::Off(xd::xg::Feature::Gen4) == nullptr) {
        if (!xd::gen4::Loaded()) {
          error = "this randomizer code includes the Gen 4 Pokemon, but their files are not available (made from your Pokemon "
                  "Battle Revolution disc on the launcher's Extra assets page)";
          return false;
        }
        if (!xr::LoadGen4Species(xd::gen4::PackFolder() / L"randomizer.txt", error)) return false;
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
      return true;
    };
  }
  std::error_code ec;
  if (!playerFilesReadOnly) {
    std::filesystem::create_directories(options.modsFolder, ec);
  }

  em::LoadResult result = em::Load(options);
  MergeLanguageToc(result);
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
                           result.Report().c_str(), reinterpret_cast<const char*>(options.modsFolder.u8string().c_str()))
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
  {
    std::vector<em::Message> ignored;
    const std::vector<em::ModFolder> found = em::FindMods(options.modsFolder, ignored);
    std::vector<xd::hd::Pack> packs;
    for (const em::Manifest& m : result.active) {
      for (const em::ModFolder& f : found) {
        if (f.manifest.id != m.id) continue;
        std::error_code tec;
        if (f.texturePack) {
          packs.push_back({f.folder, f.manifest.name, f.looseFiles});
        } else {
          for (const std::filesystem::path& t : em::LayoutFolders(f.folder, options.disc.gameId, "textures")) {
            packs.push_back({t, f.manifest.name + " (textures)", false});
          }
        }
      }
    }
    xd::hd::SetPacks(std::move(packs));
  }
  std::fprintf(stderr, "[mods] in use: %s\n", sActive.empty() ? "none" : sActive.c_str());
  const em::LoadResult::LayerResult* modelsLayer = nullptr;
  for (const auto& layer : result.extraLayers) {
    if (layer.id == xd::battle_models::kLayerId) modelsLayer = &layer;
  }
  if (!modelsOn) {
    sExtraText = "Battle Revolution models: off";
  } else if (modelsLayer == nullptr || !modelsLayer->found) {
    sExtraText = "Battle Revolution models: on, but not made yet (no folder " + modelsFolder.string() + ")";
  } else if (modelsLayer->files == 0) {
    sExtraText = "Battle Revolution models: on, but its folder has no files";
  } else {
    sExtraText = "Battle Revolution models: on, " + std::to_string(modelsLayer->files) + " files";
  }
  sExtraText += std::string("; ") + PortXD_LanguageText();
  for (const auto& layer : result.extraLayers) {
    if (layer.id == "languages") sExtraText += " (" + std::to_string(layer.files) + " files from the pack)";
  }
  std::fprintf(stderr, "[extra assets] %s\n", sExtraText.c_str());
  xd::gen4::AfterLoad(result);
  xd::added::Install(result);
  xd::scripts::Install(result);
  if (const std::string scripts = xd::scripts::ActiveText(); !scripts.empty()) {
    sActive += (sActive.empty() ? "" : "; ") + scripts;
  }
  return true;
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
