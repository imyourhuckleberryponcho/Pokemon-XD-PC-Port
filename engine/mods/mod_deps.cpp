#include "mods.h"

#include "mod_layout.h"

#include "i18n.h"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace engine::mods {

namespace fs = std::filesystem;

namespace {

using engine::i18n::Tr;

std::string Fmt(const char* format, ...) {
  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
bool Same(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

bool ReadSmall(const fs::path& file, std::string& out, size_t limit = 8u << 20) {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec || size > limit) return false;
  std::ifstream in(file, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return static_cast<bool>(in) || in.eof();
}

}

OrderResult SortMods(const std::vector<const ModFolder*>& mods) {
  OrderResult result;
  const size_t n = mods.size();
  std::map<std::string, size_t> index;
  for (size_t i = 0; i < n; i++) index[Lower(mods[i]->manifest.id)] = i;
  std::vector<std::set<size_t>> after(n);
  const auto edge = [&](size_t first, size_t second) {
    if (first != second) after[second].insert(first);
  };
  for (size_t i = 0; i < n; i++) {
    const Manifest& m = mods[i]->manifest;
    const auto look = [&](const std::string& id) -> int {
      const auto it = index.find(Lower(id));
      return it == index.end() ? -1 : static_cast<int>(it->second);
    };
    for (const std::string& id : m.required) {
      if (const int o = look(id); o >= 0) edge(static_cast<size_t>(o), i);
    }
    for (const std::string& id : m.after) {
      if (const int o = look(id); o >= 0) edge(static_cast<size_t>(o), i);
    }
    for (const std::string& id : m.onlyWith) {
      if (const int o = look(id); o >= 0) edge(static_cast<size_t>(o), i);
    }
    for (const std::string& id : m.before) {
      if (const int o = look(id); o >= 0) edge(i, static_cast<size_t>(o));
    }
  }
  std::vector<bool> done(n, false);
  size_t placed = 0;
  while (placed < n) {
    int best = -1;
    for (size_t i = 0; i < n; i++) {
      if (done[i]) continue;
      bool ready = true;
      for (size_t o : after[i]) ready = ready && done[o];
      if (!ready) continue;
      if (best < 0 || mods[i]->manifest.priority < mods[static_cast<size_t>(best)]->manifest.priority) best = static_cast<int>(i);
    }
    if (best < 0) break;
    done[static_cast<size_t>(best)] = true;
    result.order.push_back(mods[static_cast<size_t>(best)]->manifest.id);
    placed++;
  }
  for (size_t i = 0; i < n; i++) {
    if (!done[i]) {
      result.cycle.push_back(mods[i]->manifest.id);
      result.order.push_back(mods[i]->manifest.id);
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (result.order[i] != mods[i]->manifest.id) result.changed = true;
  }
  return result;
}

bool SortProfile(ModList::Profile& profile, const std::vector<ModFolder>& found, OrderResult* out) {
  std::vector<const ModFolder*> known;
  std::vector<std::pair<std::string, bool>> unknown, knownEntries;
  for (const auto& entry : profile.mods) {
    const ModFolder* f = nullptr;
    for (const ModFolder& m : found) {
      if (Same(m.manifest.id, entry.first)) f = &m;
    }
    if (f != nullptr) {
      known.push_back(f);
      knownEntries.push_back(entry);
    } else {
      unknown.push_back(entry);
    }
  }
  OrderResult r = SortMods(known);
  bool changed = false;
  std::vector<std::pair<std::string, bool>> next;
  for (const std::string& id : r.order) {
    for (const auto& entry : knownEntries) {
      if (Same(entry.first, id)) {
        next.push_back(entry);
        break;
      }
    }
  }
  for (const auto& entry : unknown) next.push_back(entry);
  changed = next != profile.mods;
  profile.mods = std::move(next);
  if (out != nullptr) *out = std::move(r);
  return changed;
}

Requirements ResolveRequirements(const ModFolder& mod, const std::vector<ModFolder>& found, const std::vector<std::string>& onIds,
                                 std::string_view gameId) {
  Requirements out;
  const auto isOn = [&](const std::string& id) {
    return std::any_of(onIds.begin(), onIds.end(), [&](const std::string& o) { return Same(o, id); });
  };
  const auto find = [&](const std::string& id) -> const ModFolder* {
    for (const ModFolder& f : found) {
      if (Same(f.manifest.id, id)) return &f;
    }
    return nullptr;
  };
  std::set<std::string> visited;
  std::vector<const ModFolder*> chain;
  std::vector<std::string> wanted = onIds;
  const std::function<void(const ModFolder&)> visit = [&](const ModFolder& m) {
    if (!visited.insert(Lower(m.manifest.id)).second) return;
    for (size_t k = 0; k < m.manifest.required.size(); k++) {
      const std::string& need = m.manifest.required[k];
      const VersionNeed want = k < m.manifest.requiredVersions.size() ? m.manifest.requiredVersions[k] : VersionNeed{};
      const ModFolder* other = find(need);
      if (other == nullptr || !want.SatisfiedBy(other->manifest.version)) {
        Requirements::Missing miss;
        miss.id = need;
        miss.op = want.op;
        miss.version = want.version;
        miss.have = other != nullptr ? (other->manifest.version.empty() ? "?" : other->manifest.version) : std::string();
        miss.neededBy = m.manifest.id;
        const auto link = m.manifest.links.find(need);
        if (link != m.manifest.links.end()) miss.link = link->second;
        out.missing.push_back(std::move(miss));
        continue;
      }
      if (!gameId.empty()) {
        if (!SupportsGame(other->manifest, gameId)) {
          out.conflicts.push_back(Fmt(Tr("\"%s\" is made for another game, so it can't be used here."), other->manifest.name.c_str()));
          continue;
        }
      }
      if (!isOn(need)) {
        visit(*other);
        if (std::none_of(chain.begin(), chain.end(), [&](const ModFolder* c) { return c == other; })) chain.push_back(other);
        wanted.push_back(other->manifest.id);
      }
    }
  };
  visit(mod);
  out.turnOn = chain;
  std::vector<std::string> all = wanted;
  all.push_back(mod.manifest.id);
  for (const std::string& id : all) {
    const ModFolder* o = find(id);
    if (o == nullptr || Same(o->manifest.id, mod.manifest.id)) continue;
    const auto names = [](const std::vector<std::string>& ids, const std::string& x) {
      return std::any_of(ids.begin(), ids.end(), [&](const std::string& y) { return Same(x, y); });
    };
    if (names(mod.manifest.conflicts, o->manifest.id) || names(o->manifest.conflicts, mod.manifest.id)) {
      out.conflicts.push_back(Fmt(Tr("\"%s\" can't be used together with \"%s\"."), mod.manifest.name.c_str(), o->manifest.name.c_str()));
    }
  }
  return out;
}

namespace {

struct Touch {
  std::set<std::string> data, rooms, hooks;
};

void DataKeys(const fs::path& file, std::set<std::string>& out) {
  std::string text;
  if (!ReadSmall(file, text)) return;
  json::Value root;
  json::Reader reader(text);
  if (!reader.Document(root) || root.kind != json::Value::Kind::Object) return;
  for (const auto& [table, entries] : root.fields) {
    if (table.empty() || table[0] == '_' || table == "add") continue;
    if (entries.kind != json::Value::Kind::Object) continue;
    for (const auto& [entry, fields] : entries.fields) {
      if (table == "text") {
        out.insert("text / " + entry);
        continue;
      }
      if (fields.kind != json::Value::Kind::Object) continue;
      for (const auto& [field, value] : fields.fields) {
        if (!field.empty() && field[0] == '_') continue;
        out.insert(table + " / " + entry + " / " + field);
      }
    }
  }
}

void HookNames(const std::string& text, std::set<std::string>& out) {
  size_t at = 0;
  while ((at = text.find("on(", at)) != std::string::npos) {
    const bool wordStart = at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_' || text[at - 1] == '.');
    at += 3;
    if (!wordStart) continue;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) at++;
    if (at >= text.size() || (text[at] != '"' && text[at] != '\'')) continue;
    const char quote = text[at++];
    const size_t end = text.find(quote, at);
    if (end == std::string::npos || end - at > 80) continue;
    out.insert(text.substr(at, end - at));
  }
}

Touch TouchOf(const ModFolder& mod, std::string_view gameId) {
  Touch t;
  if (mod.texturePack) return t;
  std::error_code ec;
  for (const LayoutFile& f : LayoutFiles(mod.folder, gameId, "data", false)) {
    if (Lower(fs::path(f.rel).extension().string()) == ".json") DataKeys(f.path, t.data);
  }
  for (const fs::path& rooms : LayoutFolders(mod.folder, gameId, "rooms")) {
    for (auto it = fs::directory_iterator(rooms, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      t.rooms.insert(Lower(it->path().stem().string()));
    }
  }
  for (const LayoutFile& f : LayoutFiles(mod.folder, gameId, "objects", false)) t.rooms.insert(Lower(fs::path(f.rel).stem().string()));
  for (const LayoutFile& f : LayoutFiles(mod.folder, gameId, "texts", false)) t.rooms.insert(Lower(fs::path(f.rel).stem().string()));
  if (mod.manifest.scripts) {
    for (const LayoutFile& f : LayoutFiles(mod.folder, gameId, "scripts", false)) {
      std::string text;
      if ((Lower(fs::path(f.rel).extension().string()) == ".luau" || Lower(fs::path(f.rel).extension().string()) == ".lua") && ReadSmall(f.path, text, 512u << 10)) HookNames(text, t.hooks);
    }
  }
  return t;
}

}

std::vector<FineConflict> FineConflicts(const std::vector<const ModFolder*>& inOrder, std::string_view gameId) {
  std::vector<FineConflict> out;
  std::vector<Touch> touches;
  for (const ModFolder* m : inOrder) touches.push_back(TouchOf(*m, gameId));
  for (size_t a = 0; a < inOrder.size(); a++) {
    for (size_t b = a + 1; b < inOrder.size(); b++) {
      const auto add = [&](FineConflict::Kind kind, const std::set<std::string>& x, const std::set<std::string>& y, const char* prefix) {
        for (const std::string& key : x) {
          if (y.count(key) != 0) {
            out.push_back({kind, std::string(prefix) + key, inOrder[a]->manifest.id, inOrder[b]->manifest.id});
          }
        }
      };
      add(FineConflict::Kind::DataField, touches[a].data, touches[b].data, "");
      add(FineConflict::Kind::StoryRoom, touches[a].rooms, touches[b].rooms, "room ");
      add(FineConflict::Kind::LuaHook, touches[a].hooks, touches[b].hooks, "hook ");
    }
  }
  return out;
}

}
