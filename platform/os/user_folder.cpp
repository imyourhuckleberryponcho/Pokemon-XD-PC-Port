#include "port_user_folder.h"

#include <borealis/data.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Folder {
  fs::path path;
  std::string utf8;
  std::string note;
  std::unique_ptr<borealis::data::Manager> manager;
};

std::mutex sLock;
std::map<std::string, Folder> sFolders;

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

fs::path AppDataOverride() {
#ifdef _WIN32
  const wchar_t* variable = _wgetenv(L"APPDATA");
  if (variable == nullptr || variable[0] == L'\0') {
    return {};
  }
  PWSTR known = nullptr;
  fs::path real;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &known))) {
    real = known;
  }
  CoTaskMemFree(known);
  std::error_code ec;
  if (!real.empty() && (fs::equivalent(variable, real, ec) || _wcsicmp(variable, real.c_str()) == 0)) {
    return {};
  }
  return fs::path(variable);
#else
  return {};
#endif
}

fs::path DescriptorAbove(std::string& note) {
#ifdef _WIN32
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const fs::path own = fs::path(exe).parent_path();
  const fs::path above = own.parent_path();
  std::error_code ec;
  if (above.empty() || above == own || fs::exists(own / L"data_location.json", ec) ||
      !fs::exists(above / L"data_location.json", ec)) {
    return {};
  }
  std::FILE* f = _wfopen((above / L"data_location.json").c_str(), L"rb");
  if (f == nullptr) {
    return {};
  }
  std::string text;
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) {
    text.append(buf, n);
  }
  std::fclose(f);
  const auto value = [&](const char* key) {
    const size_t k = text.find(std::string("\"") + key + "\"");
    const size_t colon = k == std::string::npos ? k : text.find(':', k);
    const size_t open = colon == std::string::npos ? colon : text.find('"', colon);
    std::string out;
    for (size_t i = open == std::string::npos ? text.size() : open + 1; i < text.size() && text[i] != '"'; i++) {
      if (text[i] == '\\' && i + 1 < text.size()) {
        i++;
      }
      out += text[i];
    }
    return out;
  };
  const std::string mode = value("mode");
  if (mode == "portable") {
    note = "portable (data_location.json next to the program above)";
    return above / L"portable";
  }
  if (mode == "custom" && !value("customPath").empty()) {
    const std::string custom = value("customPath");
    note = "custom location (data_location.json next to the program above)";
    return fs::path(std::u8string(custom.begin(), custom.end()));
  }
#else
  (void)note;
#endif
  return {};
}

const char* ModeName(borealis::data::LocationMode mode) {
  switch (mode) {
  case borealis::data::LocationMode::Portable: return "portable";
  case borealis::data::LocationMode::Custom: return "custom location";
  case borealis::data::LocationMode::Default:
  default: return "default";
  }
}

Folder& Find(const char* appName) {
  const std::string name = appName != nullptr && appName[0] != '\0' ? appName : "Port";
  std::lock_guard<std::mutex> guard(sLock);
  auto [it, added] = sFolders.try_emplace(name);
  Folder& folder = it->second;
  if (!added) {
    return folder;
  }

  borealis::data::Options options;
  options.portableRelativePath = "portable";
  folder.manager = std::make_unique<borealis::data::Manager>(
      borealis::AppInfo{.orgName = {}, .appName = it->first}, std::move(options));

  const fs::path appData = AppDataOverride();
  std::string aboveNote;
  const fs::path above = appData.empty() ? DescriptorAbove(aboveNote) : fs::path();
  const fs::path override = !appData.empty() ? appData / fs::path(std::u8string(it->first.begin(), it->first.end()))
                                             : above;
  const borealis::data::Status status = folder.manager->initialize(override);
  if (status) {
    folder.path = folder.manager->paths().userPath;
    folder.note = !appData.empty() ? "APPDATA set for this run"
                  : !above.empty() ? aboveNote
                                   : ModeName(folder.manager->configured_mode());
  } else {
#ifdef _WIN32
    const wchar_t* variable = _wgetenv(L"APPDATA");
    folder.path = fs::path(variable != nullptr ? variable : L".") / fs::path(std::u8string(it->first.begin(), it->first.end()));
#else
    folder.path = fs::path(".") / it->first;
#endif
    std::error_code ec;
    fs::create_directories(folder.path, ec);
    char note[1024];
    std::snprintf(note, sizeof note, "Borealis couldn't set up the data folder (code %d at %s: %s), so the old one",
                  static_cast<int>(status.code), Utf8(status.path).c_str(), status.systemError.message().c_str());
    folder.note = note;
  }
  folder.utf8 = Utf8(folder.path);
  while (folder.utf8.size() > 1 && (folder.utf8.back() == '\\' || folder.utf8.back() == '/')) {
    folder.utf8.pop_back();
  }
  folder.path = fs::path(std::u8string(folder.utf8.begin(), folder.utf8.end()));
  return folder;
}

}

fs::path PortUser_Folder(const char* appName) { return Find(appName).path; }

extern "C" const char* PortUser_FolderUtf8(const char* appName) { return Find(appName).utf8.c_str(); }

extern "C" const char* PortUser_FolderNote(const char* appName) { return Find(appName).note.c_str(); }
