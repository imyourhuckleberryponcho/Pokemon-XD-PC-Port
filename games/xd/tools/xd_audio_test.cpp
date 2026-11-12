





#include <aurora/aurora.h>
#include <port_audio.h>
#include <port_disc.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
bool PortMem_ReserveMEM1(void);
void PortMem_InitOS(uint32_t arenaLo);
uint32_t ARInit(uint32_t* stack, uint32_t numEntries);
void AIInit(uint8_t* stack);
void VIWaitForRetrace(void);
void* OSGetCurrentThread(void);
int AudioTest_StartMusyX(void);
extern char dspSlave[];
void* AudioTest_Alloc(unsigned int size);
}

namespace {
bool ok = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  ok &= cond;
}
uint32_t sNext = 0x80400000;
uint32_t sAramStack[4];
}


extern "C" void OSPanic(const char* file, int line, const char* msg, ...) {
  std::fprintf(stderr, "OSPanic %s:%d: %s\n", file, line, msg);
  std::abort();
}

extern "C" void* AudioTest_Alloc(unsigned int size) {
  const uint32_t at = (sNext + 31) & ~31u;
  if (at + size > 0x81700000) return nullptr;
  sNext = at + size;
  void* p = reinterpret_cast<void*>(static_cast<uintptr_t>(at));
  std::memset(p, 0, size);
  return p;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [seconds]\n", argv[0]);
    return 2;
  }
  const int seconds = argc >= 3 ? std::atoi(argv[2]) : 2;
  if (!PortMem_ReserveMEM1()) {
    std::fprintf(stderr, "FAILED: GameCube memory couldn't be placed at its address\n");
    return 1;
  }
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  check(PortDol_Read(dol, dolSize, 0x80409040, reinterpret_cast<uint8_t*>(dspSlave), 0x1EC0),
        "MusyX's DSP program read from main.dol");

  AuroraConfig config;
  std::memset(&config, 0, sizeof(config));
  config.appName = "XD port - audio test";
  config.mem1Size = MEM1_DEFAULT_SIZE;
  config.mem2Size = 0x01000000;
  aurora_initialize(argc, argv, &config);
  PortMem_InitOS(0x80100000);
  OSGetCurrentThread();

  ARInit(sAramStack, 4);
  static uint8_t aiStack[0x1000];
  AIInit(aiStack + sizeof(aiStack) - 8);
  check(AudioTest_StartMusyX() == 0, "MusyX starts (sndInit, as XD's GSsndInit)");

  PortAudioStats s{};
  PortAudio_GetStats(&s);
  check(s.dspTasksBooted == 1, "the DSP task manager booted MusyX's DSP program");
  check(s.dspInitialised == 1, "the program said it's initialised, and MusyX got the callback");

  const int frames = seconds * 60;
  for (int i = 0; i < frames; i++) VIWaitForRetrace();
  PortAudio_GetStats(&s);
  const double expected = seconds * 32000.0 / 160.0;
  std::printf("     after %d s of GameCube time: %u blocks played (%u dropped, %u with sound), %u command lists, "
              "%llu DSP instructions\n",
              seconds, s.aiBlocks, s.aiBlocksDropped, s.aiNonSilentBlocks, s.dspYields,
              static_cast<unsigned long long>(s.dspInstructions));
  check(s.aiBlocks > expected * 0.9 && s.aiBlocks < expected * 1.1, "a block every 5 ms of GameCube time");
  check(s.dspYields + 4 >= s.aiBlocks && s.dspYields <= s.aiBlocks + 1, "a DSP command list for every block");
  check(s.aiNonSilentBlocks == 0, "nothing playing: silence");
  check(s.aiBlocksDropped == 0, "no block dropped (the playback rate follows the game's clock)");
  aurora_shutdown();
  return ok ? 0 : 1;
}
