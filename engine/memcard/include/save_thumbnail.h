#ifndef ENGINE_SAVE_THUMBNAIL_H
#define ENGINE_SAVE_THUMBNAIL_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::memcard::thumbnail {

constexpr uint32_t kMaxWidth = 256;

struct Picture {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
  bool Empty() const { return width == 0 || height == 0; }
};

Picture Shrink(const uint8_t* rgba, uint32_t w, uint32_t h);

std::vector<uint8_t> EncodePng(const Picture& picture);
bool DecodePng(const std::vector<uint8_t>& bytes, Picture& out, std::string& error);

bool WriteFile(const std::filesystem::path& file, const Picture& picture, std::string& error);
bool ReadFile(const std::filesystem::path& file, Picture& out, std::string& error);

}

#endif
