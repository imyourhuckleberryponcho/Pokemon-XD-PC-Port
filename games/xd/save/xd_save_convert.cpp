#include <xd_save_convert.h>

#include <algorithm>
#include <cstring>

namespace xd::save {

namespace {

struct Run {
  uint32_t offset;
  uint32_t count;
  uint8_t width;
};

class UnitList {
public:
  explicit UnitList(size_t limit) : mTaken(limit, 0), mLimit(limit) {}

  bool Add(size_t base, size_t size, const Run* runs, size_t n) {
    for (size_t i = 0; i < n; i++) {
      for (uint32_t k = 0; k < runs[i].count; k++) {
        const size_t off = runs[i].offset + static_cast<size_t>(k) * runs[i].width;
        if (off + runs[i].width > size) {
          break;
        }
        if (!AddUnit(base + off, runs[i].width)) {
          return false;
        }
      }
    }
    return true;
  }
  template <size_t N> bool Add(size_t base, size_t size, const Run (&runs)[N]) { return Add(base, size, runs, N); }

  bool AddUnit(size_t at, uint8_t width) {
    if (at + width > mLimit) {
      return false;
    }
    for (size_t i = at; i < at + width; i++) {
      if (mTaken[i]) {
        return false;
      }
      mTaken[i] = 1;
    }
    mUnits.push_back({static_cast<uint32_t>(at), 1, width});
    return true;
  }

  void Apply(uint8_t* data) const {
    for (const Run& u : mUnits) {
      std::reverse(data + u.offset, data + u.offset + u.width);
    }
  }

private:
  std::vector<uint8_t> mTaken;
  std::vector<Run> mUnits;
  size_t mLimit;
};

const Run kPokemon[] = {
    {0x00, 7, 2}, {0x20, 5, 4}, {0x38, 33, 2}, {0x7C, 1, 4}, {0x80, 1, 2},
    {0x84, 1, 2}, {0x88, 1, 2}, {0x8C, 1, 2}, {0x90, 12, 2}, {0xB8, 2, 2},
};

bool AddPokemon(UnitList& u, size_t base) { return u.Add(base, kPokemonSize, kPokemon); }

const Run kGamedata[] = {{0x08, 1, 8}, {0x10, 6, 4}, {0x30, 1, 8}, {0x38, 1, 4}, {0x40, 1, 2}};

const Run kHero[] = {
    {0x000, 22, 2}, {0x02C, 1, 4}, {0x4C8, 0x20C, 2}, {0x8E2, 1, 2}, {0x8E4, 3, 4}, {0x8FA, 31, 2},
    {0x93A, 1, 2},  {0x93C, 3, 4}, {0x948, 4, 4},     {0x95A, 1, 2},
};
constexpr size_t kHeroParty = 0x30;

constexpr size_t kBoxStride = 0x170C;
const Run kBoxName[] = {{0, 10, 2}};
const Run kPcTail[] = {{0xB860, 470, 2}, {0xBC0C, 1, 4}};

const Run kMailbox[] = {{0x00, 33, 2}, {0x48, 1, 2}};

const Run kSodateya[] = {{0x02, 1, 2}, {0x04, 1, 4}};

const Run kMemoHead[] = {{0x00, 2, 2}};
const Run kMemoEntry[] = {{0x00, 2, 2}, {0x04, 2, 4}};

const Run kRule[] = {{0x00, 4, 2}, {0x08, 1, 4}, {0x14, 2, 2}, {0x1A, 2, 2}, {0x1E, 21, 2},
                     {0x48, 2, 4}, {0x50, 1, 2}, {0x8E, 1, 2}};

const Run kDarkPokemon[] = {{0x14, 4, 2}, {0x1C, 3, 4}, {0x28, 11, 2}, {0x3E, 1, 2}, {0x44, 1, 4}};

const Run kFlags[] = {{0x000, 0x6B0 / 4, 4}};

constexpr size_t kStageStride = 0x3D8;

const Run kAnywayHeader[] = {{0x04, 5, 4}, {0x18, 1, 2}};
constexpr uint32_t kFlorMagic = 0x464C4F52;
const Run kColsysState[] = {{0x00, 66, 2}};

const Run kTableHeader[] = {{0, static_cast<uint32_t>(kSectionBase / 2), 2}};

struct SectionDesc {
  int id;
  uint32_t sizeAt;
  uint32_t offsetAt;
  uint32_t size;
};
const SectionDesc kSections[] = {
    {1, 0x18, 0x38, 0x88},     {2, 0x1A, 0x3C, 0x978},   {3, 0x1C, 0x40, 0xBC50},  {10, 0x1E, 0x44, 0x4A},
    {11, 0x20, 0x48, 0xCC},    {12, 0x22, 0x4C, 0x1774}, {14, 0x24, 0x50, 0x364},  {15, 0x26, 0x54, 0x2400},
    {23, 0x28, 0x58, 0x6B4},   {16, 0x2A, 0x5C, 0xB},    {17, 0x2C, 0x60, 0x400},  {18, 0x2E, 0x64, 0x800},
    {19, 0x30, 0x68, 0x1000},  {20, 0x32, 0x6C, 0x2000}, {21, 0x34, 0x70, 0x2298}, {22, 0x36, 0x74, 0xC800},
};

bool AddMapState(UnitList& u, const uint8_t* data, size_t base, size_t size, ByteOrder from, std::string* error) {
  if (size < 0x20 || !u.Add(base, size, kAnywayHeader)) {
    return size < 0x20;
  }
  const uint8_t* h = data + base;
  if (std::memcmp(h, "ANSW", 4) != 0 || Read32(h + 0x04, from) == 0) {
    return true;
  }
  const uint32_t scriptSize = Read32(h + 0x08, from);
  size_t at = 0x20 + static_cast<size_t>(scriptSize);
  if (at + 8 > size || Read32(h + at, from) != kFlorMagic) {
    return true;
  }
  const uint32_t total = Read32(h + at + 4, from);
  if (total < 8 || at + total > size) {
    if (error) *error = "the map state's module block has a wrong size";
    return false;
  }
  if (!u.AddUnit(base + at, 4) || !u.AddUnit(base + at + 4, 4)) {
    return false;
  }
  const size_t end = at + total;
  at += 8;
  while (at + 8 <= end) {
    const uint32_t id = Read32(h + at, from);
    const uint32_t entrySize = Read32(h + at + 4, from);
    if (entrySize < 8 || at + entrySize > end) {
      if (error) *error = "the map state's module block is damaged";
      return false;
    }
    if (!u.AddUnit(base + at, 4) || !u.AddUnit(base + at + 4, 4)) {
      return false;
    }
    if (id == 0x14 && !u.Add(base + at + 8, entrySize - 8, kColsysState)) {
      return false;
    }
    at += entrySize;
  }
  return true;
}

bool CollectUnits(const uint8_t* data, ByteOrder from, UnitList& u, std::string* error) {
  if (!u.Add(0, kSectionBase, kTableHeader)) {
    return false;
  }
  for (const SectionDesc& d : kSections) {
    const size_t off = kSectionBase + (Read16(data + d.offsetAt, from) | static_cast<uint32_t>(Read16(data + d.offsetAt + 2, from)) << 16);
    size_t size = Read16(data + d.sizeAt, from);
    if (d.id == 3) {
      size |= static_cast<size_t>(Read16(data + 0x80, from)) << 16;
    }
    size = std::min<size_t>(size, d.size);
    if (off + size > kDataSize) {
      if (error) *error = "section " + std::to_string(d.id) + " lies outside the save data";
      return false;
    }
    bool ok = true;
    switch (d.id) {
    case 1:
      ok = u.Add(off, size, kGamedata);
      break;
    case 2:
      ok = u.Add(off, size, kHero);
      for (int i = 0; ok && i < kPartySize; i++) {
        if (kHeroParty + (i + 1) * kPokemonSize <= size) ok = AddPokemon(u, off + kHeroParty + i * kPokemonSize);
      }
      break;
    case 3:
      for (int b = 0; ok && b < kBoxCount; b++) {
        const size_t box = static_cast<size_t>(b) * kBoxStride;
        if (box + kBoxStride > size) break;
        ok = u.Add(off + box, kBoxStride, kBoxName);
        for (int j = 0; ok && j < kBoxSlots; j++) ok = AddPokemon(u, off + box + 0x14 + j * kPokemonSize);
      }
      if (ok) ok = u.Add(off, size, kPcTail);
      break;
    case 10:
      ok = u.Add(off, size, kMailbox);
      break;
    case 11:
      ok = u.Add(off, size, kSodateya) && (size < 8 + kPokemonSize || AddPokemon(u, off + 8));
      break;
    case 12:
      ok = u.Add(off, size, kMemoHead);
      for (size_t e = 4; ok && e + 12 <= size; e += 12) ok = u.Add(off + e, 12, kMemoEntry);
      break;
    case 14:
      for (size_t r = 0; ok && r + 0x90 <= size && r < 6 * 0x90; r += 0x90) ok = u.Add(off + r, 0x90, kRule);
      break;
    case 15:
      for (size_t e = 0; ok && e + kShadowEntrySize <= size; e += kShadowEntrySize)
        ok = u.Add(off + e, kShadowEntrySize, kDarkPokemon);
      break;
    case 23:
      ok = u.Add(off, size, kFlags);
      break;
    case 21:
      for (size_t s = 0; ok && s + kStageStride <= size; s += kStageStride)
        for (int j = 0; ok && j < 5; j++) ok = AddPokemon(u, off + s + j * kPokemonSize);
      break;
    case 22:
      ok = AddMapState(u, data, off, size, from, error);
      break;
    default:
      break;
    }
    if (!ok) {
      if (error && error->empty()) *error = "section " + std::to_string(d.id) + " overlaps another one";
      return false;
    }
  }
  return true;
}

void SlotHeaderSum(const uint8_t* slot, uint32_t* sum) {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += slot[i];
  *sum = s;
}

bool DecryptedValid(const uint8_t* slot, uint8_t* data, ByteOrder order) {
  if (!DecryptSaveData(data, order)) {
    return false;
  }
  if (Read16(data + 0x18, order) != 0x88) {
    return true;
  }
  const uint32_t at = kSectionBase + (Read16(data + 0x38, order) | static_cast<uint32_t>(Read16(data + 0x3A, order)) << 16);
  uint32_t sum;
  SlotHeaderSum(slot, &sum);
  return at + 0x3C <= kDataSize && Read32(data + at + 0x38, order) == sum;
}

uint32_t BannerSum(const uint8_t* file, ByteOrder order) {
  uint32_t s = 0;
  for (size_t i = 0; i < 0xE90; i++) s += Read32(file + i * 4, order);
  return s;
}

void Write32(uint8_t* p, uint32_t v, ByteOrder order) {
  for (int i = 0; i < 4; i++) {
    const int shift = order == ByteOrder::GameCube ? 24 - 8 * i : 8 * i;
    p[i] = static_cast<uint8_t>(v >> shift);
  }
}

}

const char* SlotResultName(SlotResult r) {
  switch (r) {
  case SlotResult::Unwritten: return "not written";
  case SlotResult::Empty: return "empty";
  case SlotResult::Converted: return "converted";
  case SlotResult::AlreadyTarget: return "already in the target order";
  case SlotResult::Invalid: return "damaged or half-written (only its save counter converted)";
  }
  return "?";
}

bool SwapSaveData(uint8_t* data, ByteOrder from, std::string* error) {
  UnitList units(kDataSize);
  if (!CollectUnits(data, from, units, error)) {
    return false;
  }
  units.Apply(data);
  return true;
}

bool SlotValid(const uint8_t* slot, ByteOrder order) {
  if (!(slot[0] == 1 && slot[1] == 1 && slot[2] == 1)) {
    return false;
  }
  std::vector<uint8_t> data(slot + kSlotHeaderSize, slot + kSlotHeaderSize + kDataSize);
  return DecryptedValid(slot, data.data(), order);
}

SlotResult ConvertSlot(uint8_t* slot, ByteOrder to, std::string* error) {
  const ByteOrder from = Other(to);
  if (!(slot[0] == 1 && slot[1] == 1 && slot[2] == 1)) {
    return SlotResult::Unwritten;
  }
  std::vector<uint8_t> data(slot + kSlotHeaderSize, slot + kSlotHeaderSize + kDataSize);
  if (DecryptedValid(slot, data.data(), from)) {
    std::string why;
    if (!SwapSaveData(data.data(), from, &why)) {
      if (error) *error = why;
      std::reverse(slot + 4, slot + 8);
      return SlotResult::Invalid;
    }
    EncryptSaveData(data.data(), to);
    std::memcpy(slot + kSlotHeaderSize, data.data(), kDataSize);
    std::reverse(slot + 4, slot + 8);
    return SlotResult::Converted;
  }
  if (SlotValid(slot, to)) {
    return SlotResult::AlreadyTarget;
  }
  if (Read32(slot + 4, from) == 0) {
    return SlotResult::Empty;
  }
  std::reverse(slot + 4, slot + 8);
  return SlotResult::Invalid;
}

bool ConvertBanner(uint8_t* file, ByteOrder to) {
  const ByteOrder from = Other(to);
  if (BannerSum(file, from) == Read32(file + 0x3A40, from)) {
    Write32(file + 0x3A40, BannerSum(file, to), to);
  }
  return BannerSum(file, to) == Read32(file + 0x3A40, to);
}

bool ConvertFile(std::vector<uint8_t>& file, ByteOrder to, ConvertReport& report, std::string& error) {
  report = ConvertReport{};
  if (file.size() != kFileSize) {
    error = "not a Pokemon XD save: " + std::to_string(file.size()) + " bytes (XD's is " + std::to_string(kFileSize) + ")";
    return false;
  }
  report.bannerOk = ConvertBanner(file.data(), to);
  if (!report.bannerOk) {
    report.warnings.push_back("the banner block's checksum is wrong in both orders (the game just redraws the banner)");
  }
  for (int s = 0; s < kSlotCount; s++) {
    std::string why;
    report.slots[s] = ConvertSlot(file.data() + SlotOffset(s), to, &why);
    if (!why.empty()) {
      report.warnings.push_back("slot " + std::to_string(s) + ": " + why);
    }
  }
  return true;
}

std::vector<uint8_t> GameViewOf(const std::vector<uint8_t>& stored) {
  std::vector<uint8_t> view = stored;
  if (view.size() != kFileSize) {
    return view;
  }
  ConvertBanner(view.data(), ByteOrder::PC);
  for (int s = 0; s < kSlotCount; s++) {
    ConvertSlot(view.data() + SlotOffset(s), ByteOrder::PC);
  }
  return view;
}

void ApplyGameWrite(std::vector<uint8_t>& stored, size_t offset, const uint8_t* bytes, size_t length,
                    std::vector<std::pair<size_t, size_t>>& changed) {
  changed.clear();
  if (stored.size() != kFileSize || offset >= kFileSize) {
    return;
  }
  length = std::min(length, kFileSize - offset);
  std::vector<uint8_t> view = GameViewOf(stored);
  std::memcpy(view.data() + offset, bytes, length);
  const size_t starts[3] = {0, SlotOffset(0), SlotOffset(1)};
  const size_t sizes[3] = {kBannerSize, kSlotSize, kSlotSize};
  for (int p = 0; p < 3; p++) {
    if (offset >= starts[p] + sizes[p] || offset + length <= starts[p]) {
      continue;
    }
    uint8_t* part = view.data() + starts[p];
    if (p == 0) {
      ConvertBanner(view.data(), ByteOrder::GameCube);
    } else {
      ConvertSlot(part, ByteOrder::GameCube);
    }
    std::memcpy(stored.data() + starts[p], part, sizes[p]);
    changed.emplace_back(starts[p], sizes[p]);
  }
}

}
