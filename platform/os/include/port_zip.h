#ifndef PORT_ZIP_H
#define PORT_ZIP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortZip_StoreFolder(const wchar_t* folder, const wchar_t* zipPath);

#ifdef __cplusplus
}

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace port_zip {

struct File {
  std::string name;
  std::string data;
  uint16_t time = 0, date = 0;
};

void Time(const std::filesystem::path& file, uint16_t* time, uint16_t* date);

bool Store(const std::filesystem::path& zipPath, const std::vector<File>& files);

bool AsJpeg(const std::string& picture, int quality, std::string& jpeg);

}
#endif

#endif
