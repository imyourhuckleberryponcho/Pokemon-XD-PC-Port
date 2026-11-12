#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <dolphin/card.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" void PortCARD_Init(void);

namespace {
bool ok = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  ok &= cond;
}
alignas(32) unsigned char sWorkArea[CARD_WORKAREA_SIZE];
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image>\n", argv[0]);
    return 2;
  }
  const auto userDir = std::filesystem::temp_directory_path() / "xd_card_test";
  std::filesystem::remove_all(userDir);
  std::filesystem::create_directories(userDir);
  const std::string userPath = userDir.string();

  AuroraConfig config;
  std::memset(&config, 0, sizeof(config));
  config.appName = "XD port - card test";
  config.userPath = userPath.c_str();
  config.mem1Size = MEM1_DEFAULT_SIZE;
  aurora_initialize(argc, argv, &config);
  check(aurora_dvd_open(argv[1]), "the disc opens (its header gives the game code)");

  PortCARD_Init();
  s32 mem = 0, sector = 0;
  s32 r = CARDProbeEx(0, &mem, &sector);
  check(r == CARD_RESULT_READY, "slot A has a card (probe: " + std::to_string(r) + ")");
  r = CARDMount(0, sWorkArea, nullptr);
  check(r == CARD_RESULT_READY, "the card mounts (" + std::to_string(r) + ")");

  CARDFileInfo file{};
  const u32 size = sector > 0 ? static_cast<u32>(sector) : 0x2000;
  r = CARDCreate(0, "porttest", size, &file);
  check(r == CARD_RESULT_READY, "a save file is created (" + std::to_string(r) + ")");
  std::vector<unsigned char> data(size), back(size);
  for (u32 i = 0; i < size; i++) data[i] = static_cast<unsigned char>(i * 7);
  check(CARDWrite(&file, data.data(), static_cast<s32>(size), 0) == CARD_RESULT_READY, "it's written");
  CARDClose(&file);
  check(CARDOpen(0, "porttest", &file) == CARD_RESULT_READY, "it opens again");
  check(CARDRead(&file, back.data(), static_cast<s32>(size), 0) == CARD_RESULT_READY && back == data,
        "it reads back the same bytes");
  CARDClose(&file);

  const auto cardDir = userDir / "USA" / "Card A";
  int gci = 0;
  std::string names;
  if (std::filesystem::exists(cardDir)) {
    for (const auto& e : std::filesystem::directory_iterator(cardDir)) {
      if (e.path().extension() == ".gci") {
        gci++;
        names += " " + e.path().filename().string();
      }
    }
  }
  check(gci == 1, "one .gci file in USA/Card A:" + names);
  check(CARDDelete(0, "porttest") == CARD_RESULT_READY, "it's deleted again");

  aurora_dvd_close();
  aurora_shutdown();
  std::error_code ec;
  std::filesystem::remove_all(userDir, ec);
  return ok ? 0 : 1;
}
