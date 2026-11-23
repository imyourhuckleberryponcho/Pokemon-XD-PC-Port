#include "mods.h"

#include "mod_layout.h"
#include "i18n.h"
#include <gs_formats.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

namespace engine::mods {

namespace fs = std::filesystem;

namespace {

using engine::i18n::Tr;

std::string Fmt(const char* format, ...) {
  char buf[4096];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

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
        error = Fmt(Tr("%s has entries #0 to #%u, no %s"), discPath.c_str(), static_cast<unsigned>(fsys.count - 1), name.c_str());
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
  error = shortMatches.empty()
              ? Fmt(Tr("%s has no entry called %s"), discPath.c_str(), name.c_str())
              : Fmt(Tr("several entries of %s are called %s; name the one meant by its full name or number (%s)"),
                    discPath.c_str(), name.c_str(), fullNames.c_str());
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
    for (const std::string& listed : list.Active().scriptsOff) {
      if (const FoundMod* f = Find(listed)) mScriptsOff.insert(f->manifest.id);
    }
    std::vector<const FoundMod*> on = ChooseMods(list);
    for (const FoundMod* mod : on) {
      mResult.active.push_back(mod->manifest);
      mResult.activeApplies.push_back(VerdictOf(*mod));
      if (!mod->texturePack) CollectFiles(*mod);
    }
    ApplyData(on);
    Build();
    FinishRows(on);
    if (!mResult.active.empty()) {
      Say(Message::Level::Info, Fmt(Tr("Mods on: %s (%d disc file(s) replaced or added)"), mResult.ActiveText().c_str(),
                                    static_cast<int>(mResult.files.size())));
    }
  }

private:
  const LoadOptions& mOpt;
  LoadResult& mResult;
  std::vector<FoundMod> mFound;
  std::map<std::string, std::string> mDiscFiles;
  std::map<std::string, Layer> mLayers;
  std::map<std::string, std::vector<uint8_t>> mBaseCache;
  std::set<std::string> mScriptsOff;

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
      if (m->texturePack) {
        Say(Message::Level::Info, Fmt(Tr("New texture pack found: \"%s\" (%s). It is on (it only changes pictures); to stop using it, set \"%s = off\" in %s."),
                                      m->manifest.name.c_str(), m->manifest.id.c_str(), m->manifest.id.c_str(),
                                      Utf8(mOpt.listFile.filename()).c_str()));
        continue;
      }
      Say(Message::Level::Info, Fmt(Tr("New mod found: \"%s\" (%s). It is off; to use it, set \"%s = on\" in %s."),
                                    m->manifest.name.c_str(), m->manifest.id.c_str(), m->manifest.id.c_str(),
                                    Utf8(mOpt.listFile.filename()).c_str()));
    }
    if (!fresh.empty() || (!exists && !mFound.empty())) {
      if (mOpt.listReadOnly) {
        mResult.listWriteSkipped = true;
      } else if (WriteModListFile(mOpt.listFile, list)) {
        mResult.listWritten = true;
      } else {
        Say(Message::Level::Warning, Fmt(Tr("Could not write the mod list %s."), Utf8(mOpt.listFile).c_str()));
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
          Say(Message::Level::Info,
              Fmt(Tr("This playthrough was set up with the mod \"%s\", which is not in the mods folder any more."), id.c_str()));
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
        Say(Message::Level::Warning, Fmt(Tr("%s turns on \"%s\", but there is no such mod in the mods folder."),
                                         Utf8(mOpt.listFile.filename()).c_str(), id.c_str()));
        continue;
      }
      if (!SupportsGame(mod->manifest, mOpt.disc.gameId)) {
        Say(Message::Level::Problem,
            Fmt(Tr("The mod \"%s\" is not used: %s"), mod->manifest.name.c_str(), NotForTargetText(mod->manifest, mOpt.disc.gameId).c_str()));
        continue;
      }
      on.push_back(mod);
    }
    for (bool changed = true; changed;) {
      changed = false;
      for (size_t i = 0; i < on.size() && !changed; i++) {
        for (const std::string& need : on[i]->manifest.onlyWith) {
          if (std::none_of(on.begin(), on.end(), [&](const FoundMod* o) { return EqualNoCase(o->manifest.id, need); })) {
            Say(Message::Level::Info, Fmt(Tr("The mod \"%s\" is a compatibility patch for \"%s\", which is not on. It is not used."),
                                          on[i]->manifest.name.c_str(), need.c_str()));
            on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
          }
        }
      }
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
            Say(Message::Level::Problem,
                (*it)->manifest.version.empty()
                    ? Fmt(Tr("The mod \"%s\" needs the mod \"%s\" in version %s %s, but the one in the mods folder is without a "
                             "version. \"%s\" is not used."),
                          m.name.c_str(), (*it)->manifest.name.c_str(), v.op.c_str(), v.version.c_str(), m.name.c_str())
                    : Fmt(Tr("The mod \"%s\" needs the mod \"%s\" in version %s %s, but the one in the mods folder is version "
                             "%s. \"%s\" is not used."),
                          m.name.c_str(), (*it)->manifest.name.c_str(), v.op.c_str(), v.version.c_str(),
                          (*it)->manifest.version.c_str(), m.name.c_str()));
            on.erase(on.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
          }
          if (!there) {
            const FoundMod* other = Find(need);
            Say(Message::Level::Problem,
                other != nullptr
                    ? Fmt(Tr("The mod \"%s\" needs the mod \"%s\", which is not on (or can't be used). \"%s\" is not used."),
                          m.name.c_str(), need.c_str(), m.name.c_str())
                    : Fmt(Tr("The mod \"%s\" needs the mod \"%s\", which is not in the mods folder. \"%s\" is not used."),
                          m.name.c_str(), need.c_str(), m.name.c_str()));
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
            Say(Message::Level::Problem,
                Fmt(Tr("The mods \"%s\" and \"%s\" can't be used together (%s says so). \"%s\", further down the list, is not "
                       "used."),
                    earlier.name.c_str(), m.name.c_str(), (names(m.conflicts, earlier.id) ? m.name : earlier.name).c_str(),
                    m.name.c_str()));
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
            Say(Message::Level::Warning,
                Fmt(Tr("The mod \"%s\" should come after \"%s\" (it says so): move the line \"%s = on\" below \"%s = on\" in "
                       "%s. Until then the list's order is used."),
                    on[i]->manifest.name.c_str(), on[j]->manifest.name.c_str(), on[i]->manifest.id.c_str(),
                    on[j]->manifest.id.c_str(), Utf8(mOpt.listFile.filename()).c_str()));
          }
        }
      }
    }
    for (size_t i = 0; i < on.size(); i++) {
      for (const std::string& want : on[i]->manifest.before) {
        for (size_t j = 0; j < i; j++) {
          if (EqualNoCase(on[j]->manifest.id, want)) {
            Say(Message::Level::Warning,
                Fmt(Tr("The mod \"%s\" should come before \"%s\" (it says so): move the line \"%s = on\" above \"%s = on\" in "
                       "%s. Until then the list's order is used."),
                    on[i]->manifest.name.c_str(), on[j]->manifest.name.c_str(), on[i]->manifest.id.c_str(),
                    on[j]->manifest.id.c_str(), Utf8(mOpt.listFile.filename()).c_str()));
          }
        }
      }
    }
    return on;
  }

  static constexpr const char* kLayerPrefix = "extra-assets:";
  static bool IsLayerId(const std::string& id) { return id.rfind(kLayerPrefix, 0) == 0; }
  const LoadOptions::ExtraLayer* mLayerFilter = nullptr;
  bool LayerSkips(const std::string& archiveLower) const {
    if (mLayerFilter == nullptr) return false;
    if (!mLayerFilter->onlyArchives.empty() && mLayerFilter->onlyArchives.count(archiveLower) == 0) return true;
    return mLayerFilter->skipArchives.count(archiveLower) > 0;
  }
  std::map<std::string, std::set<std::string>> mLayerSkipped;
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
        mLayerFilter = &layer;
        r.files = CollectFiles(mod, true);
        mLayerFilter = nullptr;
      }
      mResult.extraLayers.push_back(std::move(r));
      const auto skipped = mLayerSkipped.find(LayerIdOf(layer));
      if (skipped != mLayerSkipped.end() && !skipped->second.empty()) {
        Say(Message::Level::Info, Fmt(Tr("The built-in layer \"%s\" has files for %zu archive(s) the game doesn't have (for example %s: they belong to a pack that is not loaded); skipped."),
                                      layer.name.c_str(), skipped->second.size(), skipped->second.begin()->c_str()));
      }
    }
  }
  static std::string LayerIdOf(const LoadOptions::ExtraLayer& layer) { return kLayerPrefix + layer.id; }

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

  std::vector<std::pair<std::string, fs::path>> FilesIn(const FoundMod& mod, const char* sub) {
    std::vector<std::pair<std::string, fs::path>> out;
    for (LayoutFile& f : LayoutFiles(mod.folder, mOpt.disc.gameId, sub)) {
      if (!IsAscii(f.rel)) {
        Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has \"%s\": disc file names only have English letters, digits and "
                                           "signs; ignored."),
                                         mod.manifest.name.c_str(), f.rel.c_str()));
        continue;
      }
      out.emplace_back(std::move(f.rel), std::move(f.path));
    }
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
    if (!mod.manifest.scripts) {
      if (HasLayoutFolder(mod.folder, mOpt.disc.gameId, "scripts")) {
        Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has a \"scripts\" folder but its mod.json does not say "
                                           "\"scripts\": true; the scripts are not used."),
                                         mod.manifest.name.c_str()));
      }
      return;
    }
    if (mScriptsOff.count(mod.manifest.id) != 0) {
      mResult.scriptsSwitchedOff.push_back(mod.manifest.id);
      Say(Message::Level::Info, Fmt(Tr("The mod \"%s\" has scripts, but running scripts is switched off for it (Mods page); its "
                                       "scripts are skipped and the rest of the mod is used."),
                                    mod.manifest.name.c_str()));
      return;
    }
    std::vector<std::pair<std::string, fs::path>> found;
    for (LayoutFile& f : LayoutFiles(mod.folder, mOpt.disc.gameId, "scripts", false)) {
      const std::string scriptExt = Lower(Utf8(fs::path(f.rel).extension()));
      if (scriptExt != ".luau" && scriptExt != ".lua") {
        Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has \"scripts/%s\": only .luau (or older .lua) files are read; ignored."),
                                         mod.manifest.name.c_str(), f.rel.c_str()));
        continue;
      }
      found.emplace_back(std::move(f.rel), std::move(f.path));
    }
    if (found.empty()) {
      Say(Message::Level::Warning,
          Fmt(Tr("The mod \"%s\" says \"scripts\": true but has no scripts/*.luau."), mod.manifest.name.c_str()));
      return;
    }
    if (found.size() > kMaxScriptFiles) {
      Say(Message::Level::Problem, Fmt(Tr("The mod \"%s\" has more than %d script files; its scripts are not used."),
                                       mod.manifest.name.c_str(), static_cast<int>(kMaxScriptFiles)));
      return;
    }
    std::vector<ScriptFile> files;
    for (const auto& [name, path] : found) {
      std::error_code sizeEc;
      const auto size = fs::file_size(path, sizeEc);
      std::string text;
      if (sizeEc || size > kMaxScriptFileBytes || !ReadAll(path, text)) {
        Say(Message::Level::Problem, Fmt(Tr("The mod \"%s\": scripts/%s could not be read or is bigger than %d KB; the mod's "
                                           "scripts are not used."),
                                         mod.manifest.name.c_str(), name.c_str(), static_cast<int>(kMaxScriptFileBytes / 1024)));
        return;
      }
      files.push_back({mod.manifest.id, "scripts/" + name, std::move(text)});
    }
    for (ScriptFile& f : files) mResult.scripts.push_back(std::move(f));
  }

  size_t CollectFiles(const FoundMod& mod, bool quiet = false) {
    size_t used = 0;
    const std::string& id = mod.manifest.id;
    const auto whole = FilesIn(mod, "files");
    const auto entries = FilesIn(mod, "fsys");
    if (!quiet && whole.empty() && entries.empty() && !HasLayoutFolder(mod.folder, mOpt.disc.gameId, "data") && !HasObjects(mod) &&
        !(mod.manifest.scripts && HasLayoutFolder(mod.folder, mOpt.disc.gameId, "scripts"))) {
      Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" is on but has no files in \"files\", \"fsys\" or \"data\"."),
                                       mod.manifest.name.c_str()));
    }
    CollectScripts(mod);
    for (const auto& [rel, path] : whole) {
      if (LayerSkips(Lower(rel))) continue;
      if (mDiscFiles.find(Lower(rel)) == mDiscFiles.end() && IsSystemName(rel)) {
        Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has \"files/%s\": the game's program and disc header can't be "
                                           "replaced by mods; ignored."),
                                         mod.manifest.name.c_str(), rel.c_str()));
        continue;
      }
      bool isNew = false;
      Layer& layer = LayerFor(rel, isNew);
      if (!layer.wholeFrom.empty() && !IsLayerId(layer.wholeFrom)) {
        Say(Message::Level::Warning, Fmt(Tr("Conflict: the mods \"%s\" and \"%s\" both replace %s. \"%s\" (further down the "
                                           "list) is used."),
                                         NameOf(layer.wholeFrom).c_str(), mod.manifest.name.c_str(), layer.discPath.c_str(),
                                         mod.manifest.name.c_str()));
      }
      if (std::any_of(layer.entries.begin(), layer.entries.end(),
                      [](const auto& e) { return !IsLayerId(e.second.from); })) {
        Say(Message::Level::Warning, Fmt(Tr("Conflict: \"%s\" replaces all of %s, so the changes inside it from %s are not used."),
                                         mod.manifest.name.c_str(), layer.discPath.c_str(), EntryMods(layer).c_str()));
      }
      layer.entries.clear();
      if (!layer.onDisc && isNew) {
        Say(Message::Level::Info, Fmt(Tr("The mod \"%s\" adds %s, which the disc doesn't have."), mod.manifest.name.c_str(),
                                      rel.c_str()));
      }
      layer.whole = path;
      layer.wholeFrom = id;
      mBaseCache.erase(Lower(rel));
      used++;
    }
    for (const auto& [rel, path] : entries) {
      const size_t slash = rel.rfind('/');
      if (slash == std::string::npos) {
        Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has \"fsys/%s\": files there go in a folder named after the archive, "
                                           "like fsys/common.fsys/<entry>; ignored."),
                                         mod.manifest.name.c_str(), rel.c_str()));
        continue;
      }
      const std::string archive = rel.substr(0, slash), entryName = rel.substr(slash + 1);
      if (LayerSkips(Lower(archive))) continue;
      bool isNew = false;
      Layer& layer = LayerFor(archive, isNew);
      if (!layer.onDisc && layer.wholeFrom.empty()) {
        if (IsLayerId(id)) {
          mLayerSkipped[id].insert(archive);
          if (isNew) {
            mLayers.erase(Lower(archive));
          }
          continue;
        }
        Say(Message::Level::Problem, Fmt(Tr("The mod \"%s\" changes %s inside %s, but the disc has no %s; ignored."),
                                         mod.manifest.name.c_str(), entryName.c_str(), archive.c_str(), archive.c_str()));
        if (isNew) {
          mLayers.erase(Lower(archive));
        }
        continue;
      }
      uint32_t index = 0;
      std::string error;
      if (!FindEntry(layer, entryName, index, error)) {
        Say(Message::Level::Problem, Fmt(Tr("The mod \"%s\": %s; ignored."), mod.manifest.name.c_str(), error.c_str()));
        if (isNew) {
          mLayers.erase(Lower(archive));
        }
        continue;
      }
      const auto had = layer.entries.find(index);
      if (had != layer.entries.end() && !IsLayerId(had->second.from)) {
        Say(Message::Level::Warning, Fmt(Tr("Conflict: the mods \"%s\" and \"%s\" both replace %s inside %s. \"%s\" (further "
                                           "down the list) is used."),
                                         NameOf(had->second.from).c_str(), mod.manifest.name.c_str(), entryName.c_str(),
                                         layer.discPath.c_str(), mod.manifest.name.c_str()));
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

  bool HasObjects(const FoundMod& mod) const {
    return !mod.texturePack && (HasLayoutFolder(mod.folder, mOpt.disc.gameId, "objects") || HasLayoutFolder(mod.folder, mOpt.disc.gameId, "texts") || HasLayoutFolder(mod.folder, mOpt.disc.gameId, "sounds"));
  }
  bool AnyHasObjects(const std::vector<const FoundMod*>& on) const {
    return std::any_of(on.begin(), on.end(), [&](const FoundMod* m) { return HasObjects(*m); });
  }

  void ApplyData(const std::vector<const FoundMod*>& on) {
    std::vector<DataModFile> files;
    for (const FoundMod* mod : on) {
      if (mod->texturePack) continue;
      for (const auto& [rel, path] : FilesIn(*mod, "data")) {
        const std::string label = "data/" + rel;
        if (Lower(Utf8(fs::path(rel).extension())) != ".json") {
          Say(Message::Level::Warning, Fmt(Tr("The mod \"%s\" has \"%s\": only .json files in \"data\" are read; ignored."),
                                           mod->manifest.name.c_str(), label.c_str()));
          continue;
        }
        DataModFile f{mod->manifest.id, mod->manifest.name, label, {}};
        f.labelNamespace = mod->manifest.labelNamespace;
        if (!ReadAll(path, f.text)) {
          Say(Message::Level::Problem, Fmt(Tr("Could not read %s; its changes are not used."), Utf8(path).c_str()));
          continue;
        }
        files.push_back(std::move(f));
      }
    }
    const bool tablesForScripts = mOpt.catalogForScripts && !mResult.scripts.empty() && static_cast<bool>(mOpt.dataCatalog);
    if (files.empty() && !mOpt.generateData && !tablesForScripts && !AnyHasObjects(on)) {
      return;
    }
    if (!mOpt.dataCatalog) {
      Say(Message::Level::Problem, Tr("Data mods (\"data\" folders) are not supported for this game yet; their changes are not used."));
      return;
    }
    auto catalog = std::make_shared<DataCatalog>();
    std::string error;
    const EntryReader reader = [this](const std::string& a, const std::string& e, std::vector<uint8_t>& out) {
      return ReadEntry(a, e, out);
    };
    if (!mOpt.dataCatalog(reader, *catalog, error)) {
      Say(Message::Level::Problem, Fmt(Tr("The game's tables could not be found on this disc (%s), so the data mods' changes are not "
                                         "used."),
                                       error.c_str()));
      return;
    }
    if (files.empty() && !mOpt.generateData) {
      mResult.catalog = catalog;
      return;
    }
    if (mOpt.generateData) {
      std::vector<DataModFile> made;
      if (!mOpt.generateData(*catalog, reader, made, error)) {
        Say(Message::Level::Problem, Fmt(Tr("The generated data (randomizer) could not be made: %s. The game starts without it."),
                                         error.c_str()));
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
        Say(Message::Level::Problem, Fmt(Tr("Data mods: %s; those changes are not used."), findError.c_str()));
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
    std::string summary = Fmt(Tr("Data mods: %d number(s) set in %d table(s)"), static_cast<int>(applied.changes.size()),
                              static_cast<int>(tables.size()));
    if (same != 0) {
      summary += Fmt(Tr(" (%d of them to the value the disc has)"), static_cast<int>(same));
    }
    summary += ", " + Fmt(archiveEntries == 1 ? Tr("%d archive entry changed") : Tr("%d archive entries changed"),
                          static_cast<int>(archiveEntries));
    if (!mResult.program.empty()) {
      summary += ", " + std::string(Tr("tables in the program changed"));
    }
    if (!applied.texts.empty()) {
      summary += ", " + Fmt(Tr("%d message text(s) rewritten"), static_cast<int>(applied.texts.size()));
    }
    Say(Message::Level::Info, summary);
    mResult.dataChanges = std::move(applied.changes);
    mResult.addedRows = std::move(applied.added);
    mResult.addedTexts = std::move(applied.addedTexts);
    mResult.addedTextGroup = catalog->addedTextGroup;
    if (!mResult.addedRows.empty()) {
      std::string list;
      for (const AddedRow& r : mResult.addedRows) {
        list += (list.empty() ? "" : ", ") + r.key + " (" + r.table + " #" + std::to_string(r.id) + ")";
      }
      Say(Message::Level::Info, Fmt(Tr("Data mods add %d row(s): %s"), static_cast<int>(mResult.addedRows.size()), list.c_str()));
    }
  }

  void FinishRows(const std::vector<const FoundMod*>& on) {
    if (!mOpt.finishRows || !mResult.catalog || (mResult.addedRows.empty() && !AnyHasObjects(on))) return;
    RowsJob job;
    job.rows = &mResult.addedRows;
    job.gameId = mOpt.disc.gameId;
    for (const FoundMod* m : on) job.modFolders[m->manifest.id] = m->folder;
    for (const auto& [id, folder] : mOpt.builtInFolders) job.modFolders[id] = folder;
    job.discFiles = mOpt.disc.files;
    job.catalog = mResult.catalog.get();
    job.checkEntry = mOpt.checkEntry;
    job.say = [this](Message::Level level, const std::string& text) { Say(level, text); };
    job.readEntry = [this](const std::string& a, const std::string& e, std::vector<uint8_t>& out) { return ReadEntry(a, e, out); };
    job.readFile = [this](const std::string& path, std::vector<uint8_t>& out) {
      for (const Replacement& r : mResult.files) {
        if (!EqualNoCase(r.discPath, path)) continue;
        if (r.rebuilt) {
          out = r.data;
          return true;
        }
        return ReadBytes(r.file, out);
      }
      return mOpt.disc.read && mOpt.disc.read(path, out);
    };
    mOpt.finishRows(job);
    for (RowsJob::OutFile& f : job.outFiles) {
      Replacement* at = nullptr;
      for (Replacement& r : mResult.files) {
        if (EqualNoCase(r.discPath, f.discPath)) at = &r;
      }
      if (at == nullptr) {
        mResult.files.emplace_back();
        at = &mResult.files.back();
        at->discPath = f.discPath;
        at->added = mDiscFiles.find(Lower(f.discPath)) == mDiscFiles.end();
      }
      at->data = std::move(f.data);
      at->rebuilt = true;
      at->file.clear();
      at->entries.clear();
      at->generated.clear();
      if (!f.mods.empty()) at->from += (at->from.empty() ? "" : ", ") + f.mods;
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
      error = Fmt(base == nullptr ? Tr("%s can't be read, so %s can't be replaced inside it")
                                  : Tr("%s is not an FSYS archive, so %s can't be replaced inside it"),
                  layer.discPath.c_str(), name.c_str());
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
      Say(Message::Level::Problem, Fmt(Tr("%s can't be read, so the changes inside it from %s are not used."),
                                       layer.discPath.c_str(), EntryMods(layer).c_str()));
      return false;
    }
    std::vector<std::vector<uint8_t>> contents;
    std::vector<FsysReplacement> list;
    contents.reserve(layer.entries.size());
    std::set<std::string> from;
    std::set<uint32_t> skipped;
    std::map<uint32_t, std::vector<uint8_t>> changed;
    for (const auto& [index, e] : layer.entries) {
      contents.emplace_back();
      if (e.generated) {
        contents.back() = e.data;
      } else if (!ReadBytes(e.file, contents.back()) || contents.back().size() > 0xFFFFFFFFu) {
        Say(Message::Level::Problem, Fmt(Tr("Could not read %s; the changes inside %s are not used."), Utf8(e.file).c_str(),
                                         layer.discPath.c_str()));
        return false;
      }
      FsysEntry old;
      if (!e.generated && mOpt.checkEntry && Fsys_Entry(&fsys, index, &old)) {
        std::string why;
        const std::vector<uint8_t> asGiven = contents.back();
        if (mOpt.checkEntry(static_cast<int>(old.type), e.from, contents.back(), mResult.addedRows, why)) {
          if (contents.back() != asGiven) {
            changed[index] = contents.back();
          }
        } else {
          Say(Message::Level::Problem,
              Fmt(Tr("The mod \"%s\": %s in %s is not usable (%s). That file is switched off; the game's own is used."),
                  NameOf(e.from).c_str(), e.name.c_str(), layer.discPath.c_str(), why.c_str()));
          contents.pop_back();
          skipped.insert(index);
          continue;
        }
      }
      if (!e.generated && Fsys_Entry(&fsys, index, &old) && contents.back().size() > old.size) {
        Say(Message::Level::Info, Fmt(Tr("%s: %s in %s is bigger than the original (%u bytes, was %u). If the game runs out of "
                                         "memory there, this may be why."),
                                      NameOf(e.from).c_str(), e.name.c_str(), layer.discPath.c_str(),
                                      static_cast<unsigned>(contents.back().size()), static_cast<unsigned>(old.size)));
      }
      list.push_back({index, contents.back().data(), static_cast<uint32_t>(contents.back().size())});
      for (size_t at = 0; at <= e.from.size();) {
        const size_t comma = std::min(e.from.find(", ", at), e.from.size());
        if (comma > at) from.insert(e.from.substr(at, comma - at));
        at = comma + 2;
      }
    }
    if (list.empty()) {
      return false;
    }
    size_t size = 0;
    uint8_t* data = Fsys_Rebuild(base->data(), base->size(), list.data(), list.size(), &size);
    if (data == nullptr) {
      Say(Message::Level::Problem, Fmt(Tr("%s could not be rebuilt with the changes from %s; they are not used."),
                                       layer.discPath.c_str(), EntryMods(layer).c_str()));
      return false;
    }
    r.data.assign(data, data + size);
    std::free(data);
    r.rebuilt = true;
    for (const auto& [index, e] : layer.entries) {
      if (skipped.count(index) != 0) {
        continue;
      }
      if (const auto c = changed.find(index); c != changed.end()) {
        r.generated.emplace_back(index, c->second);
      } else if (e.generated) {
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
void AddTexturePacks(std::vector<ModFolder>& found) {
  for (ModFolder& pack : FindTexturePacks()) {
    const bool taken = std::any_of(found.begin(), found.end(), [&](const ModFolder& m) { return EqualNoCase(m.manifest.id, pack.manifest.id); });
    if (!taken) found.push_back(std::move(pack));
  }
}
std::vector<ModFolder> FindModsImpl(const fs::path& modsFolder, std::vector<Message>& messages, std::vector<BrokenFolder>* broken) {
  std::vector<ModFolder> found;
  const auto say = [&](Message::Level level, std::string text) { messages.push_back({level, std::move(text)}); };
  const auto broke = [&](const std::string& folder, std::string reason) {
    if (broken != nullptr) broken->push_back({folder, std::move(reason)});
  };
  std::error_code ec;
  if (!fs::is_directory(modsFolder, ec)) {
    AddTexturePacks(found);
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
        say(Message::Level::Warning, Fmt(Tr("\"%s\" in the mods folder is a packed file: unpack it into its own folder there (the "
                                           "folder with mod.json in it)."),
                                         name.c_str()));
        broke(name, Tr("a packed file: unpack it into its own folder in the mods folder"));
      }
      continue;
    }
    const fs::path manifestPath = e.path() / kManifestFile;
    std::string text, error;
    if (!fs::is_regular_file(manifestPath, ec)) {
      say(Message::Level::Warning, Fmt(Tr("The folder \"%s\" in the mods folder has no %s, so it isn't a mod; ignored. (If the mod "
                                         "is one folder further in, move that folder up.)"),
                                       name.c_str(), kManifestFile));
      continue;
    }
    ModFolder mod;
    if (!ReadAll(manifestPath, text) || !ParseManifest(text, mod.manifest, error)) {
      say(Message::Level::Problem, Fmt(Tr("The mod in \"%s\" can't be used: its %s %s."), name.c_str(), kManifestFile,
                                       (error.empty() ? std::string(Tr("can't be read")) : error).c_str()));
      broke(name, std::string(kManifestFile) + " " + (error.empty() ? std::string(Tr("can't be read")) : error));
      continue;
    }
    bool duplicate = false;
    for (const ModFolder& other : found) {
      if (EqualNoCase(other.manifest.id, mod.manifest.id)) {
        say(Message::Level::Problem, Fmt(Tr("The folders \"%s\" and \"%s\" both hold a mod with the id \"%s\"; only \"%s\" is used. "
                                           "Remove one of them."),
                                         other.folderName.c_str(), name.c_str(), mod.manifest.id.c_str(), other.folderName.c_str()));
        broke(name, Fmt(Tr("the same id \"%s\" as the mod in \"%s\", which is used instead"), mod.manifest.id.c_str(),
                        other.folderName.c_str()));
        duplicate = true;
      }
    }
    if (!duplicate) {
      mod.folder = e.path();
      mod.folderName = name;
      found.push_back(std::move(mod));
    }
  }
  AddTexturePacks(found);
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
    profile.mods.emplace_back(m->manifest.id, m->texturePack);
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

std::string LoadResult::ReportCapped(size_t maxMessages) const {
  std::string out;
  size_t shown = 0, more = 0;
  for (const Message& m : messages) {
    if (m.level == Message::Level::Info) continue;
    if (shown >= maxMessages) { more++; continue; }
    shown++;
    std::string text = m.text;
    if (text.size() > 240) {
      text.resize(237);
      text += "...";
    }
    out += (out.empty() ? "" : "\n\n") + text;
  }
  if (more > 0) out += "\n\n" + Fmt(Tr("... and %zu more (see the log)"), more);
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
  const auto filesIn = [&](const char* sub) {
    std::vector<std::string> out;
    for (const fs::path& root : ContentRoots(mod.folder, std::string_view())) {
      std::error_code ec;
      const fs::path folder = root / sub;
      if (!fs::is_directory(folder, ec)) continue;
      for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
           it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::u8string generic = fs::relative(it->path(), folder, ec).generic_u8string();
        out.emplace_back(reinterpret_cast<const char*>(generic.data()), generic.size());
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  if (mod.manifest.scripts) {
    return downgrade(Tr("it runs scripts (scripts/)"));
  }
  for (const std::string& f : filesIn("data")) {
    if (Lower(fs::path(f).extension().string()) == ".json") {
      return downgrade(Fmt(Tr("it changes the game's tables (data/%s)"), f.c_str()));
    }
  }
  if (!filesIn("objects").empty()) {
    return downgrade(Tr("it adds people, zones, chests or music to rooms (objects/)"));
  }
  if (!filesIn("texts").empty()) {
    return downgrade(Tr("it adds text to rooms (texts/)"));
  }
  if (!filesIn("common_script").empty()) {
    return downgrade(Tr("it changes the common script (common_script/)"));
  }
  for (const std::string& f : filesIn("files")) {
    if (!looks || !looks(f, -1)) {
      return downgrade(Fmt(Tr("it replaces %s, which isn't only pictures or sound"), f.c_str()));
    }
  }
  std::map<std::string, std::vector<uint8_t>> archives;
  for (const std::string& f : filesIn("fsys")) {
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
      return downgrade(type == -2 ? Fmt(Tr("it replaces %s inside an archive, which isn't only pictures or sound (or can't be "
                                           "checked without the disc)"),
                                         f.c_str())
                                  : Fmt(Tr("it replaces %s inside an archive, which isn't only pictures or sound"), f.c_str()));
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
