#include "mods.h"

#include <hd_textures.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>

namespace engine::mods {
namespace fs = std::filesystem;

namespace {
std::mutex sMutex;
fs::path sRoot;
std::string sGameId;
std::vector<std::string> sAlso;

std::string Utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

std::string IdFor(const std::string& folderName, std::set<std::string>& used) {
  std::string id = "texpack-";
  for (char c : folderName) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    id += ok ? c : '_';
  }
  if (id.size() > 60) id.resize(60);
  std::string unique = id;
  for (int n = 2; !used.insert(unique).second; n++) unique = id + "-" + std::to_string(n);
  return unique;
}

bool HasPicturesDirectly(const fs::path& folder) {
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    std::string ext = Utf8(it->path().extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((ext == ".png" || ext == ".dds") && hdtex::ParseName(Utf8(it->path().stem()))) return true;
  }
  return false;
}

std::string Describe(const hdtex::PackInfo& info, bool loose) {
  if (loose) return "HD texture pack (pictures only, made for Dolphin): the pictures lying loose in the texture folder.";
  std::string text = "HD texture pack (pictures only, made for Dolphin): " + std::to_string(info.textures) + " pictures";
  if (info.mipFiles != 0) text += ", " + std::to_string(info.mipFiles) + " mipmap files";
  return text + ".";
}
}

void SetTexturePackSource(const fs::path& root, std::string_view gameId, const std::vector<std::string>& alsoTargets) {
  std::lock_guard lk(sMutex);
  sRoot = root;
  sGameId = std::string(gameId);
  sAlso = alsoTargets;
}

std::vector<ModFolder> FindTexturePacks() {
  fs::path root;
  std::string gameId;
  std::vector<std::string> also;
  {
    std::lock_guard lk(sMutex);
    root = sRoot;
    gameId = sGameId;
    also = sAlso;
  }
  std::vector<ModFolder> packs;
  if (root.empty() || gameId.empty()) return packs;
  std::set<std::string> usedIds;
  const std::string id3 = gameId.substr(0, 3);
  const auto add = [&](const fs::path& folder, const std::string& name, bool loose) {
    ModFolder m;
    const hdtex::PackInfo info = hdtex::ScanPack(folder);
    m.manifest.id = IdFor(name, usedIds);
    m.manifest.name = "Textures: " + name;
    m.manifest.description = Describe(info, loose);
    m.manifest.games = {gameId};
    m.manifest.games.insert(m.manifest.games.end(), also.begin(), also.end());
    m.manifest.applies = Applies::AnyTime;
    m.manifest.appliesGiven = true;
    m.folder = folder;
    m.folderName = Utf8(folder.filename());
    m.texturePack = true;
    m.looseFiles = loose;
    packs.push_back(std::move(m));
  };
  for (const fs::path& folder : hdtex::FindPackFolders(root, gameId)) {
    const std::string name = Utf8(folder.filename());
    const bool main = folder.parent_path() == root && (name == gameId || name == id3);
    if (!main) {
      if (hdtex::ScanPack(folder).textures != 0) add(folder, name, false);
      continue;
    }
    if (HasPicturesDirectly(folder)) add(folder, name, true);
    std::vector<fs::path> subs;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      if (it->is_directory(e2)) subs.push_back(it->path());
    }
    std::sort(subs.begin(), subs.end());
    for (const fs::path& sub : subs) {
      if (hdtex::ScanPack(sub).textures != 0) add(sub, Utf8(sub.filename()), false);
    }
  }
  return packs;
}

}
