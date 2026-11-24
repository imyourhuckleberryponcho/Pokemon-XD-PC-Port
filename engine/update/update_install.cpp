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

fs::path LongPath(const fs::path& p) {
#if defined(_WIN32)
  std::error_code ec;
  std::wstring w = fs::absolute(p, ec).make_preferred().wstring();
  if (!ec && w.size() >= 200 && w.rfind(LR"(\\?\)", 0) != 0) {
    if (w.rfind(LR"(\\)", 0) == 0) return fs::path(LR"(\\?\UNC\)" + w.substr(2));
    return fs::path(LR"(\\?\)" + w);
  }
#endif
  return p;
}

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
    error = Tr("the package can't be read");
    return false;
  }
  const uint64_t tailSize = std::min<uint64_t>(size, 65557);
  std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
  if (!in.Seek(size - tailSize) || !in.Read(tail.data(), tail.size())) {
    error = Tr("the package can't be read");
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
    error = Tr("the package isn't a zip file");
    return false;
  }
  const uint16_t count = U16(&tail[end + 10]);
  const uint32_t dirSize = U32(&tail[end + 12]), dirOffset = U32(&tail[end + 16]);
  if (count == 0xFFFF || dirOffset == 0xFFFFFFFF || U16(&tail[end + 4]) != 0 || U16(&tail[end + 6]) != 0) {
    error = Tr("the package is a kind of zip this version can't unpack (ZIP64 or split)");
    return false;
  }
  if (static_cast<uint64_t>(dirOffset) + dirSize > size) {
    error = Tr("the package is damaged (its file list is outside it)");
    return false;
  }
  std::vector<uint8_t> dir(dirSize);
  if (!in.Seek(dirOffset) || !in.Read(dir.data(), dir.size())) {
    error = Tr("the package can't be read");
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
      error = Tr("the package is damaged (its file list)");
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
      error = Tr("the package is damaged (its file list)");
      return false;
    }
    std::string raw(reinterpret_cast<const char*>(&dir[p + 46]), nameLen);
    p += 46 + nameLen + extraLen + commentLen;
    e.directory = !raw.empty() && (raw.back() == '/' || raw.back() == '\\');
    if (e.directory) {
      raw.pop_back();
    }
    if (!SafeName(raw, e.name)) {
      error = Fmt(Tr("the package holds a file name that isn't allowed: %s"), raw.c_str());
      return false;
    }
    if (e.name == "__MACOSX" || e.name.rfind("__MACOSX/", 0) == 0 || e.name == ".DS_Store" ||
        (e.name.size() > 10 && e.name.compare(e.name.size() - 10, 10, "/.DS_Store") == 0)) {
      continue;
    }
    if ((e.flags & 1) != 0 || (e.method != 0 && e.method != 8) || e.packed == 0xFFFFFFFF || e.unpacked == 0xFFFFFFFF) {
      error = Fmt(Tr("the package holds a file this version can't unpack: %s"), e.name.c_str());
      return false;
    }
    totalUnpacked += e.unpacked;
    if (totalUnpacked > 2 * kMaxPackageBytes) {
      error = Tr("the package is far too large when unpacked");
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
    fs::path out = folder / PathOf(e.name);
#if defined(_WIN32)
    out.make_preferred();
#endif
    if (e.directory) {
      fs::create_directories(out, ec);
      continue;
    }
    if (detail::AncestorIsLink(folder, e.name)) {
      error = Fmt(Tr("the package writes through a link: %s"), e.name.c_str());
      return false;
    }
    fs::create_directories(out.parent_path(), ec);
    uint8_t local[30];
    if (!in.Seek(e.offset) || !in.Read(local, sizeof local) || U32(local) != 0x04034b50) {
      error = Fmt(Tr("the package is damaged (%s)"), e.name.c_str());
      return false;
    }
    const uint64_t data = static_cast<uint64_t>(e.offset) + 30 + U16(local + 26) + U16(local + 28);
    if (data + e.packed > size || !in.Seek(data)) {
      error = Fmt(Tr("the package is damaged (%s)"), e.name.c_str());
      return false;
    }
    File target(out, L"wb");
    if (target.f == nullptr) {
      error = Fmt(Tr("a file couldn't be written: %s"), Utf8(out).c_str());
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
      error = Fmt(Tr("the package is damaged: %s doesn't unpack correctly"), e.name.c_str());
      return false;
    }
    if (detail::IsLinkMode(e.unixMode)) {
      const std::string text = detail::ReadSmallFile(out);
      fs::remove(out, ec);
      if (text.empty() || !detail::MakeUnpackedLink(folder, e.name, text, error)) {
        error = error.empty() ? Fmt(Tr("the package holds an empty link: %s"), e.name.c_str()) : error;
        return false;
      }
    } else if (!detail::FinishUnpackedFile(out, e.unixMode)) {
      error = Fmt(Tr("a file's permissions couldn't be set: %s"), e.name.c_str());
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
    error = Tr("not a game folder name");
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
    error = Fmt(Tr("the choice of version couldn't be saved in %s"), Utf8(root).c_str());
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
  size_t from = 0;
  while (from <= starterName.size()) {
    const size_t bar = starterName.find('|', from);
    const std::string one = starterName.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
    if (!one.empty()) {
      l.starterNames.push_back(one);
    }
    if (bar == std::string::npos) {
      break;
    }
    from = bar + 1;
  }
  if (!l.root.empty()) {
    for (const std::string& n : l.starterNames) {
      if (fs::is_regular_file(l.root / PathOf(n), ec)) {
        l.starter = n;
        break;
      }
    }
  }
  if (!IsGameFolderName(l.running) || l.root.empty() || l.starter.empty()) {
    l.why = Fmt(Tr("Updates can only be installed in the player's folder (\"%s\" with a \"%s\" folder next to it); this copy runs "
                   "from %s."),
                (l.starterNames.empty() ? starterName : l.starterNames.front()).c_str(), std::string(kFirstFolder).c_str(),
                Utf8(own).c_str());
    l.running.clear();
    l.root.clear();
    l.suggestedRoot = SuggestPlayerFolder(own);
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
    error = Fmt(Tr("Your saves and settings are kept inside the game folder (%s\\portable). An update would start a new game "
                   "folder without them, so it can't be installed this way: download the new version by hand and move your "
                   "portable folder over."),
                l.running.c_str());
    return false;
  }
  const uint64_t size = fs::file_size(r.package, ec);
  if (ec || size != m.size) {
    error = Fmt(Tr("the downloaded package has the wrong size (%llu bytes, the update information says %llu)"),
                static_cast<unsigned long long>(ec ? 0 : size), static_cast<unsigned long long>(m.size));
    return false;
  }
  std::string hash;
  if (!Sha256File(r.package, hash, error)) {
    return false;
  }
  if (hash != m.sha256) {
    error = Tr("the downloaded package is not the one the publisher signed (its SHA-256 differs)");
    return false;
  }
  if (l.macApp) {
    return detail::InstallMacApp(r, installed, error, stop);
  }
  const std::string name = std::string(kFirstFolder) + "-" + m.version;
  if (!IsGameFolderName(name)) {
    error = Fmt(Tr("the version isn't usable as a folder name: %s"), m.version.c_str());
    return false;
  }
  if (name == l.running) {
    error = Tr("this version is the one running now");
    return false;
  }
  const fs::path partial = LongPath(l.root / PathOf(name + ".partial"));
  const fs::path target = LongPath(l.root / PathOf(name));
  const auto fail = [&](const std::string& why) {
    error = why;
    RemoveFolder(partial);
    return false;
  };
  if (!RemoveFolder(partial)) {
    error = Fmt(Tr("an earlier unfinished update couldn't be removed: %s"), Utf8(partial).c_str());
    return false;
  }
  std::string why;
  if (!(PackageIsTarGz() ? ExtractTarGz(r.package, partial, why, stop) : ExtractZip(r.package, partial, why, stop))) {
    return fail(why);
  }
  std::vector<std::string> required = r.requiredFiles;
  required.push_back(kVersionFile);
  if (!detail::UseInnerFolder(partial, required, why)) {
    return fail(why);
  }
  if (!FolderUsable(partial, required)) {
    return fail(Fmt(Tr("the package is incomplete (it should hold the launcher, the game and %s)"), kVersionFile));
  }
  detail::RestoreExecutableBits(partial);
  if (FolderVersion(partial) != m.version) {
    return fail(Fmt(Tr("the package holds version %s, not %s"), FolderVersion(partial).c_str(), m.version.c_str()));
  }
  if (fs::exists(target, ec) && !RemoveFolder(target)) {
    return fail(Fmt(Tr("the folder %s is in use and couldn't be replaced; close the programs using it"), Utf8(target).c_str()));
  }
  fs::rename(partial, target, ec);
  if (ec) {
    return fail(Fmt(Tr("the new version's folder couldn't be put in place: %s"), ec.message().c_str()));
  }
  if (!WritePointer(l.root, name, l.running, why)) {
    RemoveFolder(target);
    error = why;
    return false;
  }
  installed = name;
  if (!l.starter.empty()) {
    for (const std::string& n : l.starterNames) {
      if (n == l.starter) {
        break;
      }
      if (!fs::exists(l.root / PathOf(n), ec)) {
        fs::copy_file(l.root / PathOf(l.starter), l.root / PathOf(n), fs::copy_options::skip_existing, ec);
      }
    }
  }
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

std::string SyncedFolderName(const fs::path& path) {
  static const char* const kNames[] = {"onedrive", "dropbox", "google drive", "googledrive", "icloud", "nextcloud", "syncthing", "mobile documents",
                                       "pcloud", "mega", "box sync", "sync.com"};
  for (const auto& part : path) {
    std::string n = Utf8(part);
    std::string low;
    for (char c : n) low += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    for (const char* k : kNames) {
      if (low == k || low.compare(0, std::strlen(k), k) == 0) {
        return low.compare(0, 9, "onedrive") == 0 ? "OneDrive" : low == "mobile documents" ? "iCloud Drive" : n;
      }
    }
  }
  return {};
}

fs::path SuggestPlayerFolder(const fs::path& programFolder) {
  std::error_code ec;
  const fs::path parent = programFolder.parent_path();
  fs::path pick = parent / L"Pokemon XD PC";
  for (int i = 2; fs::exists(pick, ec) && i < 100; ++i) {
    pick = parent / PathOf("Pokemon XD PC (" + std::to_string(i) + ")");
  }
  return pick;
}

bool SetUpPlayerFolder(const InstallRequest& r, const fs::path& runningFolder, const fs::path& newRoot, std::string& error,
                       const std::function<bool()>& stop) {
  const Layout& l = r.layout;
  const Manifest& m = r.manifest;
  std::error_code ec;
  if (l.macApp || l.valid) {
    error = Tr("this copy already is in a player's folder");
    return false;
  }
  if (newRoot.empty() || (fs::exists(newRoot, ec) && !fs::is_empty(newRoot, ec))) {
    error = Fmt(Tr("the folder %s already exists and isn't empty: choose another"), Utf8(newRoot).c_str());
    return false;
  }
  if (HoldsPlayerData(runningFolder)) {
    error = Fmt(Tr("Your saves and settings are kept inside this folder (%s\portable), so a new folder would start without them: "
                   "download the new version by hand and move your portable folder over."),
                Utf8(runningFolder).c_str());
    return false;
  }
  const uint64_t size = fs::file_size(r.package, ec);
  if (ec || size != m.size) {
    error = Fmt(Tr("the downloaded package has the wrong size (%llu bytes, the update information says %llu)"),
                static_cast<unsigned long long>(ec ? 0 : size), static_cast<unsigned long long>(m.size));
    return false;
  }
  std::string hash;
  if (!Sha256File(r.package, hash, error)) return false;
  if (hash != m.sha256) {
    error = Tr("the downloaded package is not the one the publisher signed (its SHA-256 differs)");
    return false;
  }
  const fs::path partial = LongPath(fs::path(newRoot).concat(".partial"));
  const auto fail = [&](const std::string& why) {
    error = why;
    RemoveFolder(partial);
    return false;
  };
  if (!RemoveFolder(partial)) return fail(Fmt(Tr("an earlier unfinished folder couldn't be removed: %s"), Utf8(partial).c_str()));
  std::string why;
  if (!(PackageIsTarGz() ? ExtractTarGz(r.package, partial, why, stop) : ExtractZip(r.package, partial, why, stop))) return fail(why);
  const auto hasStarter = [&](const fs::path& dir) {
    for (const std::string& n : l.starterNames) {
      if (fs::is_regular_file(dir / PathOf(n), ec)) return true;
    }
    return false;
  };
  fs::path top = partial;
  if (!hasStarter(top)) {
    std::vector<fs::path> kids;
    for (const auto& e : fs::directory_iterator(partial, ec)) kids.push_back(e.path());
    if (kids.size() == 1 && fs::is_directory(kids[0], ec)) top = kids[0];
  }
  std::vector<std::string> required = r.requiredFiles;
  required.push_back(kVersionFile);
  if (!hasStarter(top) || !FolderUsable(top / PathOf(kFirstFolder), required)) {
    return fail(Tr("this download is not a whole player folder (the starter and the game folder): download the new version by hand"));
  }
  detail::RestoreExecutableBits(top / PathOf(kFirstFolder));
  detail::RestoreExecutableBits(top);
  if (FolderVersion(top / PathOf(kFirstFolder)) != m.version) {
    return fail(Fmt(Tr("the package holds version %s, not %s"), FolderVersion(top / PathOf(kFirstFolder)).c_str(), m.version.c_str()));
  }
  fs::create_directories(newRoot.parent_path(), ec);
  if (fs::exists(newRoot, ec)) fs::remove(newRoot, ec);
  fs::rename(top, newRoot, ec);
  if (ec) return fail(Fmt(Tr("the new folder couldn't be put in place: %s"), ec.message().c_str()));
  RemoveFolder(partial);
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
    error = Tr("there is no previous version to go back to");
    return false;
  }
  return WritePointer(l.root, l.previous, l.current, error);
}

}
