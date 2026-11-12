#include <aurora/aurora.h>
#include <dolphin/card.h>
#include <gc_memcard.h>
#include <xd_save.h>
#include <xd_save_convert.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" int PortXD_SaveCardRead(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, s32* result);
extern "C" int PortXD_SaveCardWrite(const CARDFileInfo* fi, const void* addr, s32 length, s32 offset, s32* result);

using namespace xd::save;

namespace {
bool ok = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  ok &= cond;
}
alignas(32) unsigned char sWorkArea[CARD_WORKAREA_SIZE];

bool ReadSaveFile(const std::filesystem::path& gci, Save& save, std::vector<uint8_t>* data = nullptr) {
  std::vector<uint8_t> bytes;
  std::vector<engine::memcard::CardFile> files;
  engine::memcard::CardImageInfo info;
  std::string error;
  if (!engine::memcard::ReadWholeFile(gci.string(), bytes) || !engine::memcard::LoadCardFiles(bytes, files, info, error) ||
      files.empty() || files[0].data.size() < kFileSize) {
    return false;
  }
  std::vector<uint8_t> file(files[0].data.begin(), files[0].data.begin() + kFileSize);
  if (data) *data = file;
  ReadReport report;
  return ReadSave(file, save, report, error);
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <XD save .gci>\n", argv[0]);
    return 2;
  }
  Save original;
  std::vector<uint8_t> originalFile;
  if (!ReadSaveFile(argv[1], original, &originalFile) || original.order != ByteOrder::GameCube) {
    std::fprintf(stderr, "%s is not a GameCube-format XD save\n", argv[1]);
    return 2;
  }
  const auto userDir = std::filesystem::temp_directory_path() / "xd_card_filter_test";
  std::filesystem::remove_all(userDir);
  const auto cardDir = userDir / "USA" / "Card A";
  std::filesystem::create_directories(cardDir);
  const auto gciPath = cardDir / "01-GXXE-PokemonXD.gci";
  std::filesystem::copy_file(argv[1], gciPath);
  const std::string userPath = userDir.string();

  AuroraConfig config;
  std::memset(&config, 0, sizeof(config));
  config.appName = "XD port - card filter test";
  config.userPath = userPath.c_str();
  config.mem1Size = MEM1_DEFAULT_SIZE;
  aurora_initialize(argc, argv, &config);
  CARDInit("GXXE", "01");
  s32 mem = 0, sector = 0;
  CARDProbeEx(0, &mem, &sector);
  check(CARDMount(0, sWorkArea, nullptr) == CARD_RESULT_READY, "the card mounts");
  CARDFileInfo fi{};
  check(CARDOpen(0, "PokemonXD", &fi) == CARD_RESULT_READY, "the copied save opens");

  std::vector<uint8_t> buf(0x28000 + 0x400, 0);
  s32 result = -1;
  check(PortXD_SaveCardRead(&fi, buf.data(), 0x4000, 0, &result) == 1 && result == CARD_RESULT_READY, "banner read");
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) sum += Read32(buf.data() + i * 4, ByteOrder::PC);
  check(sum == Read32(buf.data() + 0x3A40, ByteOrder::PC), "the banner checksum is right in PC order (no redraw)");
  for (int s = 0; s < 2; s++) {
    PortXD_SaveCardRead(&fi, buf.data() + 0x28000 + s * 0x200, 0x200, static_cast<s32>(SlotOffset(s)), &result);
  }
  const int32_t c0 = static_cast<int32_t>(Read32(buf.data() + 0x28004, ByteOrder::PC));
  const int32_t c1 = static_cast<int32_t>(Read32(buf.data() + 0x28204, ByteOrder::PC));
  check(c0 == original.saveCount || c1 == original.saveCount,
        "slot headers: the save counters read right in PC order (" + std::to_string(c0) + ", " + std::to_string(c1) + ")");
  const int newest = c1 > c0 ? 1 : 0;
  PortXD_SaveCardRead(&fi, buf.data(), 0x28000, static_cast<s32>(SlotOffset(newest)), &result);
  std::vector<uint8_t> slot(buf.begin(), buf.begin() + kSlotSize);
  check(SlotValid(slot.data(), ByteOrder::PC), "the newest slot passes the game's checks in PC order");

  std::vector<uint8_t> data(slot.begin() + kSlotHeaderSize, slot.begin() + kSlotHeaderSize + kDataSize);
  DecryptSaveData(data.data(), ByteOrder::PC);
  const uint32_t heroAt = kSectionBase + (Read16(data.data() + 0x3C, ByteOrder::PC) | Read16(data.data() + 0x3E, ByteOrder::PC) << 16);
  const uint32_t gameAt = kSectionBase + (Read16(data.data() + 0x38, ByteOrder::PC) | Read16(data.data() + 0x3A, ByteOrder::PC) << 16);
  const uint32_t money = Read32(data.data() + heroAt + 0x8E4, ByteOrder::PC) - 100;
  std::memcpy(data.data() + heroAt + 0x8E4, &money, 4);
  const int32_t counter = std::max(c0, c1) + 1;
  std::memcpy(slot.data() + 4, &counter, 4);
  uint32_t headSum = 0;
  for (int i = 0; i < 8; i++) headSum += slot[i];
  std::memcpy(data.data() + gameAt + 0x38, &headSum, 4);
  EncryptSaveData(data.data(), ByteOrder::PC);
  std::memcpy(slot.data() + kSlotHeaderSize, data.data(), kDataSize);
  const int other = 1 - newest;
  check(PortXD_SaveCardWrite(&fi, slot.data(), kSlotSize, static_cast<s32>(SlotOffset(other)), &result) == 1 &&
            result == CARD_RESULT_READY,
        "the save is written through the filter");
  CARDClose(&fi);

  CARDFileInfo other_file{};
  if (CARDCreate(0, "porttest", 0x2000, &other_file) == CARD_RESULT_READY) {
    std::vector<uint8_t> x(0x2000, 0x5A);
    check(PortXD_SaveCardWrite(&other_file, x.data(), 0x2000, 0, &result) == 0, "other files pass through");
    CARDClose(&other_file);
    CARDDelete(0, "porttest");
  }
  CARDUnmount(0);
  aurora_shutdown();

  Save after;
  std::vector<uint8_t> afterFile;
  check(ReadSaveFile(gciPath, after, &afterFile), "the card file reads afterwards");
  check(after.order == ByteOrder::GameCube && after.slot == other && after.saveCount == counter,
        "the new save is on the card in GameCube format, newest (counter " + std::to_string(after.saveCount) + ")");
  check(after.money == original.money - 100 && after.trainerName == original.trainerName &&
            after.playTimeSeconds == original.playTimeSeconds,
        "with the change (money " + std::to_string(after.money) + ") and everything else as before");
  check(std::memcmp(afterFile.data() + SlotOffset(newest), originalFile.data() + SlotOffset(newest), kSlotSize) == 0 &&
            std::memcmp(afterFile.data(), originalFile.data(), kBannerSize) == 0,
        "the slot it didn't write and the banner are byte for byte the original");

  std::error_code ec;
  std::filesystem::remove_all(userDir, ec);
  std::printf(ok ? "PASS\n" : "FAILED\n");
  return ok ? 0 : 1;
}
