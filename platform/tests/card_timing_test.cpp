#include <aurora/aurora.h>
#include <dolphin/card.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#undef CARDInit
extern "C" void CARDInit(const char* game, const char* maker);

extern "C" {
s32 PortCARD_MountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback);
s32 PortCARD_CreateAsync(s32 chan, const char* name, u32 size, CARDFileInfo* fi, CARDCallback cb);
s32 PortCARD_WriteAsync(const CARDFileInfo* fi, const void* addr, s32 length, s32 offset, CARDCallback cb);
s32 PortCARD_ReadAsync(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, CARDCallback cb);
s32 PortCARD_DeleteAsync(s32 chan, const char* name, CARDCallback cb);
s32 PortCARD_GetResultCode(s32 chan);
s32 PortCARD_GetSerialNo(s32 chan, u64* serialNo);
void PortCARD_MakeHeader(uint8_t* out, uint64_t formatTime, uint16_t encoding);
uint64_t PortCARD_SerialOf(const uint8_t* header);
void VISetNextFrameBuffer(void* fb);
uint32_t PortVI_GetFlipCount(void);
}

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

int busy_frames(s32 chan) {
  int frames = 0;
  while (PortCARD_GetResultCode(chan) == CARD_RESULT_BUSY && frames < 10000) {
    VISetNextFrameBuffer(nullptr);
    frames++;
  }
  return frames;
}

alignas(32) uint8_t sData[0x28000];
alignas(32) uint8_t sBack[0x28000];
}

int main(int argc, char** argv) {
  OSGetCurrentThread();
  namespace fs = std::filesystem;
  const fs::path dir = fs::current_path() / "card_timing_test_card";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  const std::string userPath = dir.string();
  AuroraConfig config;
  std::memset(&config, 0, sizeof(config));
  config.appName = "XD port - card timing test";
  config.userPath = userPath.c_str();
  config.mem1Size = MEM1_DEFAULT_SIZE;
  aurora_initialize(argc, argv, &config);
  CARDInit("GXXE", "01");

  u64 s1 = 0, s2 = 0;
  check(PortCARD_GetSerialNo(0, &s1) == CARD_RESULT_READY && s1 != 0, "a folder card has a serial");
  check(PortCARD_GetSerialNo(0, &s2) == CARD_RESULT_READY && s1 == s2, "the same serial when asked again");
  const fs::path hdr = dir / "USA" / "Card A" / "MC_SYSTEM_AREA";
  check(fs::exists(hdr) && fs::file_size(hdr) == 0x2000, "kept as MC_SYSTEM_AREA (8 KB, as Dolphin reads it)");
  {
    uint8_t h[0x2000];
    std::ifstream f(hdr, std::ios::binary);
    f.read(reinterpret_cast<char*>(h), sizeof(h));
    check(PortCARD_SerialOf(h) == s1, "the serial is the header's first 32 bytes XORed (SDK CARDNet.c)");
    uint16_t sum = 0, inv = 0;
    for (int i = 0; i < 0x1FC; i += 2) {
      const uint16_t d = static_cast<uint16_t>(h[i] << 8 | h[i + 1]);
      sum = static_cast<uint16_t>(sum + d);
      inv = static_cast<uint16_t>(inv + (d ^ 0xFFFF));
    }
    check(h[0x1FC] == (sum >> 8) && h[0x1FD] == (sum & 0xFF) && h[0x1FE] == (inv >> 8) && h[0x1FF] == (inv & 0xFF),
          "the header's checksums are valid (the SDK checks them at mount)");
    check(h[0x22] == 0 && h[0x23] == 0x80, "a 2043-block card, as Dolphin's default");
  }
  uint8_t h0[0x2000];
  PortCARD_MakeHeader(h0, 0, 0);
  check(h0[0] == 'D' && h0[0x26] == 0xFF && h0[0x1FFF] == 0xFF, "header made as Dolphin makes it");

  uint32_t workArea[0xA000 / 4];
  check(PortCARD_MountAsync(0, workArea, nullptr, nullptr) == CARD_RESULT_READY, "mount starts");
  check(busy_frames(0) == 5, "mount: 5 system blocks read at 512 KB/s = 78 ms -> busy 5 frames");
  check(PortCARD_GetResultCode(0) == CARD_RESULT_READY, "then ready");

  CARDFileInfo fi{};
  check(PortCARD_CreateAsync(0, "test", 0x56000, &fi, nullptr) == CARD_RESULT_READY, "create starts");
  check(busy_frames(0) == 11, "create: allocation table + directory blocks = 2 x 83.9 ms -> 11 frames");

  std::memset(sData, 0x5A, sizeof(sData));
  check(PortCARD_WriteAsync(&fi, sData, 0x28000, 0x6000, nullptr) == CARD_RESULT_READY, "a 160 KB write starts");
  check(busy_frames(0) == 107, "write 160 KB: 20 sectors + directory = 1.78 s -> 107 frames (Dolphin: 107)");
  check(PortCARD_GetResultCode(0) == CARD_RESULT_READY, "the write reports ready");

  check(PortCARD_ReadAsync(&fi, sBack, 0x28000, 0x6000, nullptr) == CARD_RESULT_READY, "a 160 KB read starts");
  check(busy_frames(0) == 19, "read 160 KB: 320 segments = 312 ms -> 19 frames");
  check(std::memcmp(sData, sBack, sizeof(sData)) == 0, "the data read back is what was written");

  check(PortCARD_ReadAsync(&fi, sBack, 0x200, 0x6000, nullptr) == CARD_RESULT_READY, "a 512-byte read starts");
  check(busy_frames(0) == 1, "short operations still finish at the next frame, not in the same one");

  check(PortCARD_DeleteAsync(0, "no such file", nullptr) == CARD_RESULT_NOFILE, "a refused delete fails at once");
  check(PortCARD_GetResultCode(0) == CARD_RESULT_NOFILE && busy_frames(0) == 0, "... and is not busy");

  aurora_shutdown();
  fs::remove_all(dir, ec);
  return ok ? 0 : 1;
}
