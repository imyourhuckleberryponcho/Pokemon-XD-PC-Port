#pragma once

#include "disc_textures.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::hdtex {

struct ToolInfo {
  std::string url;
  std::string sha256;
  std::string system;
  std::string exeName;
  uint64_t bytes = 0;
};
ToolInfo OfficialTool();
std::string ToolLicenceText();

bool ToolInstalled(const std::filesystem::path& folder, std::filesystem::path* exe = nullptr);
bool InstallTool(const std::filesystem::path& folder, std::string& error,
                 const std::function<bool(uint64_t, uint64_t)>& progress);

struct Image {
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
};
bool ReadPng(const std::filesystem::path& file, Image& out);
bool WritePng(const std::filesystem::path& file, uint32_t w, uint32_t h, const uint8_t* rgba, bool hasAlpha, int level = 6);

void BleedColour(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
bool LooksTiling(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
double ColourMatch(std::vector<uint8_t>& result, const std::vector<uint8_t>& original, uint32_t w, uint32_t h, uint32_t s,
                   bool wrap, int radius = 3);
double MeanColourDifference(const std::vector<uint8_t>& result, const std::vector<uint8_t>& original, uint32_t w, uint32_t h,
                            uint32_t s);

struct MakerOptions {
  std::filesystem::path toolExe;
  std::filesystem::path models;
  std::filesystem::path work;
  std::filesystem::path pack;
  uint32_t scale = 2;
  std::string model = "realesrgan-x4plus";
  uint32_t batch = 48;
  uint32_t minSide = 16;
  bool colourMatch = true;
  bool includeCharacters = false;
  bool includeEffects = false;
  bool includeInterface = false;
  std::string gpu;
  uint64_t limit = 0;
  std::vector<std::string> only;
};

struct MakerProgress {
  const char* phase = "";
  uint64_t done = 0, total = 0;
  std::string detail;
};

struct MakerResult {
  bool ok = false, cancelled = false;
  std::string error;
  uint64_t candidates = 0;
  uint64_t made = 0, resumed = 0, tooSmall = 0, failed = 0;
  uint64_t skippedByClass[6] = {};
  uint64_t packBytes = 0;
  double seconds = 0, colourChange = 0;
};

MakerResult MakePack(const FileSource& source, const MakerOptions& options,
                     const std::function<bool(const MakerProgress&)>& progress);

}
