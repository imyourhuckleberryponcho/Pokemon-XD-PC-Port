#ifndef ENGINE_GEN3_POKEMON_H
#define ENGINE_GEN3_POKEMON_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::gen3 {

constexpr size_t kBoxMonSize = 80;
constexpr size_t kPartyMonSize = 100;
constexpr size_t kDataOffset = 0x20;
constexpr size_t kDataSize = 48;

extern const uint8_t kSubstructOrder[24][4];

struct PlainMon {
  std::array<uint8_t, kPartyMonSize> b{};
  bool checksumOk = true;

  uint32_t Pid() const { return Get32(0x00); }
  uint32_t OtId() const { return Get32(0x04); }
  uint8_t Flags() const { return b[0x13]; }
  bool IsBadEgg() const { return (b[0x13] & 1) != 0; }
  bool HasSpecies() const { return (b[0x13] & 2) != 0; }
  uint16_t Species() const { return Get16(0x20); }
  uint16_t HeldItem() const { return Get16(0x22); }
  uint32_t Exp() const { return Get32(0x24); }
  uint32_t Ivs() const { return Get32(0x48); }
  bool IsEgg() const { return (Ivs() >> 30 & 1) != 0; }
  uint8_t Level() const { return b[0x54]; }
  bool IsEmpty() const { return !HasSpecies() && !IsBadEgg(); }

  uint16_t Get16(size_t at) const { return static_cast<uint16_t>(b[at] | b[at + 1] << 8); }
  uint32_t Get32(size_t at) const {
    return static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 | static_cast<uint32_t>(b[at + 2]) << 16 |
           static_cast<uint32_t>(b[at + 3]) << 24;
  }
  void Set16(size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
  }
  void Set32(size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) {
      b[at + i] = static_cast<uint8_t>(v >> (8 * i));
    }
  }
};

uint16_t DataChecksum(const PlainMon& mon);

PlainMon Decrypt(const uint8_t* stored, size_t size);
void Encrypt(const PlainMon& mon, uint8_t* stored, size_t size);

bool IsShiny(uint32_t pid, uint32_t otId);

}

#endif
