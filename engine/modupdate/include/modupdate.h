#ifndef ENGINE_MODUPDATE_H
#define ENGINE_MODUPDATE_H

#include <modcheck.h>
#include <modsign.h>
#include <mods.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace engine::modupdate {

struct Folders {
  std::filesystem::path modsFolder;
  std::filesystem::path stateFolder;
  std::filesystem::path Keys() const { return stateFolder / "mod_keys.json"; }
  std::filesystem::path Ini() const { return stateFolder / "modupdates.ini"; }
  std::filesystem::path Temp() const { return stateFolder / "tmp"; }
  std::filesystem::path Previous() const { return stateFolder / "previous"; }
};

bool AllowLocalForTests();
bool UrlAllowed(const std::string& url, std::string& why);
std::string HostOf(const std::string& url);

struct Choices {
  bool asked = false;
  bool always = false;
  std::map<std::string, std::string> skipped;
};
Choices LoadChoices(const Folders& f);
bool SaveChoices(const Folders& f, const Choices& c);

struct KeyStore {
  std::map<std::string, std::string> keys;
  std::string Pinned(const std::string& id) const;
};
KeyStore LoadKeys(const Folders& f);
bool SaveKeys(const Folders& f, const KeyStore& store);

enum class Trust { Trusted, FirstUse, Refused };
struct TrustVerdict {
  Trust trust = Trust::Refused;
  std::string text;
};
TrustVerdict EvaluateTrust(const engine::modcheck::UpdateFile& file, const std::string& expectedId, const std::string& pinned,
                           const std::string& authorKey);

std::string PermissionText(const std::string& code);
std::vector<std::string> AddedPermissions(const std::vector<std::string>& before, const std::vector<std::string>& now);
std::vector<std::string> PermissionCodes(const engine::modcheck::Report& report);

struct Offer {
  engine::modcheck::UpdateFile file;
  TrustVerdict trust;
  bool newer = false;
  std::string installedVersion;
  std::vector<std::string> addedPermissions;
  std::vector<std::string> installedPermissions;
  std::string error;
};
Offer FetchOffer(const std::string& url, const std::string& expectedId, const std::string& installedVersion,
                 const std::filesystem::path& installedFolder, const std::string& authorKey, const KeyStore& keys,
                 const engine::modcheck::GameRules* rules, const std::function<bool()>& stop = {});

struct Staged {
  bool ok = false;
  std::string error;
  std::filesystem::path root;
  std::filesystem::path folder;
  engine::modcheck::Report report;
  std::vector<std::string> actualPermissions;
  std::string antivirus;
  std::string findings;
};
Staged Download(const Folders& f, const Offer& offer, const engine::modcheck::GameRules* rules,
                const std::function<bool(uint64_t, uint64_t)>& progress = {});
struct AvResult {
  bool available = false, blocked = false;
  std::string text;
};
AvResult ScanDownload(const std::filesystem::path& file, const std::string& sourceUrl);

struct Change {
  bool worldChanged = false;
  bool addedRowsBreak = false;
  std::vector<std::string> reasons;
};
Change Analyse(const std::filesystem::path& oldFolder, const std::filesystem::path& newFolder);

struct Playthrough {
  std::string save;
  std::string label;
  std::string version;
};
std::vector<Playthrough> PlaythroughsUsing(const std::vector<std::pair<std::string, std::filesystem::path>>& cardRoots, const std::string& id);

struct InstallResult {
  bool ok = false;
  std::string error;
  std::filesystem::path modFolder;
  bool replaced = false;
};
InstallResult Install(const Folders& f, const Staged& staged, const std::string& id, const std::filesystem::path& existingFolder,
                      const std::string& pinKey);
struct PreviousInfo {
  bool exists = false;
  std::string version, date;
};
PreviousInfo Previous(const Folders& f, const std::string& id);
bool RollBack(const Folders& f, const std::string& id, const std::filesystem::path& existingFolder, std::string& error);
void DropStaged(const Staged& staged);

std::string ExportList(const engine::mods::ModList::Profile& profile, const std::vector<engine::mods::ModFolder>& found,
                       const std::string& listName, const std::string& gameId);
struct SharedMod {
  std::string id, name, version, updates;
  bool on = true;
};
struct SharedList {
  std::string name, game;
  std::vector<SharedMod> mods;
};
bool ParseList(const std::string& text, SharedList& out, std::string& error);
struct ImportRow {
  SharedMod mod;
  enum class State { Have, OtherVersion, Missing } state = State::Missing;
  std::string haveVersion;
  std::string link;
};
struct ImportPlan {
  std::vector<ImportRow> rows;
  std::string gameNote;
};
ImportPlan PlanImport(const SharedList& list, const std::vector<engine::mods::ModFolder>& found, const std::string& gameId);
std::vector<std::string> ApplyImport(engine::mods::ModList::Profile& profile, const ImportPlan& plan);

}

#endif
