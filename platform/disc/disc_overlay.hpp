#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace port_disc {

struct OverlayEntry {
  std::string path;
  std::filesystem::path localFile;
  std::vector<uint8_t> data;
  bool inMemory = false;
  uint64_t size = 0;
  mutable std::atomic<bool> opened{false};
};

const std::vector<std::unique_ptr<OverlayEntry>>& Overlay();
const OverlayEntry* FindOverlay(std::string_view path);

class OverlayReader {
public:
  explicit OverlayReader(const OverlayEntry& entry);
  bool Ok() const { return mOk; }
  int64_t Read(uint8_t* buf, size_t len);
  int64_t Seek(int64_t offset, int whence);

private:
  const OverlayEntry& mEntry;
  std::ifstream mFile;
  uint64_t mPos = 0;
  bool mOk = true;
};

}
