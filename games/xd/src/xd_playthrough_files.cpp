#include "xd_playthrough_files.h"

#include "xd_user_folder.h"

#include <gc_memcard.h>
#include <xd_save.h>

#include <algorithm>
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

}
