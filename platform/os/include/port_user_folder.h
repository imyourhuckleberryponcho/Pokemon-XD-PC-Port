#ifndef PORT_USER_FOLDER_H
#define PORT_USER_FOLDER_H

#ifdef __cplusplus
#include <filesystem>
std::filesystem::path PortUser_Folder(const char* appName);
extern "C" {
#endif

const char* PortUser_FolderUtf8(const char* appName);

const char* PortUser_FolderNote(const char* appName);

#ifdef __cplusplus
}
#endif

#endif
