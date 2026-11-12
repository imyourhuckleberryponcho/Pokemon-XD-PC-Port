#ifndef XD_USER_FOLDER_H
#define XD_USER_FOLDER_H

#include "xd_test_run.h"

#include <port_user_folder.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace engine::settings {
class Registry;
}

namespace xd {

enum class SettingsSave { Saved, Failed, Skipped };
SettingsSave SaveSettings(const engine::settings::Registry& registry);

inline std::filesystem::path UserFolder() { return PortUser_Folder("Pokemon XD"); }

inline std::filesystem::path SettingsFolder() {
#ifdef _WIN32
  const wchar_t* dir = _wgetenv(L"XD_SETTINGS_DIR");
#else
  const char* dir = std::getenv("XD_SETTINGS_DIR");
#endif
  if (dir != nullptr && dir[0] != 0) {
    return std::filesystem::path(dir);
  }
  return UserFolder();
}

std::filesystem::path CardRoot();
std::string ActiveSaveName();
bool SaveSlotsInUse();
void ForgetActiveSave();
class CardRootScope {
public:
  explicit CardRootScope(const std::filesystem::path& root);
  ~CardRootScope();
  CardRootScope(const CardRootScope&) = delete;
  CardRootScope& operator=(const CardRootScope&) = delete;

private:
  std::filesystem::path mBefore;
  bool mHadBefore;
};

}

extern "C" const char* PortXD_CardRootUtf8(void);
extern "C" const char* PortXD_CardSaveName(void);

#endif
