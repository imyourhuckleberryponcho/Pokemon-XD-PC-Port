#include <xd_save.h>

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
    if (order == ByteOrder::GameCube) {
      p[0] = static_cast<uint8_t>(v >> 8);
      p[1] = static_cast<uint8_t>(v);
    } else {
      p[0] = static_cast<uint8_t>(v);
      p[1] = static_cast<uint8_t>(v >> 8);
    }
  }
  void w32(uint8_t* p, uint32_t v) const {
    if (order == ByteOrder::GameCube) {
      w16(p, static_cast<uint16_t>(v >> 16));
      w16(p + 2, static_cast<uint16_t>(v));
    } else {
      w16(p, static_cast<uint16_t>(v));
      w16(p + 2, static_cast<uint16_t>(v >> 16));
    }
  }
  void w64(uint8_t* p, uint64_t v) const {
    if (order == ByteOrder::GameCube) {
      w32(p, static_cast<uint32_t>(v >> 32));
      w32(p + 4, static_cast<uint32_t>(v));
    } else {
      w32(p, static_cast<uint32_t>(v));
      w32(p + 4, static_cast<uint32_t>(v >> 32));
    }
  }
  void str(uint8_t* p, const char* s) const {
    for (; *s; s++, p += 2) {
      w16(p, static_cast<uint8_t>(*s));
    }
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

void makePokemon(const Writer& w, uint8_t* p, uint16_t species, uint8_t level, uint32_t pid, uint32_t otId,
                 const char* nick, uint16_t shadowId) {
  w.w16(p + 0x00, species);
  w.w16(p + 0x02, 13);
  w.w16(p + 0x04, 30);
  w.w16(p + 0x06, 70);
  w.w16(p + 0x08, 99);
  p[0x0E] = 10;
  p[0x0F] = 4;
  p[0x11] = level;
  p[0x1D] = 0x40;
  w.w32(p + 0x20, 1000 + level);
  w.w32(p + 0x24, otId);
  w.w32(p + 0x28, pid);
  w.str(p + 0x38, "WES");
  w.str(p + 0x4E, nick);
  w.str(p + 0x64, nick);
  for (int i = 0; i < 4; i++) {
    w.w16(p + 0x80 + i * 4, static_cast<uint16_t>(33 + i));
    p[0x82 + i * 4] = static_cast<uint8_t>(20 + i);
    p[0x83 + i * 4] = static_cast<uint8_t>(i);
  }
  for (int i = 0; i < 6; i++) {
    w.w16(p + 0x90 + i * 2, static_cast<uint16_t>(40 + i));
    w.w16(p + 0x9C + i * 2, static_cast<uint16_t>(i * 10));
    p[0xA8 + i] = static_cast<uint8_t>(31 - i);
  }
  w.w16(p + 0xBA, shadowId);
}

std::vector<uint8_t> makeSlot(ByteOrder order, int32_t count, const char* name, uint16_t keySeed) {
  const Writer w{order};
  std::vector<uint8_t> slot(kSlotSize, 0);
  slot[0] = slot[1] = slot[2] = 1;
  w.w32(slot.data() + 4, static_cast<uint32_t>(count));
  uint8_t* d = slot.data() + kSlotHeaderSize;
  for (int k = 0; k < 4; k++) {
    w.w16(d + k * 2, static_cast<uint16_t>(keySeed * 7919u + k * 104729u));
  }
  w.w16(d + 0x18, 0x88);
  uint8_t* g = d + kSectionBase;
  w.w64(g + 0x08, 0x0123456789ABCDEFull);
  w.w32(g + 0x10, 42);
  w.w32(g + 0x1C, 0x8A);
  const double play = 3 * 3600 + 25 * 60 + 7.5;
  uint64_t bits;
  std::memcpy(&bits, &play, 8);
  w.w64(g + 0x30, bits);
  uint32_t headSum = 0;
  for (int i = 0; i < 8; i++) {
    headSum += slot[i];
  }
  w.w32(g + 0x38, headSum);

  uint32_t off = 0xA0;
  for (const Layout& l : kOrder) {
    w.w16(d + l.sizeAt, static_cast<uint16_t>(l.size));
    w.w16(d + l.offsetAt, static_cast<uint16_t>(off));
    w.w16(d + l.offsetAt + 2, static_cast<uint16_t>(off >> 16));
    uint8_t* s = d + kSectionBase + off;
    switch (l.id) {
    case 2: {
      w.str(s, name);
      w.w32(s + 0x2C, 0xBEEF0000u | 12345u);
      makePokemon(w, s + 0x30, 1, 5, 0x12345678, 0xBEEF0000u | 12345u, "BULBY", 0);
      makePokemon(w, s + 0x30 + kPokemonSize, 25, 50, 0xBEEF3039u ^ 0x00010002u, 0xBEEF0000u | 12345u, "SPARKY",
                  7);
      w.w16(s + 0x4C8, 13);
      w.w16(s + 0x4CA, 4);
      w.w16(s + 0x5EC, 4);
      w.w16(s + 0x5EE, 10);
      s[0x8E0] = 0;
      w.w32(s + 0x8E4, 98765);
      w.w32(s + 0x8E8, 321);
      w.w32(s + 0x8EC, 4321);
      break;
    }
    case 3: {
      w.str(s + 2 * 0x170C, "BOX 3");
      makePokemon(w, s + 2 * 0x170C + 0x14 + 4 * kPokemonSize, 150, 70, 0xCAFEBABE, 0x00010002, "MEW2", 0);
      w.w16(s + 0xB860, 20);
      w.w16(s + 0xB862, 1);
      break;
    }
    case 11:
      makePokemon(w, s + 8, 133, 20, 0x1111, 0x2222, "EEVEE", 0);
      break;
    case 21:
      makePokemon(w, s + 0x3D8 * 2 + kPokemonSize * 4, 251, 30, 0x3333, 0x4444, "SHADOW", 9);
      break;
    case 15:
      s[7 * kShadowEntrySize] = static_cast<uint8_t>(3 << 5);
      w.w16(s + 7 * kShadowEntrySize + 0x1A, 25);
      w.w32(s + 7 * kShadowEntrySize + 0x24, 1500);
      s[9 * kShadowEntrySize] = static_cast<uint8_t>(4 << 5);
      w.w16(s + 9 * kShadowEntrySize + 0x1A, 251);
      break;
    }
    off += l.stride;
  }
  w.w16(d + 0x80, 0);
  EncryptSaveData(d, order);
  for (size_t i = kSlotHeaderSize + kDataSize; i < kSlotSize; i++) {
    slot[i] = static_cast<uint8_t>(i * 31);
  }
  return slot;
}

std::vector<uint8_t> makeFile(const std::vector<uint8_t>& s0, const std::vector<uint8_t>& s1) {
  std::vector<uint8_t> f(kFileSize, 0);
  std::memcpy(f.data() + SlotOffset(0), s0.data(), kSlotSize);
  std::memcpy(f.data() + SlotOffset(1), s1.data(), kSlotSize);
  return f;
}

void testOrder(ByteOrder order) {
  const std::string tag = std::string(" [") + ByteOrderName(order) + "]";

  {
    std::vector<uint8_t> plain(kDataSize);
    for (size_t i = 0; i < plain.size(); i++) {
      plain[i] = static_cast<uint8_t>(i * 13 + (i >> 9));
    }
    std::vector<uint8_t> work = plain;
    EncryptSaveData(work.data(), order);
    check(std::memcmp(work.data() + 0x18, plain.data() + 0x18, 0x1000) != 0, "encryption changes the data" + tag);
    check(std::memcmp(work.data(), plain.data(), 8) == 0, "the keys (words 0-3) stay as they are" + tag);
    check(DecryptSaveData(work.data(), order), "decrypting: checksums match" + tag);
    check(std::memcmp(work.data() + 0x18, plain.data() + 0x18, kDataSize - 0x18) == 0,
          "decrypt(encrypt(x)) == x (all but the checksum words)" + tag);
    uint32_t stored[4], sums[4];
    StoredChecksums(work.data(), order, stored);
    ComputeChecksums(plain.data(), order, sums);
    check(std::memcmp(stored, sums, sizeof(sums)) == 0, "the stored checksums are the data's sums" + tag);

    std::vector<uint8_t> bad = plain;
    EncryptSaveData(bad.data(), order);
    bad[0x20000] ^= 0x01;
    check(!DecryptSaveData(bad.data(), order), "one changed byte fails the checksums" + tag);
  }

  const std::vector<uint8_t> older = makeSlot(order, 5, "OLDNAME", 1);
  const std::vector<uint8_t> newer = makeSlot(order, 6, "MICHAEL", 2);
  Save save;
  ReadReport report;
  std::string error;
  const bool read = ReadSave(makeFile(older, newer), save, report, error);
  check(read, "a two-slot file reads " + error + tag);
  check(save.slot == 1 && save.saveCount == 6 && save.order == order, "the newer slot (counter 6) is used" + tag);
  check(report.slots[0].checksumOk && report.slots[0].headerSumOk && report.slots[1].headerSumOk,
        "both slots pass both checks" + tag);
  check(ToUtf8(save.trainerName) == "MICHAEL", "trainer name" + tag);
  check(save.TrainerId() == 12345 && save.SecretId() == 0xBEEF, "trainer ID and secret ID" + tag);
  check(save.money == 98765 && save.coupons == 321 && save.couponsTotal == 4321, "money and coupons" + tag);
  check(save.memcardId == 0x0123456789ABCDEFull && save.gameSaveCount == 42 && save.floorId == 0x8A,
        "game data: card serial, times saved, floor" + tag);
  check(static_cast<int>(save.playTimeSeconds) == 3 * 3600 + 25 * 60 + 7, "play time" + tag);
  const Pokemon& p0 = save.party[0];
  check(p0.species == 1 && p0.level == 5 && ToUtf8(p0.nickname) == "BULBY" && ToUtf8(p0.otName) == "WES",
        "party 1: species, level, nickname, OT" + tag);
  check(p0.pid == 0x12345678 && p0.Nature() == 0x12345678u % 25 && p0.heldItem == 13 && p0.exp == 1005,
        "party 1: PID, nature, item, exp" + tag);
  check(p0.moves[2].id == 35 && p0.moves[2].pp == 22 && p0.moves[2].ppUps == 2, "party 1: moves and PP" + tag);
  check(p0.ivs[0] == 31 && p0.ivs[5] == 26 && p0.evs[5] == 50 && p0.stats[1] == 41, "party 1: IVs, EVs, stats" + tag);
  check(p0.SecondAbility() && !p0.IsEgg() && !p0.IsShiny(), "party 1: ability flag, not egg, not shiny" + tag);
  check(save.ShadowOf(p0) == nullptr, "party 1: not a Shadow Pokemon" + tag);
  const Pokemon& p1 = save.party[1];
  check(p1.species == 25 && p1.IsShiny(), "party 2: shiny" + tag);
  const ShadowEntry* sh = save.ShadowOf(p1);
  check(sh != nullptr && sh->Snagged() && !sh->Purified() && sh->heartGauge == 1500 && sh->species == 25,
        "party 2: Shadow, snagged, heart gauge 1500" + tag);
  check(save.party[2].IsEmpty(), "party 3 is empty" + tag);
  const Pokemon& boxed = save.boxes[2].slots[4];
  check(ToUtf8(save.boxes[2].name) == "BOX 3" && boxed.species == 150 && boxed.level == 70,
        "box 3 slot 5" + tag);
  check(save.boxes[0].slots[0].IsEmpty(), "box 1 slot 1 is empty" + tag);
  check(!save.pcItems.empty() && save.pcItems[0].item == 20 && save.pcItems[0].count == 1, "PC item" + tag);
  check(!save.items.empty() && save.items[0].item == 13 && save.items[0].count == 4 && save.balls[0].count == 10,
        "bag pockets" + tag);
  check(save.dayCare.species == 133 && ToUtf8(save.dayCare.nickname) == "EEVEE", "day care" + tag);
  const Pokemon& ch = save.chambers[2][4];
  check(ch.species == 251 && save.ShadowOf(ch) != nullptr && save.ShadowOf(ch)->Purified(),
        "Purification Chamber 3, Shadow slot; its Shadow entry says purified" + tag);
  check(save.sections.size() == 16, "16 sections found" + tag);

  std::vector<uint8_t> damaged = newer;
  damaged[0x10000] ^= 0x80;
  check(ReadSave(makeFile(older, damaged), save, report, error) && save.slot == 0 &&
            ToUtf8(save.trainerName) == "OLDNAME" && !report.warnings.empty(),
        "a damaged newer slot is skipped: the older one is read" + tag);
  std::vector<uint8_t> damaged0 = older;
  damaged0[0x10000] ^= 0x80;
  const bool readBoth = ReadSave(makeFile(damaged0, damaged), save, report, error);
  check(!readBoth, "both damaged: refused (" + error + ")" + tag);
  std::vector<uint8_t> headerChanged = newer;
  headerChanged[3] = 1;
  check(ReadSave(makeFile(older, headerChanged), save, report, error) && save.slot == 0 &&
            report.slots[1].checksumOk && !report.slots[1].headerSumOk,
        "a slot whose header no longer matches its game data is skipped" + tag);
}

}

int main() {
  {
    uint16_t k[4] = {0, 0, 0, 0};
    KeyStep(k);
    check(k[0] == 0x3793 && k[1] == 0x1124 && k[2] == 0x0000 && k[3] == 0x0000, "key step from zero keys");
  }
  testOrder(ByteOrder::GameCube);
  testOrder(ByteOrder::PC);

  {
    std::vector<uint8_t> f(kFileSize, 0);
    f[SlotOffset(0)] = f[SlotOffset(0) + 1] = f[SlotOffset(0) + 2] = 1;
    f[SlotOffset(1)] = f[SlotOffset(1) + 1] = f[SlotOffset(1) + 2] = 1;
    Save save;
    ReadReport report;
    std::string error;
    const bool read = ReadSave(f, save, report, error);
    check(!read && error.find("no saved game") != std::string::npos, "an empty save file is reported as empty (" + error + ")");
  }
  {
    Save save;
    ReadReport report;
    std::string error;
    check(!ReadSave(std::vector<uint8_t>(1234), save, report, error), "a wrong size is refused");
  }
  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
