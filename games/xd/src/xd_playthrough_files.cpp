#include "xd_playthrough_files.h"

#include "xd_user_folder.h"

#include <gc_memcard.h>
#include <save_backups.h>
#include <saveext.h>
#include <xd_save.h>

#include <algorithm>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace xd::playthrough {

namespace fs = std::filesystem;

fs::path CardFolder() {
  const fs::path root = xd::CardRoot();
  std::error_code ec;
  if (fs::is_directory(root / L"USA" / L"Card A", ec)) {
    return root / L"USA" / L"Card A";
  }
  for (const auto& e : fs::directory_iterator(root, ec)) {
    const fs::path name = e.path().filename();
    if (name == L"save_backups" || name == L"playthroughs") continue;
    if (e.is_directory(ec) && fs::is_directory(e.path() / L"Card A", ec)) {
      return e.path() / L"Card A";
    }
  }
  return root / L"USA" / L"Card A";
}

fs::path SetupFolder() { return xd::CardRoot() / L"playthroughs" / L"Card A"; }

SaveId SaveIdOf(const std::vector<uint8_t>& file) {
  namespace xs = xd::save;
  SaveId id;
  xs::Save save;
  xs::ReadReport report;
  if (!xs::ReadSave(file, save, report, id.error)) {
    return id;
  }
  id.found = true;
  id.trainer = xs::ToUtf8(save.trainerName);
  id.trainerId = save.TrainerId();
  id.secretId = save.SecretId();
  uint32_t hash = 2166136261u;
  for (unsigned char c : id.trainer) {
    hash = (hash ^ c) * 16777619u;
  }
  char key[40];
  std::snprintf(key, sizeof key, "pt_%08x_%08x", save.trainerOtId, hash);
  id.key = key;
  return id;
}

SaveId SaveIdOnCard() {
  namespace mc = engine::memcard;
  SaveId none;
  const fs::path folder = CardFolder();
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    none.error = "no memory card yet";
    return none;
  }
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (e.is_regular_file(ec)) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& p : files) {
    std::ifstream in(p, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<mc::CardFile> cardFiles;
    mc::CardImageInfo info;
    std::string error;
    if (!mc::LoadCardFiles(bytes, cardFiles, info, error)) continue;
    for (const mc::CardFile& f : cardFiles) {
      if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) {
        SaveId id = SaveIdOf(f.data);
        id.file = p;
        return id;
      }
    }
  }
  none.error = "no Pokemon XD save on the memory card";
  return none;
}

bool RemapCardSave(const SaveId& id, const xd::save::IdRemap& map, size_t& changed, std::string& error) {
  namespace mc = engine::memcard;
  changed = 0;
  if (!id.found || id.file.empty()) {
    error = "no save on the card";
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(id.file.string(), bytes)) {
    error = "the save file can't be read";
    return false;
  }
  mc::CardFile gci;
  if (!mc::ReadGci(bytes, gci, error)) {
    error = "the save is not a .gci file (a card image can't be adjusted: " + error + ")";
    return false;
  }
  const std::vector<uint8_t> dataBefore = gci.data;
  if (!xd::save::RemapIds(gci.data, map, changed, error)) return false;
  if (changed == 0) return true;
  const std::vector<uint8_t> out = mc::MakeGci(gci);
  std::string backupError;
  const fs::path backups = mc::backups::FolderFor(xd::CardRoot());
  if (mc::backups::Add(backups, "Card A", id.file.filename().string(), bytes, std::time(nullptr), 10, &backupError).empty() && !backupError.empty()) {
    error = "no backup could be made first (" + backupError + "); the card was not changed";
    changed = 0;
    return false;
  }
  const fs::path tmp = id.file.wstring() + L".remap.tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
      error = "the new save file could not be written; the card was not changed";
      changed = 0;
      std::error_code ec;
      fs::remove(tmp, ec);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(tmp, id.file, ec);
  if (ec) {
    fs::remove(id.file, ec);
    fs::rename(tmp, id.file, ec);
  }
  if (ec) {
    error = "the new save file could not be put in place: " + ec.message();
    changed = 0;
    return false;
  }
  KeepExtensionPaired(dataBefore, gci.data);
  return true;
}

fs::path SetupFileFor(const SaveId& id) { return SetupFolder() / (id.key + ".json"); }

bool LooksOrSound(const std::string& path, int fsysType) {
  if (fsysType == -1) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".samp" || ext == ".thp";
  }
  switch (fsysType) {
  case 6:
  case 9:
  case 17:
  case 20:
  case 21:
  case 22:
  case 23:
  case 25:
    return true;
  default:
    return false;
  }
}

bool KeepExtensionPaired(const std::vector<uint8_t>& oldFile, const std::vector<uint8_t>& newFile) {
  const SaveId id = SaveIdOf(oldFile);
  if (!id.found) return true;
  const fs::path folder = SetupFolder();
  std::error_code ec;
  if (!fs::exists(engine::saveext::FileFor(folder, id.key), ec)) return true;
  return engine::saveext::Repair(folder, id.key, oldFile, newFile);
}

}
