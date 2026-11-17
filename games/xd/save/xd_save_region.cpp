#include <xd_save_region.h>

#include <xd_save_write.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace xd::save {

namespace {

constexpr ByteOrder kBe = ByteOrder::GameCube;

struct Table {
  int id;
  uint32_t sizeAt, offsetAt, usSize, jpSize;
  const char* name;
};
const Table kTable[] = {
    {1, 0x18, 0x38, 0x88, 0x80, "game data"},
    {2, 0x1A, 0x3C, 0x978, 0x978, "trainer and party"},
    {3, 0x1C, 0x40, 0xBC50, 0xBC0C, "PC boxes"},
    {10, 0x1E, 0x44, 0x4A, 0x4A, "mailbox"},
    {11, 0x20, 0x48, 0xCC, 0xCC, "day care"},
    {12, 0x22, 0x4C, 0x1774, 0x1774, "memo"},
    {14, 0x24, 0x50, 0x364, 0x2EC, "battle rules"},
    {15, 0x26, 0x54, 0x2400, 0x1E00, "Shadow Pokemon list"},
    {23, 0x28, 0x58, 0x6B4, 0x6B4, "story flags"},
    {16, 0x2A, 0x5C, 0xB, 0xB, "extra ribbons"},
    {17, 0x2C, 0x60, 0x400, 0x400, "reserve 1"},
    {18, 0x2E, 0x64, 0x800, 0x800, "reserve 2"},
    {19, 0x30, 0x68, 0x1000, 0x1000, "reserve 3"},
    {20, 0x32, 0x6C, 0x2000, 0x2000, "reserve 4"},
    {21, 0x34, 0x70, 0x2298, 0x2298, "Purification Chamber"},
    {22, 0x36, 0x74, 0xC800, 0xC800, "map state"},
};

uint32_t Stride(uint32_t size) { return (size + 0x1F) & ~0xFu; }

uint32_t ReadSize(const uint8_t* data, const Table& t) {
  uint32_t size = Read16(data + t.sizeAt, kBe);
  if (t.id == 3) {
    size |= static_cast<uint32_t>(Read16(data + 0x80, kBe)) << 16;
  }
  return size;
}
uint32_t ReadOffset(const uint8_t* data, const Table& t) {
  return Read16(data + t.offsetAt, kBe) | static_cast<uint32_t>(Read16(data + t.offsetAt + 2, kBe)) << 16;
}

bool DecryptSlot(const std::vector<uint8_t>& file, int s, std::vector<uint8_t>& slot, int32_t& count) {
  const uint8_t* p = file.data() + SlotOffset(s);
  if (!(p[0] == 1 && p[1] == 1 && p[2] <= 1)) {
    return false;
  }
  count = static_cast<int32_t>(Read32(p + 4, kBe));
  if (count <= 0) {
    return false;
  }
  slot.assign(p, p + kSlotSize);
  return DecryptSaveData(slot.data() + kSlotHeaderSize, kBe);
}

uint32_t HeaderSum(const uint8_t* slot) {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += slot[i];
  }
  return sum;
}

}

SaveKind LayoutOf(const std::vector<uint8_t>& file, std::string& why) {
  if (file.size() != kFileSize) {
    why = "not a Pokemon XD save: " + std::to_string(file.size()) + " bytes (XD's is " + std::to_string(kFileSize) + ")";
    return SaveKind::Unknown;
  }
  int best = -1;
  int32_t bestCount = 0;
  std::vector<uint8_t> bestSlot;
  for (int s = 0; s < kSlotCount; s++) {
    std::vector<uint8_t> slot;
    int32_t count = 0;
    if (DecryptSlot(file, s, slot, count) && (best < 0 || count > bestCount)) {
      best = s;
      bestCount = count;
      bestSlot = std::move(slot);
    }
  }
  if (best < 0) {
    why = "no save slot passes its checksums (the save is damaged, empty, or in another byte order)";
    return SaveKind::Unknown;
  }
  const uint16_t gameSize = Read16(bestSlot.data() + kSlotHeaderSize + 0x18, kBe);
  if (gameSize == 0x88) {
    return SaveKind::UsaLayout;
  }
  if (gameSize == 0x80) {
    return SaveKind::JapaneseLayout;
  }
  char buf[120];
  std::snprintf(buf, sizeof(buf), "the save's game data section is 0x%X bytes; known layouts: 0x88 (USA, Europe), 0x80 (Japan)",
                gameSize);
  why = buf;
  return SaveKind::Unknown;
}

int SaveLanguage(const Save& save) {
  for (const Section& s : save.sections) {
    if (s.id == 1) {
      return s.size >= 0x88 ? save.SectionData(1)[0x2B] : 0;
    }
  }
  return 0;
}

const char* LanguageName(int number) {
  switch (number) {
  case 2: return "English";
  case 3: return "German";
  case 4: return "French";
  case 5: return "Italian";
  case 6: return "Spanish";
  }
  return "";
}

const char* LanguageId(int number) {
  switch (number) {
  case 2: return "us";
  case 3: return "ge";
  case 4: return "fr";
  case 5: return "it";
  case 6: return "sp";
  }
  return "";
}

namespace {
const char* const kKana[] = {
    "A", "A", "I", "I", "U", "U", "E", "E", "O", "O", "KA", "GA", "KI", "GI", "KU", "GU", "KE", "GE", "KO", "GO", "SA", "ZA",
    "SHI", "JI", "SU", "ZU", "SE", "ZE", "SO", "ZO", "TA", "DA", "CHI", "JI", "", "TSU", "ZU", "TE", "DE", "TO", "DO", "NA",
    "NI", "NU", "NE", "NO", "HA", "BA", "PA", "HI", "BI", "PI", "FU", "BU", "PU", "HE", "BE", "PE", "HO", "BO", "PO", "MA",
    "MI", "MU", "ME", "MO", "YA", "YA", "YU", "YU", "YO", "YO", "RA", "RI", "RU", "RE", "RO", "WA", "WA", "I", "E", "WO", "N",
    "VU", "KA", "KE"};
}

std::string DisplayUtf8(const std::u16string& s) {
  std::u16string plain;
  std::string out;
  bool doubleNext = false;
  auto flush = [&] {
    out += ToUtf8(plain);
    plain.clear();
  };
  for (char16_t ch : s) {
    uint32_t c = ch;
    if (c >= 0x3041 && c <= 0x3096) {
      c += 0x60;
    }
    if (c >= 0xFF21 && c <= 0xFF3A) {
      c = c - 0xFF21 + 'A';
      plain.push_back(static_cast<char16_t>(c));
      continue;
    }
    if (c >= 0xFF41 && c <= 0xFF5A) {
      c = c - 0xFF41 + 'a';
      plain.push_back(static_cast<char16_t>(c));
      continue;
    }
    if (c >= 0xFF10 && c <= 0xFF19) {
      plain.push_back(static_cast<char16_t>(c - 0xFF10 + '0'));
      continue;
    }
    if (c >= 0x30A1 && c <= 0x30F6) {
      flush();
      std::string r = kKana[c - 0x30A1];
      if (c == 0x30C3) {
        doubleNext = true;
        continue;
      }
      const bool smallY = c == 0x30E3 || c == 0x30E5 || c == 0x30E7;
      if (smallY && !out.empty() && out.back() == 'I') {
        const std::string vowel = r.substr(1);
        out.pop_back();
        const bool soft = (out.size() >= 2 && (out.compare(out.size() - 2, 2, "SH") == 0 || out.compare(out.size() - 2, 2, "CH") == 0)) ||
                          (!out.empty() && out.back() == 'J');
        out += soft ? vowel : "Y" + vowel;
        continue;
      }
      if (doubleNext && !r.empty() && r[0] != 'A' && r[0] != 'I' && r[0] != 'U' && r[0] != 'E' && r[0] != 'O') {
        out += (r[0] == 'C' ? 'T' : r[0]);
      }
      doubleNext = false;
      out += r;
      continue;
    }
    if (c == 0x30FC || c == 0x30FB || c == 0x3000) {
      flush();
      out += c == 0x3000 ? " " : "-";
      continue;
    }
    plain.push_back(ch);
    flush();
  }
  flush();
  return out;
}

bool ConvertJapanese(const std::vector<uint8_t>& japanese, std::vector<uint8_t>& out, JapaneseReport& report, std::string& error) {
  report = JapaneseReport{};
  std::string why;
  const SaveKind kind = LayoutOf(japanese, why);
  if (kind == SaveKind::Unknown) {
    error = why;
    return false;
  }
  if (kind == SaveKind::UsaLayout) {
    error = "this save is already in the USA / European layout; nothing to convert";
    return false;
  }
  int best = -1;
  int32_t bestCount = 0;
  std::vector<uint8_t> src;
  for (int s = 0; s < kSlotCount; s++) {
    std::vector<uint8_t> slot;
    int32_t count = 0;
    if (DecryptSlot(japanese, s, slot, count) && (best < 0 || count > bestCount)) {
      best = s;
      bestCount = count;
      src = std::move(slot);
    }
  }
  const uint8_t* in = src.data() + kSlotHeaderSize;

  struct Placed {
    const Table* t;
    uint32_t offset;
  };
  std::vector<Placed> order;
  for (const Table& t : kTable) {
    const uint32_t size = ReadSize(in, t);
    const uint32_t off = ReadOffset(in, t);
    if (size != t.jpSize) {
      char buf[200];
      std::snprintf(buf, sizeof(buf), "the %s section is 0x%X bytes; a Japanese save has 0x%X: this file is not a save of the Japanese game as understood",
                    t.name, size, t.jpSize);
      error = buf;
      return false;
    }
    if (kSectionBase + static_cast<size_t>(off) + t.jpSize > kDataSize) {
      error = std::string("the ") + t.name + " section lies outside the save data (damaged)";
      return false;
    }
    order.push_back({&t, off});
  }
  std::sort(order.begin(), order.end(), [](const Placed& a, const Placed& b) { return a.offset < b.offset; });
  for (size_t i = 1; i < order.size(); i++) {
    if (order[i].offset < order[i - 1].offset + order[i - 1].t->jpSize) {
      error = "two sections of the save overlap (damaged)";
      return false;
    }
  }

  std::vector<uint8_t> slot(kSlotSize, 0);
  std::memcpy(slot.data(), src.data(), kSlotHeaderSize);
  slot[2] = 1;
  std::memcpy(slot.data() + kSlotSize - 0x28, src.data() + kSlotSize - 0x28, 0x28);
  uint8_t* data = slot.data() + kSlotHeaderSize;
  std::memcpy(data, in, kSectionBase);

  uint32_t next = 0;
  for (const Placed& p : order) {
    const Table& t = *p.t;
    const uint32_t off = next;
    next += Stride(t.usSize);
    if (kSectionBase + static_cast<size_t>(next) > kDataSize) {
      error = "the converted save would not fit the slot";
      return false;
    }
    Write16(data + t.sizeAt, static_cast<uint16_t>(t.usSize), kBe);
    if (t.id == 3) {
      Write16(data + 0x80, static_cast<uint16_t>(t.usSize >> 16), kBe);
    }
    Write16(data + t.offsetAt, static_cast<uint16_t>(off), kBe);
    Write16(data + t.offsetAt + 2, static_cast<uint16_t>(off >> 16), kBe);
    const uint8_t* j = in + kSectionBase + p.offset;
    uint8_t* u = data + kSectionBase + off;
    switch (t.id) {
    case 1: {
      std::memcpy(u, j, 8);
      std::memcpy(u + 8, j + 8, 0x18);
      float playTime;
      uint32_t bits = Read32(j + 8 + 0x18, kBe);
      std::memcpy(&playTime, &bits, 4);
      Write32(u + 8 + 0x18, bits, kBe);
      const double seconds = playTime;
      uint64_t d;
      std::memcpy(&d, &seconds, 8);
      Write32(u + 8 + 0x28, static_cast<uint32_t>(d >> 32), kBe);
      Write32(u + 8 + 0x2C, static_cast<uint32_t>(d), kBe);
      std::memcpy(u + 8 + 0x1C, j + 8 + 0x1C, 7);
      u[8 + 0x23] = 2;
      Write32(u + 8 + 0x30, HeaderSum(slot.data()), kBe);
      std::memcpy(u + 0x40, j + 0x38, 0x48);
      report.notes.push_back("Game data widened to the USA layout; the language is set to 2 (the game's English), the "
                             "Japanese area and language stamps are kept.");
      break;
    }
    case 3:
      std::memcpy(u, j, 0xBC0C);
      break;
    case 14: {
      for (int r = 0; r < 6; r++) {
        const uint8_t* jr = j + r * 0x7C;
        uint8_t* ur = u + r * 0x90;
        std::memcpy(ur, jr, 0x1E);
        std::memcpy(ur + 0x1E, jr + 0x1E, 11 * 2);
        std::memcpy(ur + 0x48, jr + 0x34, 0x48);
      }
      u[6 * 0x90] = j[6 * 0x7C];
      break;
    }
    case 15: {
      for (int e = 0; e < kShadowEntries; e++) {
        const uint8_t* je = j + e * 0x3C;
        uint8_t* ue = u + e * kShadowEntrySize;
        std::memcpy(ue, je, 0x28);
        std::memcpy(ue + 0x28, je + 0x28, 5 * 2);
        std::memcpy(ue + 0x3E, je + 0x34, 2);
        std::memcpy(ue + 0x40, je + 0x36, 2);
        std::memcpy(ue + 0x44, je + 0x38, 4);
      }
      break;
    }
    case 22:
      std::memcpy(u, j, t.jpSize);
      Write32(u + 4, 0, kBe);
      report.notes.push_back("The 'save anywhere' room state was dropped (the Japanese room scripts differ from the USA ones): "
                             "the game continues at the saved map's entrance, like a save made at a PC.");
      break;
    default:
      std::memcpy(u, j, t.jpSize);
      break;
    }
  }
  EncryptSaveData(data, kBe);

  out = japanese;
  std::memset(out.data() + SlotOffset(1 - best), 0, kSlotSize);
  out[SlotOffset(1 - best)] = out[SlotOffset(1 - best) + 1] = out[SlotOffset(1 - best) + 2] = 1;
  std::memcpy(out.data() + SlotOffset(best), slot.data(), kSlotSize);
  report.slotUsed = best;
  report.notes.push_back("Names keep their Japanese characters.");
  return true;
}

}
