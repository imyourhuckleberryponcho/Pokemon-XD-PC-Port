#include <gc_memcard.h>
#include <xd_save.h>
#include <xd_save_convert.h>

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

struct Writer {
  ByteOrder order;
  void w16(uint8_t* p, uint16_t v) const {
    p[order == ByteOrder::GameCube ? 0 : 1] = static_cast<uint8_t>(v >> 8);
    p[order == ByteOrder::GameCube ? 1 : 0] = static_cast<uint8_t>(v);
  }
  void w32(uint8_t* p, uint32_t v) const {
    for (int i = 0; i < 4; i++) p[order == ByteOrder::GameCube ? i : 3 - i] = static_cast<uint8_t>(v >> (24 - 8 * i));
  }
  void w64(uint8_t* p, uint64_t v) const {
    for (int i = 0; i < 8; i++) p[order == ByteOrder::GameCube ? i : 7 - i] = static_cast<uint8_t>(v >> (56 - 8 * i));
  }
  void str(uint8_t* p, const char* s) const {
    for (; *s; s++, p += 2) w16(p, static_cast<uint8_t>(*s));
  }
};

struct Layout {
  int id;
  uint32_t sizeAt, offsetAt, size, stride;
};
const Layout kOrder[] = {
    {22, 0x36, 0x74, 0xC800, 0xC810}, {3, 0x1C, 0x40, 0xBC50, 0xBC60}, {15, 0x26, 0x54, 0x2400, 0x2410},
    {2, 0x1A, 0x3C, 0x978, 0x990},    {10, 0x1E, 0x44, 0x4A, 0x60},     {21, 0x34, 0x70, 0x2298, 0x22B0},
    {11, 0x20, 0x48, 0xCC, 0xE0},     {12, 0x22, 0x4C, 0x1774, 0x1790}, {14, 0x24, 0x50, 0x364, 0x380},
    {23, 0x28, 0x58, 0x6B4, 0x6D0},   {16, 0x2A, 0x5C, 0xB, 0x20},      {17, 0x2C, 0x60, 0x400, 0x410},
    {18, 0x2E, 0x64, 0x800, 0x810},   {19, 0x30, 0x68, 0x1000, 0x1010}, {20, 0x32, 0x6C, 0x2000, 0x2010},
};

void makePokemon(const Writer& w, uint8_t* p, uint16_t species, uint8_t level, uint32_t pid, const char* nick,
                 uint16_t shadowId) {
  w.w16(p + 0x00, species);
  w.w16(p + 0x02, 13);
  w.w16(p + 0x04, 30);
  w.w16(p + 0x06, 70);
  w.w16(p + 0x08, 99);
  w.w16(p + 0x0A, 0x0102);
  p[0x0E] = 10;
  p[0x0F] = 4;
  p[0x11] = level;
  p[0x1D] = 0x40;
  p[0x1E] = 0x11;
  p[0x1F] = 0x3C;
  w.w32(p + 0x20, 1000 + level);
  w.w32(p + 0x24, 0xBEEF3039);
  w.w32(p + 0x28, pid);
  w.w32(p + 0x30, 0x808330B8);
  p[0x34] = 0x0B;
  p[0x37] = 0x02;
  w.str(p + 0x38, "WES");
  w.str(p + 0x4E, nick);
  w.str(p + 0x64, nick);
  w.w32(p + 0x7C, 0x00400D4C);
  for (int i = 0; i < 4; i++) {
    w.w16(p + 0x80 + i * 4, static_cast<uint16_t>(330 + i));
    p[0x82 + i * 4] = static_cast<uint8_t>(20 + i);
    p[0x83 + i * 4] = static_cast<uint8_t>(i);
  }
  for (int i = 0; i < 6; i++) {
    w.w16(p + 0x90 + i * 2, static_cast<uint16_t>(400 + i));
    w.w16(p + 0x9C + i * 2, static_cast<uint16_t>(i * 50 + 3));
    p[0xA8 + i] = static_cast<uint8_t>(31 - i);
  }
  w.w16(p + 0xB8, 0x1234);
  w.w16(p + 0xBA, shadowId);
  p[0xBD] = 0x14;
  p[0xC2] = 0x2C;
}

std::vector<uint8_t> makeSlot(ByteOrder order, int32_t count, const char* name, uint16_t keySeed) {
  const Writer w{order};
  std::vector<uint8_t> slot(kSlotSize, 0);
  slot[0] = slot[1] = slot[2] = 1;
  w.w32(slot.data() + 4, static_cast<uint32_t>(count));
  uint8_t* d = slot.data() + kSlotHeaderSize;
  for (int k = 0; k < 4; k++) w.w16(d + k * 2, static_cast<uint16_t>(keySeed * 7919u + k * 104729u));
  for (int i = 0x84; i < 0xA0; i += 2) w.w16(d + i, static_cast<uint16_t>(i * 0x0303));
  w.w16(d + 0x18, 0x88);
  uint8_t* g = d + kSectionBase;
  g[0] = 0x0B; g[1] = 3; g[2] = 2; g[3] = 2;
  g[4] = 0x48; g[5] = 0x79;
  w.w64(g + 0x08, 0x5674929BA853E6FEull);
  w.w32(g + 0x10, 121);
  w.w32(g + 0x1C, 0x3B);
  w.w32(g + 0x24, 0x8F);
  g[0x28] = 2; g[0x2B] = 2;
  const double play = 32 * 3600 + 19 * 60 + 33.25;
  uint64_t bits;
  std::memcpy(&bits, &play, 8);
  w.w64(g + 0x30, bits);
  uint32_t headSum = 0;
  for (int i = 0; i < 8; i++) headSum += slot[i];
  w.w32(g + 0x38, headSum);
  w.w16(g + 0x40, 0x0005);

  uint32_t off = 0xA0;
  for (const Layout& l : kOrder) {
    w.w16(d + l.sizeAt, static_cast<uint16_t>(l.size));
    w.w16(d + l.offsetAt, static_cast<uint16_t>(off));
    w.w16(d + l.offsetAt + 2, static_cast<uint16_t>(off >> 16));
    uint8_t* s = d + kSectionBase + off;
    switch (l.id) {
    case 2:
      w.str(s, name);
      w.w32(s + 0x2C, 0xBEEF3039);
      makePokemon(w, s + 0x30, 391, 56, 0x1EC2A0E2, "Armaldo", 42);
      makePokemon(w, s + 0x30 + kPokemonSize, 196, 55, 0x3ECD32B8, "Espeon", 0);
      w.w16(s + 0x4C8, 13); w.w16(s + 0x4CA, 4);
      w.w16(s + 0x7F0, 0x1F5); w.w16(s + 0x7F2, 1);
      s[0x8E0] = 0;
      w.w16(s + 0x8E2, 0x3CA5);
      w.w32(s + 0x8E4, 81061); w.w32(s + 0x8E8, 54490); w.w32(s + 0x8EC, 68000);
      s[0x8F0] = 1;
      w.w16(s + 0x8FA, 7); w.w16(s + 0x8FC, 2);
      w.str(s + 0x922, "HIZUKI");
      s[0x938] = 0x7B;
      w.w16(s + 0x93A, 0xFF11);
      w.w32(s + 0x93C, 0x3F8BDC93); w.w32(s + 0x940, 0x4208BDA5); w.w32(s + 0x944, 0xC1A00000);
      w.w32(s + 0x948, 0x16C7); w.w32(s + 0x94C, 0x65);
      w.w16(s + 0x95A, 0x8A);
      s[0x95C] = 2;
      break;
    case 3:
      w.str(s + 2 * 0x170C, "BOX 3");
      makePokemon(w, s + 2 * 0x170C + 0x14 + 4 * kPokemonSize, 150, 70, 0xCAFEBABE, "MEW2", 0);
      w.w16(s + 0xB860, 20); w.w16(s + 0xB862, 1);
      w.w32(s + 0xBC0C, 3);
      break;
    case 10:
      w.w16(s + 0x00, 0x21); w.w16(s + 0x40, 1); s[0x42] = 0x80; w.w16(s + 0x48, 5);
      break;
    case 11:
      s[0] = 1; s[1] = 20; w.w16(s + 2, 1234); w.w32(s + 4, 0xFFFFFF00);
      makePokemon(w, s + 8, 133, 20, 0x1111, "EEVEE", 0);
      break;
    case 12:
      w.w16(s, 2);
      w.w16(s + 4, 0x10); w.w32(s + 8, 0xBEEF3039); w.w32(s + 12, 0x1111);
      w.w16(s + 16, 0x20); w.w32(s + 20, 0x12345678); w.w32(s + 24, 0x9ABCDEF0);
      break;
    case 14:
      for (int r = 0; r < 6; r++) {
        uint8_t* rule = s + r * 0x90;
        w.w16(rule, 1); w.w16(rule + 6, 0x8000); w.w32(rule + 8, 0x01020304); rule[0xC] = 9;
        w.w16(rule + 0x1A, 4); w.str(rule + 0x1E, "RULE"); w.w32(rule + 0x48, 77); w.w32(rule + 0x4C, 0xFFFFFFFF);
        w.w16(rule + 0x50, 0x9124); rule[0x52 + r] = 1; w.w16(rule + 0x8E, 0xABCD);
      }
      s[0x360] = 1;
      break;
    case 15: {
      uint8_t* e = s + 42 * kShadowEntrySize;
      e[0] = static_cast<uint8_t>(4 << 5) | 1; e[1] = 0x28; e[2] = 0x80; e[6] = 0x52; e[7] = 0x80;
      e[0x0B] = 31; e[0x11] = 3; e[0x13] = 5;
      w.w16(e + 0x14, 0x3039); w.w16(e + 0x16, 0x43); w.w16(e + 0x18, 70); w.w16(e + 0x1A, 391);
      w.w32(e + 0x1C, 0x1EC2A0E2); w.w32(e + 0x20, 0x00012345); w.w32(e + 0x24, 0);
      w.str(e + 0x28, "CIPHER"); w.w16(e + 0x3E, 42); e[0x40] = 3; w.w32(e + 0x44, 9999);
      e = s + 7 * kShadowEntrySize;
      e[0] = static_cast<uint8_t>(3 << 5); w.w16(e + 0x1A, 25); w.w32(e + 0x24, 1500);
      break;
    }
    case 23:
      for (int i = 0; i < 0x6B0 / 4; i++) w.w32(s + i * 4, static_cast<uint32_t>(i) * 0x01010101u ^ 0x80000001u);
      s[0x6B0] = 0xAA;
      break;
    case 16:
      s[4] = 44; s[5] = 45;
      break;
    case 21:
      makePokemon(w, s + 0x3D8 * 2 + kPokemonSize * 4, 251, 30, 0x3333, "SHADOW", 9);
      s[0x3D8 * 2 + 0x3D4] = 1;
      break;
    case 22: {
      std::memcpy(s, "ANSW", 4);
      const uint32_t script = 0x41, mods = 8 + 0x3C + 0x8C;
      w.w32(s + 4, 0x20 + script + mods); w.w32(s + 8, script); w.w32(s + 0xC, mods); w.w32(s + 0x14, 0x378F);
      w.w16(s + 0x18, 0x64);
      for (uint32_t i = 0; i < script; i++) s[0x20 + i] = static_cast<uint8_t>(i * 5 + 1);
      uint8_t* m = s + 0x20 + script;
      w.w32(m, 0x464C4F52); w.w32(m + 4, mods);
      w.w32(m + 8, 0x15); w.w32(m + 12, 0x3C);
      for (int i = 0; i < 0x34; i++) m[16 + i] = static_cast<uint8_t>(0x40 + i);
      uint8_t* c = m + 8 + 0x3C;
      w.w32(c, 0x14); w.w32(c + 4, 0x8C); w.w16(c + 8, 0xAA55);
      for (int i = 0; i < 64; i++) w.w16(c + 12 + i * 2, static_cast<uint16_t>(i * 0x0101 + 1));
      break;
    }
    case 17:
      for (int i = 0; i < 0x400; i++) s[i] = static_cast<uint8_t>(i * 7);
      break;
    }
    off += l.stride;
  }
  EncryptSaveData(d, order);
  for (size_t i = kSlotHeaderSize + kDataSize; i < kSlotSize; i++) slot[i] = static_cast<uint8_t>(i * 31);
  return slot;
}

std::vector<uint8_t> makeFile(ByteOrder order) {
  const Writer w{order};
  std::vector<uint8_t> f(kFileSize, 0);
  std::memcpy(f.data(), "POKeMON XD", 10);
  for (size_t i = 0x40; i < 0x3A40; i++) f[i] = static_cast<uint8_t>(i * 11 + (i >> 8));
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) sum += Read32(f.data() + i * 4, order);
  w.w32(f.data() + 0x3A40, sum);
  std::vector<uint8_t> s0 = makeSlot(order, 121, "SVEN", 1), s1 = makeSlot(order, 120, "SVEN", 2);
  std::memcpy(f.data() + SlotOffset(0), s0.data(), kSlotSize);
  std::memcpy(f.data() + SlotOffset(1), s1.data(), kSlotSize);
  return f;
}

size_t firstDiff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  for (size_t i = 0; i < a.size() && i < b.size(); i++)
    if (a[i] != b[i]) return i;
  return a.size() == b.size() ? SIZE_MAX : std::min(a.size(), b.size());
}

std::string hex(size_t v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%zX", v);
  return buf;
}

std::vector<uint8_t> decrypted(const std::vector<uint8_t>& file, int s, ByteOrder order) {
  std::vector<uint8_t> d(file.begin() + SlotOffset(s) + kSlotHeaderSize, file.begin() + SlotOffset(s) + kSlotHeaderSize + kDataSize);
  if (!DecryptSaveData(d.data(), order)) d.clear();
  return d;
}

void testSynthetic() {
  const std::vector<uint8_t> gc = makeFile(ByteOrder::GameCube), pc = makeFile(ByteOrder::PC);
  {
    std::vector<uint8_t> work = gc;
    ConvertReport report;
    std::string error;
    check(ConvertFile(work, ByteOrder::PC, report, error), "GameCube file converts");
    check(report.slots[0] == SlotResult::Converted && report.slots[1] == SlotResult::Converted && report.bannerOk,
          "both slots and the banner converted");
    const size_t at = firstDiff(work, pc);
    check(at == SIZE_MAX, "GameCube -> PC gives the save the PC writer made, byte for byte" +
                              (at == SIZE_MAX ? std::string() : " (first difference at " + hex(at) + ")"));
    if (at != SIZE_MAX && at >= kBannerSize) {
      const int s = at >= SlotOffset(1) ? 1 : 0;
      const std::vector<uint8_t> a = decrypted(work, s, ByteOrder::PC), b = decrypted(pc, s, ByteOrder::PC);
      if (!a.empty() && !b.empty()) {
        const size_t d = firstDiff(a, b);
        std::printf("     decrypted slot %d data first differs at data offset %s\n", s, hex(d).c_str());
      }
    }
    ConvertFile(work, ByteOrder::GameCube, report, error);
    check(work == gc, "and back: the GameCube file again, byte for byte");
    std::vector<uint8_t> again = pc;
    ConvertFile(again, ByteOrder::GameCube, report, error);
    check(again == gc, "PC -> GameCube gives the GameCube writer's save");
    std::vector<uint8_t> twice = pc;
    ConvertFile(twice, ByteOrder::PC, report, error);
    check(twice == pc && report.slots[0] == SlotResult::AlreadyTarget, "converting to the order it has changes nothing");
  }
  {
    Save a, b;
    ReadReport ra, rb;
    std::string e;
    std::vector<uint8_t> work = gc;
    ConvertReport report;
    ConvertFile(work, ByteOrder::PC, report, e);
    check(ReadSave(gc, a, ra, e) && ReadSave(work, b, rb, e), "both read");
    check(b.order == ByteOrder::PC && b.money == a.money && b.playTimeSeconds == a.playTimeSeconds &&
              b.party[0].pid == a.party[0].pid && b.trainerName == a.trainerName && b.shadows[42].status == 4 &&
              b.boxes[2].slots[4].species == 150 && b.memcardId == a.memcardId,
          "the PC copy reads with the same contents");
  }
  {
    std::vector<uint8_t> work = gc;
    work[SlotOffset(1) + 0x9000] ^= 0x40;
    ConvertReport report;
    std::string e;
    ConvertFile(work, ByteOrder::PC, report, e);
    check(report.slots[1] == SlotResult::Invalid && Read32(work.data() + SlotOffset(1) + 4, ByteOrder::PC) == 120,
          "damaged slot: left damaged, its save counter now reads 120 in PC order");
  }
}

void testCardBoundary() {
  const std::vector<uint8_t> gcFile = makeFile(ByteOrder::GameCube), pcFile = makeFile(ByteOrder::PC);
  std::vector<uint8_t> stored = gcFile;
  const std::vector<uint8_t> view = GameViewOf(stored);
  check(view == pcFile, "the PC game reads a GameCube card as if the PC had written it");

  std::vector<uint8_t> newSlot = makeSlot(ByteOrder::PC, 122, "SVEN", 3);
  std::vector<std::pair<size_t, size_t>> changed;
  ApplyGameWrite(stored, SlotOffset(1), newSlot.data(), kSlotSize, changed);
  const std::vector<uint8_t> expect = makeSlot(ByteOrder::GameCube, 122, "SVEN", 3);
  check(changed.size() == 1 && changed[0].first == SlotOffset(1) && changed[0].second == kSlotSize,
        "a slot write stores that slot");
  check(std::memcmp(stored.data() + SlotOffset(1), expect.data(), kSlotSize) == 0,
        "... in GameCube format (what the GameCube would have written)");
  check(std::memcmp(stored.data(), gcFile.data(), SlotOffset(1)) == 0, "... and leaves the rest alone");

  std::vector<uint8_t> newer = makeSlot(ByteOrder::PC, 123, "SVEN", 4);
  ApplyGameWrite(stored, SlotOffset(0), newer.data(), 0x26000, changed);
  check(!SlotValid(stored.data() + SlotOffset(0), ByteOrder::GameCube) &&
            !SlotValid(stored.data() + SlotOffset(0), ByteOrder::PC),
        "first part only: the slot is unusable for now (as on the GameCube)");
  check(GameViewOf(stored)[SlotOffset(0) + 4] == 123, "... and the game reads back what it wrote (counter 123)");
  ApplyGameWrite(stored, SlotOffset(0) + 0x26000, newer.data() + 0x26000, 0x2000, changed);
  const std::vector<uint8_t> expect0 = makeSlot(ByteOrder::GameCube, 123, "SVEN", 4);
  check(std::memcmp(stored.data() + SlotOffset(0), expect0.data(), kSlotSize) == 0,
        "second part: the slot is complete, in GameCube format");

  std::vector<uint8_t> banner(pcFile.begin(), pcFile.begin() + kBannerSize);
  banner[0x20] = 'S';
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) sum += Read32(banner.data() + i * 4, ByteOrder::PC);
  for (int i = 0; i < 4; i++) banner[0x3A40 + i] = static_cast<uint8_t>(sum >> (8 * i));
  ApplyGameWrite(stored, 0, banner.data(), kBannerSize, changed);
  uint32_t gcSum = 0;
  for (size_t i = 0; i < 0xE90; i++) gcSum += Read32(stored.data() + i * 4, ByteOrder::GameCube);
  check(changed.size() == 1 && stored[0x20] == 'S' && gcSum == Read32(stored.data() + 0x3A40, ByteOrder::GameCube),
        "banner write: stored with a GameCube checksum");

  std::vector<uint8_t> fresh(kFileSize, 0);
  std::vector<uint8_t> header(0x2000, 0);
  header[0] = header[1] = header[2] = 1;
  ApplyGameWrite(fresh, SlotOffset(0), header.data(), header.size(), changed);
  ApplyGameWrite(fresh, SlotOffset(1), header.data(), header.size(), changed);
  check(std::memcmp(fresh.data() + SlotOffset(1), header.data(), header.size()) == 0 &&
            GameViewOf(fresh) == fresh,
        "new file: the empty slot headers are stored as written");

  std::vector<uint8_t> old = pcFile;
  check(GameViewOf(old) == pcFile, "an old PC-order save is passed to the game as it is");
}

void testReal(const char* path) {
  std::vector<uint8_t> bytes;
  std::vector<engine::memcard::CardFile> files;
  engine::memcard::CardImageInfo info;
  std::string error;
  if (!engine::memcard::ReadWholeFile(path, bytes) || !engine::memcard::LoadCardFiles(bytes, files, info, error)) {
    check(false, std::string("reading ") + path + " " + error);
    return;
  }
  for (const auto& f : files) {
    if (f.entry.fileName != "PokemonXD" || f.data.size() < kFileSize) continue;
    const std::vector<uint8_t> orig(f.data.begin(), f.data.begin() + kFileSize);
    std::vector<uint8_t> work = orig;
    ConvertReport report;
    ConvertFile(work, ByteOrder::PC, report, error);
    std::printf("     real save: slot 0 %s, slot 1 %s\n", SlotResultName(report.slots[0]), SlotResultName(report.slots[1]));
    for (const std::string& w : report.warnings) std::printf("     warning: %s\n", w.c_str());
    Save a, b;
    ReadReport ra, rb;
    check(ReadSave(orig, a, ra, error) && ReadSave(work, b, rb, error) && b.order == ByteOrder::PC,
          "real save: the PC copy reads, in PC order");
    check(a.money == b.money && a.playTimeSeconds == b.playTimeSeconds && a.trainerName == b.trainerName &&
              a.party[0].pid == b.party[0].pid && a.party[0].nickname == b.party[0].nickname,
          "real save: same trainer, money, play time, party");
    std::vector<uint8_t> back = work;
    ConvertFile(back, ByteOrder::GameCube, report, error);
    check(back == orig, "real save: back to GameCube gives the original bytes");
    check(GameViewOf(orig) == work, "real save: the card boundary gives the game the same PC copy");
    std::vector<std::pair<size_t, size_t>> changed;
    std::vector<uint8_t> stored = orig;
    for (int s = 0; s < kSlotCount; s++) {
      ApplyGameWrite(stored, SlotOffset(s), work.data() + SlotOffset(s), kSlotSize, changed);
    }
    ApplyGameWrite(stored, 0, work.data(), kBannerSize, changed);
    check(stored == orig, "real save: the game writing its PC copy back stores the original GameCube bytes");
    return;
  }
  check(false, std::string(path) + " holds no PokemonXD file");
}

}

int main(int argc, char** argv) {
  testSynthetic();
  testCardBoundary();
  if (argc > 1) testReal(argv[1]);
  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
