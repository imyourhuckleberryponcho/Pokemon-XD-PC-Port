#include "disc_check.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Entry {
  std::string sha1;
  uint64_t size = 0;
  int64_t modified = 0;
  std::string path;
};

fs::path from_utf8(const char* text) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

std::string to_utf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

bool is_sha1(const std::string& text) {
  if (text.size() != 40) {
    return false;
  }
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

std::vector<Entry> read_entries(const char* cacheFile) {
  std::vector<Entry> entries;
  std::ifstream in(from_utf8(cacheFile), std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    Entry e;
    if (!(fields >> e.sha1 >> e.size >> e.modified) || !is_sha1(e.sha1)) {
      continue;
    }
    fields.get();
    std::getline(fields, e.path);
    if (!e.path.empty()) {
      entries.push_back(std::move(e));
    }
  }
  return entries;
}

}

extern "C" {

bool DiscCheck_Stamp(const char* path, char* fullPath, size_t fullPathSize, DiscFileStamp* stamp) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  std::error_code ec;
  const fs::path p = fs::absolute(from_utf8(path), ec).lexically_normal();
  if (ec || !fs::is_regular_file(p, ec)) {
    return false;
  }
  const uint64_t size = fs::file_size(p, ec);
  if (ec) {
    return false;
  }
  const auto time = fs::last_write_time(p, ec);
  if (ec) {
    return false;
  }
  if (stamp != nullptr) {
    stamp->size = size;
    stamp->modified = static_cast<int64_t>(time.time_since_epoch().count());
  }
  if (fullPath != nullptr && fullPathSize > 0) {
    std::snprintf(fullPath, fullPathSize, "%s", to_utf8(p).c_str());
  }
  return true;
}

bool DiscCheck_Recall(const char* cacheFile, const char* fullPath, const DiscFileStamp* stamp, char sha1[41]) {
  if (cacheFile == nullptr || fullPath == nullptr || stamp == nullptr) {
    return false;
  }
  for (const Entry& e : read_entries(cacheFile)) {
    if (e.path == fullPath) {
      if (e.size != stamp->size || e.modified != stamp->modified) {
        return false;
      }
      std::memcpy(sha1, e.sha1.c_str(), 41);
      return true;
    }
  }
  return false;
}

bool DiscCheck_Remember(const char* cacheFile, const char* fullPath, const DiscFileStamp* stamp, const char* sha1) {
  if (cacheFile == nullptr || fullPath == nullptr || stamp == nullptr || sha1 == nullptr || !is_sha1(sha1) ||
      std::strchr(fullPath, '\n') != nullptr) {
    return false;
  }
  std::vector<Entry> entries = read_entries(cacheFile);
  std::vector<Entry> kept;
  kept.push_back(Entry{sha1, stamp->size, stamp->modified, fullPath});
  for (Entry& e : entries) {
    if (e.path != fullPath && kept.size() < DISC_CHECK_MAX_ENTRIES) {
      kept.push_back(std::move(e));
    }
  }

  const fs::path target = from_utf8(cacheFile);
  std::error_code ec;
  if (target.has_parent_path()) {
    fs::create_directories(target.parent_path(), ec);
  }
  fs::path temp = target;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << "# Disc images already checked (SHA-1 of the disc data), so they needn't be read again at every\n"
           "# start. Safe to delete: the disc is then simply checked again.\n"
           "# One line each: SHA-1, file size, last-modified time, full path.\n";
    for (const Entry& e : kept) {
      out << e.sha1 << ' ' << e.size << ' ' << e.modified << ' ' << e.path << '\n';
    }
    if (!out.good()) {
      out.close();
      fs::remove(temp, ec);
      return false;
    }
  }
  fs::rename(temp, target, ec);
  if (ec) {
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

}
