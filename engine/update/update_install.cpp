#include "update_internal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

using detail::Utf8;
using detail::PathOf;
using detail::RemoveFolder;
using detail::HoldsPlayerData;
using detail::SafeName;

struct File {
  std::FILE* f = nullptr;
#if defined(_WIN32)
  explicit File(const fs::path& path, const wchar_t* mode) : f(_wfopen(path.c_str(), mode)) {}
#else
  explicit File(const fs::path& path, const wchar_t* mode)
      : f(std::fopen(path.c_str(), mode[0] == L'w' ? "wb" : "rb")) {}
#endif
  ~File() {
    if (f != nullptr) {
      std::fclose(f);
    }
  }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  bool Read(void* into, size_t size) { return std::fread(into, 1, size, f) == size; }
#if defined(_WIN32)
  bool Seek(uint64_t offset) { return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0; }
#else
  bool Seek(uint64_t offset) { return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0; }
#endif
};

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}

bool ExtractZip(const fs::path& zip, const fs::path& folder, std::string& error, const std::function<bool()>& stop) {
  File in(zip, L"rb");
  std::error_code ec;
  const uint64_t size = fs::file_size(zip, ec);
  if (in.f == nullptr || ec || size < 22) {
    error = "the package can't be read";
    return false;
  }
  const uint64_t tailSize = std::min<uint64_t>(size, 65557);
  std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
  if (!in.Seek(size - tailSize) || !in.Read(tail.data(), tail.size())) {
    error = "the package can't be read";
    return false;
  }
  size_t end = std::string::npos;
  for (size_t i = tail.size() - 22 + 1; i-- > 0;) {
    if (U32(&tail[i]) == 0x06054b50) {
      end = i;
      break;
    }
  }
  if (end == std::string::npos) {
    error = "the package isn't a zip file";
    return false;
  }
  const uint16_t count = U16(&tail[end + 10]);
  const uint32_t dirSize = U32(&tail[end + 12]), dirOffset = U32(&tail[end + 16]);
  if (count == 0xFFFF || dirOffset == 0xFFFFFFFF || U16(&tail[end + 4]) != 0 || U16(&tail[end + 6]) != 0) {
    error = "the package is a kind of zip this version can't unpack (ZIP64 or split)";
    return false;
  }
  if (static_cast<uint64_t>(dirOffset) + dirSize > size) {
    error = "the package is damaged (its file list is outside it)";
    return false;
  }
  std::vector<uint8_t> dir(dirSize);
  if (!in.Seek(dirOffset) || !in.Read(dir.data(), dir.size())) {
    error = "the package can't be read";
    return false;
  }
  struct Entry {
    std::string name;
    bool directory;
    uint16_t method, flags;
    uint32_t crc, packed, unpacked, offset;
    uint32_t unixMode;
  };
  std::vector<Entry> entries;
  uint64_t totalUnpacked = 0;
  size_t p = 0;
  for (uint16_t i = 0; i < count; i++) {
    if (p + 46 > dir.size() || U32(&dir[p]) != 0x02014b50) {
      error = "the package is damaged (its file list)";
      return false;
    }
    Entry e{};
    e.flags = U16(&dir[p + 8]);
    e.unixMode = (U16(&dir[p + 4]) >> 8) == 3 ? (U32(&dir[p + 38]) >> 16) : 0;
    e.method = U16(&dir[p + 10]);
    e.crc = U32(&dir[p + 16]);
    e.packed = U32(&dir[p + 20]);
    e.unpacked = U32(&dir[p + 24]);
    const uint16_t nameLen = U16(&dir[p + 28]), extraLen = U16(&dir[p + 30]), commentLen = U16(&dir[p + 32]);
    e.offset = U32(&dir[p + 42]);
    if (p + 46 + nameLen + extraLen + commentLen > dir.size()) {
      error = "the package is damaged (its file list)";
      return false;
    }
    std::string raw(reinterpret_cast<const char*>(&dir[p + 46]), nameLen);
    p += 46 + nameLen + extraLen + commentLen;
    e.directory = !raw.empty() && (raw.back() == '/' || raw.back() == '\\');
    if (e.directory) {
      raw.pop_back();
    }
    if (!SafeName(raw, e.name)) {
      error = "the package holds a file name that isn't allowed: " + raw;
      return false;
    }
    if (e.name == "__MACOSX" || e.name.rfind("__MACOSX/", 0) == 0 || e.name == ".DS_Store" ||
        (e.name.size() > 10 && e.name.compare(e.name.size() - 10, 10, "/.DS_Store") == 0)) {
      continue;
    }
    if ((e.flags & 1) != 0 || (e.method != 0 && e.method != 8) || e.packed == 0xFFFFFFFF || e.unpacked == 0xFFFFFFFF) {
      error = "the package holds a file this version can't unpack: " + e.name;
      return false;
    }
    totalUnpacked += e.unpacked;
    if (totalUnpacked > 2 * kMaxPackageBytes) {
      error = "the package is far too large when unpacked";
      return false;
    }
    entries.push_back(std::move(e));
  }

  fs::create_directories(folder, ec);
  std::vector<uint8_t> packed(1 << 16), unpacked(1 << 16);
  for (const Entry& e : entries) {
    if (stop && stop()) {
      error = "stopped";
      return false;
    }
    const fs::path out = folder / PathOf(e.name);
    if (e.directory) {
      fs::create_directories(out, ec);
      continue;
    }
    if (detail::AncestorIsLink(folder, e.name)) {
      error = "the package writes through a link: " + e.name;
      return false;
    }
    fs::create_directories(out.parent_path(), ec);
    uint8_t local[30];
    if (!in.Seek(e.offset) || !in.Read(local, sizeof local) || U32(local) != 0x04034b50) {
      error = "the package is damaged (" + e.name + ")";
      return false;
    }
    const uint64_t data = static_cast<uint64_t>(e.offset) + 30 + U16(local + 26) + U16(local + 28);
    if (data + e.packed > size || !in.Seek(data)) {
      error = "the package is damaged (" + e.name + ")";
      return false;
    }
    File target(out, L"wb");
    if (target.f == nullptr) {
      error = "a file couldn't be written: " + Utf8(out);
      return false;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t written = 0;
    bool ok = true;
    if (e.method == 0) {
      for (uint64_t left = e.packed; ok && left > 0;) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(left, packed.size()));
        ok = in.Read(packed.data(), n) && std::fwrite(packed.data(), 1, n, target.f) == n;
        crc = crc32(crc, packed.data(), static_cast<uInt>(n));
        written += n;
        left -= n;
      }
    } else {
      z_stream z{};
      ok = inflateInit2(&z, -MAX_WBITS) == Z_OK;
      int status = Z_OK;
      for (uint64_t left = e.packed; ok && status != Z_STREAM_END;) {
        if (z.avail_in == 0) {
          if (left == 0) {
            ok = false;
            break;
          }
          const size_t n = static_cast<size_t>(std::min<uint64_t>(left, packed.size()));
          if (!in.Read(packed.data(), n)) {
            ok = false;
            break;
          }
          left -= n;
          z.next_in = packed.data();
          z.avail_in = static_cast<uInt>(n);
        }
        z.next_out = unpacked.data();
        z.avail_out = static_cast<uInt>(unpacked.size());
        status = inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
          ok = false;
          break;
        }
        const size_t n = unpacked.size() - z.avail_out;
        written += n;
        if (written > e.unpacked || std::fwrite(unpacked.data(), 1, n, target.f) != n) {
          ok = false;
          break;
        }
        crc = crc32(crc, unpacked.data(), static_cast<uInt>(n));
      }
      inflateEnd(&z);
    }
    if (std::fclose(target.f) != 0) {
      ok = false;
    }
    target.f = nullptr;
    if (!ok || written != e.unpacked || crc != e.crc) {
      error = "the package is damaged: " + e.name + " doesn't unpack correctly";
      return false;
    }
    if (detail::IsLinkMode(e.unixMode)) {
      const std::string text = detail::ReadSmallFile(out);
      fs::remove(out, ec);
      if (text.empty() || !detail::MakeUnpackedLink(folder, e.name, text, error)) {
        error = error.empty() ? "the package holds an empty link: " + e.name : error;
        return false;
      }
    } else if (!detail::FinishUnpackedFile(out, e.unixMode)) {
      error = "a file's permissions couldn't be set: " + e.name;
      return false;
    }
  }
  return true;
}

bool IsGameFolderName(std::string_view name) {
  if (name == kFirstFolder) {
    return true;
  }
  const std::string prefix = std::string(kFirstFolder) + "-";
  std::vector<uint32_t> parts;
  return name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
         name.substr(prefix.size()).front() != 'v' && ParseVersion(name.substr(prefix.size()), parts);
}

bool ReadPointer(const fs::path& root, std::string& current, std::string& previous) {
  current.clear();
  previous.clear();
  File in(root / kPointerFile, L"rb");
  if (in.f == nullptr) {
    return false;
  }
  char line[512];
  while (std::fgets(line, sizeof line, in.f) != nullptr) {
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
      text.pop_back();
    }
    const size_t eq = text.find('=');
    if (text.empty() || text[0] == '#' || eq == std::string::npos) {
      continue;
    }
    const std::string key = text.substr(0, eq), value = text.substr(eq + 1);
    if (!IsGameFolderName(value)) {
      continue;
    }
    if (key == "current") {
      current = value;
    } else if (key == "previous") {
      previous = value;
    }
  }
  return !current.empty();
}

bool WritePointer(const fs::path& root, const std::string& current, const std::string& previous, std::string& error) {
  if (!IsGameFolderName(current) || (!previous.empty() && !IsGameFolderName(previous))) {
    error = "not a game folder name";
    return false;
  }
  const std::string text = "# Which game folder the starter opens. Written by the launcher's updater; delete this file\r\n"
                           "# to start the \"" + std::string(kFirstFolder) + "\" folder again.\r\ncurrent=" + current +
                           "\r\n" + (previous.empty() ? std::string() : "previous=" + previous + "\r\n");
  const fs::path file = root / kPointerFile;
  const fs::path fresh = fs::path(file).concat(L".new");
  bool ok = false;
  {
    File out(fresh, L"wb");
    if (std::FILE* f = out.f) {
      ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
      ok = std::fflush(f) == 0 && ok;
      out.f = nullptr;
      ok = std::fclose(f) == 0 && ok;
    }
  }
#if defined(_WIN32)
  const bool moved = ok && MoveFileExW(fresh.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
  const bool moved = ok && std::rename(fresh.c_str(), file.c_str()) == 0;
#endif
  if (!moved) {
    std::error_code ec;
    fs::remove(fresh, ec);
    error = "the choice of version couldn't be saved in " + Utf8(root);
    return false;
  }
  return true;
}

bool PackageIsTarGz() {
#if defined(_WIN32) || defined(__APPLE__)
  return false;
#else
  return true;
#endif
}

fs::path GameFolderPath(const Layout& l, const std::string& name) {
  return l.macApp ? detail::MacProgramFolder(l, name) : l.root / PathOf(name);
}

std::string FolderVersion(const fs::path& folder) {
  File in(folder / kVersionFile, L"rb");
  if (in.f == nullptr) {
    return {};
  }
  char line[64]{};
  if (std::fgets(line, sizeof line, in.f) == nullptr) {
    return {};
  }
  std::string text(line);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  std::vector<uint32_t> parts;
  return ParseVersion(text, parts) ? text : std::string();
}

bool FolderUsable(const fs::path& folder, const std::vector<std::string>& requiredFiles) {
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    return false;
  }
  for (const std::string& f : requiredFiles) {
    if (!fs::is_regular_file(folder / PathOf(f), ec)) {
      return false;
    }
  }
  return true;
}

Layout FindLayout(const fs::path& programFolder, const std::string& starterName) {
  Layout l;
  std::error_code ec;
  if (detail::LooksLikeMacApp(programFolder)) {
    return detail::FindMacLayout(programFolder);
  }
  const fs::path own = fs::weakly_canonical(programFolder, ec);
  l.running = Utf8(own.filename());
  l.root = own.parent_path();
  if (!IsGameFolderName(l.running) || l.root.empty() || !fs::is_regular_file(l.root / PathOf(starterName), ec)) {
    l.why = "Updates can only be installed in the player's folder (\"" + starterName + "\" with a \"" + kFirstFolder +
            "\" folder next to it); this copy runs from " + Utf8(own) + ".";
    l.running.clear();
    l.root.clear();
    return l;
  }
  if (!ReadPointer(l.root, l.current, l.previous)) {
    l.current = kFirstFolder;
  }
  l.valid = true;
  return l;
}

bool Install(const InstallRequest& r, std::string& installed, std::string& error, const std::function<bool()>& stop) {
  installed.clear();
  const Layout& l = r.layout;
  const Manifest& m = r.manifest;
  if (!l.valid) {
    error = l.why;
    return false;
  }
  std::error_code ec;
  if (HoldsPlayerData(l.root / PathOf(l.running))) {
    error = "Your saves and settings are kept inside the game folder (" + l.running + "\\portable). An update would "
            "start a new game folder without them, so it can't be installed this way: download the new version by "
            "hand and move your portable folder over.";
    return false;
  }
  const uint64_t size = fs::file_size(r.package, ec);
  if (ec || size != m.size) {
    error = "the downloaded package has the wrong size (" + std::to_string(ec ? 0 : size) + " bytes, the update "
            "information says " + std::to_string(m.size) + ")";
    return false;
  }
  std::string hash;
  if (!Sha256File(r.package, hash, error)) {
    return false;
  }
  if (hash != m.sha256) {
    error = "the downloaded package is not the one the publisher signed (its SHA-256 differs)";
    return false;
  }
  if (l.macApp) {
    return detail::InstallMacApp(r, installed, error, stop);
  }
  const std::string name = std::string(kFirstFolder) + "-" + m.version;
  if (!IsGameFolderName(name)) {
    error = "the version isn't usable as a folder name: " + m.version;
    return false;
  }
  if (name == l.running) {
    error = "this version is the one running now";
    return false;
  }
  const fs::path partial = l.root / PathOf(name + ".partial");
  const fs::path target = l.root / PathOf(name);
  const auto fail = [&](const std::string& why) {
    error = why;
    RemoveFolder(partial);
    return false;
  };
  if (!RemoveFolder(partial)) {
    error = "an earlier unfinished update couldn't be removed: " + Utf8(partial);
    return false;
  }
  std::string why;
  if (!(PackageIsTarGz() ? ExtractTarGz(r.package, partial, why, stop) : ExtractZip(r.package, partial, why, stop))) {
    return fail(why);
  }
  std::vector<std::string> required = r.requiredFiles;
  required.push_back(kVersionFile);
  if (!FolderUsable(partial, required)) {
    return fail("the package is incomplete (it should hold the launcher, the game and " + std::string(kVersionFile) + ")");
  }
  if (FolderVersion(partial) != m.version) {
    return fail("the package holds version " + FolderVersion(partial) + ", not " + m.version);
  }
  if (fs::exists(target, ec) && !RemoveFolder(target)) {
    return fail("the folder " + Utf8(target) + " is in use and couldn't be replaced; close the programs using it");
  }
  fs::rename(partial, target, ec);
  if (ec) {
    return fail("the new version's folder couldn't be put in place: " + ec.message());
  }
  if (!WritePointer(l.root, name, l.running, why)) {
    RemoveFolder(target);
    error = why;
    return false;
  }
  installed = name;
  for (const auto& e : fs::directory_iterator(l.root, ec)) {
    const std::string n = Utf8(e.path().filename());
    const bool leftover = n.size() > 8 && n.substr(n.size() - 8) == ".partial" && IsGameFolderName(n.substr(0, n.size() - 8));
    if (!e.is_directory(ec) || n == name || n == l.running || n == kFirstFolder || HoldsPlayerData(e.path()) ||
        !(leftover || IsGameFolderName(n))) {
      continue;
    }
    RemoveFolder(e.path());
  }
  return true;
}

bool GoBack(const Layout& l, const std::vector<std::string>& requiredFiles, std::string& error) {
  if (!l.valid) {
    error = l.why;
    return false;
  }
  if (l.macApp) {
    return detail::GoBackMac(l, requiredFiles, error);
  }
  if (l.previous.empty() || !FolderUsable(l.root / PathOf(l.previous), requiredFiles)) {
    error = "there is no previous version to go back to";
    return false;
  }
  return WritePointer(l.root, l.previous, l.current, error);
}

}
