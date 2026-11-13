#include <xd_save.h>

#include <cstdio>
#include <cstring>

namespace xd::save {

namespace {

struct SectionDesc {
  int id;
  const char* name;
  uint32_t sizeAt;
  uint32_t offsetAt;
  uint32_t expected;
};
const SectionDesc kSections[] = {
    {1, "game data", 0x18, 0x38, 0x88},
    {2, "hero (trainer, party, bag)", 0x1A, 0x3C, 0x978},
    {3, "PC boxes", 0x1C, 0x40, 0xBC50},
    {10, "mailbox", 0x1E, 0x44, 0x4A},
    {11, "day care", 0x20, 0x48, 0xCC},
    {12, "memo", 0x22, 0x4C, 0x1774},
    {14, "battle rules", 0x24, 0x50, 0x364},
    {15, "Shadow Pokemon list", 0x26, 0x54, 0x2400},
    {23, "story flags", 0x28, 0x58, 0x6B4},
    {16, "extra ribbons", 0x2A, 0x5C, 0xB},
    {17, "reserve 1", 0x2C, 0x60, 0x400},
    {18, "reserve 2", 0x2E, 0x64, 0x800},
    {19, "reserve 3", 0x30, 0x68, 0x1000},
    {20, "reserve 4", 0x32, 0x6C, 0x2000},
    {21, "Purification Chamber", 0x34, 0x70, 0x2298},
    {22, "map state", 0x36, 0x74, 0xC800},
};

constexpr size_t kHeroName = 0x00;
constexpr size_t kHeroRnd = 0x2C;
constexpr size_t kHeroParty = 0x30;
constexpr size_t kHeroSex = 0x8E0;
constexpr size_t kHeroMoney = 0x8E4;
constexpr size_t kHeroCoupons = 0x8E8;
constexpr size_t kHeroCouponsAll = 0x8EC;
struct Pocket {
  size_t offset;
  int count;
};
constexpr Pocket kItems{0x4C8, 30}, kKeyItems{0x540, 43}, kBalls{0x5EC, 16}, kTmHm{0x62C, 64}, kBerries{0x72C, 46},
    kColognes{0x7E4, 3}, kBattleCds{0x7F0, 60};

constexpr size_t kBoxStride = 0x14 + kBoxSlots * kPokemonSize;
constexpr size_t kBoxPokemon = 0x14;
constexpr size_t kPcItems = kBoxCount * kBoxStride;
constexpr int kPcItemCount = 235;

constexpr size_t kChamberStride = 0x3D8;

std::vector<ItemSlot> ReadPocket(const uint8_t* base, size_t size, Pocket pocket, ByteOrder order) {
  std::vector<ItemSlot> out;
  for (int i = 0; i < pocket.count; i++) {
    const size_t at = pocket.offset + static_cast<size_t>(i) * 4;
    if (at + 4 > size) {
      break;
    }
    out.push_back({Read16(base + at, order), Read16(base + at + 2, order)});
  }
  return out;
}

void SlotHeaderSum(const uint8_t* slot, uint32_t* sum) {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) {
    s += slot[i];
  }
  *sum = s;
}

}

uint64_t Read64(const uint8_t* p, ByteOrder order) {
  const uint64_t a = Read32(p, order), b = Read32(p + 4, order);
  return order == ByteOrder::GameCube ? (a << 32 | b) : (b << 32 | a);
}

double ReadF64(const uint8_t* p, ByteOrder order) {
  const uint64_t bits = Read64(p, order);
  double d;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

std::u16string ReadString(const uint8_t* p, size_t max, ByteOrder order) {
  std::u16string s;
  for (size_t i = 0; i < max; i++) {
    const uint16_t c = Read16(p + i * 2, order);
    if (c == 0) {
      break;
    }
    s.push_back(static_cast<char16_t>(c));
  }
  return s;
}

std::string ToUtf8(const std::u16string& s) {
  std::string out;
  for (char16_t ch : s) {
    const uint32_t c = ch;
    if (c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c < 0xE000) || c >= 0xE000) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "\\u{%04X}", c);
      out += buf;
    } else if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

bool Pokemon::IsShiny() const {
  return ((otId >> 16) ^ (otId & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
}

Pokemon ParsePokemon(const uint8_t* p, ByteOrder order) {
  Pokemon m;
  std::memcpy(m.raw.data(), p, kPokemonSize);
  m.species = Read16(p + 0x00, order);
  m.heldItem = Read16(p + 0x02, order);
  m.hp = Read16(p + 0x04, order);
  m.friendship = Read16(p + 0x06, order);
  m.metLocation = Read16(p + 0x08, order);
  m.metLevel = p[0x0E];
  m.ball = p[0x0F];
  m.otGender = p[0x10];
  m.level = p[0x11];
  m.contestLuster = p[0x12];
  m.pokerus = p[0x13];
  m.marks = p[0x14];
  m.status = p[0x16];
  m.flags = p[0x1D];
  m.exp = Read32(p + 0x20, order);
  m.otId = Read32(p + 0x24, order);
  m.pid = Read32(p + 0x28, order);
  m.otName = ReadString(p + 0x38, 11, order);
  m.nickname = ReadString(p + 0x4E, 11, order);
  m.nicknameOrg = ReadString(p + 0x64, 11, order);
  m.ribbons = Read32(p + 0x7C, order);
  for (int i = 0; i < 4; i++) {
    const uint8_t* w = p + 0x80 + i * 4;
    m.moves[i] = {Read16(w, order), w[2], w[3]};
  }
  for (int i = 0; i < 6; i++) {
    m.stats[i] = Read16(p + 0x90 + i * 2, order);
    m.evs[i] = Read16(p + 0x9C + i * 2, order);
    m.ivs[i] = p[0xA8 + i];
  }
  std::memcpy(m.contest.data(), p + 0xAE, m.contest.size());
  m.shadowId = Read16(p + 0xBA, order);
  return m;
}

const ShadowEntry* Save::ShadowOf(const Pokemon& p) const {
  if (p.shadowId == 0 || p.shadowId >= kShadowEntries) {
    return nullptr;
  }
  return &shadows[p.shadowId];
}

const uint8_t* Save::SectionData(int id) const {
  for (const Section& s : sections) {
    if (s.id == id && s.offset + s.size <= kDataSize) {
      return slotBytes.data() + kSlotHeaderSize + s.offset;
    }
  }
  return nullptr;
}

bool ReadSave(const std::vector<uint8_t>& file, Save& out, ReadReport& report, std::string& error) {
  report = ReadReport{};
  if (file.size() != kFileSize) {
    error = "not a Pokemon XD save: " + std::to_string(file.size()) + " bytes (XD's is " + std::to_string(kFileSize) +
            ")";
    return false;
  }

  {
    uint32_t sumBe = 0, sumLe = 0;
    for (size_t i = 0; i < 0xE90; i++) {
      sumBe += Read32(file.data() + i * 4, ByteOrder::GameCube);
      sumLe += Read32(file.data() + i * 4, ByteOrder::PC);
    }
    report.bannerSumOk = sumBe == Read32(file.data() + 0x3A40, ByteOrder::GameCube) ||
                         sumLe == Read32(file.data() + 0x3A40, ByteOrder::PC);
    if (!report.bannerSumOk) {
      report.warnings.push_back("the banner block's checksum is wrong (the game would just redraw the banner)");
    }
  }

  std::array<std::vector<uint8_t>, kSlotCount> decrypted;
  for (int s = 0; s < kSlotCount; s++) {
    const uint8_t* slot = file.data() + SlotOffset(s);
    SlotReport& r = report.slots[s];
    r.written = slot[0] == 1 && slot[1] == 1 && slot[2] == 1;
    if (!r.written) {
      continue;
    }
    for (ByteOrder order : {ByteOrder::GameCube, ByteOrder::PC}) {
      std::vector<uint8_t> copy(slot, slot + kSlotSize);
      if (DecryptSaveData(copy.data() + kSlotHeaderSize, order)) {
        r.checksumOk = true;
        r.order = order;
        r.saveCount = static_cast<int32_t>(Read32(slot + 4, order));
        decrypted[s] = std::move(copy);
        break;
      }
    }
    if (!r.checksumOk) {
      r.saveCount = static_cast<int32_t>(Read32(slot + 4, ByteOrder::GameCube));
      continue;
    }
    const uint8_t* data = decrypted[s].data() + kSlotHeaderSize;
    const uint16_t gameSize = Read16(data + 0x18, r.order);
    if (gameSize == 0x88) {
      const uint32_t at = kSectionBase + (Read16(data + 0x38, r.order) | static_cast<uint32_t>(Read16(data + 0x3A, r.order)) << 16);
      uint32_t sum;
      SlotHeaderSum(slot, &sum);
      r.headerSumOk = at + 0x3C <= kDataSize && Read32(data + at + 0x38, r.order) == sum;
    } else {
      r.headerSumOk = true;
    }
  }

  int best = -1;
  for (int s = 0; s < kSlotCount; s++) {
    const SlotReport& r = report.slots[s];
    if (!r.written || r.saveCount <= 0) {
      continue;
    }
    if (!r.checksumOk || !r.headerSumOk) {
      report.warnings.push_back("slot " + std::to_string(s) + " (save counter " + std::to_string(r.saveCount) + ") " +
                                (r.checksumOk ? "fails the header check" : "fails its checksums") + ": not used");
      continue;
    }
    if (best < 0 || r.saveCount > report.slots[best].saveCount) {
      best = s;
    }
  }
  if (best < 0) {
    bool anyWritten = false;
    for (const SlotReport& r : report.slots) {
      anyWritten |= r.written && r.saveCount > 0;
    }
    error = anyWritten ? "no save slot passes its checks (the save is damaged)"
                       : "the file holds no saved game yet (both slots are empty: the game made the file but "
                         "never saved into it)";
    return false;
  }

  out = Save{};
  out.slot = best;
  out.order = report.slots[best].order;
  out.saveCount = report.slots[best].saveCount;
  out.slotBytes = std::move(decrypted[best]);
  const ByteOrder order = out.order;
  const uint8_t* data = out.slotBytes.data() + kSlotHeaderSize;

  for (const SectionDesc& d : kSections) {
    Section s;
    s.id = d.id;
    s.name = d.name;
    s.offset = static_cast<uint32_t>(kSectionBase) +
               (Read16(data + d.offsetAt, order) | static_cast<uint32_t>(Read16(data + d.offsetAt + 2, order)) << 16);
    s.size = Read16(data + d.sizeAt, order);
    if (d.id == 3) {
      s.size |= static_cast<uint32_t>(Read16(data + 0x80, order)) << 16;
    }
    s.expectedSize = d.expected;
    if (s.size != s.expectedSize) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "section %d (%s): size 0x%X, the game writes 0x%X", s.id, s.name, s.size,
                    s.expectedSize);
      report.warnings.push_back(buf);
    }
    if (s.offset + s.expectedSize > kDataSize) {
      report.warnings.push_back(std::string("section ") + std::to_string(s.id) + " (" + s.name +
                                ") lies outside the save data; skipped");
      continue;
    }
    out.sections.push_back(s);
  }

  if (const uint8_t* g = out.SectionData(1)) {
    out.memcardId = Read64(g + 0x08, order);
    out.gameSaveCount = Read32(g + 0x10, order);
    out.floorId = Read32(g + 0x1C, order);
    out.optionNoVibration = g[0x29];
    out.optionAudio = g[0x2A];
    out.playTimeSeconds = ReadF64(g + 0x30, order);
    out.sysHeadChecksum = Read32(g + 0x38, order);
  }
  if (const uint8_t* h = out.SectionData(2)) {
    const size_t size = 0x978;
    out.trainerName = ReadString(h + kHeroName, 11, order);
    out.trainerOtId = Read32(h + kHeroRnd, order);
    out.trainerGender = h[kHeroSex];
    out.money = Read32(h + kHeroMoney, order);
    out.coupons = Read32(h + kHeroCoupons, order);
    out.couponsTotal = Read32(h + kHeroCouponsAll, order);
    for (int i = 0; i < kPartySize; i++) {
      out.party[i] = ParsePokemon(h + kHeroParty + static_cast<size_t>(i) * kPokemonSize, order);
    }
    out.items = ReadPocket(h, size, kItems, order);
    out.keyItems = ReadPocket(h, size, kKeyItems, order);
    out.balls = ReadPocket(h, size, kBalls, order);
    out.tmHm = ReadPocket(h, size, kTmHm, order);
    out.berries = ReadPocket(h, size, kBerries, order);
    out.colognes = ReadPocket(h, size, kColognes, order);
    out.battleCds = ReadPocket(h, size, kBattleCds, order);
  }
  if (const uint8_t* b = out.SectionData(3)) {
    for (int i = 0; i < kBoxCount; i++) {
      const uint8_t* box = b + static_cast<size_t>(i) * kBoxStride;
      out.boxes[i].name = ReadString(box, 10, order);
      for (int j = 0; j < kBoxSlots; j++) {
        out.boxes[i].slots[j] = ParsePokemon(box + kBoxPokemon + static_cast<size_t>(j) * kPokemonSize, order);
      }
    }
    out.pcItems = ReadPocket(b, 0xBC50, Pocket{kPcItems, kPcItemCount}, order);
  }
  if (const uint8_t* d = out.SectionData(11)) {
    out.dayCare = ParsePokemon(d + 8, order);
  }
  if (const uint8_t* c = out.SectionData(21)) {
    for (size_t i = 0; i < out.chambers.size(); i++) {
      for (size_t j = 0; j < out.chambers[i].size(); j++) {
        out.chambers[i][j] = ParsePokemon(c + i * kChamberStride + j * kPokemonSize, order);
      }
    }
  }
  if (const uint8_t* sh = out.SectionData(15)) {
    for (int i = 0; i < kShadowEntries; i++) {
      const uint8_t* e = sh + static_cast<size_t>(i) * kShadowEntrySize;
      ShadowEntry& se = out.shadows[i];
      std::memcpy(se.raw.data(), e, kShadowEntrySize);
      se.status = static_cast<uint8_t>(e[0] >> 5);
      se.species = Read16(e + 0x1A, order);
      se.pid = Read32(e + 0x1C, order);
      se.heartGauge = static_cast<int32_t>(Read32(e + 0x24, order));
    }
  }
  return true;
}

std::vector<std::string> UnknownIds(const Save& save, const TableCounts& counts) {
  std::vector<std::string> out;
  const auto look = [&](const Pokemon& p, const std::string& where) {
    if (p.species != 0 && p.species >= counts.species) {
      out.push_back(where + ": species " + std::to_string(p.species) + " (the game has " + std::to_string(counts.species) + ")");
    }
    if (p.species == 0) return;
    if (p.heldItem != 0 && p.heldItem >= counts.items) {
      out.push_back(where + ": held item " + std::to_string(p.heldItem) + " (the game has " + std::to_string(counts.items) + ")");
    }
    for (const Move& m : p.moves) {
      if (m.id != 0 && m.id >= counts.moves) {
        out.push_back(where + ": move " + std::to_string(m.id) + " (the game has " + std::to_string(counts.moves) + ")");
      }
    }
  };
  for (size_t i = 0; i < save.party.size(); i++) look(save.party[i], "party " + std::to_string(i + 1));
  for (size_t b = 0; b < save.boxes.size(); b++) {
    for (size_t i = 0; i < save.boxes[b].slots.size(); i++) {
      look(save.boxes[b].slots[i], "box " + std::to_string(b + 1) + " slot " + std::to_string(i + 1));
    }
  }
  look(save.dayCare, "day care");
  for (size_t c = 0; c < save.chambers.size(); c++) {
    for (size_t i = 0; i < save.chambers[c].size(); i++) look(save.chambers[c][i], "purify chamber " + std::to_string(c + 1));
  }
  for (size_t i = 0; i < save.shadows.size(); i++) {
    if (save.shadows[i].species != 0 && save.shadows[i].species >= counts.species) {
      out.push_back("shadow list " + std::to_string(i) + ": species " + std::to_string(save.shadows[i].species));
    }
  }
  const auto pocket = [&](const std::vector<ItemSlot>& slots, const char* name) {
    for (const ItemSlot& s : slots) {
      if (s.item != 0 && s.count != 0 && s.item >= counts.items) {
        out.push_back(std::string(name) + ": item " + std::to_string(s.item) + " (the game has " + std::to_string(counts.items) + ")");
      }
    }
  };
  pocket(save.items, "bag items");
  pocket(save.keyItems, "key items");
  pocket(save.balls, "balls");
  pocket(save.tmHm, "TMs");
  pocket(save.berries, "berries");
  pocket(save.colognes, "colognes");
  pocket(save.battleCds, "battle CDs");
  pocket(save.pcItems, "PC items");
  if (out.size() > 12) {
    const size_t more = out.size() - 12;
    out.resize(12);
    out.push_back("... and " + std::to_string(more) + " more");
  }
  return out;
}

}
