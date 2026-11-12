#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef float f32;
typedef int BOOL;

static void unfinished(const char* function, const char* roadmapItem) {
  fprintf(stderr, "[platform/sdk] %s is not implemented yet (ROADMAP: \"%s\"). Stopping here rather than guessing.\n",
          function, roadmapItem);
  fflush(stderr);
  abort();
}

void OSVReport(const char* msg, va_list args);
void OSReport(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
}
void OSVReport(const char* msg, va_list args) { vfprintf(stdout, msg, args); }

u32 OSGetConsoleSimulatedMemSize(void) { return 0x01800000; }

u32 OSGetResetCode(void) {
  static int read = 0;
  static u32 code = 0;
  if (!read) {
    const char* v = getenv("PORT_RESET_CODE");
    code = v != NULL ? (u32)strtoul(v, NULL, 16) : 0;
    read = 1;
  }
  return code;
}

BOOL OSGetResetButtonState(void) { return 0; }

#define OS_RESET_RESTART 0
#define OS_RESET_HOTRESET 1
#define OS_RESET_SHUTDOWN 2
static void (*sResetShutdown)(void);
void PortOS_SetResetShutdown(void (*fn)(void)) { sResetShutdown = fn; }
static void reset_exit(void) {
  fflush(stdout);
  fflush(stderr);
  if (sResetShutdown != NULL) sResetShutdown();
  exit(0);
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) {
  if (reset == OS_RESET_SHUTDOWN || forceMenu) {
    reset_exit();
  }
#ifndef _WIN32
  {
    char code[16];
    static char args[32768];
    char* argv[512];
    int argc = 0;
    size_t n = 0, i = 0;
    pid_t pid;
    FILE* f = fopen("/proc/self/cmdline", "rb");
    if (f != NULL) {
      n = fread(args, 1, sizeof(args) - 1, f);
      fclose(f);
    }
    args[n] = 0;
    while (i < n && argc < 511) {
      argv[argc++] = args + i;
      i += strlen(args + i) + 1;
    }
    argv[argc] = NULL;
#ifdef __APPLE__
    static char exe[4096];
    {
      char** own = *_NSGetArgv();
      const int count = *_NSGetArgc();
      uint32_t exeSize = sizeof(exe);
      for (argc = 0; argc < count && argc < 511; ++argc) {
        argv[argc] = own[argc];
      }
      argv[argc] = NULL;
      if (_NSGetExecutablePath(exe, &exeSize) != 0) {
        argc = 0;
      }
    }
#define PORT_SELF_EXE exe
#else
#define PORT_SELF_EXE "/proc/self/exe"
#endif
    snprintf(code, sizeof(code), "%08lX",
             (unsigned long)(reset == OS_RESET_RESTART ? (0x80000000u | resetCode) : resetCode));
    setenv("PORT_RESET_CODE", code, 1);
    {
      char self[16];
      snprintf(self, sizeof(self), "%lu", (unsigned long)getpid());
      setenv("PORT_RESET_PARENT", self, 1);
    }
    if (argc == 0 || posix_spawn(&pid, PORT_SELF_EXE, NULL, NULL, argv, environ) != 0) {
      fprintf(stderr, "[platform/sdk] soft reset: couldn't start the game again\n");
      fflush(stderr);
      abort();
    }
    reset_exit();
  }
#else
  {
    char code[16];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t* cmd = _wcsdup(GetCommandLineW());
    snprintf(code, sizeof(code), "%08lX",
             (unsigned long)(reset == OS_RESET_RESTART ? (0x80000000u | resetCode) : resetCode));
    SetEnvironmentVariableA("PORT_RESET_CODE", code);
    {
      char self[16];
      snprintf(self, sizeof(self), "%lu", (unsigned long)GetCurrentProcessId());
      SetEnvironmentVariableA("PORT_RESET_PARENT", self);
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (cmd == NULL || !CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      fprintf(stderr, "[platform/sdk] soft reset: couldn't start the game again (error %lu)\n", GetLastError());
      fflush(stderr);
      abort();
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    reset_exit();
  }
#endif
}

static u32 sProgressiveMode = 0;
static u32 sSoundMode = 1;
u32 OSGetProgressiveMode(void) { return sProgressiveMode; }
void OSSetProgressiveMode(u32 on) { sProgressiveMode = on; }
u32 OSGetSoundMode(void) { return sSoundMode; }
void OSSetSoundMode(u32 mode) { sSoundMode = mode; }

typedef void (*OSErrorHandler)(u16 error, void* context, ...);
static OSErrorHandler sErrorHandlers[17];
u32 __OSFpscrEnableBits = 0;
OSErrorHandler OSSetErrorHandler(u16 error, OSErrorHandler handler) {
  OSErrorHandler old = error < 17 ? sErrorHandlers[error] : NULL;
  if (error < 17) {
    sErrorHandlers[error] = handler;
  }
  return old;
}

void PPCSync(void) {}

typedef BOOL (*PortOSModuleHandler)(void* module, void* bss);
static PortOSModuleHandler sModuleLink, sModuleUnlink;
void PortOS_SetModuleHandlers(PortOSModuleHandler link, PortOSModuleHandler unlink) {
  sModuleLink = link;
  sModuleUnlink = unlink;
}
BOOL OSLinkFixed(void* module, void* bss) {
  if (sModuleLink == NULL) {
    unfinished("OSLinkFixed (REL module) with no module handler registered", "Install converted tables");
  }
  return sModuleLink(module, bss);
}
BOOL OSUnlink(void* module) {
  if (sModuleUnlink == NULL) {
    unfinished("OSUnlink (REL module) with no module handler registered", "Install converted tables");
  }
  return sModuleUnlink(module, NULL);
}

static volatile uint32_t sFrameBufferFlips;
void PortDVD_OnFlip(uint32_t frame);
void PortFrameCost_OnFlip(uint32_t frame);
void PortCARD_OnFlip(uint32_t frame);
void VISetNextFrameBuffer(void* fb) {
  (void)fb;
  sFrameBufferFlips++;
  PortFrameCost_OnFlip(sFrameBufferFlips);
  PortDVD_OnFlip(sFrameBufferFlips);
  PortCARD_OnFlip(sFrameBufferFlips);
}
uint32_t PortVI_GetFlipCount(void) { return sFrameBufferFlips; }

static BOOL sBlack;
void VISetBlack(BOOL black) { sBlack = black; }

u32 VIGetDTVStatus(void) { return 0; }

void AIInit(u8* stack) { (void)stack; }

static u32 sArqChunkSize = 0x1000;
void ARQSetChunkSize(u32 size) { sArqChunkSize = size; }
u32 ARQGetChunkSize(void) { return sArqChunkSize; }
u32 ARGetBaseAddress(void) { return 0x4000; }

u32 ARGetDMAStatus(void) { return 0; }

#define GBA_NOT_READY 1
typedef void (*GBACallback)(s32 chan, s32 ret);
void GBAInit(void) {}
s32 GBAGetStatus(s32 chan, u8* status) { (void)chan, (void)status; return GBA_NOT_READY; }
s32 GBAGetStatusAsync(s32 chan, u8* status, GBACallback cb) { (void)chan, (void)status, (void)cb; return GBA_NOT_READY; }
s32 GBARead(s32 chan, u8* dst, u8* status) { (void)chan, (void)dst, (void)status; return GBA_NOT_READY; }
s32 GBAReadAsync(s32 chan, u8* dst, u8* status, GBACallback cb) { (void)chan, (void)dst, (void)status, (void)cb; return GBA_NOT_READY; }
s32 GBAWrite(s32 chan, u8* src, u8* status) { (void)chan, (void)src, (void)status; return GBA_NOT_READY; }
s32 GBAWriteAsync(s32 chan, u8* src, u8* status, GBACallback cb) { (void)chan, (void)src, (void)status, (void)cb; return GBA_NOT_READY; }
s32 GBAReset(s32 chan, u8* status) { (void)chan, (void)status; return GBA_NOT_READY; }
s32 GBAResetAsync(s32 chan, u8* status, GBACallback cb) { (void)chan, (void)status, (void)cb; return GBA_NOT_READY; }

u32 PERFInit(u32 a, u32 b, u32 c, void* alloc, void* dealloc, void* draw) {
  void* (*perfAlloc)(u32) = (void* (*)(u32))alloc;
  u32 i;
  (void)dealloc, (void)draw;
  if (perfAlloc != NULL) {
    perfAlloc(b * 0x10);
    for (i = 0; i < b; i++) {
      perfAlloc(a * 0xB0);
    }
    perfAlloc(c * 0x10);
  }
  return b * 0x10 + b * (a * 0xB0) + c * 0x10;
}
void PERFStartFrame(void) {}
void PERFEndFrame(void) {}
void PERFStartAutoSampling(f32 msInterval) { (void)msInterval; }
void PERFStopAutoSampling(void) {}
void PERFPreDraw(void) {}
void PERFPostDraw(void) {}
void PERFDumpScreen(void) {}
void PERFSetDrawBWBarKey(BOOL tf) { (void)tf; }
void PERFSetDrawSyncCallback(void (*cb)(u16)) { (void)cb; }

BOOL DBIsDebuggerPresent(void) { return 0; }
