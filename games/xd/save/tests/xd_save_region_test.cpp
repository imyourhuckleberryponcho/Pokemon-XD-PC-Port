#include <xd_save.h>
#include <xd_save_region.h>
#include <xd_save_write.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace xd::save;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

constexpr ByteOrder kBe = ByteOrder::GameCube;

struct Layout {
  int id;
  uint32_t sizeAt, offsetAt, usSize, jpSize;
};
const Layout kOrder[] = {
    {1, 0x18, 0x38, 0x88, 0x80},      {22, 0x36, 0x74, 0xC800, 0xC800}, {3, 0x1C, 0x40, 0xBC50, 0xBC0C}, {15, 0x26, 0x54, 0x2400, 0x1E00},
    {2, 0x1A, 0x3C, 0x978, 0x978},    {10, 0x1E, 0x44, 0x4A, 0x4A},    {21, 0x34, 0x70, 0x2298, 0x2298},
    {11, 0x20, 0x48, 0xCC, 0xCC},     {12, 0x22, 0x4C, 0x1774, 0x1774}, {14, 0x24, 0x50, 0x364, 0x2EC},
    {23, 0x28, 0x58, 0x6B4, 0x6B4},   {16, 0x2A, 0x5C, 0xB, 0xB},      {17, 0x2C, 0x60, 0x400, 0x400},
    {18, 0x2E, 0x64, 0x800, 0x800},   {19, 0x30, 0x68, 0x1000, 0x1000}, {20, 0x32, 0x6C, 0x2000, 0x2000},
};
uint32_t stride(uint32_t size) { return (size + 0x1F) & ~0xFu; }

void put16(uint8_t* p, uint16_t v) { Write16(p, v, kBe); }
void put32(uint8_t* p, uint32_t v) { Write32(p, v, kBe); }
uint16_t kana(int i) { return static_cast<uint16_t>(0x30A2 + i * 2); }

std::vector<uint8_t> makeSlot(bool japanese, int32_t count, uint8_t language) {
  std::vector<uint8_t> slot(kSlotSize, 0);
  slot[0] = slot[1] = 1;
  slot[2] = japanese ? 0 : 1;
  put32(slot.data() + 4, static_cast<uint32_t>(count));
  uint8_t* d = slot.data() + kSlotHeaderSize;
  for (int k = 0; k < 4; k++) {
    put16(d + k * 2, static_cast<uint16_t>(count * 7919u + k * 104729u + 5));
  }
  uint32_t headSum = 0;
  for (int i = 0; i < 8; i++) {
    headSum += slot[i];
  }
  uint32_t off = 0;
  put16(d + 0x18, japanese ? 0x80 : 0x88);
  for (const Layout& l : kOrder) {
    const uint32_t size = japanese ? l.jpSize : l.usSize;
    put16(d + l.sizeAt, static_cast<uint16_t>(size));
    if (l.id == 3) {
      put16(d + 0x80, static_cast<uint16_t>(size >> 16));
    }
    put16(d + l.offsetAt, static_cast<uint16_t>(off));
    put16(d + l.offsetAt + 2, static_cast<uint16_t>(off >> 16));
    uint8_t* s = d + kSectionBase + off;
    for (uint32_t i = 0; i < size; i++) {
      s[i] = static_cast<uint8_t>(l.id * 31 + i * 7 + 1);
    }
    switch (l.id) {
    case 1: break;
    case 2: {
      std::memset(s, 0, 0x30);
      for (int i = 0; i < 3; i++) {
        put16(s + i * 2, kana(i));
      }
      put32(s + 0x2C, 0xBEEF1234u);
      put16(s + 0x30, 133);
      s[0x30 + 0x11] = 20;
      break;
    }
    case 3: {
      if (japanese) {
        for (int i = 0; i < 4; i++) s[0xBC0C + i] = 0x77;
      }
      std::memset(s, 0, 0x14);
      for (int i = 0; i < 4; i++) {
        put16(s + i * 2, kana(i + 10));
      }
      break;
    }
    case 22:
      put32(s, 0x414E5357);
      put32(s + 4, 0x1234);
      break;
    case 15: {
      std::memset(s, 0, size);
      const uint32_t rec = japanese ? 0x3C : 0x48;
      uint8_t* r = s + 5 * rec;
      r[0] = 0x60;
      put16(r + 0x1A, 300);
      put32(r + 0x24, 0x55);
      for (int i = 0; i < 5; i++) {
        put16(r + 0x28 + i * 2, kana(i + 20));
      }
      if (japanese) {
        put16(r + 0x34, 0x0102);
        put16(r + 0x36, 0x0304);
        put32(r + 0x38, 0x05060708);
      }
      break;
    }
    default: break;
    }
    off += stride(size);
  }
  uint8_t* g = d + kSectionBase;
  std::memset(g, 0, japanese ? 0x80 : 0x88);
  put32(g + 0x10, 42);
  put32(g + 0x1C, 0x8A);
  const uint32_t jpSumAt = japanese ? 0x34 : 0x38;
  if (japanese) {
    const float play = 3 * 3600 + 25 * 60 + 7.5f;
    uint32_t bits;
    std::memcpy(&bits, &play, 4);
    put32(g + 8 + 0x18, bits);
    g[8 + 0x21] = 1;
    g[8 + 0x22] = 2;
    for (int i = 0x23; i < 0x2C; i++) {
      g[8 + i] = 0xEE;
    }
    for (int i = 0; i < 0x48; i++) {
      g[0x38 + i] = static_cast<uint8_t>(0x80 + i);
    }
  } else {
    g[8 + 0x23] = language;
    for (int i = 0; i < 0x48; i++) {
      g[0x40 + i] = static_cast<uint8_t>(0x80 + i);
    }
  }
  put32(g + jpSumAt, headSum);
  EncryptSaveData(d, kBe);
  return slot;
}

std::vector<uint8_t> makeFile(const std::vector<uint8_t>& slot0, const std::vector<uint8_t>& slot1) {
  std::vector<uint8_t> file(kFileSize, 0);
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) {
    sum += Read32(file.data() + i * 4, kBe);
  }
  put32(file.data() + 0x3A40, sum);
  if (!slot0.empty()) {
    std::memcpy(file.data() + SlotOffset(0), slot0.data(), kSlotSize);
  }
  if (!slot1.empty()) {
    std::memcpy(file.data() + SlotOffset(1), slot1.data(), kSlotSize);
  }
  return file;
}

struct Opened {
  Save save;
  ReadReport report;
  bool ok = false;
};
Opened open(const std::vector<uint8_t>& file) {
  Opened o;
  std::string error;
  o.ok = ReadSave(file, o.save, o.report, error);
  if (!o.ok) {
    std::printf("     ReadSave: %s\n", error.c_str());
  }
  return o;
}

void testDetection() {
  std::string why;
  const auto jp = makeFile(makeSlot(true, 3, 0), {});
  const auto us = makeFile(makeSlot(false, 3, 2), {});
  check(LayoutOf(jp, why) == SaveKind::JapaneseLayout, "a Japanese-layout save is detected from its section table");
  check(LayoutOf(us, why) == SaveKind::UsaLayout, "a USA-layout save is detected as such");
  auto pal = makeFile(makeSlot(false, 3, 4), {});
  check(LayoutOf(pal, why) == SaveKind::UsaLayout, "a European save (USA layout) is detected as such");
  const auto both = makeFile(makeSlot(false, 9, 2), makeSlot(true, 10, 0));
  check(LayoutOf(both, why) == SaveKind::JapaneseLayout, "the newest slot decides the layout");
  auto damaged = jp;
  damaged[SlotOffset(0) + 0x1000] ^= 0x55;
  check(LayoutOf(damaged, why) == SaveKind::Unknown && !why.empty(), "a damaged save is Unknown, with a reason: " + why);
  check(LayoutOf(std::vector<uint8_t>(1234), why) == SaveKind::Unknown, "a file of the wrong size is Unknown: " + why);
  check(LayoutOf(makeFile({}, {}), why) == SaveKind::Unknown, "an empty save file is Unknown: " + why);
}

void testConvert() {
  const auto jp = makeFile(makeSlot(true, 7, 0), makeSlot(true, 6, 0));
  std::vector<uint8_t> out;
  JapaneseReport rep;
  std::string error;
  const bool converted = ConvertJapanese(jp, out, rep, error);
  check(converted, "a Japanese save converts" + (error.empty() ? "" : ": " + error));
  if (out.empty()) {
    return;
  }
  check(rep.slotUsed == 0, "the newest slot (counter 7) was used");
  check(out.size() == kFileSize, "the result is a whole save file");
  check(std::memcmp(out.data(), jp.data(), kBannerSize) == 0, "the banner block is kept as it is");
  bool otherEmpty = out[SlotOffset(1)] == 1 && out[SlotOffset(1) + 1] == 1 && out[SlotOffset(1) + 2] == 1;
  for (size_t i = 3; i < kSlotSize; i++) {
    otherEmpty &= out[SlotOffset(1) + i] == 0;
  }
  check(otherEmpty, "the other slot is marked 1,1,1 with save counter 0 and holds nothing (the game refuses a file without the marker)");
  std::string why;
  check(LayoutOf(out, why) == SaveKind::UsaLayout, "the result is in the USA layout");

  Opened o = open(out);
  check(o.ok, "the USA reader accepts the converted save");
  if (!o.ok) {
    return;
  }
  check(o.report.slots[0].checksumOk && o.report.slots[0].headerSumOk, "both checks (checksums, header sum) pass");
  check(o.report.warnings.empty(), "no section size warning (" + std::to_string(o.report.warnings.size()) + ")");
  check(o.save.saveCount == 7, "the save counter is kept");
  check(o.save.gameSaveCount == 42 && o.save.floorId == 0x8A, "game data: save count and floor are kept");
  check(o.save.playTimeSeconds == 3 * 3600 + 25 * 60 + 7.5, "play time: the Japanese float became the USA double");
  check(o.save.optionNoVibration == 1 && o.save.optionAudio == 2, "game data: the options are kept");
  check(SaveLanguage(o.save) == 2, "the message language is 2");
  const uint8_t* g = o.save.SectionData(1);
  check(g[8 + 0x23] == 2 && g[8 + 0x24] == 0 && g[8 + 0x2C] != 0xEE && g[8 + 0x30 - 1] != 0xEE, "unknown Japanese block bytes are not carried");
  bool rest = true;
  for (int i = 0; i < 0x48; i++) {
    rest &= g[0x40 + i] == static_cast<uint8_t>(0x80 + i);
  }
  check(rest, "game data: the part after the block (zukan, playdata) moved 8 bytes");
  check(o.save.trainerName == std::u16string({kana(0), kana(1), kana(2)}), "the trainer's name keeps its Japanese characters");
  check(o.save.trainerOtId == 0xBEEF1234u && o.save.party[0].species == 133 && o.save.party[0].level == 20, "the party is kept");
  check(o.save.boxes[0].name == std::u16string({kana(10), kana(11), kana(12), kana(13)}), "the PC box name keeps its characters");
  const uint8_t* b = o.save.SectionData(3);
  bool boxSame = true;
  for (uint32_t i = 0x20; i < 0xBC0C; i += 97) {
    boxSame &= b[i] == static_cast<uint8_t>(3 * 31 + i * 7 + 1);
  }
  bool boxTail = true;
  for (uint32_t i = 0xBC0C; i < 0xBC50; i++) {
    boxTail &= b[i] == 0;
  }
  check(boxSame && boxTail, "PC boxes: the Japanese bytes are kept, the USA's extra 0x40 are 0");
  const uint8_t* t = o.save.SectionData(14);
  bool tool = true;
  for (int r = 0; r < 6; r++) {
    for (int i = 0; i < 0x7C; i++) {
      const uint32_t src = static_cast<uint32_t>(r * 0x7C + i);
      const uint8_t want = static_cast<uint8_t>(14 * 31 + src * 7 + 1);
      const int dst = i < 0x34 ? i : i + 0x14;
      tool &= t[r * 0x90 + dst] == want;
    }
    for (int i = 0x34; i < 0x48; i++) {
      tool &= t[r * 0x90 + i] == 0;
    }
  }
  tool &= t[6 * 0x90] == static_cast<uint8_t>(14 * 31 + (6 * 0x7C) * 7 + 1);
  check(tool, "battle rules: 6 sets moved from 0x7C to 0x90, names padded, the last byte follows");
  const uint8_t* sh = o.save.SectionData(15);
  const uint8_t* r5 = sh + 5 * 0x48;
  check(r5[0] == 0x60 && Read16(r5 + 0x1A, kBe) == 300 && Read32(r5 + 0x24, kBe) == 0x55, "Shadow list: the first 0x28 bytes are kept");
  bool name = true;
  for (int i = 0; i < 5; i++) {
    name &= Read16(r5 + 0x28 + i * 2, kBe) == kana(i + 20);
  }
  for (int i = 5; i < 11; i++) {
    name &= Read16(r5 + 0x28 + i * 2, kBe) == 0;
  }
  check(name, "Shadow list: the 5-character trainer name keeps its characters, the rest is 0");
  check(Read16(r5 + 0x3E, kBe) == 0x0102 && Read16(r5 + 0x40, kBe) == 0x0304 && Read32(r5 + 0x44, kBe) == 0x05060708,
        "Shadow list: darkPokemonID, reversPattern and walkCount moved to the USA places");
  check(o.save.shadows[5].species == 300 && o.save.shadows[5].status == 3, "Shadow list: the reader sees the entry");
  const uint8_t* flags = o.save.SectionData(23);
  bool flagsSame = true;
  for (uint32_t i = 0; i < 0x6B4; i += 13) {
    flagsSame &= flags[i] == static_cast<uint8_t>(23 * 31 + i * 7 + 1);
  }
  check(flagsSame, "story flags: copied as they are");
  const uint8_t* memo = o.save.SectionData(12);
  check(memo[0x100] == static_cast<uint8_t>(12 * 31 + 0x100 * 7 + 1), "memo: copied as it is");
  const uint8_t* m = o.save.SectionData(22);
  check(Read32(m, kBe) == 0x414E5357 && Read32(m + 4, kBe) == 0, "map state: m_valid is 0 (the room state is dropped)");
  check(!rep.notes.empty(), "the report says what was changed");

  std::vector<uint8_t> again;
  JapaneseReport rep2;
  check(!ConvertJapanese(out, again, rep2, error) && error.find("already") != std::string::npos,
        "a converted save is refused as 'already in the USA layout': " + error);
}

void testRefusals() {
  std::vector<uint8_t> out;
  JapaneseReport rep;
  std::string error;
  auto damaged = makeFile(makeSlot(true, 3, 0), {});
  damaged[SlotOffset(0) + 0x5000] ^= 1;
  check(!ConvertJapanese(damaged, out, rep, error) && error.find("checksum") != std::string::npos, "a damaged save is refused: " + error);
  check(!ConvertJapanese(std::vector<uint8_t>(100), out, rep, error) && error.find("100 bytes") != std::string::npos,
        "a file of the wrong size is refused: " + error);
  check(!ConvertJapanese(makeFile({}, {}), out, rep, error), "an empty save is refused: " + error);
  check(!ConvertJapanese(makeFile(makeSlot(false, 3, 2), {}), out, rep, error), "a USA save is refused: " + error);
  auto slot = makeSlot(true, 3, 0);
  std::vector<uint8_t> file = makeFile(slot, {});
  {
    std::vector<uint8_t> s(file.begin() + SlotOffset(0), file.begin() + SlotOffset(0) + kSlotSize);
    uint8_t* d = s.data() + kSlotHeaderSize;
    DecryptSaveData(d, kBe);
    put16(d + 0x24, 0x364);
    EncryptSaveData(d, kBe);
    std::memcpy(file.data() + SlotOffset(0), s.data(), kSlotSize);
  }
  check(!ConvertJapanese(file, out, rep, error) && error.find("battle rules") != std::string::npos,
        "a section of the wrong size is refused, naming it: " + error);
  {
    std::vector<uint8_t> s(kSlotSize);
    std::memcpy(s.data(), file.data() + SlotOffset(0), kSlotSize);
    uint8_t* d = s.data() + kSlotHeaderSize;
    DecryptSaveData(d, kBe);
    put16(d + 0x24, 0x2EC);
    put16(d + 0x50, 0x7F00);
    put16(d + 0x52, 0x2);
    EncryptSaveData(d, kBe);
    std::memcpy(file.data() + SlotOffset(0), s.data(), kSlotSize);
  }
  check(!ConvertJapanese(file, out, rep, error) && error.find("outside") != std::string::npos,
        "a section outside the data is refused: " + error);
}

void testEuropean() {
  struct Case { uint8_t n; const char* name; const char* id; };
  for (const Case& c : {Case{2, "English", "us"}, Case{3, "German", "ge"}, Case{4, "French", "fr"}, Case{5, "Italian", "it"},
                        Case{6, "Spanish", "sp"}}) {
    Opened o = open(makeFile(makeSlot(false, 5, c.n), {}));
    for (const std::string& w : o.report.warnings) {
      std::printf("     warning: %s\n", w.c_str());
    }
    check(o.ok && o.report.warnings.empty(), std::string("a European-layout save (language ") + c.name + ") is read with no warning");
    check(SaveLanguage(o.save) == c.n && std::string(LanguageName(c.n)) == c.name && std::string(LanguageId(c.n)) == c.id,
          std::string("its language is ") + c.name + " / " + c.id);
  }
  check(std::string(LanguageName(0)).empty() && std::string(LanguageId(1)).empty() && std::string(LanguageId(9)).empty(),
        "no language for 0, 1 (Japanese) or 9");
  Opened jpConverted;
  {
    std::vector<uint8_t> out;
    JapaneseReport rep;
    std::string error;
    ConvertJapanese(makeFile(makeSlot(true, 2, 0), {}), out, rep, error);
    jpConverted = open(out);
  }
  check(jpConverted.ok && SaveLanguage(jpConverted.save) == 2, "a converted Japanese save reports language 2 (English text, kana names)");
}

void testDisplay() {
  auto u = [](std::initializer_list<uint32_t> l) {
    std::u16string r;
    for (uint32_t c : l) r.push_back(static_cast<char16_t>(c));
    return r;
  };
  check(DisplayUtf8(u({0x30A2, 0x30AB})) == "AKA", "display: the Japanese trainer name (A KA) reads AKA");
  check(DisplayUtf8(u({0x30A4, 0x30FC, 0x30D6, 0x30A4})) == "I-BUI", "display: the long mark");
  check(DisplayUtf8(u({0x30DC, 0x30C3, 0x30AF, 0x30B9, 0xFF11})) == "BOKKUSU1", "display: small tsu doubles, full-width digit");
  check(DisplayUtf8(u({0x30D4, 0x30AB, 0x30C1, 0x30E5, 0x30A6})) == "PIKACHUU", "display: CHI + small YU = CHU");
  check(DisplayUtf8(u({0x30CB, 0x30E3, 0x30FC, 0x30B9})) == "NYA-SU", "display: NI + small YA = NYA");
  check(DisplayUtf8(u({0x3042, 0x304B})) == "AKA", "display: hiragana too");
  check(DisplayUtf8(u({'A', 'S', 'H'})) == "ASH" && ToUtf8(u({'A', 'S', 'H'})) == "ASH", "display: plain names are unchanged");
  check(DisplayUtf8(u({0xFF21, 0xFF42})) == "Ab", "display: full-width letters");
}

}

int main() {
  testDetection();
  testConvert();
  testRefusals();
  testEuropean();
  testDisplay();
  std::printf("%s\n", sOk ? "PASS" : "FAILED");
  return sOk ? 0 : 1;
}
