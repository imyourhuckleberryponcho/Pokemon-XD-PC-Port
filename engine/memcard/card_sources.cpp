#include <card_sources.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace engine::memcard::sources {

namespace fs = std::filesystem;

namespace {

fs::path PathOf(const std::string& utf8) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool IsDir(const fs::path& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}
bool IsFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::pair<std::string, std::string>> ReadIniPaths(const fs::path& ini) {
  std::vector<std::pair<std::string, std::string>> out;
  std::ifstream in(ini, std::ios::binary);
  if (!in) return out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto trim = [](std::string s) {
      while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
      return s;
    };
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (value.empty()) continue;
    if (key == "MemcardAPath" || key == "MemcardBPath" || key == "GCIFolderAPath" || key == "GCIFolderBPath" ||
        key == "GCIFolderAPathOverride" || key == "GCIFolderBPathOverride") {
      out.emplace_back(key, value);
    }
  }
  return out;
}

void AddGciFolder(const fs::path& folder, const std::string& where, std::vector<Found>& out) {
  std::error_code ec;
  if (!IsDir(folder)) return;
  std::vector<fs::path> names;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (e.is_regular_file(ec) && Lower(Utf8(e.path().extension())) == ".gci") names.push_back(e.path());
  }
  std::sort(names.begin(), names.end());
  for (const fs::path& p : names) out.push_back({p, false, where});
}

bool CardImageName(const fs::path& p) {
  const std::string ext = Lower(Utf8(p.extension()));
  return ext == ".raw" || ext == ".gcp" || ext == ".mcd" || ext == ".mci";
}

}

Environment RealEnvironment() {
  Environment env;
#if defined(_WIN32)
  env.os = Environment::Os::Windows;
#elif defined(__APPLE__)
  env.os = Environment::Os::Mac;
#else
  env.os = Environment::Os::Linux;
#endif
  env.getenv = [](const char* name) {
    const char* v = std::getenv(name);
    return std::string(v != nullptr ? v : "");
  };
  return env;
}

std::vector<fs::path> DolphinUserFolders(const Environment& env) {
  std::vector<fs::path> candidates;
  auto add = [&](const fs::path& p) {
    if (!p.empty()) candidates.push_back(p);
  };
  auto var = [&](const char* name) { return env.getenv ? env.getenv(name) : std::string(); };
  {
    const std::string list = var("XD_DOLPHIN_USER_DIR");
    std::string item;
    for (char c : list + ";") {
      if (c == ';' || (c == ':' && env.os != Environment::Os::Windows)) {
        if (!item.empty()) add(PathOf(item));
        item.clear();
      } else {
        item += c;
      }
    }
  }
  const std::string home = var(env.os == Environment::Os::Windows ? "USERPROFILE" : "HOME");
  switch (env.os) {
  case Environment::Os::Windows: {
    if (!var("APPDATA").empty()) add(PathOf(var("APPDATA")) / L"Dolphin Emulator");
    if (!home.empty()) add(PathOf(home) / L"Documents" / L"Dolphin Emulator");
    if (!var("OneDrive").empty()) add(PathOf(var("OneDrive")) / L"Documents" / L"Dolphin Emulator");
    if (!var("LOCALAPPDATA").empty()) add(PathOf(var("LOCALAPPDATA")) / L"Dolphin Emulator");
    break;
  }
  case Environment::Os::Mac:
    if (!home.empty()) {
      add(PathOf(home) / "Library" / "Application Support" / "Dolphin");
      add(PathOf(home) / ".dolphin-emu");
    }
    break;
  case Environment::Os::Linux: {
    const std::string xdgData = var("XDG_DATA_HOME");
    const std::string xdgConfig = var("XDG_CONFIG_HOME");
    if (!xdgData.empty()) add(PathOf(xdgData) / "dolphin-emu");
    if (!home.empty()) {
      add(PathOf(home) / ".local" / "share" / "dolphin-emu");
      add(PathOf(home) / ".dolphin-emu");
      add(PathOf(home) / ".var" / "app" / "org.DolphinEmu.dolphin-emu" / "data" / "dolphin-emu");
    }
    if (!xdgConfig.empty()) add(PathOf(xdgConfig) / "dolphin-emu");
    if (!home.empty()) add(PathOf(home) / ".config" / "dolphin-emu");
    break;
  }
  }
  std::vector<fs::path> found;
  std::set<std::string> seen;
  for (const fs::path& c : candidates) {
    std::error_code ec;
    if (!IsDir(c)) continue;
    if (!IsDir(c / "GC") && !IsFile(c / "Config" / "Dolphin.ini") && !IsFile(c / "Dolphin.ini")) continue;
    const fs::path canon = fs::weakly_canonical(c, ec);
    if (!seen.insert(Utf8(ec ? c : canon)).second) continue;
    found.push_back(c);
  }
  return found;
}

std::vector<Found> FindDolphinCardFiles(const std::vector<fs::path>& userFolders, const std::string& region) {
  std::vector<Found> all;
  for (const fs::path& user : userFolders) {
    const std::string label = userFolders.size() > 1 ? "Dolphin (" + Utf8(user) + ")" : std::string("Dolphin");
    for (const char* card : {"Card A", "Card B"}) {
      AddGciFolder(user / "GC" / PathOf(region) / card, label + ": GCI folder, " + card, all);
    }
    std::error_code ec;
    if (IsDir(user / "GC")) {
      std::vector<fs::path> raws;
      for (const auto& e : fs::directory_iterator(user / "GC", ec)) {
        if (e.is_regular_file(ec) && CardImageName(e.path())) {
          const std::string name = Utf8(e.path().filename());
          const bool namesOther = (name.find(".EUR.") != std::string::npos && region != "EUR") ||
                                  (name.find(".JAP.") != std::string::npos && region != "JAP") ||
                                  (name.find(".USA.") != std::string::npos && region != "USA");
          if (!namesOther) raws.push_back(e.path());
        }
      }
      std::sort(raws.begin(), raws.end());
      for (const fs::path& p : raws) all.push_back({p, true, label + ": memory card file"});
    }
    std::vector<fs::path> inis = {user / "Config" / "Dolphin.ini", user / "Dolphin.ini"};
    if (user.filename() == "dolphin-emu") {
      const fs::path share = user.parent_path();
      if (share.filename() == "share" && share.parent_path().filename() == ".local") {
        inis.push_back(share.parent_path().parent_path() / ".config" / "dolphin-emu" / "Dolphin.ini");
      }
    }
    for (const fs::path& ini : inis) {
      for (const auto& [key, value] : ReadIniPaths(ini)) {
        const fs::path p = PathOf(value);
        if (key.rfind("Memcard", 0) == 0) {
          if (IsFile(p)) all.push_back({p, true, label + ": memory card file chosen in Dolphin"});
        } else if (IsDir(p)) {
          AddGciFolder(p, label + ": GCI folder chosen in Dolphin", all);
          AddGciFolder(p / PathOf(region) / "Card A", label + ": GCI folder chosen in Dolphin", all);
        }
      }
    }
  }
  std::vector<Found> out;
  std::set<std::string> seen;
  for (Found& f : all) {
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(f.path, ec);
    if (seen.insert(Utf8(ec ? f.path : canon)).second) out.push_back(std::move(f));
  }
  return out;
}

const char* ContainerName(Container c) {
  switch (c) {
  case Container::Gci: return ".gci file";
  case Container::CardImage: return "memory card image";
  case Container::Gcs: return "GameShark .gcs file";
  case Container::DatelSav: return "Datel .sav file";
  default: return "unknown";
  }
}

namespace {

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

bool ReadWrapped(const std::vector<uint8_t>& bytes, size_t header, bool swapped, CardFile& out) {
  if (bytes.size() < header + kDirEntrySize + kBlockSize) return false;
  std::array<uint8_t, kDirEntrySize> raw{};
  std::memcpy(raw.data(), bytes.data() + header, kDirEntrySize);
  if (swapped) {
    std::swap(raw[0x38], raw[0x39]);
    std::swap(raw[0x36], raw[0x37]);
  }
  DirEntry e = ParseDirEntry(raw.data());
  if (e.blockCount == 0 || bytes.size() != header + kDirEntrySize + static_cast<size_t>(e.blockCount) * kBlockSize) {
    return false;
  }
  out.entry = e;
  out.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(header + kDirEntrySize), bytes.end());
  return true;
}

}

bool ReadAnyCardFile(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, Container& kind,
                     std::string& error) {
  files.clear();
  kind = Container::Unknown;
  if (bytes.empty()) {
    error = "The file is empty.";
    return false;
  }
  if (bytes.size() >= 12 && std::memcmp(bytes.data(), "DATELGC_SAVE", 12) == 0) {
    CardFile f;
    for (bool swapped : {false, true}) {
      if (ReadWrapped(bytes, 0x80, swapped, f)) {
        files.push_back(std::move(f));
        kind = Container::DatelSav;
        return true;
      }
    }
    error = "This is a Datel / Action Replay .sav file, but its contents don't add up. In Dolphin's memory card "
            "manager, or on the console, export the save as a .gci file instead.";
    return false;
  }
  if (bytes.size() >= 6 && std::memcmp(bytes.data(), "GCSAVE", 6) == 0) {
    CardFile f;
    if (ReadWrapped(bytes, 0x110, false, f)) {
      files.push_back(std::move(f));
      kind = Container::Gcs;
      return true;
    }
    error = "This is a GameShark .gcs file, but its contents don't add up. Export the save as a .gci file instead.";
    return false;
  }
  if (bytes.size() == 0x8000 || bytes.size() == 0x10000 || bytes.size() == 0x20000 || bytes.size() == 0x200) {
    error = "This looks like a Game Boy Advance save (" + std::to_string(bytes.size() / 1024) +
            " KB), not a GameCube memory card file. GBA saves go through the GBA trade, not through here.";
    return false;
  }
  if (bytes.size() >= kDirEntrySize && bytes.size() % kBlockSize == kDirEntrySize) {
    CardFile f;
    if (!ReadGci(bytes, f, error)) {
      error = "This is not a GameCube save file (.gci): " + error + ".";
      return false;
    }
    files.push_back(std::move(f));
    kind = Container::Gci;
    return true;
  }
  CardImageInfo info;
  std::string why;
  if (ReadCardImage(bytes, files, info, why)) {
    kind = Container::CardImage;
    return true;
  }
  error = "This is not a GameCube memory card file: it is neither a .gci save nor a whole memory card image (" + why +
          ").";
  return false;
}

}
