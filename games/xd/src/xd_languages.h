#ifndef XD_LANGUAGES_H
#define XD_LANGUAGES_H

#include <filesystem>
#include <string>
#include <vector>

namespace xd::languages {

constexpr const char* kSetting = "system.language";
constexpr const char* kFolderName = "languages";
constexpr const char* kManifestId = "xd-languages";
constexpr const char* kPackFile = "languages.json";
constexpr int kPackVersion = 4;
constexpr int kPackVersionOld = 2;
constexpr const char* kPalGameId = "GXXP01";
constexpr const char* kJpGameId = "GXXJ01";
constexpr const char* kJpFolderName = "languages_jp";
constexpr const char* kJpManifestId = "xd-languages-jp";
constexpr const char* kJpId = "jp";
constexpr const char* kNoPackHint =
    "For more languages, add the European (PAL) copy of the game on the Extra assets page.";
constexpr const char* kWhenHint = "Takes effect the next time the game starts.";

struct Language {
  const char* id;
  const char* name;
  int number;
  const char* tag;
};
const std::vector<Language>& All();
const Language* Find(const std::string& id);

struct Pack {
  bool found = false;
  std::string problem;
  std::filesystem::path folder;
  std::filesystem::path jpFolder;
  bool jp = false;
  bool palFound = false;
  const std::filesystem::path& FolderOf(const std::string& id) const { return id == kJpId ? jpFolder : folder; }
  std::vector<const Language*> languages;
  bool Has(const std::string& id) const;
};
std::filesystem::path PackFolder(const std::filesystem::path& dataFolder);
Pack ReadPack(const std::filesystem::path& dataFolder);
std::vector<const Language*> Available(const Pack& pack);
std::filesystem::path TableFile(const Pack& pack, const std::string& name);
std::filesystem::path JpTableFile(const Pack& pack, const std::string& name);

const Language* Active();
const Pack& LoadedPack();

std::string PackJson(const std::vector<std::string>& languageIds, int archives, const std::string& source);

}

#endif
