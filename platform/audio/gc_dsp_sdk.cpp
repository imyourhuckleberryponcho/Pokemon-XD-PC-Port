#include <dolphin/dsp.h>

#include <gc_dsp.h>
#include <port_audio.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern void* MEM1Start;

extern "C" {
void PortOS_RaiseInterrupt(void (*handler)(void*), void* arg);
void* ARGetStorageAddress(void);
u32 ARGetSize(void);
}

namespace {

constexpr int kRunBudget = 50'000'000;
constexpr u32 kMem1Size = 0x01800000;

GcDsp* sDsp = nullptr;
DSPTaskInfo* sFirstTask = nullptr;
DSPTaskInfo* sCurrentTask = nullptr;
bool sInterruptQueued = false;
uint32_t sBooted = 0, sInitialised = 0, sYields = 0;
uint64_t sInstructions = 0;
uint64_t sDspNs = 0;
uint16_t sCoefTable[0x800];
bool sHaveCoefTable = false;

uint8_t* dsp_memory(void*, uint32_t address, uint32_t size) {
  const uint32_t physical = address & 0x3FFFFFFF;
  if ((address & 0x40000000) == 0 || (address & 0xC0000000) == 0xC0000000) {
    if (physical + static_cast<uint64_t>(size) <= kMem1Size) {
      return static_cast<uint8_t*>(MEM1Start) + physical;
    }
    return nullptr;
  }
  return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
}

void dsp_log(void*, const char* message) { std::fprintf(stderr, "[platform/audio] DSP: %s\n", message); }

void raise_interrupt(void*);

GcDspHost make_host() {
  GcDspHost host{};
  host.mainMemory = dsp_memory;
  host.interruptCpu = raise_interrupt;
  host.log = dsp_log;
  host.aram = static_cast<uint8_t*>(ARGetStorageAddress());
  host.aramSize = host.aram != nullptr ? ARGetSize() : 0;
  host.dramWordsHostOrder = true;
  return host;
}

void run_until_idle() {
  if (sDsp == nullptr) return;
  const auto t0 = std::chrono::steady_clock::now();
  const int ran = GcDsp_RunUntilIdle(sDsp, kRunBudget);
  sDspNs += static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
  sInstructions += static_cast<uint64_t>(ran);
  if (ran >= kRunBudget) {
    std::fprintf(stderr, "[platform/audio] DSP: ran %d instructions without waiting for the CPU (pc %04X)\n",
                 kRunBudget, GcDsp_Pc(sDsp));
  }
}

void dsp_interrupt(void*);

GcDsp* sRealDsp = nullptr;
bool sGhost = false;
bool sGhostInterrupt = false;
struct SavedSdk {
  bool interruptQueued;
  uint32_t booted, initialised, yields;
  uint64_t instructions;
} sSavedSdk{};

void raise_interrupt(void*) {
  if (sGhost) {
    sGhostInterrupt = true;
    return;
  }
  if (!sInterruptQueued) {
    sInterruptQueued = true;
    PortOS_RaiseInterrupt(dsp_interrupt, nullptr);
  }
}

void dsp_interrupt(void*) {
  sInterruptQueued = false;
  DSPTaskInfo* task = sCurrentTask;
  if (task == nullptr) return;
  while (DSPCheckMailFromDSP() == 0) {
    run_until_idle();
    if (GcDsp_IsHalted(sDsp)) return;
  }
  u32 mail = DSPReadMailFromDSP();
  if ((task->flags & 2) && mail == 0xDCD10002) {
    mail = 0xDCD10003;
  }
  switch (mail) {
  case 0xDCD10000:
    sInitialised++;
    task->state = 1;
    if (task->init_cb != nullptr) task->init_cb(task);
    break;
  case 0xDCD10001:
    task->state = 1;
    if (task->res_cb != nullptr) task->res_cb(task);
    break;
  case 0xDCD10002:
    sYields++;
    DSPSendMailToDSP(0xCDD10003);
    while (DSPCheckMailToDSP() != 0) {
    }
    if (task->res_cb != nullptr) task->res_cb(task);
    break;
  case 0xDCD10003:
    if (task->done_cb != nullptr) task->done_cb(task);
    DSPSendMailToDSP(0xCDD10002);
    while (DSPCheckMailToDSP() != 0) {
    }
    task->state = 3;
    sFirstTask = sCurrentTask = nullptr;
    break;
  case 0xDCD10004:
    if (task->req_cb != nullptr) task->req_cb(task);
    break;
  default:
    std::fprintf(stderr, "[platform/audio] DSP: unknown mail %08lX from the task\n", static_cast<unsigned long>(mail));
    break;
  }
}

void send_and_wait(u32 mail) {
  DSPSendMailToDSP(mail);
  while (DSPCheckMailToDSP() != 0) {
    if (GcDsp_IsHalted(sDsp)) return;
  }
}

void boot_task(DSPTaskInfo* task) {
  sBooted++;
  GcDsp_WriteControl(sDsp, 0x0004);
  GcDsp_WriteControl(sDsp, 0x0001);
  while (DSPCheckMailFromDSP() == 0) {
    run_until_idle();
    if (GcDsp_IsHalted(sDsp)) return;
  }
  const u32 hello = DSPReadMailFromDSP();
  if (hello != 0x8071FEED) {
    std::fprintf(stderr, "[platform/audio] DSP: boot loader said %08lX, not 8071FEED\n", static_cast<unsigned long>(hello));
  }
  send_and_wait(0x80F3A001);
  send_and_wait(static_cast<u32>(reinterpret_cast<uintptr_t>(task->iram_mmem_addr)));
  send_and_wait(0x80F3C002);
  send_and_wait(task->iram_addr & 0xFFFF);
  send_and_wait(0x80F3A002);
  send_and_wait(task->iram_length);
  send_and_wait(0x80F3B002);
  send_and_wait(0);
  send_and_wait(0x80F3D001);
  send_and_wait(task->dsp_init_vector);
}

}

extern "C" {

void DSPInit(void) {
  if (sDsp != nullptr) return;
  const GcDspHost host = make_host();
  sDsp = GcDsp_Create(&host);
  GcDsp_InstallPortRom(sDsp);
  if (sHaveCoefTable) GcDsp_LoadCoef(sDsp, sCoefTable);
  sFirstTask = sCurrentTask = nullptr;
}

int PortDsp_UseCoefDump(const uint8_t* bytes, size_t size) {
  int order = 0;
  if (bytes != nullptr) {
    uint16_t words[0x800];
    order = GcDsp_ParseCoefDump(bytes, size, words);
    if (order == 0) return 0;
    std::memcpy(sCoefTable, words, sizeof(sCoefTable));
    sHaveCoefTable = true;
  } else {
    GcDsp_PortCoefTable(sCoefTable);
    sHaveCoefTable = false;
  }
  GcDsp* chip = sGhost ? sRealDsp : sDsp;
  if (chip != nullptr) GcDsp_LoadCoef(chip, sCoefTable);
  return order;
}

BOOL DSPCheckInit(void) { return sDsp != nullptr; }

DSPTaskInfo* DSPAddTask(DSPTaskInfo* task) {
  if (sDsp == nullptr) DSPInit();
  task->state = 0;
  task->flags = 1;
  task->next = task->prev = nullptr;
  if (sFirstTask != nullptr) {
    std::fprintf(stderr, "[platform/audio] DSP: a second task isn't supported yet\n");
    return task;
  }
  const GcDspHost host = make_host();
  GcDsp_SetHost(sDsp, &host);
  sFirstTask = sCurrentTask = task;
  boot_task(task);
  run_until_idle();
  return task;
}

DSPTaskInfo* DSPCancelTask(DSPTaskInfo* task) {
  task->flags |= 2;
  return task;
}

u32 DSPCheckMailToDSP(void) {
  run_until_idle();
  return sDsp != nullptr ? (GcDsp_CpuMailToDsp(sDsp) >> 31) & 1 : 0;
}

u32 DSPCheckMailFromDSP(void) {
  return sDsp != nullptr ? (GcDsp_CpuPeekMail(sDsp) >> 31) & 1 : 0;
}

u32 DSPReadMailFromDSP(void) {
  if (sDsp == nullptr) return 0;
  const u32 mail = GcDsp_CpuTakeMail(sDsp) & 0x7FFFFFFF;
  run_until_idle();
  return mail | 0x80000000u;
}

void DSPSendMailToDSP(u32 mail) {
  if (sDsp == nullptr) return;
  GcDsp_CpuSendMail(sDsp, mail);
  run_until_idle();
}

void DSPReset(void) {
  if (sDsp != nullptr) GcDsp_WriteControl(sDsp, 0x0001 | 0x0004);
}

void DSPHalt(void) {
  if (sDsp != nullptr) GcDsp_WriteControl(sDsp, 0x0004);
}

u32 DSPGetDMAStatus(void) { return 0; }

bool PortDsp_GhostBegin(void) {
  if (sDsp == nullptr || sCurrentTask == nullptr || sGhost) return false;
  sSavedSdk = {sInterruptQueued, sBooted, sInitialised, sYields, sInstructions};
  sRealDsp = sDsp;
  sDsp = GcDsp_Clone(sRealDsp);
  sGhost = true;
  sGhostInterrupt = sInterruptQueued;
  sInterruptQueued = false;
  return true;
}

void PortDsp_GhostService(void) {
  if (!sGhost) return;
  while (sGhostInterrupt) {
    sGhostInterrupt = false;
    dsp_interrupt(nullptr);
  }
}

void PortDsp_GhostEnd(void) {
  if (!sGhost) return;
  GcDsp_Destroy(sDsp);
  sDsp = sRealDsp;
  sRealDsp = nullptr;
  sGhost = false;
  sGhostInterrupt = false;
  sInterruptQueued = sSavedSdk.interruptQueued;
  sBooted = sSavedSdk.booted;
  sInitialised = sSavedSdk.initialised;
  sYields = sSavedSdk.yields;
  sInstructions = sSavedSdk.instructions;
}

uint64_t PortDsp_Instructions(void) { return sInstructions; }

void PortAudio_GetDspStats(PortAudioStats* out) {
  out->dspTasksBooted = sBooted;
  out->dspInitialised = sInitialised;
  out->dspYields = sYields;
  out->dspInstructions = sInstructions;
  out->dspNs = sDspNs;
}

}
