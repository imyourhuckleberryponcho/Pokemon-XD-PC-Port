#ifndef XD_SAVE_H
#define XD_SAVE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::save {

constexpr size_t kFileSize = 0x56000;
constexpr size_t kBannerSize = 0x6000;
constexpr size_t kSlotSize = 0x28000;
constexpr int kSlotCount = 2;
constexpr size_t kSlotHeaderSize = 8;
constexpr size_t kDataSize = 0x27FD0;
constexpr size_t kSectionBase = 0xA0;
constexpr size_t kPokemonSize = 0xC4;
constexpr int kPartySize = 6;
constexpr int kBoxCount = 8;
constexpr int kBoxSlots = 30;
constexpr int kShadowEntries = 0x80;
constexpr size_t kShadowEntrySize = 0x48;

constexpr size_t SlotOffset(int slot) { return kBannerSize + static_cast<size_t>(slot) * kSlotSize; }

enum class ByteOrder {
  GameCube,
  PC,
};
const char* ByteOrderName(ByteOrder order);

void KeyStep(uint16_t key[4]);
void ComputeChecksums(const uint8_t* data, ByteOrder order, uint32_t sums[4]);
void StoredChecksums(const uint8_t* data, ByteOrder order, uint32_t sums[4]);
bool DecryptSaveData(uint8_t* data, ByteOrder order);
void EncryptSaveData(uint8_t* data, ByteOrder order);

uint16_t Read16(const uint8_t* p, ByteOrder order);
uint32_t Read32(const uint8_t* p, ByteOrder order);
uint64_t Read64(const uint8_t* p, ByteOrder order);
double ReadF64(const uint8_t* p, ByteOrder order);
std::u16string ReadString(const uint8_t* p, size_t max, ByteOrder order);
std::string ToUtf8(const std::u16string& s);

struct Move {
  uint16_t id = 0;
  uint8_t pp = 0;
  uint8_t ppUps = 0;
};

struct Pokemon {
  std::array<uint8_t, kPokemonSize> raw{};
  uint16_t species = 0;
  uint16_t heldItem = 0;
  uint16_t hp = 0;
  uint16_t friendship = 0;
  uint16_t metLocation = 0;
  uint8_t metLevel = 0;
  uint8_t ball = 0;
  uint8_t otGender = 0;
  uint8_t level = 0;
  uint8_t contestLuster = 0;
  uint8_t pokerus = 0;
  uint8_t marks = 0;
  uint8_t status = 0;
  uint8_t flags = 0;
  uint32_t exp = 0;
  uint32_t otId = 0;
  uint32_t pid = 0;
  std::u16string otName;
  std::u16string nickname;
  std::u16string nicknameOrg;
  uint32_t ribbons = 0;
  std::array<Move, 4> moves{};
  std::array<uint16_t, 6> stats{};
  std::array<uint16_t, 6> evs{};
  std::array<uint8_t, 6> ivs{};
  std::array<uint8_t, 10> contest{};
  uint16_t shadowId = 0;

  bool IsEmpty() const { return species == 0; }
  bool IsEgg() const { return (flags & 0x80) != 0; }
  bool SecondAbility() const { return (flags & 0x40) != 0; }
  uint16_t OtTrainerId() const { return static_cast<uint16_t>(otId & 0xFFFF); }
  uint16_t OtSecretId() const { return static_cast<uint16_t>(otId >> 16); }
  uint8_t Nature() const { return static_cast<uint8_t>(pid % 25); }
  bool IsShiny() const;
};

struct ShadowEntry {
  std::array<uint8_t, kShadowEntrySize> raw{};
  uint8_t status = 0;
  uint16_t species = 0;
  int32_t heartGauge = 0;
  uint32_t pid = 0;
  bool Snagged() const { return status >= 3; }
  bool Purified() const { return status == 4; }
};

struct ItemSlot {
  uint16_t item = 0;
  uint16_t count = 0;
};

struct Box {
  std::u16string name;
  std::array<Pokemon, kBoxSlots> slots{};
};

struct Section {
  int id = 0;
  const char* name = "";
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t expectedSize = 0;
};

struct Save {
  ByteOrder order = ByteOrder::GameCube;
  int slot = -1;
  int32_t saveCount = 0;
  std::vector<uint8_t> slotBytes;
  std::vector<Section> sections;

  uint64_t memcardId = 0;
  uint32_t gameSaveCount = 0;
  uint32_t floorId = 0;
  double playTimeSeconds = 0;
  uint8_t optionNoVibration = 0, optionAudio = 0;
  uint32_t sysHeadChecksum = 0;

  std::u16string trainerName;
  uint32_t trainerOtId = 0;
  uint8_t trainerGender = 0;
  uint32_t money = 0;
  uint32_t coupons = 0;
  uint32_t couponsTotal = 0;
  std::array<Pokemon, kPartySize> party{};
  std::vector<ItemSlot> items, keyItems, balls, tmHm, berries, colognes, battleCds;

  std::array<Box, kBoxCount> boxes{};
  std::vector<ItemSlot> pcItems;
  Pokemon dayCare;
  std::array<std::array<Pokemon, 5>, 9> chambers{};
  std::array<ShadowEntry, kShadowEntries> shadows{};

  uint16_t TrainerId() const { return static_cast<uint16_t>(trainerOtId & 0xFFFF); }
  uint16_t SecretId() const { return static_cast<uint16_t>(trainerOtId >> 16); }
  const ShadowEntry* ShadowOf(const Pokemon& p) const;
  const uint8_t* SectionData(int id) const;
};

struct SlotReport {
  bool written = false;
  int32_t saveCount = 0;
  bool checksumOk = false;
  bool headerSumOk = false;
  ByteOrder order = ByteOrder::GameCube;
};

struct ReadReport {
  std::array<SlotReport, kSlotCount> slots{};
  bool bannerSumOk = false;
  std::vector<std::string> warnings;
};

bool ReadSave(const std::vector<uint8_t>& file, Save& out, ReadReport& report, std::string& error);

Pokemon ParsePokemon(const uint8_t* p, ByteOrder order);

}

#endif
