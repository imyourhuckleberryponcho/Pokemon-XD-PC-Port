#include <aurora/dvd.h>
#include <dolphin/dvd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" {
BOOL PortDVD_ReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio);
void* OSGetCurrentThread(void);
int OSDisableInterrupts(void);
int OSRestoreInterrupts(int level);
void PortDVD_OnFlip(uint32_t frame);
}

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
volatile bool sCalled = false;
std::thread::id sCallbackThread;
s32 sResult = -99;
alignas(32) unsigned char sBuffer[0x800];
void on_read(s32 result, DVDFileInfo*) {
  sCalled = true;
  sResult = result;
  sCallbackThread = std::this_thread::get_id();
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image>\n", argv[0]);
    return 2;
  }
  OSGetCurrentThread();
  check(aurora_dvd_open(argv[1]), "Aurora opens the disc");
  DVDFileInfo file;
  check(DVDOpen("common.fsys", &file), "DVDOpen common.fsys");

  const int level = OSDisableInterrupts();
  check(PortDVD_ReadAsyncPrio(&file, sBuffer, sizeof(sBuffer), 0, on_read, 2), "an asynchronous read starts");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  PortDVD_OnFlip(1000000);
  check(!sCalled, "with interrupts disabled, the callback hasn't run yet");
  OSRestoreInterrupts(level);
  check(sCalled, "the callback runs when interrupts are re-enabled");
  check(sCallbackThread == std::this_thread::get_id(), "it runs on the game's thread, not Aurora's worker");
  check(sResult == (s32)sizeof(sBuffer), "it reports the bytes read");
  check(sBuffer[0] == 'F' && sBuffer[1] == 'S' && sBuffer[2] == 'Y' && sBuffer[3] == 'S', "the data arrived (FSYS header)");
  aurora_dvd_close();
  return ok ? 0 : 1;
}
