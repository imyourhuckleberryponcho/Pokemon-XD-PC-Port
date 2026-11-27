#include "online_team.h"

#include "xd_playthrough_files.h"

#include <playthrough.h>

#include <filesystem>
#include <system_error>

namespace xd::online {

namespace fs = std::filesystem;

std::vector<std::string> EditorOrdersOfCardRoot(const fs::path& cardRoot, const std::vector<uint8_t>& saveFileData) {
  const xd::playthrough::SaveId id = xd::playthrough::SaveIdOf(saveFileData);
  if (!id.found) return {};
  engine::playthrough::Setup setup;
  std::string error;
  const fs::path file = engine::playthrough::FileFor(cardRoot / L"playthroughs" / L"Card A", id.key);
  std::error_code ec;
  if (!fs::exists(file, ec) || !engine::playthrough::ReadFile(file, setup, error)) return {};
  return setup.editorShadows;
}

std::vector<std::string> EditorOrdersBesideSave(const fs::path& savePath, const std::vector<uint8_t>& saveFileData) {
  std::error_code ec;
  fs::path dir = savePath.parent_path();
  for (int up = 0; up < 5 && !dir.empty(); up++, dir = dir.parent_path()) {
    if (fs::is_directory(dir / L"playthroughs", ec)) return EditorOrdersOfCardRoot(dir, saveFileData);
  }
  return {};
}

}
