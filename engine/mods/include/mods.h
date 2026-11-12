#ifndef ENGINE_MODS_H
#define ENGINE_MODS_H

#include "data_mods.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::mods {

constexpr const char* kManifestFile = "mod.json";
constexpr int kManifestFormat = 1;

struct VersionNeed {
  std::string op, version;
  bool SatisfiedBy(std::string_view have) const;
};
int CompareVersions(std::string_view a, std::string_view b);

enum class Applies { AnyTime, Restart, NewPlaythrough };
const char* AppliesId(Applies applies);
const char* AppliesName(Applies applies);
bool ParseApplies(std::string_view text, Applies& out);

struct Manifest {
  std::string id, name, version, author, description;
  std::vector<std::string> games;
  std::vector<std::string> required, conflicts;
  std::vector<VersionNeed> requiredVersions;
  std::vector<std::string> after;
  int priority = 0;
  Applies applies = Applies::NewPlaythrough;
  bool appliesGiven = false;
};

bool ParseManifest(std::string_view json, Manifest& out, std::string& error);
bool ValidModId(std::string_view id);

constexpr const char* kListFile = "mods.ini";

struct ModList {
  struct Profile {
    std::string name;
    std::vector<std::pair<std::string, bool>> mods;
  };
  std::string active = "default";
  std::vector<Profile> profiles;

  Profile& Active();
};

ModList ParseModList(std::string_view text, std::vector<std::string>& warnings);
std::string FormatModList(const ModList& list);

struct DiscView {
  std::string gameId;
  std::vector<std::string> files;
  std::function<bool(const std::string& path, std::vector<uint8_t>& out)> read;
  std::function<bool(std::vector<uint8_t>& out)> readProgram;
};

struct Message {
  enum class Level { Info, Warning, Problem };
  Level level;
  std::string text;
};

struct Replacement {
  std::string discPath;
  std::filesystem::path file;
  std::vector<uint8_t> data;
  bool rebuilt = false;
  std::vector<std::pair<uint32_t, std::filesystem::path>> entries;
  bool added = false;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> generated;
  std::string from;
};

using LooksOrSound = std::function<bool(const std::string& path, int fsysType)>;

struct AppliesVerdict {
  Applies claimed = Applies::NewPlaythrough;
  Applies effective = Applies::NewPlaythrough;
  std::string reason;
  bool Downgraded() const { return effective != claimed; }
};

struct LoadOptions {
  std::filesystem::path modsFolder;
  std::filesystem::path listFile;
  DiscView disc;
  CatalogBuilder dataCatalog;
  LooksOrSound looksOrSound;
  bool usePlaythroughMods = false;
  std::vector<std::string> playthroughMods;
  std::function<bool(const DataCatalog& catalog, const EntryReader& read, std::vector<DataModFile>& out, std::string& error)>
      generateData;
  bool listReadOnly = false;
};

struct LoadResult {
  std::vector<Manifest> active;
  std::vector<AppliesVerdict> activeApplies;
  std::vector<Replacement> files;
  std::vector<Message> messages;
  bool listWritten = false;
  bool listWriteSkipped = false;
  std::vector<DataChange> dataChanges;
  std::shared_ptr<const DataCatalog> catalog;
  std::vector<uint8_t> program;

  bool HasWarnings() const;
  std::string Report() const;
  std::string ActiveText() const;
};

LoadResult Load(const LoadOptions& options);

struct ModFolder {
  Manifest manifest;
  std::filesystem::path folder;
  std::string folderName;
};

std::vector<ModFolder> FindMods(const std::filesystem::path& modsFolder, std::vector<Message>& messages);
bool ReadModListFile(const std::filesystem::path& file, ModList& list, std::vector<std::string>& warnings);
bool WriteModListFile(const std::filesystem::path& file, const ModList& list);
std::vector<std::string> AddNewMods(ModList& list, const std::vector<ModFolder>& found);

AppliesVerdict CheckApplies(const ModFolder& mod, const LooksOrSound& looks, const DiscView* disc = nullptr);

}

#endif
