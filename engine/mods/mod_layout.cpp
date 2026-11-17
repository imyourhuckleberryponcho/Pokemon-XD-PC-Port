#include "mod_layout.h"

#include "i18n.h"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>

namespace engine::mods {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  return out;
}

std::string Upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  return out;
}

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

struct KnownGame {
  const char* code;
  const char* name;
  const char* shortName;
};
const KnownGame kKnown[] = {{"GXX", "Pokemon XD: Gale of Darkness", "XD"}, {"GC6", "Pokemon Colosseum", "Colosseum"}};

fs::path GameFolderPath(const fs::path& modFolder, const std::string& upperName) {
  std::error_code ec;
  const fs::path games = modFolder / kGamesFolder;
  for (fs::directory_iterator it(games, ec), end; !ec && it != end; it.increment(ec)) {
    if (Upper(Utf8(it->path().filename())) == upperName) return it->path();
  }
  return games / upperName;
}

}

bool ValidGameFolderName(std::string_view name) {
  if (name.size() != 3 && name.size() != 4 && name.size() != 6) return false;
  for (char c : name) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

bool GameCodeMatches(std::string_view code, std::string_view discId) {
  if (code.empty() || code.size() > discId.size()) return false;
  return Lower(discId.substr(0, code.size())) == Lower(code);
}

bool SupportsGame(const Manifest& manifest, std::string_view discId) {
  if (discId.empty()) return true;
  return std::any_of(manifest.games.begin(), manifest.games.end(), [&](const std::string& g) { return GameCodeMatches(g, discId); });
}

bool SplitGameFolder(std::string_view rel, std::string& folder, std::string& rest) {
  const std::string_view prefix = kGamesFolder;
  if (rel.size() <= prefix.size() + 1 || Lower(rel.substr(0, prefix.size())) != prefix || rel[prefix.size()] != '/') return false;
  rel.remove_prefix(prefix.size() + 1);
  const size_t slash = rel.find('/');
  folder = Upper(rel.substr(0, slash));
  rest = slash == std::string_view::npos ? std::string() : std::string(rel.substr(slash + 1));
  return !folder.empty();
}

std::string StripGameFolder(std::string_view rel) {
  std::string folder, rest;
  if (SplitGameFolder(rel, folder, rest) && !rest.empty()) return rest;
  return std::string(rel);
}

std::vector<std::string> GameFolderNames(const fs::path& modFolder) {
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path games = modFolder / kGamesFolder;
  if (!fs::is_directory(games, ec)) return out;
  for (fs::directory_iterator it(games, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_directory(e2)) continue;
    const std::string name = Upper(Utf8(it->path().filename()));
    if (ValidGameFolderName(name)) out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<fs::path> ContentRoots(const fs::path& modFolder, std::string_view discId) {
  std::vector<fs::path> roots{modFolder};
  std::vector<std::string> names = GameFolderNames(modFolder);
  if (discId.empty()) {
    for (const std::string& n : names) roots.push_back(GameFolderPath(modFolder, n));
    return roots;
  }
  std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
  for (const std::string& n : names) {
    if (GameCodeMatches(n, discId)) roots.push_back(GameFolderPath(modFolder, n));
  }
  return roots;
}

std::vector<LayoutFile> LayoutFiles(const fs::path& modFolder, std::string_view discId, std::string_view sub, bool recursive) {
  std::vector<std::vector<LayoutFile>> perRoot;
  const std::vector<fs::path> roots = ContentRoots(modFolder, discId);
  for (size_t r = 0; r < roots.size(); r++) {
    std::vector<LayoutFile> files;
    std::error_code ec;
    const fs::path folder = roots[r] / std::string(sub);
    if (fs::is_directory(folder, ec)) {
      const std::string from = r == 0 ? std::string() : Upper(Utf8(roots[r].filename()));
      const auto add = [&](const fs::directory_entry& e) {
        std::error_code e2;
        if (!e.is_regular_file(e2)) return;
        const std::u8string generic = fs::relative(e.path(), folder, e2).generic_u8string();
        files.push_back({std::string(reinterpret_cast<const char*>(generic.data()), generic.size()), e.path(), from});
      };
      if (recursive) {
        for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) add(*it);
      } else {
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) add(*it);
      }
    }
    std::sort(files.begin(), files.end(), [](const LayoutFile& a, const LayoutFile& b) { return a.rel < b.rel; });
    perRoot.push_back(std::move(files));
  }
  std::set<std::string> later;
  std::vector<LayoutFile> out;
  for (size_t r = perRoot.size(); r-- > 0;) {
    std::vector<LayoutFile> keep;
    for (LayoutFile& f : perRoot[r]) {
      if (later.insert(Lower(f.rel)).second) keep.push_back(std::move(f));
    }
    out.insert(out.begin(), std::make_move_iterator(keep.begin()), std::make_move_iterator(keep.end()));
  }
  return out;
}

bool HasLayoutFolder(const fs::path& modFolder, std::string_view discId, std::string_view sub) {
  return !LayoutFolders(modFolder, discId, sub).empty();
}

std::vector<fs::path> LayoutFolders(const fs::path& modFolder, std::string_view discId, std::string_view sub) {
  std::vector<fs::path> out;
  for (const fs::path& root : ContentRoots(modFolder, discId)) {
    std::error_code ec;
    if (fs::is_directory(root / std::string(sub), ec)) out.push_back(root / std::string(sub));
  }
  return out;
}

bool ResolveLayoutFile(const fs::path& modFolder, std::string_view discId, std::string_view rel, fs::path& out) {
  const fs::path p{std::string(rel)};
  if (rel.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory() || rel.find("..") != std::string_view::npos) return false;
  const std::vector<fs::path> roots = ContentRoots(modFolder, discId);
  for (size_t r = roots.size(); r-- > 0;) {
    std::error_code ec;
    if (fs::is_regular_file(roots[r] / p, ec)) {
      out = roots[r] / p;
      return true;
    }
  }
  return false;
}

bool HasSharedContent(const fs::path& modFolder) {
  std::error_code ec;
  for (fs::directory_iterator it(modFolder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_directory(e2)) continue;
    const std::string name = Lower(Utf8(it->path().filename()));
    if (name == kGamesFolder || name == "docs" || name == "images" || name == "screenshots") continue;
    for (fs::recursive_directory_iterator f(it->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
      std::error_code e3;
      if (f->is_regular_file(e3)) return true;
    }
  }
  return false;
}

std::string GameNameOf(std::string_view code, bool shortForm) {
  for (const KnownGame& g : kKnown) {
    if (GameCodeMatches(g.code, code) || GameCodeMatches(code, g.code)) return shortForm ? g.shortName : g.name;
  }
  return std::string();
}

std::vector<GameSupport> SupportOf(const fs::path& modFolder, const Manifest& manifest) {
  std::vector<GameSupport> out;
  const std::vector<std::string> folders = GameFolderNames(modFolder);
  const bool layered = !folders.empty();
  const bool shared = HasSharedContent(modFolder);
  for (const std::string& code : manifest.games) {
    GameSupport s;
    s.code = code;
    s.name = GameNameOf(code, false);
    s.shortName = GameNameOf(code, true);
    for (const std::string& f : folders) {
      if (GameCodeMatches(f, code) || GameCodeMatches(code, f)) s.folders.push_back(f);
    }
    if (!layered || !s.folders.empty()) {
      s.level = GameSupport::Level::Full;
    } else {
      s.level = shared ? GameSupport::Level::SharedOnly : GameSupport::Level::Nothing;
    }
    out.push_back(std::move(s));
  }
  return out;
}

std::string SupportText(const std::vector<GameSupport>& support, bool translate) {
  using engine::i18n::Tr;
  const auto say = [&](const char* text) { return translate ? std::string(Tr(text)) : std::string(text); };
  std::string out;
  std::set<std::string> done;
  for (const GameSupport& s : support) {
    const std::string name = !s.shortName.empty() ? s.shortName : s.code;
    if (!done.insert(name + "|" + std::to_string(static_cast<int>(s.level))).second) continue;
    if (!out.empty()) out += "; ";
    out += name;
    if (s.level == GameSupport::Level::SharedOnly) out += ": " + say("shared parts only");
    if (s.level == GameSupport::Level::Nothing) out += ": " + say("nothing for it in this mod");
  }
  return out;
}

}
