#include <gen3_pokemon.h>

#include <cstring>

namespace engine::gen3 {

const uint8_t kSubstructOrder[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
    {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
    {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
};

uint16_t DataChecksum(const PlainMon& mon) {
  uint16_t sum = 0;
  for (size_t i = 0; i < kDataSize; i += 2) {
    sum = static_cast<uint16_t>(sum + mon.Get16(kDataOffset + i));
  }
  return sum;
}

PlainMon Decrypt(const uint8_t* stored, size_t size) {
  PlainMon m;
  std::memcpy(m.b.data(), stored, size < kPartyMonSize ? size : kPartyMonSize);
  const uint32_t key = m.Pid() ^ m.OtId();
  uint8_t data[kDataSize];
  for (size_t i = 0; i < kDataSize; i += 4) {
    const uint32_t w = m.Get32(kDataOffset + i) ^ key;
    for (int k = 0; k < 4; k++) {
      data[i + k] = static_cast<uint8_t>(w >> (8 * k));
    }
  }
  const uint8_t* order = kSubstructOrder[m.Pid() % 24];
  for (int pos = 0; pos < 4; pos++) {
    std::memcpy(m.b.data() + kDataOffset + order[pos] * 12, data + pos * 12, 12);
  }
  m.checksumOk = DataChecksum(m) == m.Get16(0x1C);
  return m;
}

void Encrypt(const PlainMon& mon, uint8_t* stored, size_t size) {
  PlainMon m = mon;
  m.Set16(0x1C, DataChecksum(m));
  const uint8_t* order = kSubstructOrder[m.Pid() % 24];
  uint8_t data[kDataSize];
  for (int pos = 0; pos < 4; pos++) {
    std::memcpy(data + pos * 12, m.b.data() + kDataOffset + order[pos] * 12, 12);
  }
  const uint32_t key = m.Pid() ^ m.OtId();
  for (size_t i = 0; i < kDataSize; i += 4) {
    const uint32_t w = (static_cast<uint32_t>(data[i]) | static_cast<uint32_t>(data[i + 1]) << 8 |
                        static_cast<uint32_t>(data[i + 2]) << 16 | static_cast<uint32_t>(data[i + 3]) << 24) ^
                       key;
    m.Set32(kDataOffset + i, w);
  }
  std::memcpy(stored, m.b.data(), size < kPartyMonSize ? size : kPartyMonSize);
}

bool IsShiny(uint32_t pid, uint32_t otId) {
  return ((otId >> 16) ^ (otId & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
}

}
