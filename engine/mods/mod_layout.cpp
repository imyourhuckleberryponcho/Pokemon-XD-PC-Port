#include "mod_layout.h"

#include "i18n.h"

#include <algorithm>
#include <fstream>
#include <iterator>
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
  const char* edition;
  const char* name;
  const char* shortName;
};
const KnownGame kKnown[] = {{"GXX", "", "Pokemon XD: Gale of Darkness", "XD"},
                            {"GXX", "xg", "Pokemon XG", "XG"},
                            {"GC6", "", "Pokemon Colosseum", "Colosseum"}};

struct GameAlias {
  const char* name;
  const char* code;
  const char* edition;
};
const GameAlias kAliases[] = {{"xd", "GXX", ""}, {"xg", "GXX", "xg"}, {"colosseum", "GC6", ""}};

struct Code {
  std::string base;
  std::string edition;
};
Code ParseCode(std::string_view in) {
  std::string s(in), edition;
  const size_t hash = s.find('#');
  if (hash != std::string::npos) {
    edition = Lower(s.substr(hash + 1));
    s.resize(hash);
  }
  const std::string lower = Lower(s);
  for (const GameAlias& a : kAliases) {
    if (lower == a.name) return {a.code, a.edition};
  }
  return {s, edition};
}

std::string Canon(std::string_view code) { return ParseCode(code).base; }

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
  for (const GameAlias& a : kAliases) {
    if (Upper(a.name) == name) return true;
  }
  if (name.size() != 3 && name.size() != 4 && name.size() != 6) return false;
  for (char c : name) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

std::string MakeTarget(std::string_view discId, std::string_view edition) {
  std::string out(discId);
  if (!edition.empty() && Lower(edition) != "xd") out += "#" + Lower(edition);
  return out;
}

bool GameCodeMatches(std::string_view codeIn, std::string_view discIdIn) {
  const Code code = ParseCode(codeIn), disc = ParseCode(discIdIn);
  if (code.base.empty() || code.base.size() > disc.base.size() || code.edition != disc.edition) return false;
  return Lower(std::string_view(disc.base).substr(0, code.base.size())) == Lower(code.base);
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

std::vector<std::string> ReadGameCodes(const fs::path& modFolder) {
  std::vector<std::string> out;
  std::ifstream in(modFolder / "mod.json", std::ios::binary);
  if (!in) return out;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Manifest m;
  std::string error;
  if (ParseManifest(text, m, error)) out = m.games;
  return out;
}

bool SupportsGameCodes(const std::vector<std::string>& codes, std::string_view target) {
  return std::any_of(codes.begin(), codes.end(), [&](const std::string& g) { return GameCodeMatches(g, target); });
}

std::vector<fs::path> ContentRoots(const fs::path& modFolder, std::string_view discId) {
  std::vector<fs::path> roots{modFolder};
  std::vector<std::string> names = GameFolderNames(modFolder);
  if (discId.empty()) {
    for (const std::string& n : names) roots.push_back(GameFolderPath(modFolder, n));
    return roots;
  }
  std::stable_sort(names.begin(), names.end(),
                   [](const std::string& a, const std::string& b) { return Canon(a).size() < Canon(b).size(); });
  std::string target(discId);
  bool any = false;
  for (const std::string& n : names) any = any || GameCodeMatches(n, target);
  if (!any) {
    const Code t = ParseCode(discId);
    if (!t.edition.empty()) {
      const std::vector<std::string> codes = ReadGameCodes(modFolder);
      if (SupportsGameCodes(codes, discId) && SupportsGameCodes(codes, t.base)) target = t.base;
    }
  }
  for (const std::string& n : names) {
    if (GameCodeMatches(n, target)) roots.push_back(GameFolderPath(modFolder, n));
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
  const Code c = ParseCode(code);
  for (const KnownGame& g : kKnown) {
    if (g.edition != c.edition) continue;
    const std::string_view known(g.code);
    const size_t n = std::min(known.size(), c.base.size());
    if (n > 0 && Lower(known.substr(0, n)) == Lower(std::string_view(c.base).substr(0, n))) return shortForm ? g.shortName : g.name;
  }
  return std::string();
}

std::string GameTag(const Manifest& manifest) { return GameTag(manifest.games); }

std::string GameTag(const std::vector<std::string>& games) {
  std::vector<std::string> names;
  for (const KnownGame& k : kKnown) {
    const std::string name = k.shortName;
    for (const std::string& g : games) {
      if (GameNameOf(g, true) == name && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
  }
  for (const std::string& g : games) {
    if (GameNameOf(g, true).empty() && std::find(names.begin(), names.end(), g) == names.end()) names.push_back(g);
  }
  std::string out;
  for (const std::string& n : names) out += (out.empty() ? "" : " + ") + n;
  return out;
}

std::string TargetName(std::string_view target) {
  const std::string name = GameNameOf(target, true);
  return name.empty() ? std::string(target) : name;
}

std::string NotForTargetText(const Manifest& manifest, std::string_view target) {
  std::string text = engine::i18n::Tr("Made for %s, not %s.");
  std::string tag = GameTag(manifest), now = TargetName(target);
  const bool sameGame = std::any_of(manifest.games.begin(), manifest.games.end(), [&](const std::string& g) { return GameNameOf(g, true) == now; });
  if (sameGame) {
    tag.clear();
    for (const std::string& g : manifest.games) tag += (tag.empty() ? "" : ", ") + g;
    now = std::string(target);
  }
  size_t at = text.find("%s");
  if (at != std::string::npos) text.replace(at, 2, tag);
  at = text.find("%s");
  if (at != std::string::npos) text.replace(at, 2, now);
  return text;
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
    if (s.folders.empty()) {
      const Code c = ParseCode(code);
      if (!c.edition.empty() && std::any_of(manifest.games.begin(), manifest.games.end(), [&](const std::string& g) { return GameCodeMatches(g, c.base); })) {
        for (const std::string& f : folders) {
          if (GameCodeMatches(f, c.base) || GameCodeMatches(c.base, f)) s.folders.push_back(f);
        }
      }
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
