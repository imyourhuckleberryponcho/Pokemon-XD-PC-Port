#ifndef PORT_USER_FOLDER_H
#define PORT_USER_FOLDER_H

#ifdef __cplusplus
#include <filesystem>
#include <string>
std::filesystem::path PortUser_Folder(const char* appName);

enum class PortUserKind {
  User,
  Portable,
  Custom,
  ThisRun,
  TestAppData
};
struct PortUserLocation {
  PortUserKind kind = PortUserKind::User;
  std::filesystem::path current;
  std::filesystem::path userDefault;
  std::filesystem::path portable;
  std::string portableProblem;
  bool canSwitch = false;
};
PortUserLocation PortUser_Location(const char* appName);
bool PortUser_SwitchLocation(const char* appName, bool toPortable, std::string& error);
extern "C" {
#endif

const char* PortUser_FolderUtf8(const char* appName);

const char* PortUser_FolderNote(const char* appName);

#ifdef __cplusplus
}
#endif

#endif
