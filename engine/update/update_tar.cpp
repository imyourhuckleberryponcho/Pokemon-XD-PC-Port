#include "update_internal.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

class GzReader {
 public:
  explicit GzReader(const fs::path& file) {
#if defined(_WIN32)
    f_ = _wfopen(file.c_str(), L"rb");
#else
    f_ = std::fopen(file.c_str(), "rb");
#endif
    z_ = {};
    ok_ = f_ != nullptr && inflateInit2(&z_, 16 + MAX_WBITS) == Z_OK;
    in_.resize(1 << 16);
  }
  ~GzReader() {
    if (ok_) {
      inflateEnd(&z_);
    }
    if (f_ != nullptr) {
      std::fclose(f_);
    }
  }
  bool Ok() const { return ok_; }
  bool Read(void* into, size_t size) {
    auto* out = static_cast<uint8_t*>(into);
    while (size > 0) {
      if (z_.avail_in == 0) {
        if (!Fill()) {
          return false;
        }
      }
      z_.next_out = out;
      z_.avail_out = static_cast<uInt>(std::min<size_t>(size, 1u << 30));
      const uInt want = z_.avail_out;
      const int status = inflate(&z_, Z_NO_FLUSH);
      const size_t got = want - z_.avail_out;
      out += got;
      size -= got;
      if (status == Z_STREAM_END) {
        ended_ = true;
        return size == 0;
      }
      if (status != Z_OK && status != Z_BUF_ERROR) {
        return false;
      }
      if (status == Z_BUF_ERROR && got == 0 && z_.avail_in == 0 && !Fill()) {
        return false;
      }
    }
    return true;
  }
  bool Skip(uint64_t size) {
    uint8_t scratch[4096];
    while (size > 0) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(size, sizeof scratch));
      if (!Read(scratch, n)) {
        return false;
      }
      size -= n;
    }
    return true;
  }
  bool FinishedCleanly() {
    uint8_t scratch[4096];
    while (!ended_) {
      if (z_.avail_in == 0 && !Fill()) {
        return false;
      }
      z_.next_out = scratch;
      z_.avail_out = sizeof scratch;
      const int status = inflate(&z_, Z_NO_FLUSH);
      if (status == Z_STREAM_END) {
        ended_ = true;
      } else if (status != Z_OK && status != Z_BUF_ERROR) {
        return false;
      }
    }
    return true;
  }

 private:
  bool Fill() {
    const size_t n = std::fread(in_.data(), 1, in_.size(), f_);
    if (n == 0) {
      return false;
    }
    z_.next_in = in_.data();
    z_.avail_in = static_cast<uInt>(n);
    return true;
  }
  std::FILE* f_ = nullptr;
  z_stream z_{};
  bool ok_ = false;
  bool ended_ = false;
  std::vector<uint8_t> in_;
};

uint64_t Octal(const char* p, size_t n, bool& ok) {
  uint64_t v = 0;
  size_t i = 0;
  if (n > 0 && (static_cast<uint8_t>(p[0]) & 0x80) != 0) {
    v = static_cast<uint8_t>(p[0]) & 0x7F;
    for (i = 1; i < n; i++) {
      v = (v << 8) | static_cast<uint8_t>(p[i]);
    }
    return v;
  }
  while (i < n && (p[i] == ' ' || p[i] == '\0')) {
    i++;
  }
  for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
    v = v * 8 + static_cast<uint64_t>(p[i] - '0');
  }
  for (; i < n; i++) {
    if (p[i] != ' ' && p[i] != '\0') {
      ok = false;
    }
  }
  return v;
}

std::string Field(const char* p, size_t n) {
  size_t len = 0;
  while (len < n && p[len] != '\0') {
    len++;
  }
  return std::string(p, len);
}

bool AllZero(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] != 0) {
      return false;
    }
  }
  return true;
}

void ReadPax(const std::string& data, std::string& path, std::string& linkpath) {
  size_t p = 0;
  while (p < data.size()) {
    const size_t space = data.find(' ', p);
    if (space == std::string::npos) {
      return;
    }
    const size_t len = static_cast<size_t>(std::strtoull(data.substr(p, space - p).c_str(), nullptr, 10));
    if (len == 0 || p + len > data.size() || len <= space - p + 1) {
      return;
    }
    const std::string record = data.substr(space + 1, p + len - space - 2);
    const size_t eq = record.find('=');
    if (eq != std::string::npos) {
      const std::string key = record.substr(0, eq);
      if (key == "path") {
        path = record.substr(eq + 1);
      } else if (key == "linkpath") {
        linkpath = record.substr(eq + 1);
      }
    }
    p += len;
  }
}

}

bool ExtractTarGz(const fs::path& archive, const fs::path& folder, std::string& error,
                  const std::function<bool()>& stop) {
  GzReader in(archive);
  if (!in.Ok()) {
    error = "the package can't be read";
    return false;
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  std::string longName, longLink, paxPath, paxLink;
  uint64_t totalBytes = 0;
  std::vector<uint8_t> buffer(1 << 16);
  int zeroBlocks = 0;
  for (;;) {
    if (stop && stop()) {
      error = "stopped";
      return false;
    }
    uint8_t header[512];
    if (!in.Read(header, sizeof header)) {
      error = "the package is damaged (it ends inside the file list)";
      return false;
    }
    if (AllZero(header, sizeof header)) {
      if (++zeroBlocks == 2) {
        break;
      }
      continue;
    }
    zeroBlocks = 0;
    const char* h = reinterpret_cast<const char*>(header);
    bool numbersOk = true;
    const uint64_t size = Octal(h + 124, 12, numbersOk);
    const uint32_t mode = static_cast<uint32_t>(Octal(h + 100, 8, numbersOk));
    const uint64_t stored = Octal(h + 148, 8, numbersOk);
    uint64_t sum = 0;
    for (size_t i = 0; i < 512; i++) {
      sum += (i >= 148 && i < 156) ? ' ' : header[i];
    }
    if (!numbersOk || sum != stored) {
      error = "the package is damaged (a file header doesn't add up)";
      return false;
    }
    const char type = h[156] == '\0' ? '0' : h[156];
    if (type == 'L' || type == 'K' || type == 'x') {
      if (size > 64 * 1024) {
        error = "the package holds an oversized header";
        return false;
      }
      std::string data(static_cast<size_t>(size), '\0');
      if (!in.Read(data.data(), data.size()) || !in.Skip((512 - size % 512) % 512)) {
        error = "the package is damaged";
        return false;
      }
      if (type == 'L') {
        longName = Field(data.data(), data.size());
      } else if (type == 'K') {
        longLink = Field(data.data(), data.size());
      } else {
        ReadPax(data, paxPath, paxLink);
      }
      continue;
    }
    if (type == 'g') {
      if (!in.Skip(size + (512 - size % 512) % 512)) {
        error = "the package is damaged";
        return false;
      }
      continue;
    }
    std::string name = !paxPath.empty() ? paxPath : !longName.empty() ? longName : Field(h, 100);
    const std::string prefix = Field(h + 345, 155);
    if (paxPath.empty() && longName.empty() && !prefix.empty() && std::memcmp(h + 257, "ustar", 5) == 0) {
      name = prefix + "/" + name;
    }
    const std::string link = !paxLink.empty() ? paxLink : !longLink.empty() ? longLink : Field(h + 157, 100);
    longName.clear(), longLink.clear(), paxPath.clear(), paxLink.clear();
    std::string raw = name;
    const bool dir = type == '5' || (!raw.empty() && raw.back() == '/');
    while (raw.size() > 1 && raw.back() == '/') {
      raw.pop_back();
    }
    std::string safe;
    if (!detail::SafeName(raw, safe)) {
      error = "the package holds a file name that isn't allowed: " + name;
      return false;
    }
    if (detail::AncestorIsLink(folder, safe)) {
      error = "the package writes through a link: " + safe;
      return false;
    }
    const fs::path out = folder / detail::PathOf(safe);
    if (dir) {
      fs::create_directories(out, ec);
      if (!in.Skip(size + (512 - size % 512) % 512)) {
        error = "the package is damaged";
        return false;
      }
      continue;
    }
    if (type == '2') {
      if (!in.Skip(size + (512 - size % 512) % 512)) {
        error = "the package is damaged";
        return false;
      }
      fs::create_directories(out.parent_path(), ec);
      if (!detail::MakeUnpackedLink(folder, safe, link, error)) {
        return false;
      }
      continue;
    }
    if (type != '0' && type != '7') {
      error = "the package holds something that isn't a file, folder or link: " + safe;
      return false;
    }
    totalBytes += size;
    if (totalBytes > 2 * kMaxPackageBytes) {
      error = "the package is far too large when unpacked";
      return false;
    }
    fs::create_directories(out.parent_path(), ec);
#if defined(_WIN32)
    std::FILE* target = _wfopen(out.c_str(), L"wb");
#else
    std::FILE* target = std::fopen(out.c_str(), "wb");
#endif
    if (target == nullptr) {
      error = "a file couldn't be written: " + detail::Utf8(out);
      return false;
    }
    bool ok = true;
    for (uint64_t left = size; ok && left > 0;) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buffer.size()));
      ok = in.Read(buffer.data(), n) && std::fwrite(buffer.data(), 1, n, target) == n;
      left -= n;
    }
    ok = std::fclose(target) == 0 && ok;
    if (!ok || !in.Skip((512 - size % 512) % 512)) {
      error = "the package is damaged: " + safe + " doesn't unpack correctly";
      return false;
    }
    if (!detail::FinishUnpackedFile(out, mode)) {
      error = "a file's permissions couldn't be set: " + safe;
      return false;
    }
  }
  if (!in.FinishedCleanly()) {
    error = "the package is damaged (its checksum doesn't fit)";
    return false;
  }
  return true;
}

}
