#include "mods.h"

#include <gs_formats.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

namespace engine::mods {

namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return out;
}

bool EqualNoCase(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

bool ReadAll(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

bool ReadBytes(const fs::path& path, std::vector<uint8_t>& out) {
  std::string text;
  if (!ReadAll(path, text)) {
    return false;
  }
  out.assign(text.begin(), text.end());
  return true;
}

bool WriteAll(const fs::path& path, const std::string& text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path temp = fs::path(path).concat(".new");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out.good()) {
      return false;
    }
  }
  fs::rename(temp, path, ec);
  return !ec;
}

bool IsAscii(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

using FoundMod = ModFolder;

bool ResolveEntry(const FsysArchive& fsys, const std::string& discPath, const std::string& name, uint32_t& index,
                  std::string& error) {
  if (name.size() >= 2 && name[0] == '#') {
    char* end = nullptr;
    const unsigned long n = std::strtoul(name.c_str() + 1, &end, 10);
    if (end != name.c_str() + 1 && (*end == '\0' || *end == '.')) {
      if (n >= fsys.count) {
        error = discPath + " has entries #0 to #" + std::to_string(fsys.count - 1) + ", no " + name;
        return false;
      }
      index = static_cast<uint32_t>(n);
      return true;
    }
  }
  std::vector<uint32_t> shortMatches;
  std::string fullNames;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e)) {
      continue;
    }
    if (e.fullName[0] != '\0' && EqualNoCase(e.fullName, name)) {
      index = i;
      return true;
    }
    if (EqualNoCase(e.name, name)) {
      shortMatches.push_back(i);
      fullNames += std::string(fullNames.empty() ? "" : ", ") +
                   (e.fullName[0] != '\0' ? e.fullName : "#" + std::to_string(i));
    }
  }
  if (shortMatches.size() == 1) {
    index = shortMatches[0];
    return true;
  }
  error = shortMatches.empty() ? discPath + " has no entry called " + name
                               : "several entries of " + discPath + " are called " + name +
                                     "; name the one meant by its full name or number (" + fullNames + ")";
  return false;
}

struct Layer {
  std::string discPath;
  bool onDisc = true;
  fs::path whole;
  std::string wholeFrom;
  struct Entry {
    fs::path file;
    std::string from;
    std::string name;
    bool generated = false;
    std::vector<uint8_t> data;
  };
  std::map<uint32_t, Entry> entries;
};

class Loader {
public:
  Loader(const LoadOptions& options, LoadResult& result) : mOpt(options), mResult(result) {
    for (const std::string& f : options.disc.files) {
      mDiscFiles.emplace(Lower(f), f);
    }
  }

  void Run() {
    FindMods();
    ModList list = ReadList();
    CollectExtraLayers();
    std::vector<const FoundMod*> on = ChooseMods(list);
    for (const FoundMod* mod : on) {
      mResult.active.push_back(mod->manifest);
      mResult.activeApplies.push_back(VerdictOf(*mod));
      CollectFiles(*mod);
    }
    ApplyData(on);
    Build();
    if (!mResult.active.empty()) {
      Say(Message::Level::Info, "Mods on: " + mResult.ActiveText() + " (" + std::to_string(mResult.files.size()) +
                                    " disc file(s) replaced or added)");
    }
  }

private:
  const LoadOptions& mOpt;
  LoadResult& mResult;
  std::vector<FoundMod> mFound;
  std::map<std::string, std::string> mDiscFiles;
  std::map<std::string, Layer> mLayers;
  std::map<std::string, std::vector<uint8_t>> mBaseCache;

  void Say(Message::Level level, std::string text) { mResult.messages.push_back({level, std::move(text)}); }

  void FindMods() { mFound = engine::mods::FindMods(mOpt.modsFolder, mResult.messages); }

  const FoundMod* Find(std::string_view id) const {
    for (const FoundMod& m : mFound) {
      if (EqualNoCase(m.manifest.id, id)) {
        return &m;
      }
    }
    return nullptr;
  }

  ModList ReadList() {
    ModList list;
    std::vector<std::string> warnings;
    const bool exists = ReadModListFile(mOpt.listFile, list, warnings);
    for (std::string& w : warnings) {
      Say(Message::Level::Warning, std::move(w));
    }
    const std::vector<std::string> fresh = AddNewMods(list, mFound);
    for (const std::string& id : fresh) {
      const FoundMod* m = Find(id);
      Say(Message::Level::Info, "New mod found: \"" + m->manifest.name + "\" (" + m->manifest.id + "). It is off; to use "
                                    "it, set \"" + m->manifest.id + " = on\" in " + Utf8(mOpt.listFile.filename()) + ".");
    }
    if (!fresh.empty() || (!exists && !mFound.empty())) {
      if (mOpt.listReadOnly) {
        mResult.listWriteSkipped = true;
      } else if (WriteModListFile(mOpt.listFile, list)) {
        mResult.listWritten = true;
      } else {
        Say(Message::Level::Warning, "Could not write the mod list " + Utf8(mOpt.listFile) + ".");
      }
    }
    return list;
  }

  std::map<const FoundMod*, AppliesVerdict> mVerdicts;
  const AppliesVerdict& VerdictOf(const FoundMod& mod) {
    auto it = mVerdicts.find(&mod);
    if (it == mVerdicts.end()) {
      it = mVerdicts.emplace(&mod, CheckApplies(mod, mOpt.looksOrSound, &mOpt.disc)).first;
    }
    return it->second;
  }

  bool InPlaythrough(std::string_view id) const {
    return std::any_of(mOpt.playthroughMods.begin(), mOpt.playthroughMods.end(),
                       [&](const std::string& p) { return EqualNoCase(p, id); });
  }

  std::vector<const FoundMod*> ChooseMods(ModList& list) {
    std::vector<const FoundMod*> on;
    if (mOpt.usePlaythroughMods) {
      for (const std::string& id : mOpt.playthroughMods) {
        if (Find(id) == nullptr) {
          Say(Message::Level::Info, "This playthrough was set up with the mod \"" + id +
                                        "\", which is not in the mods folder any more.");
        }
      }
    }
    for (const auto& [id, listed] : list.Active().mods) {
      bool enabled = listed;
      const FoundMod* found = Find(id);
      if (mOpt.usePlaythroughMods && found != nullptr && VerdictOf(*found).effective == Applies::NewPlaythrough) {
        enabled = InPlaythrough(id);
      }
      if (!enabled) {
        continue;
      }
      const FoundMod* mod = found;
      if (mod == nullptr) {
        Say(Message::Level::Warning, Utf8(mOpt.listFile.filename()) + " turns on \"" + id +
                                         "\", but there is no such mod in the mods folder.");
        continue;
      }
      const auto& games = mod->manifest.games;
      if (std::none_of(games.begin(), games.end(), [&](const std::string& g) { return EqualNoCase(g, mOpt.disc.gameId); })) {
        std::string gameList;
        for (const std::string& g : games) {
          gameList += (gameList.empty() ? "" : ", ") + g;
        }
        Say(Message::Level::Problem, "The mod \"" + mod->manifest.name + "\" is made for another game or game version (" +
                                         gameList + "); this disc is " + mOpt.disc.gameId + ". It is not used.");
        continue;
      }
      on.push_back(mod);
    }
    for (bool changed = true; changed;) {
      changed = false;
      for (size_t i = 0; i < on.size() && !changed; i++) {
        const Manifest& m = on[i]->manifest;
        for (size_t k = 0; k < m.required.size() && !changed; k++) {
          const std::string& need = m.required[k];
          const auto it = std::find_if(on.begin(), on.end(), [&](const FoundMod* o) { return EqualNoCase(o->manifest.id, need); });
          const bool there = it != on.end();
          if (there && k < m.requiredVersions.size() && !m.requiredVersions[k].SatisfiedBy((*it)->manifest.version)) {
            const VersionNeed& v = m.requiredVersions[k];
            Say(Message::Level::Problem, "The mod \"" + m.name + "\" needs the mod \"" + (*it)->manifest.name + "\" in version " +
                                             v.op + " " + v.version + ", but the one in the mods folder is " +
                                             ((*it)->manifest.version.empty() ? std::string("without a version")
                                                                              : "version " + (*it)->manifest.version) +
                                             ". \"" + m.name + "\" is not used.");
            on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
          }
          if (!there) {
            const FoundMod* other = Find(need);
            Say(Message::Level::Problem, "The mod \"" + m.name + "\" needs the mod \"" + need + "\", which is " +
                                             (other != nullptr ? "not on (or can't be used)" : "not in the mods folder") +
                                             ". \"" + m.name + "\" is not used.");
            on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
          }
        }
        for (size_t j = 0; j < i && !changed; j++) {
          const Manifest& earlier = on[j]->manifest;
          const auto names = [](const std::vector<std::string>& ids, const std::string& id) {
            return std::any_of(ids.begin(), ids.end(), [&](const std::string& x) { return EqualNoCase(x, id); });
          };
          if (names(m.conflicts, earlier.id) || names(earlier.conflicts, m.id)) {
            Say(Message::Level::Problem, "The mods \"" + earlier.name + "\" and \"" + m.name +
                                             "\" can't be used together (" +
                                             (names(m.conflicts, earlier.id) ? m.name : earlier.name) +
                                             " says so). \"" + m.name + "\", further down the list, is not used.");
            on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
          }
        }
      }
    }
    for (size_t i = 0; i < on.size(); i++) {
      for (const std::string& want : on[i]->manifest.after) {
        for (size_t j = i + 1; j < on.size(); j++) {
          if (EqualNoCase(on[j]->manifest.id, want)) {
            Say(Message::Level::Warning, "The mod \"" + on[i]->manifest.name + "\" should come after \"" + on[j]->manifest.name +
                                             "\" (it says so): move the line \"" + on[i]->manifest.id + " = on\" below \"" +
                                             on[j]->manifest.id + " = on\" in " + Utf8(mOpt.listFile.filename()) +
                                             ". Until then the list's order is used.");
          }
        }
      }
    }
    return on;
  }

  static constexpr const char* kLayerPrefix = "extra-assets:";
  static bool IsLayerId(const std::string& id) { return id.rfind(kLayerPrefix, 0) == 0; }
  void CollectExtraLayers() {
    for (const LoadOptions::ExtraLayer& layer : mOpt.extraLayers) {
      LoadResult::LayerResult r;
      r.id = layer.id;
      std::error_code ec;
      r.found = fs::is_directory(layer.folder, ec);
      if (r.found) {
        FoundMod mod;
        mod.folder = layer.folder;
        mod.manifest.id = kLayerPrefix + layer.id;
        mod.manifest.name = layer.name;
        r.files = CollectFiles(mod, true);
      }
      mResult.extraLayers.push_back(std::move(r));
    }
  }

  Layer& LayerFor(const std::string& path, bool& isNew) {
    const std::string key = Lower(path);
    auto it = mLayers.find(key);
    isNew = it == mLayers.end();
    if (isNew) {
      Layer layer;
      const auto disc = mDiscFiles.find(key);
      layer.onDisc = disc != mDiscFiles.end();
      layer.discPath = layer.onDisc ? disc->second : path;
      it = mLayers.emplace(key, std::move(layer)).first;
    }
    return it->second;
  }

  std::vector<std::pair<std::string, fs::path>> FilesIn(const FoundMod& mod, const fs::path& folder) {
    std::vector<std::pair<std::string, fs::path>> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
      return out;
    }
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      const std::u8string generic = fs::relative(it->path(), folder, ec).generic_u8string();
      const std::string rel(reinterpret_cast<const char*>(generic.data()), generic.size());
      if (!IsAscii(rel)) {
        Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" has \"" + rel +
                                         "\": disc file names only have English letters, digits and signs; ignored.");
        continue;
      }
      out.emplace_back(rel, it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  static bool IsSystemName(const std::string& path) {
    static const char* const kNames[] = {"boot.bin", "bi2.bin", "apploader.img", "fst.bin", "main.dol", "start.dol"};
    const std::string lower = Lower(path);
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".dol") == 0) {
      return true;
    }
    return std::any_of(std::begin(kNames), std::end(kNames), [&](const char* n) { return lower == n; });
  }

  void CollectScripts(const FoundMod& mod) {
    std::error_code ec;
    const fs::path folder = mod.folder / "scripts";
    if (!mod.manifest.scripts) {
      if (fs::is_directory(folder, ec)) {
        Say(Message::Level::Warning, "The mod \"" + mod.manifest.name +
                                         "\" has a \"scripts\" folder but its mod.json does not say \"scripts\": true; the scripts are not used.");
      }
      return;
    }
    std::vector<std::pair<std::string, fs::path>> found;
    if (fs::is_directory(folder, ec)) {
      for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = Utf8(it->path().filename());
        if (Lower(Utf8(it->path().extension())) != ".lua") {
          Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" has \"scripts/" + name + "\": only .lua files are read; ignored.");
          continue;
        }
        found.emplace_back(name, it->path());
      }
    }
    std::sort(found.begin(), found.end());
    if (found.empty()) {
      Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" says \"scripts\": true but has no scripts/*.lua.");
      return;
    }
    if (found.size() > kMaxScriptFiles) {
      Say(Message::Level::Problem, "The mod \"" + mod.manifest.name + "\" has more than " + std::to_string(kMaxScriptFiles) +
                                       " script files; its scripts are not used.");
      return;
    }
    std::vector<ScriptFile> files;
    for (const auto& [name, path] : found) {
      std::error_code sizeEc;
      const auto size = fs::file_size(path, sizeEc);
      std::string text;
      if (sizeEc || size > kMaxScriptFileBytes || !ReadAll(path, text)) {
        Say(Message::Level::Problem, "The mod \"" + mod.manifest.name + "\": scripts/" + name + " could not be read or is bigger than " +
                                         std::to_string(kMaxScriptFileBytes / 1024) + " KB; the mod's scripts are not used.");
        return;
      }
      files.push_back({mod.manifest.id, "scripts/" + name, std::move(text)});
    }
    for (ScriptFile& f : files) mResult.scripts.push_back(std::move(f));
  }

  size_t CollectFiles(const FoundMod& mod, bool quiet = false) {
    size_t used = 0;
    const std::string& id = mod.manifest.id;
    const auto whole = FilesIn(mod, mod.folder / "files");
    const auto entries = FilesIn(mod, mod.folder / "fsys");
    std::error_code ec;
    if (!quiet && whole.empty() && entries.empty() && !fs::is_directory(mod.folder / "data", ec) &&
        !(mod.manifest.scripts && fs::is_directory(mod.folder / "scripts", ec))) {
      Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" is on but has no files in \"files\", \"fsys\" or \"data\".");
    }
    CollectScripts(mod);
    for (const auto& [rel, path] : whole) {
      if (mDiscFiles.find(Lower(rel)) == mDiscFiles.end() && IsSystemName(rel)) {
        Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" has \"files/" + rel +
                                         "\": the game's program and disc header can't be replaced by mods; ignored.");
        continue;
      }
      bool isNew = false;
      Layer& layer = LayerFor(rel, isNew);
      if (!layer.wholeFrom.empty() && !IsLayerId(layer.wholeFrom)) {
        Say(Message::Level::Warning, "Conflict: the mods \"" + NameOf(layer.wholeFrom) + "\" and \"" + mod.manifest.name +
                                         "\" both replace " + layer.discPath + ". \"" + mod.manifest.name +
                                         "\" (further down the list) is used.");
      }
      if (std::any_of(layer.entries.begin(), layer.entries.end(),
                      [](const auto& e) { return !IsLayerId(e.second.from); })) {
        Say(Message::Level::Warning, "Conflict: \"" + mod.manifest.name + "\" replaces all of " + layer.discPath +
                                         ", so the changes inside it from " + EntryMods(layer) + " are not used.");
      }
      layer.entries.clear();
      if (!layer.onDisc && isNew) {
        Say(Message::Level::Info, "The mod \"" + mod.manifest.name + "\" adds " + rel + ", which the disc doesn't have.");
      }
      layer.whole = path;
      layer.wholeFrom = id;
      mBaseCache.erase(Lower(rel));
      used++;
    }
    for (const auto& [rel, path] : entries) {
      const size_t slash = rel.rfind('/');
      if (slash == std::string::npos) {
        Say(Message::Level::Warning, "The mod \"" + mod.manifest.name + "\" has \"fsys/" + rel +
                                         "\": files there go in a folder named after the archive, like "
                                         "fsys/common.fsys/<entry>; ignored.");
        continue;
      }
      const std::string archive = rel.substr(0, slash), entryName = rel.substr(slash + 1);
      bool isNew = false;
      Layer& layer = LayerFor(archive, isNew);
      if (!layer.onDisc && layer.wholeFrom.empty()) {
        Say(Message::Level::Problem, "The mod \"" + mod.manifest.name + "\" changes " + entryName + " inside " + archive +
                                         ", but the disc has no " + archive + "; ignored.");
        if (isNew) {
          mLayers.erase(Lower(archive));
        }
        continue;
      }
      uint32_t index = 0;
      std::string error;
      if (!FindEntry(layer, entryName, index, error)) {
        Say(Message::Level::Problem, "The mod \"" + mod.manifest.name + "\": " + error + "; ignored.");
        if (isNew) {
          mLayers.erase(Lower(archive));
        }
        continue;
      }
      const auto had = layer.entries.find(index);
      if (had != layer.entries.end() && !IsLayerId(had->second.from)) {
        Say(Message::Level::Warning, "Conflict: the mods \"" + NameOf(had->second.from) + "\" and \"" + mod.manifest.name +
                                         "\" both replace " + entryName + " inside " + layer.discPath + ". \"" +
                                         mod.manifest.name + "\" (further down the list) is used.");
      }
      layer.entries[index] = {path, id, entryName};
      used++;
    }
    return used;
  }

  bool ReadEntry(const std::string& archive, const std::string& entryName, std::vector<uint8_t>& out) {
    if (archive == kProgramArchive) {
      return mOpt.disc.readProgram && mOpt.disc.readProgram(out);
    }
    Layer temp;
    const Layer* layer = nullptr;
    const auto it = mLayers.find(Lower(archive));
    if (it != mLayers.end()) {
      layer = &it->second;
    } else {
      const auto d = mDiscFiles.find(Lower(archive));
      if (d == mDiscFiles.end()) {
        return false;
      }
      temp.discPath = d->second;
      layer = &temp;
    }
    uint32_t index = 0;
    std::string error;
    if (!FindEntry(*layer, entryName, index, error)) {
      return false;
    }
    const auto e = layer->entries.find(index);
    if (e != layer->entries.end()) {
      if (e->second.generated) {
        out = e->second.data;
        return true;
      }
      return ReadBytes(e->second.file, out);
    }
    const std::vector<uint8_t>* base = Base(*layer);
    FsysArchive fsys;
    uint32_t n = 0;
    uint8_t* p = base != nullptr && Fsys_Open(base->data(), base->size(), &fsys) ? Fsys_Extract(&fsys, index, &n) : nullptr;
    if (p == nullptr) {
      return false;
    }
    out.assign(p, p + n);
    std::free(p);
    return true;
  }

  void ApplyData(const std::vector<const FoundMod*>& on) {
    std::vector<DataModFile> files;
    for (const FoundMod* mod : on) {
      for (const auto& [rel, path] : FilesIn(*mod, mod->folder / "data")) {
        const std::string label = "data/" + rel;
        if (Lower(Utf8(fs::path(rel).extension())) != ".json") {
          Say(Message::Level::Warning, "The mod \"" + mod->manifest.name + "\" has \"" + label +
                                           "\": only .json files in \"data\" are read; ignored.");
          continue;
        }
        DataModFile f{mod->manifest.id, mod->manifest.name, label, {}};
        if (!ReadAll(path, f.text)) {
          Say(Message::Level::Problem, "Could not read " + Utf8(path) + "; its changes are not used.");
          continue;
        }
        files.push_back(std::move(f));
      }
    }
    if (files.empty() && !mOpt.generateData) {
      return;
    }
    if (!mOpt.dataCatalog) {
      Say(Message::Level::Problem, "Data mods (\"data\" folders) are not supported for this game yet; their changes are not used.");
      return;
    }
    auto catalog = std::make_shared<DataCatalog>();
    std::string error;
    const EntryReader reader = [this](const std::string& a, const std::string& e, std::vector<uint8_t>& out) {
      return ReadEntry(a, e, out);
    };
    if (!mOpt.dataCatalog(reader, *catalog, error)) {
      Say(Message::Level::Problem, "The game's tables could not be found on this disc (" + error +
                                       "), so the data mods' changes are not used.");
      return;
    }
    if (mOpt.generateData) {
      std::vector<DataModFile> made;
      if (!mOpt.generateData(*catalog, reader, made, error)) {
        Say(Message::Level::Problem, "The generated data (randomizer) could not be made: " + error + ". The game starts without it.");
        made.clear();
      }
      for (DataModFile& f : made) files.push_back(std::move(f));
      if (files.empty()) return;
    }
    DataApplyResult applied = ApplyDataMods(*catalog, files, reader, mResult.messages);
    mResult.catalog = catalog;
    std::map<std::pair<std::string, std::string>, std::set<std::string>> fromMods;
    std::set<std::string> tables;
    for (const DataChange& c : applied.changes) {
      tables.insert(c.table);
      if (const DataTable* t = catalog->FindTable(c.table)) {
        for (const DataPlace& p : t->places) {
          fromMods[{p.archive, p.entry}].insert(c.mod);
        }
      }
    }
    for (const TextChange& c : applied.texts) {
      for (const DataPlace& p : catalog->text.places) {
        fromMods[{p.archive, p.entry}].insert(c.mod);
      }
    }
    size_t archiveEntries = 0;
    for (auto& [place, bytes] : applied.entries) {
      if (place.first == kProgramArchive) {
        mResult.program = std::move(bytes);
        continue;
      }
      archiveEntries++;
      bool isNew = false;
      Layer& layer = LayerFor(place.first, isNew);
      uint32_t index = 0;
      std::string findError;
      if (!FindEntry(layer, place.second, index, findError)) {
        Say(Message::Level::Problem, "Data mods: " + findError + "; those changes are not used.");
        continue;
      }
      Layer::Entry& e = layer.entries[index];
      std::string from;
      for (const std::string& id : fromMods[place]) {
        from += (from.empty() ? "" : ", ") + id;
      }
      e.generated = true;
      e.data = std::move(bytes);
      e.file.clear();
      e.from = from;
      e.name = place.second;
    }
    size_t same = 0;
    for (const DataChange& c : applied.changes) {
      same += c.before == c.after;
    }
    Say(Message::Level::Info, "Data mods: " + std::to_string(applied.changes.size()) + " number(s) set in " +
                                  std::to_string(tables.size()) + " table(s)" +
                                  (same != 0 ? " (" + std::to_string(same) + " of them to the value the disc has)" : std::string()) +
                                  ", " + std::to_string(archiveEntries) + " archive entr" +
                                  (archiveEntries == 1 ? "y" : "ies") + " changed" +
                                  (mResult.program.empty() ? std::string() : std::string(", tables in the program changed")) +
                                  (applied.texts.empty() ? std::string() : ", " + std::to_string(applied.texts.size()) + " message text(s) rewritten"));
    mResult.dataChanges = std::move(applied.changes);
    mResult.addedRows = std::move(applied.added);
    mResult.addedTexts = std::move(applied.addedTexts);
    mResult.addedTextGroup = catalog->addedTextGroup;
    if (!mResult.addedRows.empty()) {
      std::string list;
      for (const AddedRow& r : mResult.addedRows) {
        list += (list.empty() ? "" : ", ") + r.key + " (" + r.table + " #" + std::to_string(r.id) + ")";
      }
      Say(Message::Level::Info, "Data mods add " + std::to_string(mResult.addedRows.size()) + " row(s): " + list);
    }
  }

  std::string NameOf(const std::string& id) const {
    const FoundMod* m = Find(id);
    return m != nullptr ? m->manifest.name : id;
  }

  std::string EntryMods(const Layer& layer) const {
    std::set<std::string> ids;
    std::string out;
    for (const auto& [index, e] : layer.entries) {
      if (ids.insert(e.from).second) {
        out += (out.empty() ? "\"" : ", \"") + NameOf(e.from) + "\"";
      }
    }
    return out;
  }

  const std::vector<uint8_t>* Base(const Layer& layer) {
    const std::string key = Lower(layer.discPath);
    auto it = mBaseCache.find(key);
    if (it != mBaseCache.end()) {
      return &it->second;
    }
    std::vector<uint8_t> bytes;
    const bool ok = !layer.wholeFrom.empty() ? ReadBytes(layer.whole, bytes)
                                             : (mOpt.disc.read && mOpt.disc.read(layer.discPath, bytes));
    if (!ok) {
      return nullptr;
    }
    return &mBaseCache.emplace(key, std::move(bytes)).first->second;
  }

  bool FindEntry(const Layer& layer, const std::string& name, uint32_t& index, std::string& error) {
    const std::vector<uint8_t>* base = Base(layer);
    FsysArchive fsys;
    if (base == nullptr || !Fsys_Open(base->data(), base->size(), &fsys)) {
      error = layer.discPath + (base == nullptr ? " can't be read" : " is not an FSYS archive") + ", so " + name +
              " can't be replaced inside it";
      return false;
    }
    return ResolveEntry(fsys, layer.discPath, name, index, error);
  }

  void Build() {
    for (auto& [key, layer] : mLayers) {
      Replacement r;
      r.discPath = layer.discPath;
      r.added = !layer.onDisc;
      r.file = layer.whole;
      r.from = layer.wholeFrom.empty() ? std::string() : layer.wholeFrom;
      if (!layer.entries.empty() && !Rebuild(layer, r)) {
        if (layer.wholeFrom.empty()) {
          continue;
        }
        r.rebuilt = false;
        r.data.clear();
      }
      mResult.files.push_back(std::move(r));
    }
  }

  bool Rebuild(const Layer& layer, Replacement& r) {
    const std::vector<uint8_t>* base = Base(layer);
    FsysArchive fsys;
    if (base == nullptr || !Fsys_Open(base->data(), base->size(), &fsys)) {
      Say(Message::Level::Problem, layer.discPath + " can't be read, so the changes inside it from " + EntryMods(layer) +
                                       " are not used.");
      return false;
    }
    std::vector<std::vector<uint8_t>> contents;
    std::vector<FsysReplacement> list;
    contents.reserve(layer.entries.size());
    std::set<std::string> from;
    for (const auto& [index, e] : layer.entries) {
      contents.emplace_back();
      if (e.generated) {
        contents.back() = e.data;
      } else if (!ReadBytes(e.file, contents.back()) || contents.back().size() > 0xFFFFFFFFu) {
        Say(Message::Level::Problem, "Could not read " + Utf8(e.file) + "; the changes inside " + layer.discPath +
                                         " are not used.");
        return false;
      }
      FsysEntry old;
      if (!e.generated && Fsys_Entry(&fsys, index, &old) && contents.back().size() > old.size) {
        Say(Message::Level::Info, NameOf(e.from) + ": " + e.name + " in " + layer.discPath + " is bigger than the original (" +
                                      std::to_string(contents.back().size()) + " bytes, was " + std::to_string(old.size) +
                                      "). If the game runs out of memory there, this may be why.");
      }
      list.push_back({index, contents.back().data(), static_cast<uint32_t>(contents.back().size())});
      for (size_t at = 0; at <= e.from.size();) {
        const size_t comma = std::min(e.from.find(", ", at), e.from.size());
        if (comma > at) from.insert(e.from.substr(at, comma - at));
        at = comma + 2;
      }
    }
    size_t size = 0;
    uint8_t* data = Fsys_Rebuild(base->data(), base->size(), list.data(), list.size(), &size);
    if (data == nullptr) {
      Say(Message::Level::Problem, layer.discPath + " could not be rebuilt with the changes from " + EntryMods(layer) +
                                       "; they are not used.");
      return false;
    }
    r.data.assign(data, data + size);
    std::free(data);
    r.rebuilt = true;
    for (const auto& [index, e] : layer.entries) {
      if (e.generated) {
        r.generated.emplace_back(index, e.data);
      } else {
        r.entries.emplace_back(index, e.file);
      }
    }
    for (const std::string& id : from) {
      if (id != r.from) {
        r.from += (r.from.empty() ? "" : ", ") + id;
      }
    }
    return true;
  }
};

}

namespace {
std::vector<ModFolder> FindModsImpl(const fs::path& modsFolder, std::vector<Message>& messages, std::vector<BrokenFolder>* broken) {
  std::vector<ModFolder> found;
  const auto say = [&](Message::Level level, std::string text) { messages.push_back({level, std::move(text)}); };
  const auto broke = [&](const std::string& folder, std::string reason) {
    if (broken != nullptr) broken->push_back({folder, std::move(reason)});
  };
  std::error_code ec;
  if (!fs::is_directory(modsFolder, ec)) {
    return found;
  }
  std::vector<fs::directory_entry> entries;
  for (const auto& e : fs::directory_iterator(modsFolder, ec)) {
    entries.push_back(e);
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
  for (const auto& e : entries) {
    const std::string name = Utf8(e.path().filename());
    if (!e.is_directory(ec)) {
      const std::string ext = Lower(Utf8(e.path().extension()));
      if (ext == ".zip" || ext == ".7z" || ext == ".rar") {
        say(Message::Level::Warning, "\"" + name + "\" in the mods folder is a packed file: unpack it into its own "
                                     "folder there (the folder with mod.json in it).");
        broke(name, "a packed file: unpack it into its own folder in the mods folder");
      }
      continue;
    }
    const fs::path manifestPath = e.path() / kManifestFile;
    std::string text, error;
    if (!fs::is_regular_file(manifestPath, ec)) {
      say(Message::Level::Warning, "The folder \"" + name + "\" in the mods folder has no " + kManifestFile +
                                       ", so it isn't a mod; ignored. (If the mod is one folder further in, move that "
                                       "folder up.)");
      continue;
    }
    ModFolder mod;
    if (!ReadAll(manifestPath, text) || !ParseManifest(text, mod.manifest, error)) {
      say(Message::Level::Problem, "The mod in \"" + name + "\" can't be used: its " + kManifestFile + " " +
                                       (error.empty() ? std::string("can't be read") : error) + ".");
      broke(name, std::string(kManifestFile) + " " + (error.empty() ? std::string("can't be read") : error));
      continue;
    }
    bool duplicate = false;
    for (const ModFolder& other : found) {
      if (EqualNoCase(other.manifest.id, mod.manifest.id)) {
        say(Message::Level::Problem, "The folders \"" + other.folderName + "\" and \"" + name +
                                         "\" both hold a mod with the id \"" + mod.manifest.id + "\"; only \"" +
                                         other.folderName + "\" is used. Remove one of them.");
        broke(name, "the same id \"" + mod.manifest.id + "\" as the mod in \"" + other.folderName + "\", which is used instead");
        duplicate = true;
      }
    }
    if (!duplicate) {
      mod.folder = e.path();
      mod.folderName = name;
      found.push_back(std::move(mod));
    }
  }
  return found;
}
}

std::vector<ModFolder> FindMods(const fs::path& modsFolder, std::vector<Message>& messages) {
  return FindModsImpl(modsFolder, messages, nullptr);
}
std::vector<ModFolder> FindMods(const fs::path& modsFolder, std::vector<Message>& messages, std::vector<BrokenFolder>& broken) {
  return FindModsImpl(modsFolder, messages, &broken);
}

bool ReadModListFile(const fs::path& file, ModList& list, std::vector<std::string>& warnings) {
  std::string text;
  std::error_code ec;
  const bool exists = fs::is_regular_file(file, ec) && ReadAll(file, text);
  list = exists ? ParseModList(text, warnings) : ModList{};
  list.Active();
  return exists;
}

bool WriteModListFile(const fs::path& file, const ModList& list) { return WriteAll(file, FormatModList(list)); }

std::vector<std::string> AddNewMods(ModList& list, const std::vector<ModFolder>& found) {
  ModList::Profile& profile = list.Active();
  std::vector<const ModFolder*> fresh;
  for (const ModFolder& m : found) {
    bool listed = false;
    for (const auto& entry : profile.mods) {
      listed |= EqualNoCase(entry.first, m.manifest.id);
    }
    if (!listed) {
      fresh.push_back(&m);
    }
  }
  std::stable_sort(fresh.begin(), fresh.end(),
                   [](const ModFolder* a, const ModFolder* b) { return a->manifest.priority < b->manifest.priority; });
  std::vector<std::string> ids;
  for (const ModFolder* m : fresh) {
    profile.mods.emplace_back(m->manifest.id, false);
    ids.push_back(m->manifest.id);
  }
  return ids;
}

bool LoadResult::HasWarnings() const {
  return std::any_of(messages.begin(), messages.end(), [](const Message& m) { return m.level != Message::Level::Info; });
}

std::string LoadResult::Report() const {
  std::string out;
  for (const Message& m : messages) {
    if (m.level != Message::Level::Info) {
      out += (out.empty() ? "" : "\n\n") + m.text;
    }
  }
  return out;
}

std::string LoadResult::ActiveText() const {
  std::string out;
  for (const Manifest& m : active) {
    out += (out.empty() ? "" : ", ") + m.id + (m.version.empty() ? "" : " " + m.version);
  }
  return out;
}

AppliesVerdict CheckApplies(const ModFolder& mod, const LooksOrSound& looks, const DiscView* disc) {
  AppliesVerdict v;
  v.claimed = v.effective = mod.manifest.applies;
  if (v.claimed != Applies::AnyTime) {
    return v;
  }
  const auto downgrade = [&](std::string why) {
    v.effective = Applies::NewPlaythrough;
    v.reason = std::move(why);
    return v;
  };
  const auto filesIn = [](const fs::path& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      const std::u8string generic = fs::relative(it->path(), folder, ec).generic_u8string();
      out.emplace_back(reinterpret_cast<const char*>(generic.data()), generic.size());
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  if (mod.manifest.scripts) {
    return downgrade("it runs scripts (scripts/)");
  }
  for (const std::string& f : filesIn(mod.folder / "data")) {
    if (Lower(fs::path(f).extension().string()) == ".json") {
      return downgrade("it changes the game's tables (data/" + f + ")");
    }
  }
  for (const std::string& f : filesIn(mod.folder / "files")) {
    if (!looks || !looks(f, -1)) {
      return downgrade("it replaces " + f + ", which isn't only pictures or sound");
    }
  }
  std::map<std::string, std::vector<uint8_t>> archives;
  for (const std::string& f : filesIn(mod.folder / "fsys")) {
    const size_t slash = f.rfind('/');
    int type = -2;
    if (slash != std::string::npos && disc != nullptr && disc->read) {
      const std::string archive = f.substr(0, slash), entry = f.substr(slash + 1);
      std::string onDisc;
      for (const std::string& d : disc->files) {
        if (EqualNoCase(d, archive)) onDisc = d;
      }
      if (!onDisc.empty()) {
        auto it = archives.find(Lower(onDisc));
        if (it == archives.end()) {
          std::vector<uint8_t> bytes;
          if (!disc->read(onDisc, bytes)) bytes.clear();
          it = archives.emplace(Lower(onDisc), std::move(bytes)).first;
        }
        FsysArchive fsys;
        uint32_t index = 0;
        FsysEntry e;
        std::string error;
        if (!it->second.empty() && Fsys_Open(it->second.data(), it->second.size(), &fsys) &&
            ResolveEntry(fsys, onDisc, entry, index, error) && Fsys_Entry(&fsys, index, &e)) {
          type = static_cast<int>(e.type);
        }
      }
    }
    if (!looks || !looks(f, type)) {
      return downgrade("it replaces " + f + " inside an archive, which isn't only pictures or sound" +
                       (type == -2 ? std::string(" (or can't be checked without the disc)") : std::string()));
    }
  }
  return v;
}

LoadResult Load(const LoadOptions& options) {
  LoadResult result;
  Loader(options, result).Run();
  return result;
}

}
