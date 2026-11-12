#include <gc_memcard.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace engine::memcard {

namespace {

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

std::string text(const uint8_t* p, size_t max) {
  size_t n = 0;
  while (n < max && p[n] != 0) {
    n++;
  }
  return std::string(reinterpret_cast<const char*>(p), n);
}

std::string hex(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%X", v);
  return buf;
}

constexpr size_t kDirCheckCode = 0x1FFA;
constexpr size_t kDirCheckSum = 0x1FFC;
constexpr size_t kFatCheckCode = 4;
constexpr uint16_t kFatChainEnd = 0xFFFF;

}

DirEntry ParseDirEntry(const uint8_t* p) {
  DirEntry e;
  std::memcpy(e.raw.data(), p, kDirEntrySize);
  e.gameCode = std::string(reinterpret_cast<const char*>(p), 4);
  e.makerCode = std::string(reinterpret_cast<const char*>(p + 4), 2);
  e.bannerFormat = p[7];
  e.fileName = text(p + 8, 32);
  e.modTime = be32(p + 0x28);
  e.imageOffset = be32(p + 0x2C);
  e.iconFormat = be16(p + 0x30);
  e.iconSpeed = be16(p + 0x32);
  e.permissions = p[0x34];
  e.copyCount = p[0x35];
  e.firstBlock = be16(p + 0x36);
  e.blockCount = be16(p + 0x38);
  e.commentOffset = be32(p + 0x3C);
  return e;
}

void CardChecksum(const uint8_t* p, size_t length, uint16_t* sum, uint16_t* sumInv) {
  uint16_t s = 0, si = 0;
  for (size_t i = 0; i + 1 < length; i += 2) {
    const uint16_t w = be16(p + i);
    s = static_cast<uint16_t>(s + w);
    si = static_cast<uint16_t>(si + static_cast<uint16_t>(~w));
  }
  *sum = s == 0xFFFF ? 0 : s;
  *sumInv = si == 0xFFFF ? 0 : si;
}

bool ReadGci(const std::vector<uint8_t>& bytes, CardFile& out, std::string& error) {
  if (bytes.size() < kDirEntrySize + kBlockSize) {
    error = "too small for a .gci file (" + std::to_string(bytes.size()) + " bytes)";
    return false;
  }
  DirEntry e = ParseDirEntry(bytes.data());
  const size_t want = kDirEntrySize + static_cast<size_t>(e.blockCount) * kBlockSize;
  if (e.blockCount == 0 || bytes.size() != want) {
    error = "not a .gci file: its header says " + std::to_string(e.blockCount) + " blocks (" +
            std::to_string(want) + " bytes), the file has " + std::to_string(bytes.size());
    return false;
  }
  out.entry = e;
  out.data.assign(bytes.begin() + kDirEntrySize, bytes.end());
  return true;
}

std::vector<uint8_t> MakeGci(const CardFile& file) {
  std::vector<uint8_t> out(file.entry.raw.begin(), file.entry.raw.end());
  out.insert(out.end(), file.data.begin(), file.data.end());
  return out;
}

bool ReadCardImage(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, CardImageInfo& info,
                   std::string& error) {
  files.clear();
  info = CardImageInfo{};
  if (bytes.size() % kBlockSize != 0 || bytes.size() < (kSystemBlocks + 1) * kBlockSize) {
    error = "not a card image (size " + std::to_string(bytes.size()) + " isn't a number of 8 KB blocks)";
    return false;
  }
  info.blocks = static_cast<uint32_t>(bytes.size() / kBlockSize);
  const uint16_t mbits = be16(bytes.data() + 0x22);
  if (mbits != 0 && static_cast<uint32_t>(mbits) * 16 != info.blocks) {
    info.warnings.push_back("the card header says " + std::to_string(mbits) + " Mbit (" +
                            std::to_string(mbits * 16) + " blocks), the image has " +
                            std::to_string(info.blocks) + " blocks");
  }

  const uint8_t* dir[2] = {bytes.data() + 1 * kBlockSize, bytes.data() + 2 * kBlockSize};
  bool dirOk[2];
  for (int i = 0; i < 2; i++) {
    uint16_t s, si;
    CardChecksum(dir[i], kBlockSize - 4, &s, &si);
    dirOk[i] = s == be16(dir[i] + kDirCheckSum) && si == be16(dir[i] + kDirCheckSum + 2);
    if (!dirOk[i]) {
      info.warnings.push_back("directory copy " + std::to_string(i) + " has a wrong checksum");
    }
  }
  if (dirOk[0] && dirOk[1]) {
    const int16_t c0 = static_cast<int16_t>(be16(dir[0] + kDirCheckCode));
    const int16_t c1 = static_cast<int16_t>(be16(dir[1] + kDirCheckCode));
    info.directory = static_cast<int16_t>(c0 - c1) < 0 ? 1 : 0;
  } else if (dirOk[0] || dirOk[1]) {
    info.directory = dirOk[0] ? 0 : 1;
  } else {
    error = "card image: neither directory copy has a valid checksum";
    return false;
  }

  const uint8_t* fat[2] = {bytes.data() + 3 * kBlockSize, bytes.data() + 4 * kBlockSize};
  bool fatOk[2];
  for (int i = 0; i < 2; i++) {
    uint16_t s, si;
    CardChecksum(fat[i] + 4, kBlockSize - 4, &s, &si);
    fatOk[i] = s == be16(fat[i]) && si == be16(fat[i] + 2);
    if (!fatOk[i]) {
      info.warnings.push_back("allocation table copy " + std::to_string(i) + " has a wrong checksum");
    }
  }
  if (fatOk[0] && fatOk[1]) {
    const int16_t c0 = static_cast<int16_t>(be16(fat[0] + kFatCheckCode));
    const int16_t c1 = static_cast<int16_t>(be16(fat[1] + kFatCheckCode));
    info.fat = static_cast<int16_t>(c0 - c1) < 0 ? 1 : 0;
  } else if (fatOk[0] || fatOk[1]) {
    info.fat = fatOk[0] ? 0 : 1;
  } else {
    error = "card image: neither allocation table copy has a valid checksum";
    return false;
  }
  const uint8_t* table = fat[info.fat];

  for (int n = 0; n < kMaxFiles; n++) {
    const uint8_t* p = dir[info.directory] + n * kDirEntrySize;
    DirEntry e = ParseDirEntry(p);
    if (!e.IsUsed()) {
      continue;
    }
    CardFile f;
    f.entry = e;
    f.data.reserve(static_cast<size_t>(e.blockCount) * kBlockSize);
    uint16_t block = e.firstBlock;
    bool ok = true;
    for (uint16_t i = 0; i < e.blockCount; i++) {
      if (block < kSystemBlocks || block >= info.blocks) {
        ok = false;
        break;
      }
      const uint8_t* b = bytes.data() + static_cast<size_t>(block) * kBlockSize;
      f.data.insert(f.data.end(), b, b + kBlockSize);
      block = be16(table + static_cast<size_t>(block) * 2);
      if (i + 1 < e.blockCount && block == kFatChainEnd) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      info.warnings.push_back("file '" + e.fileName + "' (" + e.gameCode + "): its block chain is broken at block " +
                              hex(block) + "; skipped");
      continue;
    }
    files.push_back(std::move(f));
  }
  return true;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) {
    return false;
  }
  in.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(size));
  if (size > 0) {
    in.read(reinterpret_cast<char*>(out.data()), size);
  }
  return static_cast<bool>(in);
}

bool LoadCardFiles(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, CardImageInfo& info,
                   std::string& error) {
  files.clear();
  info = CardImageInfo{};
  if (bytes.size() >= kDirEntrySize && bytes.size() % kBlockSize == kDirEntrySize) {
    CardFile f;
    if (!ReadGci(bytes, f, error)) {
      return false;
    }
    files.push_back(std::move(f));
    return true;
  }
  return ReadCardImage(bytes, files, info, error);
}

}
