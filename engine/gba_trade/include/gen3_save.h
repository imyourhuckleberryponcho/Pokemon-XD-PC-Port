#ifndef ENGINE_GEN3_SAVE_H
#define ENGINE_GEN3_SAVE_H

#include <gen3_pokemon.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::gen3 {

constexpr size_t kFlashSize = 0x20000;
constexpr size_t kSectorSize = 0x1000;
constexpr size_t kSectorDataSize = 0xF80;
constexpr int kSectorsPerSlot = 14;
constexpr uint32_t kSectorSignature = 0x08012025;
constexpr int kPartySize = 6;
constexpr int kBoxCount = 14;
constexpr int kBoxSlots = 30;
constexpr int kGiftRibbons = 11;
constexpr int kDexFlagBytes = 52;
constexpr uint16_t kNationalUnown = 201;
constexpr uint16_t kNationalSpinda = 327;

enum class Game { Unknown, RubySapphire, Emerald, FireRedLeafGreen };
const char* GameName(Game game);

struct Layout {
  Game game;
  uint32_t partyCount, party, seen1, seen2, giftRibbons;
  uint32_t sb1Size, sb2Size;
};
const Layout* LayoutFor(Game game);

struct Save {
  Game game = Game::Unknown;
  std::vector<uint8_t> file;
  int slot = -1;
  uint32_t counter = 0;
  int firstSector = 0;
  std::array<std::array<uint8_t, kSectorDataSize>, kSectorsPerSlot> sections{};
  std::array<uint16_t, kSectorsPerSlot> checksumLength{};

  const Layout& L() const { return *LayoutFor(game); }
  uint8_t* Sb1(uint32_t offset) { return Piece(1, offset); }
  const uint8_t* Sb1(uint32_t offset) const { return const_cast<Save*>(this)->Piece(1, offset); }
  uint8_t* Sb2(uint32_t offset) { return sections[0].data() + offset; }
  const uint8_t* Sb2(uint32_t offset) const { return sections[0].data() + offset; }
  uint8_t* Storage(uint32_t offset) { return Piece(5, offset); }
  const uint8_t* Storage(uint32_t offset) const { return const_cast<Save*>(this)->Piece(5, offset); }

  const uint8_t* TrainerName() const { return Sb2(0x00); }
  uint8_t TrainerGender() const { return *Sb2(0x08); }
  uint32_t TrainerOtId() const;

  int PartyCount() const { return *Sb1(L().partyCount); }
  PlainMon PartyMon(int index) const;
  void SetPartyMon(int index, const PlainMon& mon);
  PlainMon BoxMon(int box, int slot) const;
  const uint8_t* BoxName(int box) const;

  uint8_t* GiftRibbons() { return Sb1(L().giftRibbons); }
  const uint8_t* GiftRibbons() const { return Sb1(L().giftRibbons); }

  bool DexSeen(uint16_t national) const;
  bool DexOwned(uint16_t national) const;
  void RegisterInDex(uint16_t national, uint32_t pid);

private:
  uint8_t* Piece(int firstSection, uint32_t offset) {
    return sections[firstSection + offset / kSectorDataSize].data() + offset % kSectorDataSize;
  }
};

bool ReadSave(const std::vector<uint8_t>& bytes, Game forced, Save& out, std::string& error,
              std::vector<std::string>& warnings);

std::vector<uint8_t> WriteSave(const Save& save, std::string& error);

uint16_t SectionChecksum(const uint8_t* data, size_t length);

}

#endif
