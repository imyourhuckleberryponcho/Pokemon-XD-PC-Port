#include <xd_gba.h>
#include <xd_save_write.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace xd::gba;
using xd::save::ByteOrder;
namespace g3 = engine::gen3;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

class FakeRules : public GameRules {
public:
  FakeRules() {
    for (int i = 0; i < 256; i++) {
      west[i] = static_cast<uint16_t>(0xE000 + i);
      jp[i] = static_cast<uint16_t>(0xE100 + i);
    }
    for (int i = 0; i < 26; i++) {
      west[0xBB + i] = static_cast<uint16_t>('A' + i);
      west[0xD5 + i] = static_cast<uint16_t>('a' + i);
    }
    west[0] = ' ';
    jp[0x50] = 0x3042;
  }
  bool SpeciesValid(uint16_t s) const override { return s >= 1 && s <= 411 && !(s >= 252 && s <= 276); }
  uint8_t SecondAbility(uint16_t s) const override { return s % 2 == 0 ? 7 : 0; }
  uint8_t GrowthGroup(uint16_t) const override { return 0; }
  std::array<uint16_t, 6> BaseStats(uint16_t) const override { return {50, 60, 70, 80, 90, 100}; }
  uint16_t NationalDex(uint16_t s) const override { return s <= 251 ? s : static_cast<uint16_t>(s - 25); }
  uint16_t TradeEvolution(uint16_t s, uint16_t item) const override {
    if (s == 64) return 65;
    if (s == 95 && item == 199) return 208;
    return 0;
  }
  bool GrowthKnown(uint8_t g) const override { return g == 0; }
  uint32_t ExpForLevel(uint8_t g, uint8_t l) const override { return g == 0 && l <= 100 ? (l <= 1 ? 0 : l * l * l) : 0; }
  bool NatureRate(uint8_t nature, int stat, uint8_t& mul, uint8_t& div) const override {
    mul = 10;
    div = 10;
    if (nature == 1 && stat == 1) mul = 11;
    if (nature == 1 && stat == 2) mul = 9;
    return true;
  }
  uint8_t MovePP(uint16_t m) const override { return m == 0 || m >= 400 ? 0 : m == 1 ? 3 : 20; }
  bool ItemKnown(uint16_t i) const override { return i >= 1 && i <= 500; }
  bool ItemImportant(uint16_t i) const override { return i == 300; }
  bool ItemHasName(uint16_t i) const override { return i != 400; }
  uint16_t ItemHoldEffect(uint16_t i) const override { return i == 229 ? 0x26 : 0; }
  const std::array<uint16_t, 256>& GbaCharsJapanese() const override { return jp; }
  const std::array<uint16_t, 256>& GbaCharsWestern() const override { return west; }
  std::array<uint16_t, 256> west{}, jp{};
};
const FakeRules kRules;

void GameRotate(uint8_t* data, uint32_t order) {
  uint8_t tmp[12];
  uint32_t n;
  order %= 24;
  n = order / 6;
  if (n != 0) {
    n *= 12;
    std::memcpy(tmp, data + n, 12);
    std::memmove(data + 12, data, n);
    std::memcpy(data, tmp, 12);
  }
  order %= 6;
  n = order >> 1;
  if (n != 0) {
    n *= 12;
    std::memcpy(tmp, data + 12 + n, 12);
    std::memmove(data + 24, data + 12, n);
    std::memcpy(data + 12, tmp, 12);
  }
  if (order & 1) {
    std::memcpy(tmp, data + 36, 12);
    std::memcpy(data + 36, data + 24, 12);
    std::memcpy(data + 24, tmp, 12);
  }
}

void TestShuffle() {
  bool same = true;
  for (uint32_t k = 0; k < 24; k++) {
    uint8_t data[48];
    for (int i = 0; i < 48; i++) {
      data[i] = static_cast<uint8_t>(i / 12);
    }
    GameRotate(data, k);
    for (int pos = 0; pos < 4; pos++) {
      same &= data[pos * 12] == g3::kSubstructOrder[k][pos];
    }
  }
  check(same, "the GameCube's _rotatepp gives the GBA's substructure orders (all 24)");
}

void Str(XdMon& m, size_t at, const char* s) {
  for (size_t i = 0; i < 11; i++) {
    m.SetU16(at + i * 2, *s ? static_cast<uint16_t>(*s++) : 0);
  }
}

XdMon MakeXd(ByteOrder order, uint16_t species, uint32_t pid, uint16_t shadowId = 0) {
  XdMon m = EmptyMon(order);
  m.SetU16(0x00, species);
  m.SetU16(0x02, 13);
  m.SetU16(0x06, 120);
  m.SetU16(0x08, 0x1234);
  m.SetU16(0x0A, 0xA1A2);
  m.SetU16(0x0C, 0xB1B2);
  m.SetU8(0x0E, 15);
  m.SetU8(0x0F, 4);
  m.SetU8(0x10, 1);
  m.SetU8(0x12, 9);
  m.SetU8(0x13, 0x21);
  m.SetU8(0x14, 5);
  m.SetU8(0x1A, 0x13);
  m.SetU8(0x1D, 0x40);
  m.SetU32(0x20, 27000);
  m.SetU32(0x24, 0x7FE77599);
  m.SetU32(0x28, pid);
  m.SetU32(0x30, 1);
  m.SetU8(0x34, 11);
  m.SetU8(0x35, 3);
  m.SetU8(0x36, 2);
  m.SetU8(0x37, 2);
  Str(m, 0x38, "Sven");
  Str(m, 0x4E, "Nick");
  Str(m, 0x64, "Orig");
  m.SetU32(0x7C, 0x80400000u | 0x5u << 12);
  for (int i = 0; i < 4; i++) {
    m.SetU16(0x80 + i * 4, static_cast<uint16_t>(10 + i));
    m.SetU8(0x82 + i * 4, static_cast<uint8_t>(15 + i));
    m.SetU8(0x83 + i * 4, static_cast<uint8_t>(i));
  }
  for (int i = 0; i < 6; i++) {
    m.SetU16(0x9C + i * 2, static_cast<uint16_t>(10 * (i + 1)));
    m.SetU8(0xA8 + i, static_cast<uint8_t>(5 * i + 1));
  }
  for (int i = 0; i < 5; i++) {
    m.SetU8(0xAE + i, static_cast<uint8_t>(20 + i));
    m.SetU8(0xB3 + i, static_cast<uint8_t>(i % 4));
  }
  m.SetU16(0xBA, shadowId);
  m.SetU16(0x04, 1);
  ResetStats(m, kRules);
  m.SetU16(0x04, 20);
  return m;
}

void TestConvert(ByteOrder order) {
  const std::string tag = std::string(" [") + xd::save::ByteOrderName(order) + "]";
  XdMon a = MakeXd(order, 152, 0x12345679);
  check(a.Level() == 30, "fake curve: level 30 from experience" + tag);
  const g3::PlainMon g = ToGba(a, kRules);
  uint8_t stored[100];
  g3::Encrypt(g, stored, 100);
  const g3::PlainMon g2 = g3::Decrypt(stored, 100);
  check(g2.checksumOk && g2.Species() == 152 && g2.Pid() == 0x12345679 && g2.OtId() == 0x7FE77599,
        "XD -> GBA: species, PID, OT" + tag);
  check(g2.b[0x08] == 0xBB + ('O' - 'A') && g2.b[0x08 + 4] == 0xFF && g2.b[0x08 + 5] == 0,
        "XD -> GBA: the ORIGINAL name, end mark, then zeros" + tag);
  check(g2.b[0x12] == 2 && (g2.Get32(0x44) >> 23 & 0xF) == 15, "XD -> GBA: English, game of origin 15 (Colosseum/XD)" + tag);
  check((g2.Get32(0x44) >> 8 & 0xFF) == 0x34, "XD -> GBA: met location cut to 8 bits" + tag);
  check(g2.b[0x2A] == 0xB1 && g2.b[0x2B] == 0xB2, "XD -> GBA: the spare word stays big-endian (game quirk)" + tag);
  check((g2.Ivs() >> 31) == 1, "XD -> GBA: second ability kept (species has one)" + tag);
  check(g2.Get32(0x4C) >> 31 == 1 && (g2.Get32(0x4C) >> 15 & 1) && (g2.Get32(0x4C) >> 24 & 1) &&
            (g2.Get32(0x4C) >> 27 & 0xF) == 5,
        "XD -> GBA: ribbons (event, champion, national, spare) moved to the GBA's bits" + tag);
  check(g2.b[0x38 + 3] == 60 && g2.b[0x38 + 4] == 40, "XD -> GBA: EV order (Speed is 4th on the GBA)" + tag);

  XdMon b = FromGba(g2, kRules, order);
  bool same = true;
  for (size_t at : {0x00, 0x02, 0x04, 0x0A, 0x0C, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x24, 0x28, 0x30}) {
    same &= a.raw[at] == b.raw[at] && a.raw[at + 1] == b.raw[at + 1];
  }
  check(same, "GBA -> XD: the plain fields come back" + tag);
  check(b.U16(0x08) == 0x34, "GBA -> XD: met location as the GBA has it" + tag);
  check(b.U8(0x1A) == 0x13 && b.U8(0x1D) == 0x40, "GBA -> XD: spare flag bits and ability bit" + tag);
  check(b.Text(0x4E) == u"Orig" && b.Text(0x64) == u"Orig" && b.Text(0x38) == u"Sven", "GBA -> XD: names" + tag);
  check(std::memcmp(a.raw.data() + 0x7C, b.raw.data() + 0x7C, 4) == 0, "GBA -> XD: ribbons" + tag);
  check(std::memcmp(a.raw.data() + 0x80, b.raw.data() + 0x80, 16) == 0, "GBA -> XD: moves, PP, PP Ups" + tag);
  check(std::memcmp(a.raw.data() + 0x90, b.raw.data() + 0x90, 0x28) == 0, "GBA -> XD: stats, EVs, IVs, contest" + tag);
  check(b.U8(0x34) == 11 && b.U8(0x35) == 3 && b.U8(0x36) == 2 && b.U8(0x37) == 2, "GBA -> XD: origin record" + tag);

  g3::PlainMon c = g2;
  c.Set16(0x20, 151);
  c.Set16(0x2C, 1);
  c.b[0x34] = 9;
  c.b[0x54] = 120;
  c.Set16(0x56, 999);
  c.Set32(0x50, 0x00000304);
  const XdMon d = FromGba(c, kRules, order);
  check(!d.AbilityBit(), "GBA -> XD: ability bit dropped for a species without a second ability" + tag);
  check(d.U8(0x83) == 0 && d.U8(0x82) == 3, "GBA -> XD: PP Ups ignored for a <=4 PP move, PP capped at its max" + tag);
  check(d.Level() == 100 && d.Hp() == d.U16(0x90), "GBA -> XD: level capped at 100, HP at max HP" + tag);
  check(d.U8(0x16) == 8 && d.U8(0x18) == 4, "GBA -> XD: sleep with its turns" + tag);
  g3::PlainMon e = g2;
  e.Set32(0x50, 0x00000580);
  const XdMon f = FromGba(e, kRules, order);
  check(f.U8(0x16) == 4 && f.U8(0x17) == 5, "GBA -> XD: bad poison with its counter" + tag);
  check(ToGba(f, kRules).Get32(0x50) == 0x0580, "XD -> GBA: bad poison back" + tag);
  g3::PlainMon j = g2;
  j.b[0x12] = 1;
  for (int i = 0; i < 10; i++) {
    j.b[0x08 + i] = 0x50;
  }
  const XdMon jm = FromGba(j, kRules, order);
  check(jm.Text(0x4E).size() == 5 && jm.Text(0x64).size() == 10 && jm.Text(0x4E)[0] == 0x3042,
        "GBA -> XD: Japanese table; nickname cut to 5 (game quirk), original name kept" + tag);
  check(FromGba(g3::PlainMon{}, kRules, order).raw == EmptyMon(order).raw, "GBA -> XD: an empty slot stays empty" + tag);
}

void TestStates() {
  const ByteOrder o = ByteOrder::GameCube;
  XdMon m = MakeXd(o, 152, 0x1000);
  check(TradeState(m, Side::Xd, -1, kRules) == State::Tradeable, "state: a normal Pokemon can be traded");
  XdMon egg = m;
  egg.SetU8(0x1D, 0x80);
  check(TradeState(egg, Side::Xd, -1, kRules) == State::Egg, "state: egg");
  XdMon bad = m;
  bad.SetU8(0x1D, 0x20);
  check(TradeState(bad, Side::Xd, -1, kRules) == State::BadEgg, "state: bad egg");
  XdMon sh = MakeXd(o, 152, 0x1000, 9);
  check(TradeState(sh, Side::Xd, 3, kRules) == State::Shadow, "state: Shadow (snagged, not purified)");
  check(TradeState(sh, Side::Xd, 4, kRules) == State::Tradeable, "state: purified Shadow Pokemon can go");
  XdMon mew = MakeXd(o, 151, 0x1000);
  mew.SetU32(0x30, 0);
  check(TradeState(mew, Side::Xd, -1, kRules) == State::NoEventFlag, "state: Mew without its event flag");
  XdMon evs = m;
  for (int i = 0; i < 6; i++) evs.SetU16(0x9C + i * 2, 100);
  check(TradeState(evs, Side::Xd, -1, kRules) == State::Impossible, "state: 600 EVs in all is impossible");
  XdMon stat = m;
  stat.SetU16(0x92, static_cast<uint16_t>(stat.U16(0x92) + 1));
  check(TradeState(stat, Side::Xd, -1, kRules) == State::Impossible, "state: Attack 1 above its maximum");
  XdMon key = m;
  key.SetU16(0x02, 300);
  check(TradeState(key, Side::Xd, -1, kRules) == State::ItemNotAllowed && TradeState(key, Side::Gba, -1, kRules) == State::Tradeable,
        "state: a key item can't leave XD");
  XdMon noname = m;
  noname.SetU16(0x02, 400);
  check(TradeState(noname, Side::Gba, -1, kRules) == State::ItemNotAllowed, "state: an item XD has no name for can't come in");
  XdMon nature = MakeXd(o, 152, 1);
  check(TradeState(nature, Side::Xd, -1, kRules) == State::Tradeable, "state: nature-adjusted stats are accepted");
}

struct SecLayout {
  int id;
  uint32_t sizeAt, offsetAt, size, stride;
};
const SecLayout kSecs[] = {
    {2, 0x1A, 0x3C, 0x978, 0x990},    {3, 0x1C, 0x40, 0xBC50, 0xBC60},  {10, 0x1E, 0x44, 0x4A, 0x60},
    {11, 0x20, 0x48, 0xCC, 0xE0},     {12, 0x22, 0x4C, 0x1774, 0x1790}, {14, 0x24, 0x50, 0x364, 0x380},
    {15, 0x26, 0x54, 0x2400, 0x2410}, {23, 0x28, 0x58, 0x6B4, 0x6D0},   {16, 0x2A, 0x5C, 0xB, 0x20},
    {17, 0x2C, 0x60, 0x400, 0x410},   {18, 0x2E, 0x64, 0x800, 0x810},   {19, 0x30, 0x68, 0x1000, 0x1010},
    {20, 0x32, 0x6C, 0x2000, 0x2010}, {21, 0x34, 0x70, 0x2298, 0x22B0}, {22, 0x36, 0x74, 0xC800, 0xC810},
};

std::vector<uint8_t> MakeXdFile(ByteOrder order) {
  std::vector<uint8_t> file(xd::save::kFileSize, 0);
  std::vector<uint8_t> slot(xd::save::kSlotSize, 0);
  slot[0] = slot[1] = slot[2] = 1;
  xd::save::Write32(slot.data() + 4, 7, order);
  uint8_t* d = slot.data() + 8;
  for (int k = 0; k < 4; k++) xd::save::Write16(d + k * 2, static_cast<uint16_t>(0x1234 * (k + 3)), order);
  xd::save::Write16(d + 0x18, 0x88, order);
  uint8_t* g = d + 0xA0;
  xd::save::Write32(g + 0x10, 41, order);
  uint32_t off = 0xA0;
  for (const SecLayout& l : kSecs) {
    xd::save::Write16(d + l.sizeAt, static_cast<uint16_t>(l.size), order);
    xd::save::Write16(d + l.offsetAt, static_cast<uint16_t>(off), order);
    xd::save::Write16(d + l.offsetAt + 2, static_cast<uint16_t>(off >> 16), order);
    uint8_t* s = d + 0xA0 + off;
    if (l.id == 2) {
      const XdMon mons[3] = {MakeXd(order, 152, 0x111, 9), MakeXd(order, 155, 0x222), MakeXd(order, 158, 0x333)};
      for (int i = 0; i < 3; i++) std::memcpy(s + 0x30 + i * 0xC4, mons[i].raw.data(), 0xC4);
      for (int i = 3; i < 6; i++) std::memcpy(s + 0x30 + i * 0xC4, EmptyMon(order).raw.data(), 0xC4);
    }
    if (l.id == 15) {
      s[9 * 0x48] = static_cast<uint8_t>(4 << 5);
    }
    if (l.id == 12) {
      xd::save::Write16(s, 1, order);
      xd::save::Write16(s + 4, 152, order);
    }
    if (l.id == 16) {
      s[2] = 7;
    }
    off += l.stride;
  }
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += slot[i];
  xd::save::Write32(g + 0x38, sum, order);
  xd::save::EncryptSaveData(d, order);
  std::memcpy(file.data() + xd::save::SlotOffset(0), slot.data(), slot.size());
  return file;
}

std::vector<uint8_t> MakeGbaFile() {
  std::vector<uint8_t> file(g3::kFlashSize, 0xFF);
  std::array<std::array<uint8_t, g3::kSectorDataSize>, g3::kSectorsPerSlot> sec{};
  const g3::Layout& l = *g3::LayoutFor(g3::Game::Emerald);
  auto sb1 = [&](uint32_t o) { return sec[1 + o / g3::kSectorDataSize].data() + o % g3::kSectorDataSize; };
  sec[0][0xAC] = 0x77;
  *sb1(l.partyCount) = 3;
  const uint16_t species[3] = {280, 64, 25};
  for (int i = 0; i < 3; i++) {
    XdMon x = MakeXd(ByteOrder::GameCube, species[i], 0x9000u + i);
    x.SetU8(0x34, 10);
    x.SetU32(0x30, 0);
    g3::PlainMon p = ToGba(x, kRules);
    g3::Encrypt(p, sb1(l.party + i * 100), 100);
  }
  sb1(l.giftRibbons)[5] = 44;
  for (int id = 0; id < g3::kSectorsPerSlot; id++) {
    uint8_t* s = file.data() + (g3::kSectorsPerSlot + (id + 2) % g3::kSectorsPerSlot) * g3::kSectorSize;
    std::memset(s, 0, g3::kSectorSize);
    std::memcpy(s, sec[id].data(), g3::kSectorDataSize);
    s[0xFF4] = static_cast<uint8_t>(id);
    s[0xFF5] = 0;
    const uint16_t c = g3::SectionChecksum(s, g3::kSectorDataSize);
    s[0xFF6] = static_cast<uint8_t>(c);
    s[0xFF7] = static_cast<uint8_t>(c >> 8);
    s[0xFF8] = 0x25; s[0xFF9] = 0x20; s[0xFFA] = 0x01; s[0xFFB] = 0x08;
    s[0xFFC] = 3; s[0xFFD] = s[0xFFE] = s[0xFFF] = 0;
  }
  return file;
}

void TestTrade(ByteOrder order) {
  const std::string tag = std::string(" [") + xd::save::ByteOrderName(order) + "]";
  const std::vector<uint8_t> xdFile = MakeXdFile(order);
  xd::save::Save xd;
  xd::save::ReadReport rep;
  std::string err;
  check(xd::save::ReadSave(xdFile, xd, rep, err), "synthetic XD save reads" + tag + " " + err);
  g3::Save gba;
  std::vector<std::string> warn;
  check(g3::ReadSave(MakeGbaFile(), g3::Game::Unknown, gba, err, warn) && gba.game == g3::Game::Emerald,
        "synthetic Emerald save reads" + tag + " " + err);
  const PartyView gp = GbaParty(gba, kRules, order);
  check(gp.count == 3 && gp.states[0] == State::Tradeable, "GBA party as XD sees it" + tag);
  check(gp.mons[2].U16(0xB8) == 2, "GBA records keep their party slot number (pokeconv)" + tag);

  TradeResult no = Trade(xdFile, xd, rep, gba, TradeRequest{1, 1}, kRules);
  check(!no.ok && no.error.find("evolve") != std::string::npos, "a trade evolution is refused (not supported yet)" + tag);

  TradeResult r = Trade(xdFile, xd, rep, gba, TradeRequest{0, 0}, kRules);
  check(r.ok, "trade XD #1 (purified, Shadow entry 9) <-> GBA #1" + tag + " " + r.error);
  if (!r.ok) {
    return;
  }
  xd::save::Save nx;
  xd::save::ReadReport nrep;
  check(xd::save::ReadSave(r.xdFile, nx, nrep, err), "new XD save reads" + tag);
  check(nx.slot == 1 && nx.saveCount == 8 && nx.gameSaveCount == 42, "XD: other slot, counter 8, saved count +1" + tag);
  check(std::memcmp(r.xdFile.data() + xd::save::SlotOffset(0), xdFile.data() + xd::save::SlotOffset(0),
                    xd::save::kSlotSize) == 0,
        "XD: the old slot is untouched" + tag);
  const PartyView np = XdParty(nx, kRules);
  check(np.mons[0].Species() == 280 && np.mons[0].U16(0x06) == 70 && np.mons[0].U16(0xB8) == 0,
        "XD: GBA Pokemon arrived in slot 1, friendship 70" + tag);
  check(np.mons[1].Species() == 155 && np.mons[2].Species() == 158, "XD: the rest of the party unchanged" + tag);
  const uint8_t* sh = nx.SectionData(15);
  check(((xd::save::Read32(sh + 9 * 0x48, order) >> 15) & 7) == 5 && nx.shadows[9].status == 4,
        "XD: Shadow entry 9 marked traded away (place 5), still purified" + tag);
  check(xd::save::Read32(sh + 0x24, order) == 0xFFFFFFFFu, "XD: Shadow entry 0 as pokemonInit leaves it" + tag);
  const uint8_t* memo = nx.SectionData(12);
  check(xd::save::Read16(memo, order) == 2 && (xd::save::Read16(memo + 16, order) & 0x3FFF) == 280 &&
            xd::save::Read32(memo + 24, order) == 0x9000u,
        "XD: Strategy Memo entry added for the newcomer" + tag);
  const uint8_t* rib = nx.SectionData(16);
  check(rib[5] == 44 && rib[2] == 7, "XD: gift ribbon taken from the GBA where XD had none" + tag);

  g3::Save ng;
  check(g3::ReadSave(r.gbaFile, g3::Game::Unknown, ng, err, warn), "new GBA save reads" + tag);
  check(ng.slot == 0 && ng.counter == 4, "GBA: other slot, counter 4" + tag);
  const g3::PlainMon sent = ng.PartyMon(0);
  check(sent.checksumOk && sent.Species() == 152 && sent.Pid() == 0x111 && sent.b[0x29] == 70,
        "GBA: XD Pokemon arrived in slot 1, friendship 70" + tag);
  check(ng.PartyMon(1).Species() == 64 && ng.PartyCount() == 3, "GBA: rest of the party unchanged" + tag);
  check(ng.DexSeen(152) && ng.DexOwned(152), "GBA: Pokedex registered" + tag);
  check(ng.GiftRibbons()[2] == 7 && ng.GiftRibbons()[5] == 44, "GBA: gift ribbon from XD filled in" + tag);
  const XdMon back = FromGba(sent, kRules, order);
  const XdMon orig = XdParty(xd, kRules).mons[0];
  check(back.Pid() == orig.Pid() && back.U16(0x90) == orig.U16(0x90) && back.Text(0x4E) == orig.Text(0x64),
        "GBA record converts back to the XD Pokemon" + tag);

  check(!Trade(xdFile, xd, rep, gba, TradeRequest{4, 0}, kRules).ok, "an empty XD slot can't be chosen" + tag);
  check(!Trade(xdFile, xd, rep, gba, TradeRequest{0, 5}, kRules).ok, "an empty GBA slot can't be chosen" + tag);
}

}

int main() {
  TestShuffle();
  TestConvert(ByteOrder::GameCube);
  TestConvert(ByteOrder::PC);
  TestStates();
  TestTrade(ByteOrder::GameCube);
  TestTrade(ByteOrder::PC);
  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
