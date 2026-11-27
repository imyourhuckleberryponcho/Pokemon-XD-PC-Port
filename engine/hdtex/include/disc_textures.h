#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::hdtex {

uint64_t Xxh64(const uint8_t* data, size_t size);

enum class TexSource : uint8_t {
  Map,
  Object,
  Camera,
  Model,
  Menu,
  Flash,
  Face,
  Particle,
  MoveEffect,
  LensFlare,
};
const char* SourceName(TexSource s);

enum class TexClass : uint8_t {
  Surface3D,
  Interface,
  Effect,
  TextOrMask,
  Character,
  AlphaDither
};
const char* ClassName(TexClass c);

struct DiscTexture {
  std::string stem;
  uint32_t width = 0, height = 0, format = 0;
  bool mipmapped = false;
  TexSource source = TexSource::Map;
  std::string fsys;
  std::string member;
  std::vector<uint8_t> level0;
  std::vector<uint8_t> tlut;
  uint32_t tlutFormat = 0;
  TexClass cls = TexClass::Surface3D;
};

bool DecodeRgba(const DiscTexture& t, std::vector<uint8_t>& rgba);

TexClass ClassifyByOrigin(const DiscTexture& t);
bool HasAlphaDither(const uint8_t* rgba, uint32_t width, uint32_t height);

struct FileSource {
  std::function<std::vector<std::string>()> list;
  std::function<bool(const std::string&, std::vector<uint8_t>&)> read;
};

struct ExportStats {
  uint64_t members = 0;
  uint64_t found = 0;
  uint64_t unique = 0;
  uint64_t byClass[6] = {};
  uint64_t byFormat[16] = {};
  uint64_t level0Bytes = 0;
};

bool ForEachDiscTexture(const FileSource& source, const std::function<void(DiscTexture&)>& each,
                        const std::function<bool(uint32_t, uint32_t)>& progress, ExportStats* stats);

}
