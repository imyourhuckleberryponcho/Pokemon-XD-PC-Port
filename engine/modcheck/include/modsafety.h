#ifndef ENGINE_MODSAFETY_H
#define ENGINE_MODSAFETY_H

#include "modcheck.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::modcheck {

struct SafetyLimits {
  size_t maxFiles = 5000;
  uint64_t maxTotalBytes = 1ull << 30;
  uint64_t maxFileBytes = 512ull << 20;
  uint32_t maxRatio = 200;
  uint32_t maxTotalRatio = 100;
  size_t maxJsonBytes = 8u << 20;
  int maxJsonDepth = 64;
  uint32_t maxImageSide = 8192;
  uint64_t maxImagePixels = 40ull << 20;
  size_t maxPathBytes = 240;
};

std::string ExecutableNameReason(const std::string& path);
std::string ExecutableContentReason(const uint8_t* data, size_t size);
std::string ArchiveReason(const std::string& path, const uint8_t* data, size_t size);
std::string UnsafePathReason(const std::string& path);
std::string TraversalReason(const std::string& path);

enum class Kind { ModJson, Json, Lua, StoryText, Text, Png, Dds, DiscEntry, DiscFile, Unknown };
Kind KindOf(const std::string& path, const std::vector<GameRules::ExtraFolder>* extras = nullptr);
const char* KindName(Kind kind);

bool CheckJsonLimits(const std::string& path, std::string_view text, const SafetyLimits& limits, Report& report);
bool CheckPng(const std::string& path, const std::vector<uint8_t>& data, const SafetyLimits& limits, Report& report,
              uint32_t* width = nullptr, uint32_t* height = nullptr);
bool CheckDds(const std::string& path, const std::vector<uint8_t>& data, const SafetyLimits& limits, Report& report);

struct ZipEntry {
  std::string name;
  uint64_t packed = 0, size = 0;
  uint32_t crc = 0;
  uint16_t method = 0;
  uint32_t offset = 0;
  bool directory = false;
};
bool ScanZip(const std::vector<uint8_t>& zip, const SafetyLimits& limits, Report& report, std::vector<ZipEntry>* entries = nullptr);
bool ExtractZip(const std::vector<uint8_t>& zip, const std::filesystem::path& dest, const SafetyLimits& limits, std::string& error);
bool CheckAndExtractArchive(const std::vector<uint8_t>& zip, const std::filesystem::path& dest, const Options& options,
                            const SafetyLimits& limits, Report& report);

std::string Sha256Hex(const uint8_t* data, size_t size);
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t seed = 0);
bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out, uint64_t maxBytes = 1ull << 31);
std::string PathText(const std::filesystem::path& path);
std::string LowerAscii(std::string_view text);

}

#endif
