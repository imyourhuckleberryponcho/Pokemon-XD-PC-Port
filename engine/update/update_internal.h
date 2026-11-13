#ifndef ENGINE_UPDATE_INTERNAL_H
#define ENGINE_UPDATE_INTERNAL_H

#include "update.h"

namespace engine::update::detail {

std::string Utf8(const std::filesystem::path& p);
std::filesystem::path PathOf(const std::string& utf8);

bool SafeName(std::string name, std::string& out);
bool RemoveFolder(const std::filesystem::path& folder);
bool HoldsPlayerData(const std::filesystem::path& folder);

constexpr bool IsLinkMode(uint32_t unixMode) { return (unixMode & 0170000u) == 0120000u; }
bool FinishUnpackedFile(const std::filesystem::path& file, uint32_t unixMode);
bool MakeUnpackedLink(const std::filesystem::path& root, const std::string& relative, const std::string& target,
                      std::string& error);
bool AncestorIsLink(const std::filesystem::path& root, const std::string& relative);

std::string ReadSmallFile(const std::filesystem::path& file);

bool LooksLikeMacApp(const std::filesystem::path& programFolder);
Layout FindMacLayout(const std::filesystem::path& programFolder);
std::filesystem::path MacProgramFolder(const Layout& layout, const std::string& name);
bool InstallMacApp(const InstallRequest& request, std::string& installed, std::string& error,
                   const std::function<bool()>& stop);
bool GoBackMac(const Layout& layout, const std::vector<std::string>& requiredFiles, std::string& error);

}

#endif
