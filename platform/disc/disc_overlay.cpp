#include "disc_overlay.hpp"
#include "port_disc.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <system_error>

namespace port_disc {

namespace {

std::vector<std::unique_ptr<OverlayEntry>> sOverlay;

std::string Key(std::string_view path) {
  std::string key;
  for (char c : path) {
    c = c == '\\' ? '/' : c;
    if (c == '/' && (key.empty() || key.back() == '/')) {
      continue;
    }
    key.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  if (!key.empty() && key.back() == '/') {
    key.pop_back();
  }
  return key;
}

std::filesystem::path FromUtf8(const char* text) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

}

const std::vector<std::unique_ptr<OverlayEntry>>& Overlay() { return sOverlay; }

const OverlayEntry* FindOverlay(std::string_view path) {
  if (sOverlay.empty()) {
    return nullptr;
  }
  const std::string key = Key(path);
  for (const auto& e : sOverlay) {
    if (Key(e->path) == key) {
      return e.get();
    }
  }
  return nullptr;
}

OverlayReader::OverlayReader(const OverlayEntry& entry) : mEntry(entry) {
  if (!entry.inMemory) {
    mFile.open(entry.localFile, std::ios::binary);
    mOk = mFile.is_open();
  }
}

int64_t OverlayReader::Read(uint8_t* buf, size_t len) {
  if (!mOk) {
    return -1;
  }
  const uint64_t left = mPos < mEntry.size ? mEntry.size - mPos : 0;
  const size_t n = len < left ? len : static_cast<size_t>(left);
  if (n == 0) {
    return 0;
  }
  if (mEntry.inMemory) {
    std::memcpy(buf, mEntry.data.data() + mPos, n);
  } else {
    mFile.clear();
    mFile.seekg(static_cast<std::streamoff>(mPos));
    mFile.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
    if (static_cast<size_t>(mFile.gcount()) != n) {
      return -1;
    }
  }
  mPos += n;
  return static_cast<int64_t>(n);
}

int64_t OverlayReader::Seek(int64_t offset, int whence) {
  const int64_t base = whence == 1 ? static_cast<int64_t>(mPos) : whence == 2 ? static_cast<int64_t>(mEntry.size) : 0;
  const int64_t to = base + offset;
  if (to < 0) {
    return -1;
  }
  mPos = static_cast<uint64_t>(to);
  return to;
}

}

using namespace port_disc;

extern "C" {

bool PortDisc_SetOverlay(const PortDiscOverlayFile* files, size_t count) {
  std::vector<std::unique_ptr<OverlayEntry>> next;
  for (size_t i = 0; i < count; i++) {
    const PortDiscOverlayFile& f = files[i];
    if (f.path == nullptr || f.path[0] == '\0' || (f.localFile == nullptr && f.data == nullptr && f.size != 0)) {
      return false;
    }
    auto e = std::make_unique<OverlayEntry>();
    e->path = f.path;
    while (!e->path.empty() && (e->path.front() == '/' || e->path.front() == '\\')) {
      e->path.erase(0, 1);
    }
    for (char& c : e->path) {
      c = c == '\\' ? '/' : c;
    }
    for (const auto& other : next) {
      if (Key(other->path) == Key(e->path)) {
        return false;
      }
    }
    if (f.localFile != nullptr) {
      e->localFile = FromUtf8(f.localFile);
      std::error_code ec;
      const uintmax_t size = std::filesystem::file_size(e->localFile, ec);
      if (ec || !std::filesystem::is_regular_file(e->localFile, ec)) {
        return false;
      }
      e->size = size;
    } else {
      e->inMemory = true;
      e->size = f.size;
      if (f.size != 0) {
        e->data.assign(f.data, f.data + f.size);
      }
    }
    if (e->size > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    next.push_back(std::move(e));
  }
  sOverlay = std::move(next);
  return true;
}

size_t PortDisc_OverlayCount(void) { return sOverlay.size(); }

uint8_t* PortDisc_ReadFile(const char* path, size_t* size) {
  const OverlayEntry* e = FindOverlay(path);
  if (e == nullptr) {
    return PortDisc_ReadOriginalFile(path, size);
  }
  auto* data = static_cast<uint8_t*>(std::malloc(e->size ? static_cast<size_t>(e->size) : 1));
  OverlayReader reader(*e);
  if (data == nullptr || reader.Read(data, static_cast<size_t>(e->size)) != static_cast<int64_t>(e->size)) {
    std::free(data);
    return nullptr;
  }
  if (size != nullptr) {
    *size = static_cast<size_t>(e->size);
  }
  return data;
}

}
