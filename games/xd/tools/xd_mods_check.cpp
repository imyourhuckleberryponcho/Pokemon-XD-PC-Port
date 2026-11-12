#include "../src/data/xd_data_mods.h"
#include "../src/dol_tables.h"
#include "../src/xd_user_folder.h"

#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <gs_formats.h>
#include <mods.h>
#include <mods_disc.h>
#include <port_disc.h>
#include <port_user_folder.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace em = engine::mods;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

std::vector<uint8_t> read_local(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool read_dvd(s32 entry, std::vector<uint8_t>& out) {
  DVDFileInfo info{};
  if (entry < 0 || !DVDFastOpen(entry, &info)) {
    return false;
  }
  out.assign(info.length, 0);
  const s32 n = info.length != 0 ? DVDReadPrio(&info, out.data(), static_cast<s32>(info.length), 0, 2) : 0;
  DVDClose(&info);
  return n == static_cast<s32>(info.length);
}

std::vector<uint8_t> extract(const std::vector<uint8_t>& archive, uint32_t index) {
  FsysArchive f{};
  uint32_t n = 0;
  uint8_t* p = Fsys_Open(archive.data(), archive.size(), &f) ? Fsys_Extract(&f, index, &n) : nullptr;
  std::vector<uint8_t> out(p, p + (p != nullptr ? n : 0));
  std::free(p);
  return out;
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [mods folder] [mod list file]\n", argv[0]);
    return 2;
  }
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  const fs::path user = xd::SettingsFolder();
  em::LoadOptions options;
  options.listReadOnly = true;
  options.modsFolder = argc >= 3 ? fs::path(argv[2]) : user / L"mods";
  options.listFile = argc >= 4 ? fs::path(argv[3]) : user / em::kListFile;
  options.disc = em::OpenDiscView();
  options.dataCatalog = xd::data::BuildDataCatalog;
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  std::printf("disc %s, mods in %s, list %s\n", options.disc.gameId.c_str(), options.modsFolder.string().c_str(),
              options.listFile.string().c_str());

  const em::LoadResult result = em::Load(options);
  if (result.listWriteSkipped) {
    std::printf("[mods] the list file is left as it is (this tool only reads)\n");
  }
  for (const em::Message& m : result.messages) {
    static const char* const kLabel[] = {"", "warning: ", "problem: "};
    std::printf("[mods] %s%s\n", kLabel[static_cast<int>(m.level)], m.text.c_str());
  }
  std::printf("mods on: %s\n", result.active.empty() ? "none" : result.ActiveText().c_str());

  check(aurora_dvd_open(argv[1]), "Aurora opens the disc");
  std::vector<s32> before;
  for (const em::Replacement& r : result.files) {
    before.push_back(DVDConvertPathToEntrynum(("/" + r.discPath).c_str()));
  }
  check(em::InstallOverlay(result), "the overlay takes the mods' files");
  PortDisc_InstallOverlayInAurora();

  for (size_t i = 0; i < result.files.size(); i++) {
    const em::Replacement& r = result.files[i];
    const std::vector<uint8_t> expected = r.rebuilt ? r.data : read_local(r.file);
    const s32 entry = DVDConvertPathToEntrynum(("/" + r.discPath).c_str());
    if (r.added) {
      check(before[i] < 0 && entry >= 0, r.discPath + ": added to the game's file table");
    } else {
      check(entry == before[i] && entry >= 0, r.discPath + ": keeps its file number (" + std::to_string(entry) + ")");
    }
    std::vector<uint8_t> got;
    check(read_dvd(entry, got) && got == expected,
          r.discPath + ": the game's DVD reads give the mod's " + std::to_string(expected.size()) + " bytes (from " + r.from + ")");
    size_t size = 0;
    uint8_t* port = PortDisc_ReadFile(r.discPath.c_str(), &size);
    check(port != nullptr && std::vector<uint8_t>(port, port + size) == expected, r.discPath + ": so does PortDisc_ReadFile");
    PortDisc_Free(port);

    if (r.rebuilt && !r.added) {
      std::vector<uint8_t> base;
      if (!r.file.empty()) {
        base = read_local(r.file);
      } else {
        options.disc.read(r.discPath, base);
      }
      FsysArchive a{}, b{};
      const bool opened = Fsys_Open(got.data(), got.size(), &a) && Fsys_Open(base.data(), base.size(), &b);
      check(opened && a.count == b.count, r.discPath + ": still an FSYS archive with all " + std::to_string(b.count) + " entries");
      uint32_t asBefore = 0, fromMod = 0, wrong = 0;
      for (uint32_t k = 0; k < a.count && k < b.count; k++) {
        const fs::path* modFile = nullptr;
        for (const auto& [index, file] : r.entries) {
          modFile = index == k ? &file : modFile;
        }
        const std::vector<uint8_t>* made = nullptr;
        for (const auto& [index, data] : r.generated) {
          made = index == k ? &data : made;
        }
        const std::vector<uint8_t> now = extract(got, k);
        const bool fromAMod = modFile != nullptr || made != nullptr;
        if (made != nullptr ? now == *made : modFile != nullptr ? now == read_local(*modFile) : now == extract(base, k)) {
          (fromAMod ? fromMod : asBefore)++;
        } else {
          wrong++;
          std::printf("     entry #%u is not what it should be\n", k);
        }
      }
      check(wrong == 0 && fromMod == r.entries.size() + r.generated.size(),
            r.discPath + ": " + std::to_string(fromMod) + " entries hold the mod's files, the other " +
                std::to_string(asBefore) + " are unchanged");
    }
  }
  std::printf(sOk ? "xd_mods_check: all passed\n" : "xd_mods_check: FAILED\n");
  aurora_dvd_close();
  PortDisc_Close();
  return sOk ? 0 : 1;
}
