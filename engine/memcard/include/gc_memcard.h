#ifndef ENGINE_GC_MEMCARD_H
#define ENGINE_GC_MEMCARD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::memcard {

constexpr size_t kBlockSize = 0x2000;
constexpr size_t kDirEntrySize = 0x40;
constexpr int kMaxFiles = 127;
constexpr int kSystemBlocks = 5;

struct DirEntry {
  std::array<uint8_t, kDirEntrySize> raw{};
  std::string gameCode;
  std::string makerCode;
  uint8_t bannerFormat = 0;
  std::string fileName;
  uint32_t modTime = 0;
  uint32_t imageOffset = 0;
  uint16_t iconFormat = 0;
  uint16_t iconSpeed = 0;
  uint8_t permissions = 0;
  uint8_t copyCount = 0;
  uint16_t firstBlock = 0;
  uint16_t blockCount = 0;
  uint32_t commentOffset = 0;

  bool IsUsed() const { return raw[0] != 0xFF; }
};

struct CardFile {
  DirEntry entry;
  std::vector<uint8_t> data;
};

DirEntry ParseDirEntry(const uint8_t* p);

void CardChecksum(const uint8_t* p, size_t length, uint16_t* sum, uint16_t* sumInv);

bool ReadGci(const std::vector<uint8_t>& bytes, CardFile& out, std::string& error);
std::vector<uint8_t> MakeGci(const CardFile& file);

struct CardImageInfo {
  uint32_t blocks = 0;
  int directory = -1;
  int fat = -1;
  std::vector<std::string> warnings;
};
bool ReadCardImage(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, CardImageInfo& info,
                   std::string& error);

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out);

bool LoadCardFiles(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, CardImageInfo& info,
                   std::string& error);

}

#endif
