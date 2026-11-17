#ifndef ENGINE_MOD_LAYOUT_H
#define ENGINE_MOD_LAYOUT_H

#include "mods.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::mods {

constexpr const char* kGamesFolder = "games";

bool ValidGameFolderName(std::string_view name);
bool GameCodeMatches(std::string_view code, std::string_view discId);
bool SupportsGame(const Manifest& manifest, std::string_view discId);

bool SplitGameFolder(std::string_view rel, std::string& folder, std::string& rest);
std::string StripGameFolder(std::string_view rel);

std::vector<std::string> GameFolderNames(const std::filesystem::path& modFolder);
std::vector<std::filesystem::path> ContentRoots(const std::filesystem::path& modFolder, std::string_view discId);

struct LayoutFile {
  std::string rel;
  std::filesystem::path path;
  std::string from;
};
std::vector<LayoutFile> LayoutFiles(const std::filesystem::path& modFolder, std::string_view discId, std::string_view sub,
                                    bool recursive = true);
bool HasLayoutFolder(const std::filesystem::path& modFolder, std::string_view discId, std::string_view sub);
std::vector<std::filesystem::path> LayoutFolders(const std::filesystem::path& modFolder, std::string_view discId, std::string_view sub);

bool ResolveLayoutFile(const std::filesystem::path& modFolder, std::string_view discId, std::string_view rel, std::filesystem::path& out);

struct GameSupport {
  std::string code;
  std::string name;
  std::string shortName;
  enum class Level {
    Full,
    SharedOnly,
    Nothing
  } level = Level::Full;
  std::vector<std::string> folders;
};
std::vector<GameSupport> SupportOf(const std::filesystem::path& modFolder, const Manifest& manifest);
std::string SupportText(const std::vector<GameSupport>& support, bool translate = true);
bool HasSharedContent(const std::filesystem::path& modFolder);
std::string GameNameOf(std::string_view code, bool shortForm);

}

#endif
