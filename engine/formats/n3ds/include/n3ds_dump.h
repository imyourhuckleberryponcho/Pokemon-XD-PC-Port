#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace n3ds {

using Bytes = std::vector<uint8_t>;

class Fail : public std::runtime_error {
 public:
  Fail(const std::string& m, int code = 1) : std::runtime_error(m), code(code) {}
  int code;
};

class ByteFile {
 public:
  ByteFile() = default;
  explicit ByteFile(const std::string& path);
  Bytes read(uint64_t off, uint64_t len) const;
  uint64_t size() const { return size_; }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  uint64_t size_ = 0;
};

class RomFs {
 public:
  RomFs(const ByteFile& f, uint64_t ncch_base, uint64_t romfs_off);
  bool has(const std::string& path) const { return files.count(path) != 0; }
  Bytes read(const std::string& path) const;
  std::pair<uint64_t, uint64_t> where(const std::string& path) const;
  std::map<std::string, std::pair<uint64_t, uint64_t>> files;
  ByteFile file;
  std::string product;

 private:
  uint64_t l3_ = 0, fdo_ = 0;
  void walk(const Bytes& dmeta, const Bytes& fmeta, uint32_t doff, const std::string& path);
};

uint64_t find_ncch(const ByteFile& f, int part = 0);
RomFs open_dump(const std::string& path, int part = 0);

Bytes lz11(const uint8_t* b, size_t n, uint64_t limit = 0);
Bytes lz11_maybe(const Bytes& b);

class Garc {
 public:
  Garc(const ByteFile& f, uint64_t off, uint64_t size);
  size_t count() const { return entries.size(); }
  Bytes read(size_t i, size_t sub = 0) const;
  size_t nsub(size_t i) const;
  std::vector<std::pair<int64_t, int64_t>> entries_raw(size_t i) const { return entries[i]; }
  std::vector<std::vector<std::pair<int64_t, int64_t>>> entries;

 private:
  ByteFile f_;
  uint64_t off_ = 0, data_off_ = 0;
};

Garc garc_at(const RomFs& r, const std::string& path);

std::vector<Bytes> pc_split(const Bytes& b, bool* ok = nullptr);

struct Detected {
  std::string models, personal, levelup, text;
};
Detected detect(const RomFs& r);

}
