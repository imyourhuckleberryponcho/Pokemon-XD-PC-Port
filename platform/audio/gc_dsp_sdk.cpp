#include <dolphin/dsp.h>

#include <gc_dsp.h>
#include <port_audio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

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
std::atomic<uint64_t> sInstructions{0};
std::atomic<uint64_t> sDspNs{0};
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

thread_local uint64_t tlLastRunNs = 0;

void run_until_idle() {
  if (sDsp == nullptr) return;
  const auto t0 = std::chrono::steady_clock::now();
  const int ran = GcDsp_RunUntilIdle(sDsp, kRunBudget);
  const uint64_t ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
  sDspNs.fetch_add(ns, std::memory_order_relaxed);
  sInstructions.fetch_add(static_cast<uint64_t>(ran), std::memory_order_relaxed);
  tlLastRunNs = ns;
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

constexpr int kSteadyLists = 8;
struct Worker {
  std::mutex m;
  std::condition_variable cv;
  bool job = false;
  bool busy = false;
  std::thread thread;
};
Worker* sWorker = nullptr;
bool sAsyncWanted = false;
bool sAsyncBroken = false;
bool sPending = false;
bool sHandlerDone = false;
bool sDeferredResume = false;
bool sHandlerPredicted = false;
bool sLastWasBabe = false;
int sSteady = 0;
bool sPredRaised = false;
uint32_t sPredMail = 0;
std::atomic<bool> sWorkerRaised{false};
thread_local bool tlOnWorker = false;
double sTestFactor = 1.0;
uint64_t sAsyncLists = 0, sAsyncMismatch = 0;

int usable_cpus() {
#ifdef _WIN32
  DWORD_PTR proc = 0, sys = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc != 0) {
    int n = 0;
    for (DWORD_PTR m = proc; m != 0; m &= m - 1) n++;
    return n;
  }
#elif defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == 0) return CPU_COUNT(&set);
#endif
  return static_cast<int>(std::thread::hardware_concurrency());
}

void async_init() {
  const char* e = std::getenv("XD_DSP_THREAD");
  if (e != nullptr && e[0] == '0') {
    sAsyncWanted = false;
  } else if (e != nullptr && e[0] != '\0') {
    sAsyncWanted = true;
  } else {
    sAsyncWanted = usable_cpus() >= 2;
  }
  const char* c = std::getenv("XD_TEST_SOUND_COST");
  if (c != nullptr && (c[0] == 'x' || c[0] == 'X')) sTestFactor = std::strtod(c + 1, nullptr);
}

void worker_main() {
  tlOnWorker = true;
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
  Worker& w = *sWorker;
  std::unique_lock<std::mutex> lock(w.m);
  for (;;) {
    w.cv.wait(lock, [&] { return w.job; });
    w.job = false;
    lock.unlock();
    run_until_idle();
    if (sTestFactor > 1.0) {
      const uint64_t until = static_cast<uint64_t>(static_cast<double>(tlLastRunNs) * (sTestFactor - 1.0));
      const auto t0 = std::chrono::steady_clock::now();
      while (static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count()) < until) {
      }
    }
    lock.lock();
    w.busy = false;
    w.cv.notify_all();
  }
}

void join_chip() {
  if (!sPending) return;
  Worker& w = *sWorker;
  {
    std::unique_lock<std::mutex> lock(w.m);
    w.cv.wait(lock, [&] { return !w.busy; });
  }
  sPending = false;
  const bool raised = sWorkerRaised.exchange(false);
  const uint32_t peek = GcDsp_CpuPeekMail(sDsp);
  const uint32_t mail = ((peek >> 31) & 1) ? (peek & 0x7FFFFFFF) : 0xFFFFFFFFu;
  bool bad = !raised || mail != sPredMail;
  if (sHandlerDone) {
    GcDsp_CpuTakeMail(sDsp);
    sInterruptQueued = false;
    run_until_idle();
    if (sDeferredResume) {
      sDeferredResume = false;
      GcDsp_CpuSendMail(sDsp, 0xCDD10003);
      run_until_idle();
      run_until_idle();
    }
    if (sInterruptQueued) bad = true;
  }
  sHandlerPredicted = false;
  sHandlerDone = false;
  if (bad) {
    sAsyncMismatch++;
    sAsyncBroken = true;
    std::fprintf(stderr,
                 "[platform/audio] DSP thread: the chip did not end its list as predicted (interrupt %d, mail %08lX); "
                 "running it on the game's thread from now on\n",
                 raised ? 1 : 0, static_cast<unsigned long>(mail));
  }
}

void launch_async(uint32_t mail) {
  GcDsp_CpuSendMail(sDsp, mail);
  if (sWorker == nullptr) {
    sWorker = new Worker();
    sWorker->thread = std::thread(worker_main);
    sWorker->thread.detach();
  }
  sPending = true;
  sHandlerDone = false;
  sWorkerRaised.store(false);
  if (sAsyncLists++ == 0) {
    std::fprintf(stderr, "[platform/audio] DSP thread: the sound chip's lists now run on a thread of their own "
                         "(XD_DSP_THREAD=0 turns that off)\n");
  }
  {
    std::lock_guard<std::mutex> lock(sWorker->m);
    sWorker->busy = true;
    sWorker->job = true;
  }
  sWorker->cv.notify_all();
  sInterruptQueued = true;
  sHandlerPredicted = true;
  PortOS_RaiseInterrupt(dsp_interrupt, nullptr);
}

void raise_interrupt(void*) {
  if (tlOnWorker) {
    sWorkerRaised.store(true);
    return;
  }
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
  u32 mail;
  bool predicted = false;
  if (sHandlerPredicted && sPending) {
    predicted = true;
    sHandlerPredicted = false;
    sHandlerDone = true;
    mail = sPredMail | 0x80000000u;
  } else {
    sHandlerPredicted = false;
    while (DSPCheckMailFromDSP() == 0) {
      run_until_idle();
      if (GcDsp_IsHalted(sDsp)) return;
    }
    mail = DSPReadMailFromDSP();
  }
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
    if (predicted) {
      sDeferredResume = true;
    } else {
      DSPSendMailToDSP(0xCDD10003);
      while (DSPCheckMailToDSP() != 0) {
      }
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
  async_init();
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
  join_chip();
  GcDsp* chip = sGhost ? sRealDsp : sDsp;
  if (chip != nullptr) GcDsp_LoadCoef(chip, sCoefTable);
  return order;
}

BOOL DSPCheckInit(void) { return sDsp != nullptr; }

DSPTaskInfo* DSPAddTask(DSPTaskInfo* task) {
  if (sDsp == nullptr) DSPInit();
  join_chip();
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
  if (sPending) return 0;
  run_until_idle();
  return sDsp != nullptr ? (GcDsp_CpuMailToDsp(sDsp) >> 31) & 1 : 0;
}

u32 DSPCheckMailFromDSP(void) {
  join_chip();
  return sDsp != nullptr ? (GcDsp_CpuPeekMail(sDsp) >> 31) & 1 : 0;
}

u32 DSPReadMailFromDSP(void) {
  if (sDsp == nullptr) return 0;
  join_chip();
  const u32 mail = GcDsp_CpuTakeMail(sDsp) & 0x7FFFFFFF;
  run_until_idle();
  return mail | 0x80000000u;
}

void PortDsp_Join(void) { join_chip(); }

void DSPSendMailToDSP(u32 mail) {
  if (sDsp == nullptr) return;
  join_chip();
  const bool isAddress = sLastWasBabe && (mail >> 16) != 0xBABE;
  sLastWasBabe = (mail >> 16) == 0xBABE;
  if (isAddress && !sGhost && sCurrentTask != nullptr) {
    if (sAsyncWanted && !sAsyncBroken && sSteady >= kSteadyLists && !sInterruptQueued && (sCurrentTask->flags & 2) == 0) {
      launch_async(mail);
      return;
    }
    const bool queued0 = sInterruptQueued;
    GcDsp_CpuSendMail(sDsp, mail);
    run_until_idle();
    const uint32_t peek = GcDsp_CpuPeekMail(sDsp);
    const bool raised = !queued0 && sInterruptQueued;
    const uint32_t m = ((peek >> 31) & 1) ? (peek & 0x7FFFFFFF) : 0xFFFFFFFFu;
    static const bool log = std::getenv("XD_DSP_THREAD_LOG") != nullptr;
    static int logged = 0;
    if (log && logged++ < 20) {
      std::fprintf(stderr, "[platform/audio] DSP thread: a list ended: interrupt %d (queued before %d), mail %08lX\n",
                   raised ? 1 : 0, queued0 ? 1 : 0, static_cast<unsigned long>(m));
    }
    if (raised && (m == 0x5CD10001u || m == 0x5CD10002u) && (sSteady == 0 || (sPredRaised && sPredMail == m))) {
      sSteady++;
    } else {
      sSteady = 0;
    }
    sPredRaised = raised;
    sPredMail = m;
    return;
  }
  GcDsp_CpuSendMail(sDsp, mail);
  run_until_idle();
}

void DSPReset(void) {
  join_chip();
  if (sDsp != nullptr) GcDsp_WriteControl(sDsp, 0x0001 | 0x0004);
}

void DSPHalt(void) {
  join_chip();
  if (sDsp != nullptr) GcDsp_WriteControl(sDsp, 0x0004);
}

u32 DSPGetDMAStatus(void) { return 0; }

bool PortDsp_GhostEnter(GcDsp* chip, bool pendingInterrupt) {
  join_chip();
  if (sDsp == nullptr || sCurrentTask == nullptr || sGhost || chip == nullptr) return false;
  sSavedSdk = {sInterruptQueued, sBooted, sInitialised, sYields, sInstructions.load()};
  sRealDsp = sDsp;
  sDsp = chip;
  sGhost = true;
  sGhostInterrupt = pendingInterrupt;
  sInterruptQueued = false;
  return true;
}

bool PortDsp_GhostBegin(void) {
  join_chip();
  if (sDsp == nullptr || sCurrentTask == nullptr || sGhost) return false;
  return PortDsp_GhostEnter(GcDsp_Clone(sDsp), sInterruptQueued);
}

void* PortDsp_GhostClone(bool* pendingInterrupt) {
  join_chip();
  if (sDsp == nullptr || sCurrentTask == nullptr || sGhost) return nullptr;
  if (pendingInterrupt != nullptr) *pendingInterrupt = sInterruptQueued;
  return GcDsp_Clone(sDsp);
}

void PortDsp_GhostDestroy(void* chip) {
  if (chip != nullptr) GcDsp_Destroy(static_cast<GcDsp*>(chip));
}

void PortDsp_GhostService(void) {
  if (!sGhost) return;
  while (sGhostInterrupt) {
    sGhostInterrupt = false;
    dsp_interrupt(nullptr);
  }
}

void* PortDsp_GhostLeave(bool* pendingInterrupt) {
  join_chip();
  if (!sGhost) return nullptr;
  GcDsp* chip = sDsp;
  if (pendingInterrupt != nullptr) *pendingInterrupt = sGhostInterrupt;
  sDsp = sRealDsp;
  sRealDsp = nullptr;
  sGhost = false;
  sGhostInterrupt = false;
  sInterruptQueued = sSavedSdk.interruptQueued;
  sBooted = sSavedSdk.booted;
  sInitialised = sSavedSdk.initialised;
  sYields = sSavedSdk.yields;
  sInstructions.store(sSavedSdk.instructions);
  return chip;
}

void PortDsp_GhostEnd(void) {
  if (!sGhost) return;
  PortDsp_GhostDestroy(PortDsp_GhostLeave(nullptr));
}

uint64_t PortDsp_Instructions(void) { return sInstructions.load(std::memory_order_relaxed); }

void PortAudio_GetDspStats(PortAudioStats* out) {
  out->dspTasksBooted = sBooted;
  out->dspInitialised = sInitialised;
  out->dspYields = sYields;
  out->dspInstructions = sInstructions.load(std::memory_order_relaxed);
  out->dspNs = sDspNs.load(std::memory_order_relaxed);
}

}
