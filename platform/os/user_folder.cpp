#include "port_user_folder.h"

#include <borealis/data.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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
  PortUserKind kind = PortUserKind::User;
  fs::path userDefault;
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

fs::path DataFolderFlag() {
#ifdef _WIN32
  const wchar_t* value = _wgetenv(L"XD_DATA_FOLDER");
  return value != nullptr && value[0] != L'\0' ? fs::path(value) : fs::path();
#else
  const char* value = std::getenv("XD_DATA_FOLDER");
  return value != nullptr && value[0] != '\0' ? fs::path(value) : fs::path();
#endif
}

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

fs::path ExeDir() {
#ifdef _WIN32
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  return fs::path(exe).parent_path();
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof buffer;
  if (_NSGetExecutablePath(buffer, &size) != 0) return {};
  std::error_code ec;
  return fs::weakly_canonical(fs::path(buffer), ec).parent_path();
#else
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

bool IsGameFolderName(const std::string& name) {
  if (name == "game") return true;
  if (name.size() <= 5 || name.compare(0, 5, "game-") != 0) return false;
  for (size_t i = 5; i < name.size(); i++) {
    if (!(name[i] == '.' || (name[i] >= '0' && name[i] <= '9'))) return false;
  }
  return true;
}

struct Places {
  fs::path exe;
  fs::path above;
  fs::path write;
  bool macApp = false;
};

Places FindPlaces() {
  Places p;
  p.exe = ExeDir();
  if (p.exe.empty()) return p;
#ifdef __APPLE__
  const fs::path macos = p.exe;
  if (macos.filename() == "MacOS" && macos.parent_path().filename() == "Contents") {
    const fs::path app = macos.parent_path().parent_path();
    if (app.extension() == ".app") {
      p.macApp = true;
      p.above = app.parent_path();
      p.write = p.above;
      return p;
    }
  }
#endif
  p.above = p.exe.parent_path();
  if (p.above == p.exe) p.above.clear();
  p.write = !p.above.empty() && IsGameFolderName(Utf8(p.exe.filename())) ? p.above : p.exe;
  return p;
}

std::string ReadWholeFile(const fs::path& file) {
  std::string text;
  std::FILE* f =
#ifdef _WIN32
      _wfopen(file.c_str(), L"rb");
#else
      std::fopen(file.c_str(), "rb");
#endif
  if (f == nullptr) return text;
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  return text;
}

std::string JsonValue(const std::string& text, const char* key) {
  const size_t k = text.find(std::string("\"") + key + "\"");
  const size_t colon = k == std::string::npos ? k : text.find(':', k);
  const size_t open = colon == std::string::npos ? colon : text.find('"', colon);
  std::string out;
  for (size_t i = open == std::string::npos ? text.size() : open + 1; i < text.size() && text[i] != '"'; i++) {
    if (text[i] == '\\' && i + 1 < text.size()) i++;
    out += text[i];
  }
  return out;
}

fs::path DescriptorIn(const fs::path& dir, std::string& note, const char* where) {
  std::error_code ec;
  const fs::path file = dir / "data_location.json";
  if (dir.empty() || !fs::exists(file, ec)) return {};
  const std::string text = ReadWholeFile(file);
  const std::string mode = JsonValue(text, "mode");
  if (mode == "portable") {
    note = std::string("portable (data_location.json ") + where + ")";
    return dir / "portable";
  }
  if (mode == "custom" && !JsonValue(text, "customPath").empty()) {
    note = std::string("custom location (data_location.json ") + where + ")";
    return FromUtf8(JsonValue(text, "customPath"));
  }
  return {};
}

fs::path DescriptorAbove(std::string& note) {
  const Places p = FindPlaces();
  std::error_code ec;
  if (p.above.empty() || fs::exists(p.exe / "data_location.json", ec)) return {};
  return DescriptorIn(p.above, note, p.macApp ? "next to the app" : "next to the program above");
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
  const fs::path flagFolder = DataFolderFlag();
  const char* locationFile = std::getenv("XD_LOCATION_FILE");
  const bool locationFileWins = locationFile != nullptr && std::strcmp(locationFile, "1") == 0;
  std::string aboveNote;
  fs::path above;
  if (flagFolder.empty()) {
    if (appData.empty()) {
      above = DescriptorAbove(aboveNote);
    } else if (locationFileWins) {
      const Places places = FindPlaces();
      above = DescriptorIn(places.exe, aboveNote, "next to the program");
      if (above.empty()) above = DescriptorIn(places.above, aboveNote, "next to the program above");
    }
  }
  const fs::path override = !flagFolder.empty() ? flagFolder
                            : !above.empty()    ? above
                            : !appData.empty()  ? appData / fs::path(std::u8string(it->first.begin(), it->first.end()))
                                                : fs::path();
  const borealis::data::Status status = folder.manager->initialize(override);
  if (status) {
    folder.path = folder.manager->paths().userPath;
    folder.userDefault = appData.empty() ? folder.manager->paths().cachePath
                                         : appData / fs::path(std::u8string(it->first.begin(), it->first.end()));
    if (!flagFolder.empty()) {
      folder.note = "--data-folder (for this run)";
      folder.kind = PortUserKind::ThisRun;
    } else if (!above.empty()) {
      folder.note = aboveNote;
      folder.kind = aboveNote.rfind("portable", 0) == 0 ? PortUserKind::Portable : PortUserKind::Custom;
    } else if (!appData.empty()) {
      folder.note = "APPDATA set for this run";
      folder.kind = PortUserKind::TestAppData;
    } else {
      const auto mode = folder.manager->configured_mode();
      folder.note = ModeName(mode);
      folder.kind = mode == borealis::data::LocationMode::Portable ? PortUserKind::Portable
                    : mode == borealis::data::LocationMode::Custom ? PortUserKind::Custom
                                                                   : PortUserKind::User;
    }
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
  if (folder.userDefault.empty()) folder.userDefault = folder.path;
  if (folder.kind == PortUserKind::ThisRun || folder.kind == PortUserKind::Portable ||
      folder.kind == PortUserKind::Custom) {
    const char* cache = std::getenv("XD_CACHE_DIR");
    if (cache == nullptr || cache[0] == '\0') {
      const fs::path dir = folder.path / "cache";
      std::error_code ec;
      fs::create_directories(dir, ec);
#ifdef _WIN32
      _wputenv_s(L"XD_CACHE_DIR", dir.c_str());
#else
      setenv("XD_CACHE_DIR", dir.c_str(), 1);
#endif
    }
  }
  return folder;
}

}

fs::path PortUser_Folder(const char* appName) { return Find(appName).path; }

extern "C" const char* PortUser_FolderUtf8(const char* appName) { return Find(appName).utf8.c_str(); }

extern "C" const char* PortUser_FolderNote(const char* appName) { return Find(appName).note.c_str(); }

namespace {

std::FILE* OpenForWrite(const fs::path& file) {
#ifdef _WIN32
  return _wfopen(file.c_str(), L"wb");
#else
  return std::fopen(file.c_str(), "wb");
#endif
}

}

PortUserLocation PortUser_Location(const char* appName) {
  auto& folder = Find(appName);
  PortUserLocation out;
  out.kind = folder.kind;
  out.current = folder.path;
  out.userDefault = folder.userDefault;
  const Places places = FindPlaces();
  out.portable = places.write.empty() ? fs::path() : places.write / "portable";
  std::error_code ec;
  if (places.write.empty()) {
    out.portableProblem = "the program's folder isn't known";
  } else if (places.macApp && Utf8(places.exe).find("/AppTranslocation/") != std::string::npos) {
    out.portableProblem = "macOS runs this app from a read-only copy until you move it out of the Downloads folder; "
                          "move the app to a folder first";
  } else if (folder.kind != PortUserKind::Portable) {
    static std::mutex probeLock;
    static fs::path probed;
    static std::string probeProblem;
    std::lock_guard<std::mutex> guard(probeLock);
    if (probed != places.write) {
      const fs::path probe = places.write / "write_test.tmp";
      probeProblem.clear();
      if (std::FILE* f = OpenForWrite(probe)) {
        std::fclose(f);
        fs::remove(probe, ec);
      } else {
        probeProblem = "this folder can't be written to: " + Utf8(places.write);
      }
      probed = places.write;
    }
    out.portableProblem = probeProblem;
  }
  const char* locationFile = std::getenv("XD_LOCATION_FILE");
  out.canSwitch = folder.kind != PortUserKind::ThisRun &&
                  (folder.kind != PortUserKind::TestAppData ||
                   (locationFile != nullptr && std::strcmp(locationFile, "1") == 0));
  return out;
}

bool PortUser_SwitchLocation(const char* appName, bool toPortable, std::string& error) {
  const PortUserLocation here = PortUser_Location(appName);
  if (!here.canSwitch) {
    error = "This run keeps its data in a folder it was given (--data-folder); the choice can't be changed from here.";
    return false;
  }
  const Places places = FindPlaces();
  std::error_code ec;
  std::vector<fs::path> files;
  for (const fs::path& dir : {places.exe, places.above, here.userDefault}) {
    if (!dir.empty() && fs::exists(dir / "data_location.json", ec)) files.push_back(dir / "data_location.json");
  }
  if (!toPortable) {
    for (const fs::path& file : files) {
      fs::remove(file, ec);
      if (ec) {
        error = "can't remove " + Utf8(file) + ": " + ec.message();
        return false;
      }
    }
    return true;
  }
  if (!here.portableProblem.empty()) {
    error = here.portableProblem;
    return false;
  }
  const fs::path file = places.write / "data_location.json";
  fs::path temp = file;
  temp += ".tmp";
  std::FILE* f = OpenForWrite(temp);
  if (f == nullptr) {
    error = "can't write " + Utf8(file);
    return false;
  }
  const char text[] = "{\"version\": 1, \"mode\": \"portable\"}\n";
  const bool wrote = std::fwrite(text, 1, sizeof text - 1, f) == sizeof text - 1;
  std::fclose(f);
  if (!wrote) {
    error = "can't write " + Utf8(file);
    return false;
  }
  fs::remove(file, ec);
  fs::rename(temp, file, ec);
  if (ec) {
    error = "can't write " + Utf8(file) + ": " + ec.message();
    return false;
  }
  for (const fs::path& other : files) {
    std::error_code same;
    if (!fs::equivalent(other, file, same)) fs::remove(other, ec);
  }
  return true;
}
