#include "data/xd_data_mods.h"
#include "xd_user_folder.h"
#include "xd_playthrough_files.h"
#include "xd_randomizer.h"

#include <mods.h>
#include <mods_disc.h>
#include <port_disc.h>
#include <port_message.h>
#include <sdk_port.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::pt {
bool BootWorldMods(std::vector<std::string>& ids);
std::string BootRandomizer();
void RandomizerMade(const std::string& code);
void AfterModsLoaded(const engine::mods::LoadResult& result, const std::filesystem::path& modsFolder,
                     const std::filesystem::path& listFile, const engine::mods::DiscView& disc);
}

namespace {
std::string sActive;

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

extern "C" bool PortXD_LoadMods(void) {
  namespace em = engine::mods;
  const char* off = std::getenv("XD_MODS");
  if (off != nullptr && std::strcmp(off, "0") == 0) {
    std::fprintf(stderr, "[mods] XD_MODS=0: no mods\n");
    return true;
  }
  const std::filesystem::path user = xd::SettingsFolder();
  em::LoadOptions options;
  options.modsFolder = user / L"mods";
  options.listFile = user / em::kListFile;
  const bool playerFilesReadOnly = xd::PlayerFilesReadOnly();
  options.listReadOnly = playerFilesReadOnly;
  options.disc = em::OpenDiscView();
  options.dataCatalog = xd::data::BuildDataCatalog;
  options.looksOrSound = xd::playthrough::LooksOrSound;
  options.usePlaythroughMods = xd::pt::BootWorldMods(options.playthroughMods);
  const std::string randomizerCode = xd::pt::BootRandomizer();
  if (!randomizerCode.empty()) {
    options.generateData = [randomizerCode](const em::DataCatalog& catalog, const em::EntryReader& read,
                                            std::vector<em::DataModFile>& out, std::string& error) {
      namespace xr = xd::randomizer;
      engine::randomizer::Settings s;
      xr::ApplyStyle(s, xr::Style::Classic);
      bool seedOnly = false;
      if (!engine::randomizer::DecodeCode(xr::GetSchema(), randomizerCode, s, seedOnly, error)) return false;
      xr::Result r;
      if (!xr::Generate(catalog, read, s, r, error)) return false;
      out.push_back({xr::kModId, "Randomizer", "data/randomizer.json", r.mod.ToJson("randomizer " + r.code)});
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

  const em::LoadResult result = em::Load(options);
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
    PortMessage_Show(PORT_MESSAGE_WARNING, "Pokemon XD - mods",
                     ("Please check your mods:\n\n" + result.Report() +
                      "\n\nThe game starts with the mods that can be used. Mods are in " + std::string(reinterpret_cast<const char*>(
                                                                             options.modsFolder.u8string().c_str())) +
                      ", the list of mods that are on in mods.ini next to it.")
                         .c_str());
  }
  if (!result.program.empty() && !ApplyProgramChanges(result.program)) {
    std::fprintf(stderr, "[mods] problem: the data mods' changes to main.dol's tables could not be used\n");
    PortMessage_Show(PORT_MESSAGE_WARNING, "Pokemon XD - mods",
                     "The mods' changes to the game program's tables could not be used. The game starts without mods.");
    PortDisc_SetOverlay(nullptr, 0);
    return false;
  }
  if (!em::InstallOverlay(result)) {
    PortMessage_Show(PORT_MESSAGE_WARNING, "Pokemon XD - mods",
                     "The mods' files could not be prepared (was a mod changed while the game started?). "
                     "The game starts without mods.");
    return false;
  }
  sActive = result.ActiveText();
  return true;
}

extern "C" const char* PortXD_ActiveMods(void) { return sActive.c_str(); }
