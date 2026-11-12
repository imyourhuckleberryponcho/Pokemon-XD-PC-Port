#include <xd_save.h>

namespace xd::save {

namespace {

constexpr size_t kWords = kDataSize / 2;
constexpr size_t kSumBlockWords = 0x4FFA;

void Write16(uint8_t* p, uint16_t v, ByteOrder order) {
  if (order == ByteOrder::GameCube) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
  } else {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
}

constexpr int kSumWord[4] = {10, 8, 6, 4};

}

const char* ByteOrderName(ByteOrder order) {
  return order == ByteOrder::GameCube ? "GameCube (big-endian)" : "PC (little-endian)";
}

uint16_t Read16(const uint8_t* p, ByteOrder order) {
  return order == ByteOrder::GameCube ? static_cast<uint16_t>(p[0] << 8 | p[1]) : static_cast<uint16_t>(p[1] << 8 | p[0]);
}

uint32_t Read32(const uint8_t* p, ByteOrder order) {
  if (order == ByteOrder::GameCube) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
  }
  return static_cast<uint32_t>(p[3]) << 24 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[0];
}

void KeyStep(uint16_t key[4]) {
  static const uint32_t primes[4] = {0x43, 0x29, 0x17, 0x13};
  uint16_t v[4];
  for (int j = 0; j < 4; j++) {
    v[j] = static_cast<uint16_t>(key[j] + primes[j]);
  }
  key[0] = static_cast<uint16_t>((v[0] & 0xF) | ((v[1] << 4) & 0xF0) | ((v[2] << 8) & 0xF00) | ((v[3] << 12) & 0xF000));
  key[1] = static_cast<uint16_t>(((v[0] >> 4) & 0xF) | (v[1] & 0xF0) | ((v[2] << 4) & 0xF00) | ((v[3] << 8) & 0xF000));
  key[2] = static_cast<uint16_t>(((v[0] & 0xF00) >> 8) | ((v[1] & 0xF00) >> 4) | (v[2] & 0xF00) | ((v[3] << 4) & 0xF000));
  key[3] = static_cast<uint16_t>(((v[0] >> 12) & 0xF) | ((v[1] >> 8) & 0xF0) | ((v[2] >> 4) & 0xF00) | (v[3] & 0xF000));
}

void ComputeChecksums(const uint8_t* data, ByteOrder order, uint32_t sums[4]) {
  for (int i = 0; i < 4; i++) {
    uint32_t s = 0;
    for (size_t k = i * kSumBlockWords; k < (i + 1) * kSumBlockWords; k++) {
      if (k >= 4 && k < 12) {
        continue;
      }
      s += Read16(data + k * 2, order);
    }
    sums[i] = s;
  }
}

void StoredChecksums(const uint8_t* data, ByteOrder order, uint32_t sums[4]) {
  for (int i = 0; i < 4; i++) {
    const size_t w = static_cast<size_t>(kSumWord[i]);
    sums[i] = Read16(data + w * 2, order) | static_cast<uint32_t>(Read16(data + (w + 1) * 2, order)) << 16;
  }
}

bool DecryptSaveData(uint8_t* data, ByteOrder order) {
  uint16_t key[4];
  for (int k = 0; k < 4; k++) {
    key[k] = Read16(data + k * 2, order);
  }
  for (size_t i = 4; i < kWords; i += 4) {
    for (int k = 0; k < 4; k++) {
      uint8_t* p = data + (i + k) * 2;
      Write16(p, static_cast<uint16_t>(Read16(p, order) - key[k]), order);
    }
    KeyStep(key);
  }
  uint32_t stored[4], sums[4];
  StoredChecksums(data, order, stored);
  ComputeChecksums(data, order, sums);
  return sums[0] == stored[0] && sums[1] == stored[1] && sums[2] == stored[2] && sums[3] == stored[3];
}

void EncryptSaveData(uint8_t* data, ByteOrder order) {
  uint32_t sums[4];
  ComputeChecksums(data, order, sums);
  for (int i = 0; i < 4; i++) {
    const size_t w = static_cast<size_t>(kSumWord[i]);
    Write16(data + w * 2, static_cast<uint16_t>(sums[i]), order);
    Write16(data + (w + 1) * 2, static_cast<uint16_t>(sums[i] >> 16), order);
  }
  uint16_t key[4];
  for (int k = 0; k < 4; k++) {
    key[k] = Read16(data + k * 2, order);
  }
  for (size_t i = 4; i < kWords; i += 4) {
    for (int k = 0; k < 4; k++) {
      uint8_t* p = data + (i + k) * 2;
      Write16(p, static_cast<uint16_t>(Read16(p, order) + key[k]), order);
    }
    KeyStep(key);
  }
}

}
