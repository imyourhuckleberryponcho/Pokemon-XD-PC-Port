#include <xd_gba.h>

#include <cstring>

namespace xd::gba {

namespace {

using engine::gen3::PlainMon;

constexpr size_t kSpecies = 0x00, kItem = 0x02, kHp = 0x04, kFriend = 0x06, kFloor = 0x08, kAmari = 0x0A,
                 kPara1Amari = 0x0C, kCatchLevel = 0x0E, kBall = 0x0F, kOtSex = 0x10, kLevel = 0x11, kFur = 0x12,
                 kPokerus = 0x13, kMark = 0x14, kMail = 0x15, kCond = 0x16, kCondCount = 0x17, kCondTurn = 0x18,
                 kCondTurnNow = 0x19, kFlagAmari = 0x1A, kFlags = 0x1D, kExp = 0x20, kOtId = 0x24, kPid = 0x28,
                 kCondAmari = 0x2C, kEventGet = 0x30, kAttest = 0x34, kOtName = 0x38, kNickname = 0x4E,
                 kNicknameOrg = 0x64, kRibbon = 0x7C, kMoves = 0x80, kStats = 0x90, kEvs = 0x9C, kIvs = 0xA8,
                 kContest = 0xAE, kMedals = 0xB3;
constexpr int kGbaIndexOfXdStat[6] = {0, 1, 2, 4, 5, 3};
constexpr uint8_t kFlagTamago = 0x80, kFlagTokusei = 0x40, kFlagFusei = 0x20;

uint32_t Swap32(uint32_t v) { return v >> 24 | (v >> 8 & 0xFF00) | (v << 8 & 0xFF0000) | v << 24; }

bool AttestValid(const XdMon& m) {
  return m.U8(kAttest) != 0 && m.U8(kAttest + 1) != 0 && m.U8(kAttest + 2) != 0 && m.U8(kAttest + 3) != 0;
}

bool CheckValid(const XdMon& m, const GameRules& rules) {
  const uint16_t species = m.Species();
  if (species == 0 || !rules.SpeciesValid(species) || !AttestValid(m)) {
    return false;
  }
  return !m.Invalid();
}

bool GetTokusei(const XdMon& m, const GameRules& rules) {
  return rules.SecondAbility(m.Species()) != 0 && m.AbilityBit();
}
void SetFlag(XdMon& m, uint8_t bit, bool on) {
  m.SetU8(kFlags, static_cast<uint8_t>(on ? (m.U8(kFlags) | bit) : (m.U8(kFlags) & ~bit)));
}
void SetTokusei(XdMon& m, bool on, const GameRules& rules) {
  if (rules.SecondAbility(m.Species()) == 0) {
    on = false;
  }
  SetFlag(m, kFlagTokusei, on);
}

uint8_t MaxPP(uint16_t move, uint8_t ups, const GameRules& rules) {
  const uint8_t pp = rules.MovePP(move);
  return static_cast<uint8_t>(pp + ups * (pp * 20) / 100);
}

void StoreName(XdMon& m, size_t at, const std::u16string& s) {
  for (size_t i = 0; i < 11; i++) {
    m.SetU16(at + i * 2, i < s.size() && i < 10 ? static_cast<uint16_t>(s[i]) : 0);
  }
}

uint16_t ConditionFromXd(const XdMon& m) {
  switch (m.U8(kCond)) {
  case 4:
    return static_cast<uint16_t>(static_cast<int16_t>(m.U8(kCondCount)) << 8 | 0x80);
  case 5:
    return 0x40;
  case 7:
    return 0x20;
  case 6:
    return 0x10;
  case 3:
    return 0x08;
  case 8:
    return static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(m.U8(kCondTurn))));
  default:
    return 0;
  }
}

void ConditionToXd(XdMon& m, uint16_t cond) {
  auto write = [&](uint8_t id) {
    m.SetU8(kCond, id);
    m.SetU8(kCondCount, 0);
    m.SetU8(kCondTurn, 0);
    m.SetU8(kCondTurnNow, 0);
  };
  if (cond & 0x80) {
    write(4);
    m.SetU8(kCondCount, static_cast<uint8_t>((cond & 0xF00) >> 8));
    return;
  }
  if (cond & 0x40) {
    write(5);
    return;
  }
  if (cond & 0x20) {
    write(7);
    return;
  }
  if (cond & 0x10) {
    write(6);
    return;
  }
  if (cond & 0x08) {
    write(3);
    return;
  }
  const int sleep = cond & 7;
  if (sleep != 0) {
    write(8);
    m.SetU8(kCondTurn, static_cast<uint8_t>(sleep));
  }
}

bool EventFlagOk(const XdMon& m) {
  switch (m.Species()) {
  case 0x97:
  case 0x19A:
    return m.U32(kEventGet) != 0;
  }
  return true;
}

bool PokemonErrorFree(const XdMon& m, const GameRules& rules) {
  if (m.Species() == 0) {
    return true;
  }
  if (m.Egg()) {
    return true;
  }
  if (!EventFlagOk(m)) {
    return false;
  }
  XdMon tmp = m;
  ResetStats(tmp, rules);
  for (int i = 0; i < 6; i++) {
    if (tmp.U16(kStats + i * 2) < m.U16(kStats + i * 2)) {
      return false;
    }
  }
  uint32_t sum = 0;
  for (int i = 0; i < 6; i++) {
    sum += m.U16(kEvs + i * 2);
  }
  if (sum > 0x1FE) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    if (m.U16(kEvs + i * 2) > 0xFF) {
      return false;
    }
  }
  return true;
}

}

void XdMon::SetU16(size_t at, uint16_t v) {
  if (order == ByteOrder::GameCube) {
    raw[at] = static_cast<uint8_t>(v >> 8);
    raw[at + 1] = static_cast<uint8_t>(v);
  } else {
    raw[at] = static_cast<uint8_t>(v);
    raw[at + 1] = static_cast<uint8_t>(v >> 8);
  }
}

void XdMon::SetU32(size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    const int shift = order == ByteOrder::GameCube ? 24 - 8 * i : 8 * i;
    raw[at + i] = static_cast<uint8_t>(v >> shift);
  }
}

const std::array<uint16_t, 256>& GbaTableFor(const GameRules& rules, int xdLanguage) {
  return xdLanguage == 1 ? rules.GbaCharsJapanese() : rules.GbaCharsWestern();
}

std::u16string GbaToText(const GameRules& rules, const uint8_t* src, int len, int xdLanguage) {
  const auto& table = GbaTableFor(rules, xdLanguage);
  std::u16string s;
  while (len != 0 && *src != 0xFF) {
    s.push_back(static_cast<char16_t>(table[*src++]));
    len--;
  }
  return s;
}

int TextToGba(const GameRules& rules, const std::u16string& text, int xdLanguage, uint8_t* dst) {
  const auto& table = GbaTableFor(rules, xdLanguage);
  int n = 0;
  for (char16_t c : text) {
    if (c == 0) {
      break;
    }
    int i = 0;
    while (c != table[i]) {
      i++;
      if (i >= 256) {
        i = 0xB7;
        break;
      }
    }
    if (dst != nullptr) {
      *dst++ = static_cast<uint8_t>(i);
    }
    n++;
  }
  return n;
}

XdMon EmptyMon(ByteOrder order) {
  XdMon m;
  m.order = order;
  m.SetU8(kOtSex, 2);
  m.SetU8(kMail, 0xFF);
  return m;
}

void ResetStats(XdMon& m, const GameRules& rules) {
  const uint16_t species = m.Species();
  const uint8_t group = rules.GrowthGroup(species);
  uint8_t level = 0;
  if (rules.GrowthKnown(group)) {
    int l;
    for (l = 1; l < 101; l++) {
      if (rules.ExpForLevel(group, static_cast<uint8_t>(l)) > m.U32(kExp)) {
        break;
      }
    }
    level = static_cast<uint8_t>(l - 1);
  }
  const uint16_t oldMax = m.U16(kStats);
  m.SetU8(kLevel, level > 100 ? 100 : level);
  const auto base = rules.BaseStats(species);
  const uint8_t nature = static_cast<uint8_t>(m.Pid() % 25);
  uint16_t newMax = 0;
  for (int i = 0; i < 6; i++) {
    if (i == 0 && species == 0x12F) {
      m.SetU16(kStats, 1);
      newMax = 1;
      continue;
    }
    const int add = i == 0 ? level + 10 : 5;
    uint32_t value = static_cast<uint32_t>(add + static_cast<int32_t>(level * (m.U8(kIvs + i) + base[i] * 2 +
                                                                               ((m.U16(kEvs + i * 2) >> 2) & 0x3FFF))) /
                                                     100) &
                     0xFFFF;
    if (i != 0) {
      uint8_t mul = 0, div = 0;
      if (!rules.NatureRate(nature, i, mul, div)) {
        value = 0;
      } else {
        int32_t r = static_cast<int32_t>(value * mul);
        if (div != 0) {
          r /= div;
        }
        value = static_cast<uint32_t>(r);
      }
    }
    m.SetU16(kStats + i * 2, static_cast<uint16_t>(value));
    if (i == 0) {
      newMax = static_cast<uint16_t>(value);
    }
  }
  const uint16_t hp = m.Hp();
  if (hp != 0 || oldMax == 0) {
    uint16_t v = species == 0x12F ? 1 : static_cast<uint16_t>(hp + (newMax - oldMax));
    if (v > m.U16(kStats)) {
      v = m.U16(kStats);
    }
    m.SetU16(kHp, v);
  }
}

PlainMon ToGba(const XdMon& m, const GameRules& rules) {
  PlainMon g;
  if (!CheckValid(m, rules)) {
    return g;
  }
  g.Set32(0x00, m.Pid());
  g.Set32(0x04, m.OtId());
  uint32_t version;
  switch (m.U8(kAttest)) {
  case 8: version = 1; break;
  case 9: version = 2; break;
  case 10: version = 3; break;
  case 1: version = 4; break;
  case 2: version = 5; break;
  case 11: version = 15; break;
  default: version = 0; break;
  }
  const uint8_t langarea = m.U8(kAttest + 3);
  uint8_t language;
  switch (langarea) {
  case 1: language = 1; break;
  case 2: language = 2; break;
  case 3: language = 5; break;
  case 4: language = 3; break;
  case 5: language = 4; break;
  case 6: language = 7; break;
  case 8: language = 2; break;
  case 9: language = 2; break;
  default: language = 0; break;
  }
  g.b[0x12] = language;
  g.b[0x13] = static_cast<uint8_t>((m.U8(kFlagAmari) & 0x1F) << 3 | (m.Egg() ? 4 : 0) | 2 | (m.Invalid() ? 1 : 0));
  {
    uint8_t buf[16] = {};
    const int len = TextToGba(rules, m.Text(kNicknameOrg), langarea, buf);
    for (int i = 0; i < len && 0x08 + i < 0x1C; i++) {
      g.b[0x08 + i] = buf[i];
    }
    if (len < 10) {
      g.b[0x08 + len] = 0xFF;
      std::memset(g.b.data() + len + 9, 0, static_cast<size_t>(9 - len));
    }
  }
  {
    uint8_t buf[16] = {};
    const int len = TextToGba(rules, m.Text(kOtName), langarea, buf);
    for (int i = 0; i < len && 0x14 + i < 0x20; i++) {
      g.b[0x14 + i] = buf[i];
    }
    if (len < 7) {
      g.b[0x14 + len] = 0xFF;
      std::memset(g.b.data() + len + 0x15, 0, static_cast<size_t>(6 - len));
    }
  }
  g.b[0x1B] = m.U8(kMark);
  g.Set16(0x1E, m.U16(kAmari));
  g.Set16(0x20, m.Species());
  g.Set16(0x22, m.Item());
  g.Set32(0x24, m.U32(kExp));
  g.b[0x29] = static_cast<uint8_t>(m.U16(kFriend));
  const uint16_t para1 = m.U16(kPara1Amari);
  g.b[0x2A] = static_cast<uint8_t>(para1 >> 8);
  g.b[0x2B] = static_cast<uint8_t>(para1);
  uint8_t ppUps = 0;
  for (int i = 0; i < 4; i++) {
    g.Set16(0x2C + i * 2, m.U16(kMoves + i * 4));
    ppUps = static_cast<uint8_t>(ppUps | m.U8(kMoves + i * 4 + 3) << (i * 2));
    g.b[0x34 + i] = m.U8(kMoves + i * 4 + 2);
  }
  g.b[0x28] = ppUps;
  for (int i = 0; i < 6; i++) {
    g.b[0x38 + kGbaIndexOfXdStat[i]] = static_cast<uint8_t>(m.U16(kEvs + i * 2));
  }
  for (int i = 0; i < 5; i++) {
    g.b[0x3E + i] = m.U8(kContest + i);
  }
  g.b[0x43] = m.U8(kFur);
  const uint32_t misc = m.U8(kPokerus) | (m.U16(kFloor) & 0xFFu) << 8 | (m.U8(kCatchLevel) & 0x7Fu) << 16 |
                        (version & 0xFu) << 23 | (m.U8(kBall) & 0xFu) << 27 | (m.U8(kOtSex) & 1u) << 31;
  g.Set32(0x44, misc);
  uint32_t ivs = 0;
  for (int i = 0; i < 6; i++) {
    ivs |= (m.U8(kIvs + i) & 0x1Fu) << (5 * kGbaIndexOfXdStat[i]);
  }
  ivs |= (m.Egg() ? 1u : 0u) << 30 | (GetTokusei(m, rules) ? 1u : 0u) << 31;
  g.Set32(0x48, ivs);
  const uint32_t xr = m.U32(kRibbon);
  uint32_t r = 0;
  for (int i = 0; i < 5; i++) {
    r |= (m.U8(kMedals + i) & 7u) << (3 * i);
  }
  for (int i = 0; i < 12; i++) {
    r |= (xr >> (31 - i) & 1u) << (15 + i);
  }
  r |= (xr >> 12 & 0xFu) << 27;
  r |= (m.U32(kEventGet) != 0 ? 1u : 0u) << 31;
  g.Set32(0x4C, r);
  const uint32_t rawCond = Swap32(ConditionFromXd(m)) | (m.U32(kCondAmari) & 0xFFFFF000u);
  g.b[0x50] = static_cast<uint8_t>(rawCond >> 24);
  g.b[0x51] = static_cast<uint8_t>(rawCond >> 16);
  g.b[0x52] = static_cast<uint8_t>(rawCond >> 8);
  g.b[0x53] = static_cast<uint8_t>(rawCond);
  g.b[0x54] = m.Level();
  g.b[0x55] = m.U8(kMail);
  g.Set16(0x56, m.Hp());
  for (int i = 0; i < 6; i++) {
    g.Set16(0x58 + kGbaIndexOfXdStat[i] * 2, m.U16(kStats + i * 2));
  }
  g.Set16(0x1C, engine::gen3::DataChecksum(g));
  return g;
}

XdMon FromGba(const PlainMon& g, const GameRules& rules, ByteOrder order) {
  XdMon m = EmptyMon(order);
  if (!g.HasSpecies() && !g.IsBadEgg()) {
    return m;
  }
  const uint32_t misc = g.Get32(0x44), ivs = g.Get32(0x48), rib = g.Get32(0x4C);
  m.SetU32(kPid, g.Pid());
  m.SetU32(kOtId, g.OtId());
  uint8_t ver;
  switch (misc >> 23 & 0xF) {
  case 1: ver = 8; break;
  case 2: ver = 9; break;
  case 3: ver = 10; break;
  case 4: ver = 1; break;
  case 5: ver = 2; break;
  case 15: ver = 11; break;
  default: ver = 0; break;
  }
  uint8_t area, lang;
  switch (g.b[0x12]) {
  case 1: area = 1; lang = 1; break;
  case 2: area = 2; lang = 2; break;
  case 3: area = 3; lang = 4; break;
  case 4: area = 3; lang = 5; break;
  case 5: area = 3; lang = 3; break;
  case 7: area = 3; lang = 6; break;
  default: area = 0; lang = 0; break;
  }
  if (ver != 0 && area != 0 && lang != 0) {
    m.SetU8(kAttest, ver);
    m.SetU8(kAttest + 1, 3);
    m.SetU8(kAttest + 2, area);
    m.SetU8(kAttest + 3, lang);
  }
  const std::u16string name = GbaToText(rules, g.b.data() + 0x08, 10, lang);
  StoreName(m, kNickname, name);
  StoreName(m, kNicknameOrg, name);
  if (lang == 1) {
    m.SetU16(kNickname + 5 * 2, 0);
  }
  m.SetU8(kFlagAmari, static_cast<uint8_t>(g.b[0x13] >> 3));
  SetFlag(m, kFlagFusei, g.IsBadEgg());
  {
    const std::u16string ot = GbaToText(rules, g.b.data() + 0x14, 7, lang);
    for (size_t i = 0; i <= ot.size(); i++) {
      m.SetU16(kOtName + i * 2, i < ot.size() ? static_cast<uint16_t>(ot[i]) : 0);
    }
  }
  m.SetU8(kMark, g.b[0x1B]);
  m.SetU16(kAmari, g.Get16(0x1E));
  m.SetU16(kSpecies, g.Species());
  m.SetU16(kItem, g.HeldItem());
  m.SetU32(kExp, g.Exp());
  m.SetU16(kFriend, g.b[0x29]);
  m.SetU16(kPara1Amari, static_cast<uint16_t>(g.b[0x2A] << 8 | g.b[0x2B]));
  for (int i = 0; i < 4; i++) {
    const size_t w = kMoves + i * 4;
    const uint16_t move = g.Get16(0x2C + i * 2);
    m.SetU16(w, move);
    uint8_t ups = static_cast<uint8_t>(g.b[0x28] >> (i * 2) & 3);
    if (ups > 3) {
      ups = 3;
    }
    if (!(move != 0 && move != 0x163 && rules.MovePP(move) <= 4)) {
      m.SetU8(w + 3, ups);
    }
    uint8_t pp = g.b[0x34 + i];
    const uint8_t maxPp = MaxPP(move, m.U8(w + 3), rules);
    if (maxPp < pp) {
      pp = maxPp;
    }
    m.SetU8(w + 2, pp);
  }
  for (int i = 0; i < 6; i++) {
    m.SetU16(kEvs + i * 2, g.b[0x38 + kGbaIndexOfXdStat[i]]);
  }
  for (int i = 0; i < 5; i++) {
    m.SetU8(kContest + i, g.b[0x3E + i]);
  }
  m.SetU8(kFur, g.b[0x43]);
  m.SetU8(kPokerus, static_cast<uint8_t>(misc));
  m.SetU16(kFloor, static_cast<uint16_t>(misc >> 8 & 0xFF));
  m.SetU8(kCatchLevel, static_cast<uint8_t>(misc >> 16 & 0x7F));
  m.SetU8(kBall, static_cast<uint8_t>(misc >> 27 & 0xF));
  m.SetU8(kOtSex, static_cast<uint8_t>(misc >> 31));
  for (int i = 0; i < 6; i++) {
    m.SetU8(kIvs + i, static_cast<uint8_t>(ivs >> (5 * kGbaIndexOfXdStat[i]) & 0x1F));
  }
  SetFlag(m, kFlagTamago, (ivs >> 30 & 1) != 0);
  SetTokusei(m, (ivs >> 31) != 0, rules);
  for (int i = 0; i < 5; i++) {
    m.SetU8(kMedals + i, static_cast<uint8_t>(rib >> (3 * i) & 7));
  }
  uint32_t xr = m.U32(kRibbon);
  for (int i = 0; i < 12; i++) {
    const uint32_t bit = 1u << (31 - i);
    xr = (rib >> (15 + i) & 1) != 0 ? (xr | bit) : (xr & ~bit);
  }
  xr = (xr & ~0xFF000u) | (rib >> 27 & 0xFu) << 12;
  m.SetU32(kRibbon, xr);
  m.SetU32(kEventGet, rib >> 31);
  const uint32_t cond = g.Get32(0x50);
  ConditionToXd(m, static_cast<uint16_t>(cond));
  m.SetU32(kCondAmari, cond & 0xFFFFF000u);
  m.SetU8(kLevel, g.b[0x54] > 100 ? 100 : g.b[0x54]);
  m.SetU8(kMail, g.b[0x55]);
  m.SetU16(kStats, g.Get16(0x58));
  {
    uint16_t hp = g.Get16(0x56);
    if (hp > m.U16(kStats)) {
      hp = m.U16(kStats);
    }
    m.SetU16(kHp, hp);
  }
  for (int i = 1; i < 6; i++) {
    m.SetU16(kStats + i * 2, g.Get16(0x58 + kGbaIndexOfXdStat[i] * 2));
  }
  return m;
}

State TradeState(const XdMon& m, Side side, int shadowStatus, const GameRules& rules) {
  if (m.Invalid()) {
    return State::BadEgg;
  }
  if (!CheckValid(m, rules)) {
    return State::Empty;
  }
  if (m.Egg()) {
    return State::Egg;
  }
  const uint16_t shadow = m.ShadowId();
  if (shadow != 0 && !(shadow < xd::save::kShadowEntries && shadowStatus == 4)) {
    return State::Shadow;
  }
  if (!EventFlagOk(m)) {
    return State::NoEventFlag;
  }
  if (!PokemonErrorFree(m, rules)) {
    return State::Impossible;
  }
  const uint16_t item = m.Item();
  bool itemOk = true;
  if (item != 0) {
    itemOk = rules.ItemKnown(item) && (side == Side::Xd ? !rules.ItemImportant(item) : rules.ItemHasName(item));
  }
  return itemOk ? State::Tradeable : State::ItemNotAllowed;
}

const char* StateText(State s) {
  switch (s) {
  case State::Empty: return "there is no Pokemon there";
  case State::BadEgg: return "it is a Bad Egg (a damaged record)";
  case State::Egg: return "it is an Egg";
  case State::Shadow: return "it is a Shadow Pokemon (purify it first)";
  case State::ItemNotAllowed: return "it holds an item the other game can't take (take the item off first)";
  case State::NoEventFlag: return "it is a Mew or Deoxys without its event flag";
  case State::Impossible: return "its stats or EVs are higher than its level allows (the game refuses it)";
  case State::Tradeable: return "can be traded";
  }
  return "?";
}

}
