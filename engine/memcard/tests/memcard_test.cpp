#include <gc_memcard.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace engine::memcard;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v >> 16));
  put16(p + 2, static_cast<uint16_t>(v));
}

void makeEntry(uint8_t* e, const char* game, const char* name, uint16_t first, uint16_t blocks) {
  std::memset(e, 0, kDirEntrySize);
  std::memcpy(e, game, 4);
  std::memcpy(e + 4, "01", 2);
  e[6] = 0xFF;
  std::memcpy(e + 8, name, std::strlen(name));
  put32(e + 0x28, 12345);
  put32(e + 0x2C, 0x40);
  put16(e + 0x36, first);
  put16(e + 0x38, blocks);
  put16(e + 0x3A, 0xFFFF);
  put32(e + 0x3C, 0);
}

void sealDir(uint8_t* dir, int16_t checkCode) {
  put16(dir + 0x1FFA, static_cast<uint16_t>(checkCode));
  uint16_t s, si;
  CardChecksum(dir, kBlockSize - 4, &s, &si);
  put16(dir + 0x1FFC, s);
  put16(dir + 0x1FFE, si);
}

void sealFat(uint8_t* fat, int16_t checkCode) {
  put16(fat + 4, static_cast<uint16_t>(checkCode));
  uint16_t s, si;
  CardChecksum(fat + 4, kBlockSize - 4, &s, &si);
  put16(fat, s);
  put16(fat + 2, si);
}

std::vector<uint8_t> makeCard() {
  const uint32_t blocks = 64;
  std::vector<uint8_t> card(blocks * kBlockSize, 0);
  put16(card.data() + 0x22, 4);
  for (int d = 1; d <= 2; d++) {
    std::memset(card.data() + d * kBlockSize, 0xFF, kBlockSize);
  }
  uint8_t* dirOld = card.data() + 1 * kBlockSize;
  uint8_t* dirNew = card.data() + 2 * kBlockSize;
  makeEntry(dirOld, "GOLD", "old", 5, 1);
  sealDir(dirOld, 3);
  makeEntry(dirNew, "GXXE", "PokemonXD", 7, 3);
  makeEntry(dirNew + kDirEntrySize, "GTST", "other", 6, 1);
  sealDir(dirNew, 4);

  uint8_t* fat0 = card.data() + 3 * kBlockSize;
  uint8_t* fat1 = card.data() + 4 * kBlockSize;
  for (uint8_t* fat : {fat0, fat1}) {
    put16(fat + 7 * 2, 5);
    put16(fat + 5 * 2, 9);
    put16(fat + 9 * 2, 0xFFFF);
    put16(fat + 6 * 2, 0xFFFF);
  }
  sealFat(fat0, 10);
  sealFat(fat1, 11);
  fat1[100] ^= 1;

  for (int b : {7, 5, 9, 6}) {
    std::memset(card.data() + b * kBlockSize, b, kBlockSize);
  }
  return card;
}

}

int main() {
  {
    uint8_t w[2] = {0xFF, 0xFF};
    uint16_t s, si;
    CardChecksum(w, 2, &s, &si);
    check(s == 0 && si == 0, "card checksum: 0xFFFF counts as 0 (and its inverse 0x0000 stays 0)");
  }

  std::vector<uint8_t> card = makeCard();
  std::vector<CardFile> files;
  CardImageInfo info;
  std::string error;
  const bool read = ReadCardImage(card, files, info, error);
  check(read, "the card image reads " + error);
  check(info.blocks == 64, "64 blocks");
  check(info.directory == 1, "the newer directory copy is used");
  check(info.fat == 0, "the damaged allocation table copy is skipped");
  check(!info.warnings.empty(), "and that is reported");
  check(files.size() == 2, "two files");
  if (files.size() == 2) {
    const CardFile& a = files[0];
    check(a.entry.gameCode == "GXXE" && a.entry.makerCode == "01" && a.entry.fileName == "PokemonXD",
          "file 1 is GXXE-01 PokemonXD");
    check(a.entry.blockCount == 3 && a.data.size() == 3 * kBlockSize, "3 blocks");
    check(a.data[0] == 7 && a.data[kBlockSize] == 5 && a.data[2 * kBlockSize] == 9,
          "its blocks come in chain order 7, 5, 9");
    check(files[1].entry.fileName == "other" && files[1].data[0] == 6, "file 2 is block 6");

    const std::vector<uint8_t> gci = MakeGci(a);
    check(gci.size() == kDirEntrySize + 3 * kBlockSize, ".gci = 64-byte header + blocks");
    CardFile back;
    check(ReadGci(gci, back, error) && back.data == a.data && back.entry.raw == a.entry.raw,
          ".gci reads back the same");
    std::vector<CardFile> viaLoad;
    CardImageInfo li;
    check(LoadCardFiles(gci, viaLoad, li, error) && viaLoad.size() == 1, "LoadCardFiles takes a .gci by its size");
    std::vector<uint8_t> shortGci(gci.begin(), gci.end() - 1);
    check(!ReadGci(shortGci, back, error), "a .gci one byte short is refused");
  }
  {
    std::vector<CardFile> viaLoad;
    CardImageInfo li;
    check(LoadCardFiles(card, viaLoad, li, error) && viaLoad.size() == 2, "LoadCardFiles takes a card image");
  }

  {
    std::vector<uint8_t> broken = makeCard();
    uint8_t* fat0 = broken.data() + 3 * kBlockSize;
    put16(fat0 + 5 * 2, 0xFFFF);
    sealFat(fat0, 10);
    check(ReadCardImage(broken, files, info, error) && files.size() == 1, "a file with a broken chain is skipped");
  }
  {
    std::vector<uint8_t> bad = makeCard();
    bad[1 * kBlockSize + 5] ^= 1;
    bad[2 * kBlockSize + 5] ^= 1;
    const bool readBad = ReadCardImage(bad, files, info, error);
    check(!readBad, "no valid directory: refused (" + error + ")");
  }
  check(!ReadCardImage(std::vector<uint8_t>(1000), files, info, error), "not whole blocks: refused");

  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
