#include "mods.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace engine::mods {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
bool Same(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

std::vector<std::string> FilesIn(const fs::path& folder) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) return out;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::u8string generic = fs::relative(it->path(), folder, ec).generic_u8string();
    out.emplace_back(reinterpret_cast<const char*>(generic.data()), generic.size());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string Plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

struct Keys {
  std::set<std::string> whole, entries, archives;
};
Keys KeysOf(const ModChanges& c) {
  Keys k;
  for (const std::string& f : c.files) k.whole.insert(Lower(f));
  for (const std::string& e : c.entries) {
    const size_t slash = e.rfind('/');
    if (slash == std::string::npos) continue;
    k.entries.insert(Lower(e));
    k.archives.insert(Lower(e.substr(0, slash)));
  }
  return k;
}

std::vector<std::string> Shared(const Keys& a, const Keys& b) {
  std::vector<std::string> out;
  for (const std::string& w : a.whole) {
    if (b.whole.count(w) != 0 || b.archives.count(w) != 0) out.push_back(w);
  }
  for (const std::string& w : b.whole) {
    if (a.archives.count(w) != 0 && a.whole.count(w) == 0) out.push_back(w);
  }
  for (const std::string& e : a.entries) {
    if (b.entries.count(e) != 0) out.push_back(e);
  }
  return out;
}

std::string FirstOf(const std::vector<std::string>& names) {
  std::string out = names.front();
  if (names.size() > 1) out += " and " + std::to_string(names.size() - 1) + " more";
  return out;
}

}

ModChanges ChangesOf(const ModFolder& mod) {
  ModChanges c;
  c.files = FilesIn(mod.folder / "files");
  c.entries = FilesIn(mod.folder / "fsys");
  for (const std::string& f : FilesIn(mod.folder / "data")) {
    if (Lower(fs::path(f).extension().string()) == ".json") c.dataFiles.push_back(f);
  }
  return c;
}

std::string DescribeChanges(const ModChanges& c) {
  std::string out;
  const auto add = [&](size_t n, const char* one, const char* many) {
    if (n != 0) out += (out.empty() ? "" : ", ") + Plural(n, one, many);
  };
  add(c.files.size(), "game file", "game files");
  add(c.entries.size(), "archive entry", "archive entries");
  add(c.dataFiles.size(), "table file", "table files");
  return out.empty() ? "nothing" : out;
}

Review ReviewList(const ModList::Profile& profile, const std::vector<ModFolder>& found, std::string_view gameId) {
  Review review;
  const auto findMod = [&](const std::string& id) -> const ModFolder* {
    for (const ModFolder& f : found) {
      if (Same(f.manifest.id, id)) return &f;
    }
    return nullptr;
  };
  for (const auto& [id, on] : profile.mods) {
    ReviewRow row;
    row.id = id;
    row.on = on;
    row.mod = findMod(id);
    review.rows.push_back(std::move(row));
  }
  std::vector<size_t> alive;
  for (size_t i = 0; i < review.rows.size(); i++) {
    ReviewRow& row = review.rows[i];
    if (row.mod == nullptr) {
      row.problems.push_back("Not in the mods folder any more (left in the list; it isn't used).");
      continue;
    }
    const std::vector<std::string>& games = row.mod->manifest.games;
    if (!gameId.empty() && std::none_of(games.begin(), games.end(), [&](const std::string& g) { return Same(g, gameId); })) {
      std::string list;
      for (const std::string& g : games) list += (list.empty() ? "" : ", ") + g;
      row.problems.push_back("Made for another game or game version (" + list + "); this disc is " + std::string(gameId) +
                             ". It isn't used.");
      continue;
    }
    if (row.on) alive.push_back(i);
  }
  const auto drop = [&](size_t at, std::string why) {
    review.rows[alive[at]].problems.push_back(std::move(why));
    alive.erase(alive.begin() + static_cast<std::ptrdiff_t>(at));
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t a = 0; a < alive.size() && !changed; a++) {
      const Manifest& m = review.rows[alive[a]].mod->manifest;
      for (size_t k = 0; k < m.required.size() && !changed; k++) {
        const std::string& need = m.required[k];
        const auto it = std::find_if(alive.begin(), alive.end(),
                                     [&](size_t o) { return Same(review.rows[o].mod->manifest.id, need); });
        if (it != alive.end() && k < m.requiredVersions.size() &&
            !m.requiredVersions[k].SatisfiedBy(review.rows[*it].mod->manifest.version)) {
          const VersionNeed& v = m.requiredVersions[k];
          drop(a, "Needs \"" + review.rows[*it].mod->manifest.name + "\" in version " + v.op + " " + v.version + ", but the one in the mods "
                  "folder is " + (review.rows[*it].mod->manifest.version.empty() ? std::string("without a version")
                                                                                   : "version " + review.rows[*it].mod->manifest.version) +
                  ". Not used.");
          changed = true;
        } else if (it == alive.end()) {
          const ModFolder* other = findMod(need);
          drop(a, "Needs the mod \"" + (other != nullptr ? other->manifest.name : need) + "\", which is " +
                      (other != nullptr ? "not on (or can't be used)" : "not in the mods folder") + ". Not used.");
          changed = true;
        }
      }
      for (size_t b = 0; b < a && !changed; b++) {
        const Manifest& earlier = review.rows[alive[b]].mod->manifest;
        const auto names = [](const std::vector<std::string>& ids, const std::string& id) {
          return std::any_of(ids.begin(), ids.end(), [&](const std::string& x) { return Same(x, id); });
        };
        if (names(m.conflicts, earlier.id) || names(earlier.conflicts, m.id)) {
          drop(a, "Can't be used together with \"" + earlier.name + "\" (" + (names(m.conflicts, earlier.id) ? m.name : earlier.name) +
                      " says so); this one, further down the list, isn't used.");
          changed = true;
        }
      }
    }
  }
  for (size_t a = 0; a < alive.size(); a++) {
    const Manifest& m = review.rows[alive[a]].mod->manifest;
    for (const std::string& want : m.after) {
      for (size_t b = a + 1; b < alive.size(); b++) {
        const Manifest& later = review.rows[alive[b]].mod->manifest;
        if (Same(later.id, want)) {
          review.rows[alive[a]].warnings.push_back("Wants to come after \"" + later.name + "\": move it further down the list.");
        }
      }
    }
  }
  std::vector<Keys> keys;
  for (size_t a : alive) keys.push_back(KeysOf(ChangesOf(*review.rows[a].mod)));
  for (size_t a = 0; a < alive.size(); a++) {
    for (size_t b = a + 1; b < alive.size(); b++) {
      const std::vector<std::string> shared = Shared(keys[a], keys[b]);
      if (shared.empty()) continue;
      const std::string& nameA = review.rows[alive[a]].mod->manifest.name;
      const std::string& nameB = review.rows[alive[b]].mod->manifest.name;
      review.rows[alive[a]].warnings.push_back("Clashes with \"" + nameB + "\" over " + FirstOf(shared) + ": \"" + nameB +
                                               "\", further down the list, wins.");
      review.rows[alive[b]].warnings.push_back("Clashes with \"" + nameA + "\" over " + FirstOf(shared) +
                                               ": this one, further down the list, wins.");
    }
  }
  return review;
}

bool MoveMod(ModList::Profile& profile, size_t index, int dir) {
  if (dir != -1 && dir != 1) return false;
  if (index >= profile.mods.size()) return false;
  if (dir == -1 && index == 0) return false;
  if (dir == 1 && index + 1 >= profile.mods.size()) return false;
  std::swap(profile.mods[index], profile.mods[static_cast<size_t>(static_cast<long long>(index) + dir)]);
  return true;
}

}
