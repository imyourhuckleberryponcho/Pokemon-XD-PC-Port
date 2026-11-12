#include <gen3_save.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace engine::gen3;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void Put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void Put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

PlainMon MakeMon(uint16_t species, uint32_t pid, uint32_t ot) {
  PlainMon m;
  m.Set32(0x00, pid);
  m.Set32(0x04, ot);
  for (int i = 0; i < 10; i++) {
    m.b[0x08 + i] = static_cast<uint8_t>(0xBB + i);
  }
  m.b[0x12] = 2;
  m.b[0x13] = 2;
  m.Set16(0x20, species);
  m.Set16(0x22, 13);
  m.Set32(0x24, 12345);
  m.b[0x28] = 0x1B;
  m.b[0x29] = 70;
  for (int i = 0; i < 4; i++) {
    m.Set16(0x2C + i * 2, static_cast<uint16_t>(10 + i));
    m.b[0x34 + i] = static_cast<uint8_t>(20 + i);
  }
  for (int i = 0; i < 12; i++) {
    m.b[0x38 + i] = static_cast<uint8_t>(i * 3);
  }
  m.Set32(0x44, 0x12345678);
  m.Set32(0x48, 0x3FFFFFFF);
  m.Set32(0x4C, 0x80000001);
  m.b[0x54] = 42;
  m.Set16(0x56, 100);
  m.Set16(0x58, 120);
  return m;
}

struct Content {
  std::array<std::array<uint8_t, kSectorDataSize>, kSectorsPerSlot> sec{};
};

Content MakeContent(Game game, int partyCount, uint32_t hint) {
  Content c;
  const Layout& l = *LayoutFor(game);
  auto sb1 = [&](uint32_t off) { return c.sec[1 + off / kSectorDataSize].data() + off % kSectorDataSize; };
  auto storage = [&](uint32_t off) { return c.sec[5 + off / kSectorDataSize].data() + off % kSectorDataSize; };
  const uint8_t name[8] = {0xC7, 0xBF, 0xC8, 0xC8, 0xD3, 0xFF, 0, 0};
  std::memcpy(c.sec[0].data(), name, 8);
  c.sec[0][0x08] = 1;
  Put32(c.sec[0].data() + 0x0A, 0x22220011u);
  Put32(c.sec[0].data() + 0xAC, hint);
  c.sec[0][0x5C] = 0x05;
  c.sec[0][0x28] = 0x01;
  sb1(l.seen1)[0] = 0x05;
  sb1(l.seen2)[0] = 0x05;
  *sb1(l.partyCount) = static_cast<uint8_t>(partyCount);
  for (int i = 0; i < partyCount; i++) {
    Encrypt(MakeMon(static_cast<uint16_t>(1 + i), 0x1000u * (i + 1) + 7u * i, 0x22220011u), sb1(l.party + i * kPartyMonSize),
            kPartyMonSize);
  }
  sb1(l.giftRibbons)[3] = 9;
  {
    uint8_t rec[kBoxMonSize];
    Encrypt(MakeMon(200, 0xABCDEF01, 0x22220011u), rec, kBoxMonSize);
    const uint32_t at = 4 + (2 * kBoxSlots + 4) * kBoxMonSize;
    for (uint32_t i = 0; i < kBoxMonSize; i++) {
      *storage(at + i) = rec[i];
    }
    const int crossing = (0xF80 - 4) / kBoxMonSize;
    const uint32_t at2 = 4 + crossing * kBoxMonSize;
    Encrypt(MakeMon(201, 0x0BADF00D, 0x22220011u), rec, kBoxMonSize);
    for (uint32_t i = 0; i < kBoxMonSize; i++) {
      *storage(at2 + i) = rec[i];
    }
  }
  return c;
}

void WriteSlot(std::vector<uint8_t>& file, const Content& c, int slot, int first, uint32_t counter, Game game) {
  (void)game;
  for (int id = 0; id < kSectorsPerSlot; id++) {
    uint8_t* s = file.data() + (static_cast<size_t>(slot) * kSectorsPerSlot + (id + first) % kSectorsPerSlot) * kSectorSize;
    std::memset(s, 0, kSectorSize);
    std::memcpy(s, c.sec[id].data(), kSectorDataSize);
    Put16(s + 0xFF4, static_cast<uint16_t>(id));
    Put16(s + 0xFF6, SectionChecksum(s, kSectorDataSize));
    Put32(s + 0xFF8, kSectorSignature);
    Put32(s + 0xFFC, counter);
  }
}

void TestRecords() {
  bool allOrders = true;
  for (uint32_t k = 0; k < 24; k++) {
    const PlainMon m = MakeMon(25, 0x01000000u * 3 + k, 0xDEADBEEF);
    uint8_t stored[kPartyMonSize];
    Encrypt(m, stored, kPartyMonSize);
    const PlainMon back = Decrypt(stored, kPartyMonSize);
    PlainMon expect = m;
    expect.Set16(0x1C, DataChecksum(m));
    allOrders &= back.checksumOk && back.b == expect.b;
    allOrders &= std::memcmp(stored + 0x20, expect.b.data() + 0x20, 48) != 0;
  }
  check(allOrders, "records: encrypt -> decrypt for all 24 substructure orders");
  {
    const PlainMon m = MakeMon(25, 0x11111111, 0x22222222);
    uint8_t stored[kPartyMonSize];
    Encrypt(m, stored, kPartyMonSize);
    stored[0x30] ^= 0x40;
    check(!Decrypt(stored, kPartyMonSize).checksumOk, "records: a changed byte fails the record checksum");
    uint8_t zero[kPartyMonSize] = {};
    const PlainMon z = Decrypt(zero, kPartyMonSize);
    check(z.checksumOk && z.IsEmpty(), "records: an all-zero slot is empty and fine");
  }
  {
    PlainMon m = MakeMon(1, 5, 5);
    uint8_t stored[kPartyMonSize];
    Encrypt(m, stored, kPartyMonSize);
    check(stored[0x20] == 1 && stored[0x20 + 12] == 0x78 && stored[0x20 + 36] == 10,
          "records: order 5 is Growth, Misc, EVs, Attacks");
    PlainMon m8 = MakeMon(1, 8, 8);
    Encrypt(m8, stored, kPartyMonSize);
    check(stored[0x20] == 10 && stored[0x20 + 12] == 0 && stored[0x20 + 24] == 1 && stored[0x20 + 36] == 0x78,
          "records: order 8 is Attacks, EVs, Growth, Misc");
  }
  check(IsShiny(0x00010002, 0x00030000) && !IsShiny(0x12345678, 0x0), "shininess");
}

void TestGame(Game game, uint32_t hint) {
  const std::string tag = std::string(" [") + GameName(game) + "]";
  std::vector<uint8_t> file(kFlashSize, 0xFF);
  const Content older = MakeContent(game, 2, hint);
  const Content newer = MakeContent(game, 3, hint);
  WriteSlot(file, older, 0, 5, 10, game);
  WriteSlot(file, newer, 1, 6, 11, game);
  Save s;
  std::string error;
  std::vector<std::string> warnings;
  check(ReadSave(file, Game::Unknown, s, error, warnings), "reads" + tag + (error.empty() ? "" : ": " + error));
  check(s.game == game, std::string("layout found: ") + GameName(s.game) + tag);
  check(s.slot == 1 && s.counter == 11 && s.firstSector == 6, "newer slot B chosen, rotation 6" + tag);
  check(s.PartyCount() == 3, "party count 3" + tag);
  const PlainMon p2 = s.PartyMon(2);
  check(p2.checksumOk && p2.Species() == 3 && p2.Level() == 42, "party #3 decrypts" + tag);
  check(s.TrainerOtId() == 0x22220011u && s.TrainerGender() == 1, "trainer" + tag);
  check(s.BoxMon(2, 4).Species() == 200 && s.BoxMon(2, 4).checksumOk, "box record" + tag);
  check(s.BoxMon(0, (0xF80 - 4) / kBoxMonSize).Species() == 201, "box record across a section end" + tag);
  check(s.GiftRibbons()[3] == 9, "gift ribbons" + tag);
  check(s.DexSeen(1) && s.DexSeen(3) && !s.DexSeen(2) && s.DexOwned(1) && !s.DexOwned(3), "Pokedex flags" + tag);

  PlainMon m = MakeMon(386, 0x77777777, 0x12345678);
  s.SetPartyMon(1, m);
  s.RegisterInDex(kNationalSpinda, 0xCAFED00D);
  std::vector<uint8_t> out = WriteSave(s, error);
  check(!out.empty(), "writes" + tag);
  check(std::memcmp(out.data() + kSectorsPerSlot * kSectorSize, file.data() + kSectorsPerSlot * kSectorSize,
                    kSectorsPerSlot * kSectorSize) == 0,
        "slot B (the one read) is left as it was" + tag);
  Save r;
  check(ReadSave(out, Game::Unknown, r, error, warnings), "re-reads" + tag);
  check(r.slot == 0 && r.counter == 12 && r.firstSector == 7, "written to slot A, counter 12, rotation 7" + tag);
  const PlainMon back = r.PartyMon(1);
  check(back.checksumOk && back.Species() == 386 && back.Pid() == 0x77777777, "the changed record is there" + tag);
  check(r.PartyMon(0).Species() == 1 && r.PartyMon(2).Species() == 3, "the others are unchanged" + tag);
  check(r.DexSeen(kNationalSpinda) && r.DexOwned(kNationalSpinda), "Pokedex registered" + tag);
  uint32_t sp = 0;
  std::memcpy(&sp, r.Sb2(0x20), 4);
  check(sp == 0xCAFED00D, "Spinda's personality stored" + tag);
  check(std::memcmp(r.Sb2(0x5C), r.Sb1(r.L().seen1), kDexFlagBytes) == 0 &&
            std::memcmp(r.Sb2(0x5C), r.Sb1(r.L().seen2), kDexFlagBytes) == 0,
        "all three seen copies agree" + tag);
  check(std::memcmp(out.data() + 0x1C000, file.data() + 0x1C000, kFlashSize - 0x1C000) == 0,
        "Hall of Fame etc. untouched" + tag);

  std::vector<uint8_t> bad = file;
  bad[kSectorsPerSlot * kSectorSize + 6 * kSectorSize + 0x100] ^= 1;
  Save b;
  check(!ReadSave(bad, Game::Unknown, b, error, warnings), "a damaged newest slot is refused" + tag);
}

void TestPicking() {
  std::vector<uint8_t> file(kFlashSize, 0xFF);
  const Content a = MakeContent(Game::Emerald, 1, 0x12345678);
  const Content b = MakeContent(Game::Emerald, 2, 0x12345678);
  WriteSlot(file, a, 1, 0, 0xFFFFFFFFu, Game::Emerald);
  WriteSlot(file, b, 0, 1, 0, Game::Emerald);
  Save s;
  std::string error;
  std::vector<std::string> warnings;
  check(ReadSave(file, Game::Unknown, s, error, warnings) && s.slot == 0 && s.PartyCount() == 2,
        "counter wrap: 0 is newer than 0xFFFFFFFF");
  std::vector<uint8_t> blank(kFlashSize, 0xFF);
  check(!ReadSave(blank, Game::Unknown, s, error, warnings), "a blank file is refused: " + error);
  std::vector<uint8_t> small(1000, 0);
  check(!ReadSave(small, Game::Unknown, s, error, warnings), "a wrong-size file is refused: " + error);
  std::vector<uint8_t> once(kFlashSize, 0xFF);
  WriteSlot(once, a, 1, 0, 1, Game::Emerald);
  check(ReadSave(once, Game::Unknown, s, error, warnings) && s.slot == 1, "a save written once (other slot blank)");
  std::vector<uint8_t> clock = once;
  clock.resize(kFlashSize + 16, 0x42);
  check(ReadSave(clock, Game::Unknown, s, error, warnings), "16 extra bytes (clock) are accepted");
  std::vector<uint8_t> out = WriteSave(s, error);
  check(out.size() == clock.size() && out[kFlashSize + 5] == 0x42, "...and kept when writing");
  check(!ReadSave(once, Game::FireRedLeafGreen, s, error, warnings), "forcing FireRed on an Emerald save is refused");
}

}

int main() {
  TestRecords();
  TestGame(Game::RubySapphire, 0);
  TestGame(Game::Emerald, 0x9ABCDEF0);
  TestGame(Game::FireRedLeafGreen, 1);
  {
    std::vector<uint8_t> file(kFlashSize, 0xFF);
    WriteSlot(file, MakeContent(Game::RubySapphire, 2, 0x55555555), 1, 0, 1, Game::RubySapphire);
    Save s;
    std::string error;
    std::vector<std::string> warnings;
    check(ReadSave(file, Game::Unknown, s, error, warnings) && s.game == Game::RubySapphire,
          "Ruby/Sapphire found by its Pokedex copies despite an Emerald-like hint");
  }
  TestPicking();
  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
