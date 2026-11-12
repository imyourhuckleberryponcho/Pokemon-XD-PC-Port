#include <gen3_save.h>

#include <cstring>

namespace engine::gen3 {

namespace {

const Layout kLayouts[] = {
    {Game::RubySapphire, 0x234, 0x238, 0x938, 0x3A8C, 0x3110, 0x3AC0, 0x890},
    {Game::Emerald, 0x234, 0x238, 0x988, 0x3B24, 0x31A8, 0x3D88, 0xF2C},
    {Game::FireRedLeafGreen, 0x034, 0x038, 0x5F8, 0x3A18, 0x309C, 0x3D68, 0xF24},
};
constexpr uint32_t kStorageSize = 0x83D0;
constexpr uint32_t kStorageMons = 4;
constexpr uint32_t kStorageNames = kStorageMons + kBoxCount * kBoxSlots * kBoxMonSize;

uint16_t Rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}
void Wr16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void Wr32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

uint16_t PieceSize(uint32_t structSize, int piece) {
  const uint32_t start = static_cast<uint32_t>(piece) * kSectorDataSize;
  if (structSize <= start) {
    return 0;
  }
  const uint32_t left = structSize - start;
  return static_cast<uint16_t>(left < kSectorDataSize ? left : kSectorDataSize);
}
uint16_t ExactSize(const Layout& l, int id) {
  if (id == 0) {
    return PieceSize(l.sb2Size, 0);
  }
  if (id <= 4) {
    return PieceSize(l.sb1Size, id - 1);
  }
  return PieceSize(kStorageSize, id - 5);
}

struct SlotScan {
  bool present = false;
  bool valid = false;
  uint32_t counter = 0;
  int firstSector = -1;
  std::array<const uint8_t*, kSectorsPerSlot> sector{};
  std::string problem;
};

SlotScan ScanSlot(const std::vector<uint8_t>& bytes, int slot) {
  SlotScan s;
  int seen = 0;
  bool counterSet = false;
  for (int i = 0; i < kSectorsPerSlot; i++) {
    const uint8_t* sec = bytes.data() + (static_cast<size_t>(slot) * kSectorsPerSlot + i) * kSectorSize;
    if (Rd32(sec + 0xFF8) != kSectorSignature) {
      continue;
    }
    s.present = true;
    const uint16_t id = Rd16(sec + 0xFF4);
    if (id >= kSectorsPerSlot || s.sector[id] != nullptr) {
      s.problem = "a section number is wrong or appears twice";
      return s;
    }
    const uint32_t counter = Rd32(sec + 0xFFC);
    if (counterSet && counter != s.counter) {
      s.problem = "its sectors come from different saves (counters differ)";
      return s;
    }
    s.counter = counter;
    counterSet = true;
    s.sector[id] = sec;
    if (id == 0) {
      s.firstSector = i;
    }
    seen++;
  }
  if (!s.present) {
    s.problem = "empty (never written)";
    return s;
  }
  if (seen != kSectorsPerSlot) {
    s.problem = "some of its sectors are missing";
    return s;
  }
  s.valid = true;
  return s;
}

uint16_t MatchingLength(const uint8_t* sec, const Layout& l, int id) {
  const uint16_t stored = Rd16(sec + 0xFF6);
  const uint16_t exact = ExactSize(l, id);
  if (exact != 0 && SectionChecksum(sec, exact) == stored) {
    return exact;
  }
  if (SectionChecksum(sec, kSectorDataSize) == stored) {
    return static_cast<uint16_t>(kSectorDataSize);
  }
  return 0;
}

int LayoutScore(const Save& s) {
  const Layout& l = s.L();
  const int count = s.PartyCount();
  if (count < 1 || count > kPartySize) {
    return -1;
  }
  int score = 0;
  int bad = 0;
  for (int i = 0; i < count; i++) {
    const PlainMon m = s.PartyMon(i);
    if (!m.checksumOk) {
      bad++;
    } else if (m.IsEmpty()) {
      return -1;
    }
  }
  if (bad == count) {
    return -1;
  }
  score += 10;
  const uint8_t* seen = s.Sb2(0x5C);
  if (std::memcmp(seen, s.Sb1(l.seen1), kDexFlagBytes) == 0 && std::memcmp(seen, s.Sb1(l.seen2), kDexFlagBytes) == 0) {
    score += 10;
  }
  return score;
}

bool DexBit(const uint8_t* flags, uint16_t national) {
  const uint16_t n = static_cast<uint16_t>(national - 1);
  return (flags[n / 8] >> (n % 8) & 1) != 0;
}
void SetDexBit(uint8_t* flags, uint16_t national) {
  const uint16_t n = static_cast<uint16_t>(national - 1);
  flags[n / 8] = static_cast<uint8_t>(flags[n / 8] | 1 << (n % 8));
}

}

const char* GameName(Game game) {
  switch (game) {
  case Game::RubySapphire:
    return "Ruby/Sapphire";
  case Game::Emerald:
    return "Emerald";
  case Game::FireRedLeafGreen:
    return "FireRed/LeafGreen";
  default:
    return "unknown";
  }
}

const Layout* LayoutFor(Game game) {
  for (const Layout& l : kLayouts) {
    if (l.game == game) {
      return &l;
    }
  }
  return &kLayouts[1];
}

uint16_t SectionChecksum(const uint8_t* data, size_t length) {
  uint32_t sum = 0;
  for (size_t i = 0; i + 4 <= length; i += 4) {
    sum += Rd32(data + i);
  }
  return static_cast<uint16_t>((sum >> 16) + sum);
}

uint32_t Save::TrainerOtId() const { return Rd32(Sb2(0x0A)); }

PlainMon Save::PartyMon(int index) const { return Decrypt(Sb1(L().party + index * kPartyMonSize), kPartyMonSize); }

void Save::SetPartyMon(int index, const PlainMon& mon) { Encrypt(mon, Sb1(L().party + index * kPartyMonSize), kPartyMonSize); }

PlainMon Save::BoxMon(int box, int slot) const {
  uint8_t rec[kBoxMonSize];
  const uint32_t at = kStorageMons + static_cast<uint32_t>(box * kBoxSlots + slot) * kBoxMonSize;
  for (uint32_t i = 0; i < kBoxMonSize; i++) {
    rec[i] = *Storage(at + i);
  }
  return Decrypt(rec, kBoxMonSize);
}

const uint8_t* Save::BoxName(int box) const { return Storage(kStorageNames + static_cast<uint32_t>(box) * 9); }

bool Save::DexSeen(uint16_t national) const { return national >= 1 && national <= kDexFlagBytes * 8 && DexBit(Sb2(0x5C), national); }
bool Save::DexOwned(uint16_t national) const { return national >= 1 && national <= kDexFlagBytes * 8 && DexBit(Sb2(0x28), national); }

void Save::RegisterInDex(uint16_t national, uint32_t pid) {
  if (national < 1 || national > kDexFlagBytes * 8) {
    return;
  }
  if (!DexSeen(national)) {
    SetDexBit(Sb2(0x5C), national);
    SetDexBit(Sb1(L().seen1), national);
    SetDexBit(Sb1(L().seen2), national);
    if (national == kNationalUnown) {
      Wr32(Sb2(0x1C), pid);
    }
    if (national == kNationalSpinda) {
      Wr32(Sb2(0x20), pid);
    }
  }
  if (!DexOwned(national)) {
    SetDexBit(Sb2(0x28), national);
    if (national == kNationalUnown) {
      Wr32(Sb2(0x1C), pid);
    }
    if (national == kNationalSpinda) {
      Wr32(Sb2(0x20), pid);
    }
  }
}

bool ReadSave(const std::vector<uint8_t>& bytes, Game forced, Save& out, std::string& error,
              std::vector<std::string>& warnings) {
  if (bytes.size() < kFlashSize || bytes.size() > kFlashSize + 0x100) {
    error = "not a GBA Pokemon save: " + std::to_string(bytes.size()) +
            " bytes (a Ruby/Sapphire/Emerald/FireRed/LeafGreen save is 131072 bytes, 128 KB)";
    return false;
  }
  if (bytes.size() != kFlashSize) {
    warnings.push_back(std::to_string(bytes.size() - kFlashSize) +
                       " extra bytes after the 128 KB save (an emulator's clock data?): kept as they are");
  }

  SlotScan slots[2] = {ScanSlot(bytes, 0), ScanSlot(bytes, 1)};
  int best = -1;
  if (slots[0].valid && slots[1].valid) {
    const uint32_t a = slots[0].counter, b = slots[1].counter;
    if ((a == 0xFFFFFFFFu && b == 0) || (b == 0xFFFFFFFFu && a == 0)) {
      best = a == 0 ? 0 : 1;
    } else {
      best = a >= b ? 0 : 1;
    }
  } else if (slots[0].valid) {
    best = 0;
  } else if (slots[1].valid) {
    best = 1;
  }
  if (best < 0) {
    if (!slots[0].present && !slots[1].present) {
      error = "this file holds no Pokemon save (no save sectors found: a blank or different kind of save)";
    } else {
      error = "the save is damaged: slot A is " + slots[0].problem + ", slot B is " + slots[1].problem;
    }
    return false;
  }
  for (int i = 0; i < 2; i++) {
    if (i != best && slots[i].present && !slots[i].valid) {
      warnings.push_back(std::string("the older save slot (") + (i == 0 ? "A" : "B") + ") is " + slots[i].problem +
                         "; the newer one is fine");
    }
  }

  Save s;
  s.file = bytes;
  s.slot = best;
  s.counter = slots[best].counter;
  s.firstSector = slots[best].firstSector;
  for (int id = 0; id < kSectorsPerSlot; id++) {
    std::memcpy(s.sections[id].data(), slots[best].sector[id], kSectorDataSize);
  }

  const uint32_t hint = Rd32(s.Sb2(0xAC));
  const Game hinted = hint == 0 ? Game::RubySapphire : hint == 1 ? Game::FireRedLeafGreen : Game::Emerald;
  Game chosen = Game::Unknown;
  int bestScore = -1;
  for (Game g : {hinted, Game::RubySapphire, Game::Emerald, Game::FireRedLeafGreen}) {
    s.game = g;
    const int score = LayoutScore(s);
    if (score > bestScore) {
      bestScore = score;
      chosen = g;
    }
  }
  if (forced != Game::Unknown) {
    s.game = forced;
    const int score = LayoutScore(s);
    if (score < 0) {
      error = std::string("this save doesn't look like ") + GameName(forced) +
              " (its party can't be read that way)" +
              (chosen != Game::Unknown && bestScore >= 0 ? std::string("; it looks like ") + GameName(chosen) : "");
      return false;
    }
    if (chosen != forced && bestScore > score) {
      warnings.push_back(std::string("the save fits ") + GameName(chosen) + " better than " + GameName(forced) +
                         " (used " + GameName(forced) + " as asked)");
    }
  } else {
    if (bestScore < 0) {
      error = "the save's party can't be read with any of the GBA games' layouts (an empty party, a damaged "
              "save, or not Ruby/Sapphire/Emerald/FireRed/LeafGreen)";
      return false;
    }
    if (bestScore < 20) {
      warnings.push_back("the save's three Pokedex copies don't agree; guessed it is " + std::string(GameName(chosen)));
    }
    s.game = chosen;
  }

  for (int id = 0; id < kSectorsPerSlot; id++) {
    const uint16_t len = MatchingLength(slots[best].sector[id], s.L(), id);
    if (len == 0) {
      error = "the save is damaged: section " + std::to_string(id) +
              " of the newest slot fails its checksum (a game would use the older slot or call the save "
              "corrupted; load and save it in the game or emulator once, then try again)";
      return false;
    }
    s.checksumLength[id] = len;
  }
  out = std::move(s);
  return true;
}

std::vector<uint8_t> WriteSave(const Save& save, std::string& error) {
  if (save.slot < 0 || save.counter % 2 != static_cast<uint32_t>(save.slot)) {
    error = "unexpected save: its newest slot doesn't match its save counter (a GBA game would read the "
            "other slot); refusing to write";
    return {};
  }
  std::vector<uint8_t> out = save.file;
  const uint32_t counter = save.counter + 1;
  const int slot = static_cast<int>(counter % 2);
  const int first = (save.firstSector + 1) % kSectorsPerSlot;
  for (int id = 0; id < kSectorsPerSlot; id++) {
    const int physical = (id + first) % kSectorsPerSlot;
    uint8_t* sec = out.data() + (static_cast<size_t>(slot) * kSectorsPerSlot + physical) * kSectorSize;
    std::memset(sec, 0, kSectorSize);
    std::memcpy(sec, save.sections[id].data(), kSectorDataSize);
    Wr16(sec + 0xFF4, static_cast<uint16_t>(id));
    Wr16(sec + 0xFF6, SectionChecksum(save.sections[id].data(), save.checksumLength[id]));
    Wr32(sec + 0xFF8, kSectorSignature);
    Wr32(sec + 0xFFC, counter);
  }
  return out;
}

}
