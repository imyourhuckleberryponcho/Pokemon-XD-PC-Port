#include "save_backups.h"

#include "gc_memcard.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#define gmtime_s(tmOut, timeIn) gmtime_r((timeIn), (tmOut))
#endif

namespace fs = std::filesystem;

namespace engine::memcard::backups {
namespace {

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

fs::path PathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

bool IsBackupName(const std::string& name) {
  static const char kPattern[] = "dddd-dd-dd_dd-dd-dd";
  if (name.size() < sizeof(kPattern) - 1) return false;
  for (size_t i = 0; i + 1 < sizeof(kPattern); i++) {
    const bool digit = std::isdigit(static_cast<unsigned char>(name[i])) != 0;
    if (kPattern[i] == 'd' ? !digit : name[i] != kPattern[i]) return false;
  }
  const std::string rest = name.substr(sizeof(kPattern) - 1);
  if (rest.empty()) return true;
  if (rest.size() < 2 || rest[0] != '_') return false;
  return std::all_of(rest.begin() + 1, rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

bool Older(const std::string& a, const std::string& b) {
  const std::string ab = a.substr(0, 19), bb = b.substr(0, 19);
  if (ab != bb) return ab < bb;
  const int an = a.size() > 20 ? std::atoi(a.c_str() + 20) : 1, bn = b.size() > 20 ? std::atoi(b.c_str() + 20) : 1;
  return an < bn;
}

std::vector<fs::path> Folders(const fs::path& cardBackups) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(cardBackups, ec)) {
    if (e.is_directory(ec) && IsBackupName(Utf8(e.path().filename()))) {
      out.push_back(e.path());
    }
  }
  std::sort(out.begin(), out.end(),
            [](const fs::path& a, const fs::path& b) { return Older(Utf8(a.filename()), Utf8(b.filename())); });
  return out;
}

bool ReadAll(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) return false;
  in.seekg(0);
  out.resize(static_cast<size_t>(size));
  if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(in);
}

bool WriteAll(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

void Describe(const std::vector<uint8_t>& bytes, File& f) {
  if (bytes.size() < kDirEntrySize) return;
  const DirEntry e = ParseDirEntry(bytes.data());
  f.savedAt = e.modTime;
  if (e.commentOffset != 0xFFFFFFFFu && kDirEntrySize + static_cast<size_t>(e.commentOffset) + 32 <= bytes.size()) {
    const uint8_t* c = bytes.data() + kDirEntrySize + e.commentOffset;
    std::string line;
    bool printable = c[0] != 0;
    for (size_t i = 0; i < 32 && c[i] != 0; i++) {
      if (c[i] >= 0x20 && c[i] < 0x7F) {
        line += static_cast<char>(c[i]);
      } else if (c[i] >= 0xA0) {
        line += static_cast<char>(0xC0 | (c[i] >> 6));
        line += static_cast<char>(0x80 | (c[i] & 0x3F));
      } else {
        printable = false;
      }
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (printable) f.comment = line;
  }
}

std::string Stamp(std::time_t now, const char* format) {
  std::tm tm{};
  localtime_s(&tm, &now);
  char buf[32];
  std::strftime(buf, sizeof buf, format, &tm);
  return buf;
}

}

std::vector<Backup> List(const fs::path& backupsFolder) {
  std::vector<Backup> out;
  std::error_code ec;
  for (const auto& card : fs::directory_iterator(backupsFolder, ec)) {
    if (!card.is_directory(ec)) continue;
    for (const fs::path& folder : Folders(card.path())) {
      Backup b;
      b.folder = folder;
      b.card = Utf8(card.path().filename());
      b.name = Utf8(folder.filename());
      b.when = b.name.substr(0, 10) + " " + b.name.substr(11, 2) + ":" + b.name.substr(14, 2) + ":" + b.name.substr(17, 2);
      for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (!e.is_regular_file(ec)) continue;
        File f;
        f.name = Utf8(e.path().filename());
        f.size = e.file_size(ec);
        std::vector<uint8_t> bytes;
        if (ReadAll(e.path(), bytes)) Describe(bytes, f);
        b.size += f.size;
        b.files.push_back(std::move(f));
      }
      std::sort(b.files.begin(), b.files.end(), [](const File& x, const File& y) { return x.name < y.name; });
      if (!b.files.empty()) out.push_back(std::move(b));
    }
  }
  std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) {
    if (a.name != b.name) return Older(b.name, a.name);
    return a.card < b.card;
  });
  return out;
}

void Prune(const fs::path& cardBackups, size_t keep) {
  std::vector<fs::path> folders = Folders(cardBackups);
  std::error_code ec;
  for (size_t i = 0; i + keep < folders.size(); i++) {
    fs::remove_all(folders[i], ec);
  }
}

fs::path Add(const fs::path& backupsFolder, const std::string& card, const std::string& fileName,
             const std::vector<uint8_t>& bytes, std::time_t now, size_t keep, std::string* error) {
  const fs::path cardBackups = backupsFolder / PathOf(card);
  const fs::path file = PathOf(fileName);
  std::error_code ec;
  const std::vector<fs::path> folders = Folders(cardBackups);
  for (auto it = folders.rbegin(); it != folders.rend(); ++it) {
    if (fs::is_regular_file(*it / file, ec)) {
      std::vector<uint8_t> newest;
      if (ReadAll(*it / file, newest) && newest == bytes) return {};
      break;
    }
  }
  const std::string base = Stamp(now, "%Y-%m-%d_%H-%M-%S");
  fs::path folder = cardBackups / PathOf(base);
  for (int n = 2; fs::exists(folder, ec); n++) {
    folder = cardBackups / PathOf(base + "_" + std::to_string(n));
  }
  fs::create_directories(folder, ec);
  if (ec || !WriteAll(folder / file, bytes)) {
    if (error != nullptr) *error = "can't write " + Utf8(folder / file);
    fs::remove_all(folder, ec);
    return {};
  }
  Prune(cardBackups, keep);
  return folder;
}

bool Restore(const Backup& backup, const std::string& fileName, const fs::path& cardFolder,
             const fs::path& backupsFolder, size_t keep, std::string& error) {
  const fs::path file = PathOf(fileName);
  std::vector<uint8_t> bytes;
  if (!ReadAll(backup.folder / file, bytes)) {
    error = "The backup's file can't be read: " + Utf8(backup.folder / file);
    return false;
  }
  std::error_code ec;
  const fs::path target = cardFolder / file;
  if (fs::exists(target, ec)) {
    std::vector<uint8_t> current;
    if (!ReadAll(target, current)) {
      error = "The current save can't be read, so it can't be backed up first: nothing was changed.";
      return false;
    }
    std::string why;
    if (Add(backupsFolder, backup.card, fileName, current, std::time(nullptr), keep, &why).empty() && !why.empty()) {
      error = "The current save couldn't be backed up (" + why + "): nothing was changed.";
      return false;
    }
  }
  fs::create_directories(cardFolder, ec);
  const fs::path temp = cardFolder / PathOf(fileName + ".restoring");
  if (!WriteAll(temp, bytes)) {
    fs::remove(temp, ec);
    error = "Can't write into the memory card's folder: " + Utf8(cardFolder);
    return false;
  }
  fs::rename(temp, target, ec);
  if (ec) {
    fs::remove(temp, ec);
    error = "Can't replace the save (" + ec.message() + "): nothing was changed.";
    return false;
  }
  return true;
}

std::string SizeText(uint64_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    std::snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    std::snprintf(buf, sizeof buf, "%llu KB", static_cast<unsigned long long>((bytes + 512) / 1024));
  } else {
    std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(bytes));
  }
  return buf;
}

std::string CardTimeText(uint32_t secondsSince2000) {
  if (secondsSince2000 == 0) return {};
  const std::time_t t = static_cast<std::time_t>(946684800) + secondsSince2000;
  std::tm tm{};
  gmtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

}
