#include <aurora/card.h>
#include <dolphin/card.h>
#include <dolphin/dvd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

extern "C" bool PortTime_IsDeterministic(void);
extern "C" uint32_t PortTime_CardSeconds(void);

namespace {

constexpr size_t kHeaderSize = 0x2000;
constexpr const char* kHeaderFile = "MC_SYSTEM_AREA";

struct CardHeader {
  bool loaded = false;
  std::array<uint8_t, kHeaderSize> bytes{};
};
CardHeader sHeader[2];

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void put_be16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

uint64_t now_gc_seconds() {
  if (PortTime_IsDeterministic()) {
    return PortTime_CardSeconds();
  }
  const std::time_t now = std::time(nullptr);
  std::tm local{}, utc{};
#ifdef _WIN32
  localtime_s(&local, &now);
  gmtime_s(&utc, &now);
#else
  localtime_r(&now, &local);
  gmtime_r(&now, &utc);
#endif
  utc.tm_isdst = local.tm_isdst;
  const std::time_t offset = now - std::mktime(&utc);
  return static_cast<uint64_t>(now + offset) - 946684800u;
}

std::filesystem::path folder_of(s32 chan) {
  const DVDDiskID* id = DVDGetCurrentDiskID();
  char game[5] = {};
  if (id != nullptr) std::memcpy(game, id->gameName, 4);
  char path[1024];
  if (aurora_card_get_path(game, AURORA_CARD_GCI_DIRECTORY, chan, path, sizeof(path)) == 0) return {};
  return std::filesystem::u8path(path);
}

}

extern "C" {

void PortCARD_MakeHeader(uint8_t* out, uint64_t formatTime, uint16_t encoding) {
  static const char kFlashId[12] = {'D', 'O', 'L', 'P', 'H', 'I', 'N', 'S', 'L', 'O', 'T', 'A'};
  std::memset(out, 0xFF, kHeaderSize);
  uint64_t rand = formatTime;
  for (int i = 0; i < 12; i++) {
    rand = (rand * 0x41C64E6Dull + 0x3039ull) >> 16;
    out[i] = static_cast<uint8_t>(static_cast<uint8_t>(kFlashId[i]) + static_cast<uint32_t>(rand));
    rand = ((rand * 0x41C64E6Dull + 0x3039ull) >> 16) & 0x7FFFull;
  }
  for (int i = 0; i < 8; i++) out[0x0C + i] = static_cast<uint8_t>(formatTime >> (56 - 8 * i));
  std::memset(out + 0x14, 0, 12);
  put_be16(out + 0x20, 0);
  put_be16(out + 0x22, 0x80);
  put_be16(out + 0x24, encoding);
  uint16_t sum = 0, inv = 0;
  for (size_t i = 0; i < 0x1FC; i += 2) {
    const uint16_t d = be16(out + i);
    sum = static_cast<uint16_t>(sum + d);
    inv = static_cast<uint16_t>(inv + (d ^ 0xFFFF));
  }
  put_be16(out + 0x1FC, sum == 0xFFFF ? 0 : sum);
  put_be16(out + 0x1FE, inv == 0xFFFF ? 0 : inv);
}

uint64_t PortCARD_SerialOf(const uint8_t* header) {
  uint64_t code = 0;
  for (int w = 0; w < 4; w++) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | header[w * 8 + i];
    code ^= v;
  }
  return code;
}

void PortCARD_NewSerial(s32 chan) {
  if (chan < 0 || chan >= 2 || aurora_card_get_type(chan) != AURORA_CARD_GCI_DIRECTORY) return;
  const std::filesystem::path folder = folder_of(chan);
  CardHeader& h = sHeader[chan];
  const DVDDiskID* id = DVDGetCurrentDiskID();
  const uint16_t encoding = id != nullptr && id->gameName[3] == 'J' ? 1 : 0;
  PortCARD_MakeHeader(h.bytes.data(), now_gc_seconds() + static_cast<uint64_t>(chan), encoding);
  h.loaded = true;
  if (folder.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  std::ofstream f(folder / kHeaderFile, std::ios::binary | std::ios::trunc);
  if (!f || !f.write(reinterpret_cast<const char*>(h.bytes.data()), kHeaderSize)) {
    std::fprintf(stderr, "[card] couldn't write %s in the card folder; the serial lasts this run only\n", kHeaderFile);
  }
}

s32 PortCARD_GetSerialNo(s32 chan, u64* serialNo) {
  if (chan < 0 || chan >= 2 || aurora_card_get_type(chan) != AURORA_CARD_GCI_DIRECTORY) {
    return CARDGetSerialNo(chan, serialNo);
  }
  CardHeader& h = sHeader[chan];
  if (!h.loaded) {
    const std::filesystem::path folder = folder_of(chan);
    std::ifstream f(folder / kHeaderFile, std::ios::binary);
    if (!folder.empty() && f && f.read(reinterpret_cast<char*>(h.bytes.data()), kHeaderSize)) {
      h.loaded = true;
    } else {
      PortCARD_NewSerial(chan);
    }
  }
  if (serialNo != nullptr) *serialNo = PortCARD_SerialOf(h.bytes.data());
  return CARD_RESULT_READY;
}

}
