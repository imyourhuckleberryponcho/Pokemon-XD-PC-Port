#include <aurora/time.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#include <dbghelp.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#endif

extern "C" int64_t OSGetTime(void);

namespace {

#if defined(__linux__) || defined(__APPLE__)
class FairMutex {
public:
  void lock() {
    std::unique_lock<std::mutex> l(m_);
    const uint64_t ticket = next_++;
    cv_.wait(l, [&] { return serving_ == ticket; });
  }
  void unlock() {
    {
      std::lock_guard<std::mutex> l(m_);
      ++serving_;
    }
    cv_.notify_all();
  }

private:
  std::mutex m_;
  std::condition_variable cv_;
  uint64_t next_ = 0, serving_ = 0;
};
using SchedMutex = FairMutex;
using SchedCv = std::condition_variable_any;
#else
using SchedMutex = std::mutex;
using SchedCv = std::condition_variable;
#endif

constexpr int kIdlePriority = 32;
constexpr int kMainPriority = 16;

struct PThread {
  void* key = nullptr;
  int priority = kMainPriority;
  int suspend = 0;
  bool waiting = false;
  bool dead = false;
  bool idle = false;
  bool idleEnabled = false;
  uint64_t seq = 0;
  void* (*func)(void*) = nullptr;
  void* param = nullptr;
  void* result = nullptr;
  bool started = false;
  SchedCv cv;
  std::vector<PThread*> joiners;
};

SchedMutex gSched;
SchedCv gTimerCv;
std::unordered_map<void*, PThread*> gThreads;
std::unordered_map<void*, std::deque<PThread*>> gQueues;
PThread* gRunning = nullptr;
uint64_t gSeq = 0;
thread_local PThread* tSelf = nullptr;
thread_local bool tInterruptsEnabled = true;
thread_local bool tInHandler = false;
struct PendingIrq {
  std::function<void()> run;
  uint32_t site = 0, seq = 0;
};
std::deque<PendingIrq> gPendingInterrupts;
alignas(8) unsigned char gMainThreadStruct[0x318];
alignas(8) unsigned char gIdleThreadStruct[0x318];

[[noreturn]] void fatal(const char* what) {
  std::fprintf(stderr, "[platform/sdk] OS threads: %s\n", what);
  std::fflush(stderr);
  std::abort();
}

bool runnable(const PThread* t) {
  return !t->dead && t->suspend == 0 && !t->waiting && (!t->idle || t->idleEnabled);
}

PThread* pick() {
  PThread* best = nullptr;
  for (auto& [key, t] : gThreads) {
    if (runnable(t) && (best == nullptr || t->priority < best->priority ||
                        (t->priority == best->priority && t->seq < best->seq))) {
      best = t;
    }
  }
  return best;
}

void make_ready(PThread* t) { t->seq = ++gSeq; }

void wait_for_token(std::unique_lock<SchedMutex>& lock, PThread* self) {
  self->cv.wait(lock, [&] { return gRunning == self; });
}

void hand_over(std::unique_lock<SchedMutex>& lock, PThread* self, PThread* next) {
  gRunning = next;
  if (next != nullptr) {
    next->cv.notify_one();
  }
  if (next == nullptr || next->idle) {
    gTimerCv.notify_all();
  }
  if (self != nullptr && !self->dead) {
    wait_for_token(lock, self);
  }
}

struct IrqSite {
  uintptr_t fn = 0;
  bool alarm = false;
  uint32_t seq = 0;
  uint32_t recorded = 0;
};
struct IrqNote {
  uint64_t count;
  uint32_t site, seq;
  char kind;
};
struct IrqSiteDecl {
  uint64_t rva;
  int alarm;
};
std::vector<IrqSite> gIrqSites;
std::unordered_map<uintptr_t, uint32_t> gIrqSiteOf[2];
uint64_t gIrqCount = 0;
uint64_t gIrqDelivered = 0;
std::vector<IrqNote> gIrqNotes;
bool gIrqNotesOn = true;
constexpr size_t kIrqNotesMax = 1u << 18;
bool gSchedOn = false;
bool gSchedFailed = false;
std::deque<IrqNote> gSchedule;
std::vector<IrqSiteDecl> gSchedSites;
std::vector<bool> gSchedSiteTaken;
bool gSchedByOrder = false;
std::vector<PendingIrq> gIrqPool;
uint64_t gSchedDelivered = 0;

uint64_t gTraceFrom = UINT64_MAX, gTraceTo = 0;
bool gTraceByFrame = false;
extern "C" uint32_t PortVI_GetFlipCount(void);
void irq_trace_init() {
  static bool done = false;
  if (done) return;
  done = true;
  if (const char* e = std::getenv("XD_IRQ_TRACE"); e != nullptr && *e != '\0') {
    char* end = nullptr;
    if (*e == 'f') {
      gTraceByFrame = true;
      e++;
    }
    gTraceFrom = std::strtoull(e, &end, 10);
    gTraceTo = (end != nullptr && *end == '-') ? std::strtoull(end + 1, nullptr, 10) : gTraceFrom;
#ifdef _WIN32
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
#endif
  }
}
#ifdef _WIN32
__declspec(noinline) void irq_trace(char what) {
  void* stack[10];
  const USHORT n = RtlCaptureStackBackTrace(2, 10, stack, nullptr);
  std::string line;
  for (USHORT i = 0; i < n && i < 7; i++) {
    char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    line += ' ';
    line += SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(stack[i]), &disp, sym) ? sym->Name : "?";
  }
  std::fprintf(stderr, "[irq trace] %llu %c thread %lu <-%s\n", static_cast<unsigned long long>(gIrqCount), what,
               GetCurrentThreadId(), line.c_str());
}
#else
__attribute__((noinline)) void irq_trace(char what) {
  void* stack[12];
  const int n = backtrace(stack, 12);
  std::string line;
  for (int i = 2; i < n && i < 9; i++) {
    Dl_info info{};
    line += ' ';
    line += (dladdr(stack[i], &info) != 0 && info.dli_sname != nullptr) ? info.dli_sname : "?";
  }
  std::fprintf(stderr, "[irq trace] %llu %c thread %lu <-%s\n", static_cast<unsigned long long>(gIrqCount), what,
#ifdef __APPLE__
               reinterpret_cast<unsigned long>(pthread_self()), line.c_str());
#else
               static_cast<unsigned long>(pthread_self()), line.c_str());
#endif
}
#endif
inline void irq_point(char what) {
  ++gIrqCount;
  if (gTraceFrom != UINT64_MAX) {
    const uint64_t at = gTraceByFrame ? PortVI_GetFlipCount() : gIrqCount;
    if (at >= gTraceFrom && at <= gTraceTo) irq_trace(what);
  }
}

#ifdef _WIN32
uintptr_t image_base() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }
#else
uintptr_t image_base() {
  static const uintptr_t base = [] {
    static const char anchor = 0;
    Dl_info info{};
    if (dladdr(&anchor, &info) != 0) {
      return reinterpret_cast<uintptr_t>(info.dli_fbase);
    }
    return static_cast<uintptr_t>(0);
  }();
  return base;
}
#endif

void irq_map_recorded(IrqSite& s) {
  if (gSchedSites.empty()) return;
  const uint64_t rva = s.fn - image_base();
  for (size_t i = 0; i < gSchedSites.size() && !gSchedByOrder && s.recorded == 0; i++) {
    if (!gSchedSiteTaken[i] && gSchedSites[i].rva == rva && (gSchedSites[i].alarm != 0) == s.alarm) {
      s.recorded = static_cast<uint32_t>(i + 1);
    }
  }
  if (s.recorded == 0) {
    gSchedByOrder = true;
    for (size_t i = 0; i < gSchedSites.size(); i++) {
      if (!gSchedSiteTaken[i] && (gSchedSites[i].alarm != 0) == s.alarm) {
        s.recorded = static_cast<uint32_t>(i + 1);
        break;
      }
    }
  }
  if (s.recorded != 0) gSchedSiteTaken[s.recorded - 1] = true;
}

uint32_t irq_site(uintptr_t fn, bool alarm) {
  auto& map = gIrqSiteOf[alarm ? 1 : 0];
  auto it = map.find(fn);
  if (it != map.end()) return it->second;
  IrqSite s;
  s.fn = fn;
  s.alarm = alarm;
  irq_map_recorded(s);
  gIrqSites.push_back(s);
  const uint32_t n = static_cast<uint32_t>(gIrqSites.size());
  map[fn] = n;
  return n;
}

void irq_note(const PendingIrq& p, char kind) {
  gIrqDelivered++;
  if (!gIrqNotesOn) return;
  if (gIrqNotes.size() >= kIrqNotesMax) {
    gIrqNotesOn = false;
    gIrqNotes.clear();
    gIrqNotes.shrink_to_fit();
    return;
  }
  gIrqNotes.push_back({gIrqCount, p.site, p.seq, kind});
}

bool alarm_take(uint32_t recordedSite, uint32_t seq, PendingIrq* out, int64_t* when, bool peek, bool advance);
void idle_run(std::unique_lock<SchedMutex>& lock);

void sched_stop(const std::string& why, bool failed) {
  if (!gSchedOn) return;
  gSchedOn = false;
  gSchedFailed = failed;
  std::fprintf(stderr, "[replay] interrupt schedule %s after %llu interrupts (delivery point %llu, %zu left): %s\n",
               failed ? "DROPPED" : "finished", static_cast<unsigned long long>(gSchedDelivered),
               static_cast<unsigned long long>(gIrqCount), gSchedule.size(), why.c_str());
  if (failed && !gSchedule.empty()) {
    const IrqNote& e = gSchedule.front();
    std::fprintf(stderr, "[replay]   next recorded: point %llu site %u seq %u kind %c; raised and waiting here:",
                 static_cast<unsigned long long>(e.count), e.site, e.seq, e.kind);
    for (const PendingIrq& p : gIrqPool) {
      std::fprintf(stderr, " %u.%u", gIrqSites[p.site - 1].recorded, p.seq);
    }
    std::fprintf(stderr, "\n");
  }
  std::fflush(stderr);
  gSchedule.clear();
  for (PendingIrq& p : gIrqPool) gPendingInterrupts.push_back(std::move(p));
  gIrqPool.clear();
  gTimerCv.notify_all();
}

bool sched_obtain(const IrqNote& e, PendingIrq* out) {
  for (size_t i = 0; i < gIrqPool.size(); i++) {
    if (gIrqSites[gIrqPool[i].site - 1].recorded == e.site && gIrqPool[i].seq == e.seq) {
      *out = std::move(gIrqPool[i]);
      gIrqPool.erase(gIrqPool.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return alarm_take(e.site, e.seq, out, nullptr, false, e.kind != 'p');
}

bool sched_due(char kind) {
  if (gSchedule.empty()) {
    sched_stop("the recording's list is at its end", false);
    return !gPendingInterrupts.empty();
  }
  const IrqNote& e = gSchedule.front();
  if (e.count < gIrqCount) {
    sched_stop("the game went past the place of the next recorded interrupt", true);
    return !gPendingInterrupts.empty();
  }
  return e.count == (kind == 'b' ? gIrqCount + 1 : gIrqCount) && e.kind == kind;
}

void run_one(std::unique_lock<SchedMutex>& lock, PendingIrq& p) {
  const bool wasEnabled = tInterruptsEnabled;
  tInterruptsEnabled = false;
  tInHandler = true;
  lock.unlock();
  p.run();
  lock.lock();
  tInHandler = false;
  tInterruptsEnabled = wasEnabled;
}

void run_handlers(std::unique_lock<SchedMutex>& lock, char kind) {
  bool first = true;
  while (gSchedOn) {
    if (gSchedule.empty()) {
      sched_stop("the recording's list is at its end", false);
      break;
    }
    const IrqNote e = gSchedule.front();
    const char want = (kind == 'i' && !first) ? 'j' : kind;
    if (e.count != gIrqCount || e.kind != want) {
      if (e.count < gIrqCount) {
        sched_stop("the game went past the place of the next recorded interrupt", true);
        break;
      }
      return;
    }
    PendingIrq p;
    if (!sched_obtain(e, &p)) {
      sched_stop("the next recorded interrupt doesn't exist here (not raised, or its alarm isn't set)", true);
      break;
    }
    gSchedule.pop_front();
    gSchedDelivered++;
    irq_note(p, want);
    first = false;
    run_one(lock, p);
  }
  while (!gSchedOn && !gPendingInterrupts.empty()) {
    PendingIrq p = std::move(gPendingInterrupts.front());
    gPendingInterrupts.pop_front();
    irq_note(p, (kind == 'i' && !first) ? 'j' : kind);
    first = false;
    run_one(lock, p);
  }
}

bool irq_due(char kind) { return gSchedOn ? sched_due(kind) : !gPendingInterrupts.empty(); }

void reschedule(std::unique_lock<SchedMutex>& lock, char kind = 'p') {
  if (tInHandler) {
    return;
  }
  PThread* self = tSelf;
  if (tInterruptsEnabled && !tInHandler) {
    if (kind == 'b') {
      if (irq_due('b')) {
        irq_point('b');
        run_handlers(lock, 'b');
      }
    } else {
      if (kind == 'p') irq_point('p');
      run_handlers(lock, 'p');
    }
  }
  PThread* next = pick();
  if (self != nullptr && runnable(self) && next != nullptr && next != self && next->priority >= self->priority) {
    next = self;
  }
  if (next != self) {
    hand_over(lock, self, next);
  }
}

PThread* current_or_register() {
  if (tSelf == nullptr && !tInHandler) {
    auto* t = new PThread();
    t->key = gMainThreadStruct;
    make_ready(t);
    std::lock_guard<SchedMutex> guard(gSched);
    if (gThreads.count(t->key)) {
      fatal("a second thread tried to become the main thread");
    }
    gThreads[t->key] = t;
    if (gRunning == nullptr) {
      gRunning = t;
    }
    tSelf = t;
  }
  return tSelf;
}

void thread_main(PThread* self) {
  tSelf = self;
  {
    std::unique_lock<SchedMutex> lock(gSched);
    wait_for_token(lock, self);
  }
  void* result = self->func(self->param);
  std::unique_lock<SchedMutex> lock(gSched);
  self->result = result;
  self->dead = true;
  for (PThread* j : self->joiners) {
    j->waiting = false;
    make_ready(j);
  }
  self->joiners.clear();
  hand_over(lock, self, pick());
}

#ifdef _WIN32
void start_game_thread(PThread* t) { std::thread(thread_main, t).detach(); }
#else
void start_game_thread(PThread* t) {
#ifdef __APPLE__
  constexpr size_t kStack = 0x100000, kGuard = 0x4000;
#else
  constexpr size_t kStack = 0x100000, kGuard = 0x1000;
#endif
  void* block = mmap(nullptr, kGuard + kStack, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_STACK, -1, 0);
  if (block == MAP_FAILED) {
    fatal("no memory below 2 GB for a game thread's stack");
  }
  mprotect(block, kGuard, PROT_NONE);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, static_cast<char*>(block) + kGuard, kStack);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t id;
  const int err = pthread_create(
      &id, &attr,
      [](void* p) -> void* {
        constexpr size_t kSignalStack = 0x10000;
        if (void* alt = mmap(nullptr, kSignalStack, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            alt != MAP_FAILED) {
          stack_t ss{};
          ss.ss_sp = alt;
          ss.ss_size = kSignalStack;
          sigaltstack(&ss, nullptr);
        }
        thread_main(static_cast<PThread*>(p));
        return nullptr;
      },
      t);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    fatal("could not start a game thread");
  }
}
#endif

struct Alarm {
  int64_t when = 0;
  int64_t period = 0;
  void (*handler)(void*, void*) = nullptr;
  bool armed = false;
  uint32_t site = 0, seq = 0;
};
std::map<void*, Alarm> gAlarms;

void alarm_arm(Alarm& a, int64_t when, int64_t period, void (*handler)(void*, void*)) {
  a.when = when;
  a.period = period;
  a.handler = handler;
  a.armed = true;
  a.site = irq_site(reinterpret_cast<uintptr_t>(handler), true);
  a.seq = ++gIrqSites[a.site - 1].seq;
}

PendingIrq alarm_fire(void* key, Alarm& a) {
  auto handler = a.handler;
  PendingIrq p{[key, handler] { handler(key, nullptr); }, a.site, a.seq};
  if (a.period > 0) {
    a.when += a.period;
  } else {
    a.armed = false;
  }
  return p;
}
bool gTimerStarted = false;

bool gDeterministic = false;
int64_t gVTime = 0;
int64_t gVBase = 0;
std::chrono::steady_clock::time_point gRealBase;
constexpr int64_t kDeterministicStart = 366LL * 86400LL * 40500000LL;

void init_time() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* det = std::getenv("XD_DETERMINISTIC");
  gDeterministic = det != nullptr && det[0] == '1';
  irq_trace_init();
  if (gDeterministic) {
    gVTime = gVBase = kDeterministicStart;
    gRealBase = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[platform/sdk] XD_DETERMINISTIC: the GameCube clock is virtual (moves while the CPU is idle)\n");
  }
}

int64_t now_locked() {
  init_time();
  return gDeterministic ? gVTime : OSGetTime();
}

bool gIdleHandlersRunning = false;

bool gYieldWaiting = false;
SchedCv gYieldCv;

bool gBusyWaiting = false;
int64_t gBusyUntil = 0;
std::atomic<int64_t> gIrqDebt{0};

bool yield_sleeps() {
  static const bool sleeps = [] {
    const char* spin = std::getenv("XD_YIELD_SPIN");
    return spin == nullptr || std::strcmp(spin, "1") != 0;
  }();
  return sleeps;
}

bool cpu_idle() {
  return (gRunning == nullptr || gRunning->idle || gYieldWaiting || gBusyWaiting) && gPendingInterrupts.empty() &&
         !gIdleHandlersRunning;
}

bool pace_to(std::unique_lock<SchedMutex>& lock, int64_t t) {
  const float scale = aurora::time::scale();
  const double speed = scale > 0.0f ? scale : 1.0;
  const auto due = gRealBase + std::chrono::microseconds(static_cast<int64_t>(
                                   static_cast<double>(t - gVBase) * 1e6 / 40500000.0 / speed));
  const auto now = std::chrono::steady_clock::now();
  if (now > due + std::chrono::milliseconds(100)) {
    gVBase = t;
    gRealBase = now;
  } else if (now < due) {
    lock.unlock();
    std::this_thread::sleep_until(due - std::chrono::microseconds(1500));
    while (std::chrono::steady_clock::now() < due) {
      std::this_thread::yield();
    }
    lock.lock();
    return false;
  }
  return true;
}

void raise_interrupt(std::unique_lock<SchedMutex>& lock, PendingIrq irq);

bool alarm_take(uint32_t recordedSite, uint32_t seq, PendingIrq* out, int64_t* when, bool peek, bool advance) {
  for (auto& [key, a] : gAlarms) {
    if (a.armed && a.seq == seq && a.site != 0 && gIrqSites[a.site - 1].recorded == recordedSite) {
      if (when != nullptr) *when = a.when;
      if (peek) return true;
      if (advance && a.when > gVTime) gVTime = a.when;
      *out = alarm_fire(key, a);
      return true;
    }
  }
  return false;
}

void deterministic_step(std::unique_lock<SchedMutex>& lock) {
  static bool warnedBusy = false;
  if (!gTimerCv.wait_for(lock, std::chrono::seconds(2), [] { return cpu_idle(); })) {
    if (!warnedBusy) {
      warnedBusy = true;
      std::fprintf(stderr, "[platform/sdk] deterministic time: the GameCube CPU has been busy for 2 s without "
                           "idling (the clock waits for it; a game loop polling for an interrupt would hang here)\n");
    }
    return;
  }
  void* key = nullptr;
  int64_t next = INT64_MAX;
  for (auto& [k, a] : gAlarms) {
    if (a.armed && a.when < next) {
      next = a.when;
      key = k;
    }
  }
  if (gBusyWaiting && gBusyUntil > gVTime && gBusyUntil < next) {
    if (!pace_to(lock, gBusyUntil)) {
      return;
    }
    gVTime = gBusyUntil;
    gBusyWaiting = false;
    gYieldCv.notify_all();
    return;
  }
  if (key == nullptr) {
    gTimerCv.wait(lock);
    return;
  }
  if (next > gVTime) {
    if (!pace_to(lock, next)) {
      return;
    }
    gVTime = next;
  }
  Alarm& a = gAlarms[key];
  if (!a.armed || a.when != next) {
    return;
  }
  raise_interrupt(lock, alarm_fire(key, a));
}

void scheduled_idle_step(std::unique_lock<SchedMutex>& lock) {
  gTimerCv.wait(lock, [] { return !gSchedOn || (gRunning == nullptr && !gIdleHandlersRunning && !gThreads.empty()); });
  if (!gSchedOn) return;
  if (gSchedule.empty()) {
    sched_stop("the recording's list is at its end", false);
  } else {
    const IrqNote e = gSchedule.front();
    if (e.kind != 'i' || e.count != gIrqCount) {
      sched_stop(e.count < gIrqCount ? "the game went past the place of the next recorded interrupt"
                                     : "every game thread waits, but the recorded run was still working here",
                 true);
    } else {
      int64_t when = 0;
      if (alarm_take(e.site, e.seq, nullptr, &when, true, false) && when > gVTime) {
        if (!pace_to(lock, when)) return;
      }
    }
  }
  idle_run(lock);
}

#ifndef _WIN32
std::atomic<bool> gTimerQuit{false}, gTimerGone{false};
void stop_timer_at_exit() {
  gTimerQuit.store(true);
  gTimerCv.notify_all();
  for (int i = 0; i < 300 && !gTimerGone.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
#define TIMER_QUIT() gTimerQuit.load()
#else
#define TIMER_QUIT() false
#endif

void timer_main() {
  init_time();
  std::unique_lock<SchedMutex> lock(gSched);
  for (;;) {
#ifndef _WIN32
    if (gTimerQuit.load()) {
      gTimerGone.store(true);
      return;
    }
#endif
    if (gSchedOn) {
      scheduled_idle_step(lock);
      continue;
    }
    if (gDeterministic) {
      deterministic_step(lock);
      continue;
    }
    const int64_t now = OSGetTime();
    int64_t next = INT64_MAX;
    void* dueKey = nullptr;
    for (auto& [key, a] : gAlarms) {
      if (a.armed && a.when < next) {
        next = a.when;
        dueKey = key;
      }
    }
    if (dueKey != nullptr && next <= now) {
      raise_interrupt(lock, alarm_fire(dueKey, gAlarms[dueKey]));
      continue;
    }
    if (next == INT64_MAX) {
      gTimerCv.wait(lock);
      continue;
    }
    const float scale = aurora::time::scale();
    const int64_t remainingUs = static_cast<int64_t>((next - now) * 8 / 324 / (scale > 0.0f ? scale : 1.0f));
    if (remainingUs > 2000) {
      gTimerCv.wait_for(lock, std::chrono::microseconds(remainingUs - 1500));
    } else {
      lock.unlock();
      while (!TIMER_QUIT() && OSGetTime() < next) {
        std::this_thread::yield();
      }
      lock.lock();
    }
  }
}

void ensure_timer() {
  if (!gTimerStarted) {
    gTimerStarted = true;
#ifdef _WIN32
    timeBeginPeriod(1);
#else
    std::atexit(stop_timer_at_exit);
#endif
    std::thread(timer_main).detach();
  }
}

void raise_interrupt(std::unique_lock<SchedMutex>& lock, PendingIrq irq) {
  if (gSchedOn) {
    gIrqPool.push_back(std::move(irq));
    return;
  }
  gPendingInterrupts.push_back(std::move(irq));
  if (gYieldWaiting || gBusyWaiting) {
    gYieldCv.notify_all();
  }
  if (gRunning == nullptr && !tInHandler && !gIdleHandlersRunning) {
    idle_run(lock);
  }
}

void idle_run(std::unique_lock<SchedMutex>& lock) {
  if (gRunning != nullptr || gIdleHandlersRunning || tInHandler) return;
  if (!gSchedOn && gPendingInterrupts.empty()) return;
  gIdleHandlersRunning = true;
  tInterruptsEnabled = true;
  run_handlers(lock, 'i');
  gIdleHandlersRunning = false;
  gTimerCv.notify_all();
  PThread* next = pick();
  if (next != nullptr) {
    gRunning = next;
    next->cv.notify_one();
  }
}

}

extern "C" {

void PortVI_FrameHookIfDue(uint32_t fields);

int OSDisableInterrupts(void) {
  const int previous = tInterruptsEnabled ? 1 : 0;
  tInterruptsEnabled = false;
  return previous;
}

thread_local int64_t tBusyOwed = 0;
int64_t PortOS_BusyFor(int64_t ticks);

void PortOS_BusyLater(int64_t ticks) {
  if (ticks > 0) tBusyOwed += ticks;
}

int PortOS_InHandler(void) { return tInHandler ? 1 : 0; }

int PortOS_OnGameThread(void) { return tSelf != nullptr ? 1 : 0; }

int OSEnableInterrupts(void) {
  const int previous = tInterruptsEnabled ? 1 : 0;
  tInterruptsEnabled = true;
  if (!previous && tBusyOwed > 0 && tSelf != nullptr && !tInHandler) {
    const int64_t owed = tBusyOwed;
    tBusyOwed = 0;
    PortOS_BusyFor(owed);
  }
  if (!previous && tSelf != nullptr && !tInHandler) {
    {
      std::unique_lock<SchedMutex> lock(gSched);
      irq_point('e');
      if (irq_due('p')) {
        reschedule(lock, 'e');
      }
    }
    PortVI_FrameHookIfDue(2);
  }
  return previous;
}

int OSRestoreInterrupts(int level) {
  if (level) {
    return OSEnableInterrupts();
  }
  return OSDisableInterrupts();
}

int64_t PortOS_BusyFor(int64_t ticks) {
  init_time();
  if (ticks <= 0 || !tInterruptsEnabled || tInHandler) {
    return 0;
  }
  PThread* self = current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  if (gRunning != self || gBusyWaiting) {
    return 0;
  }
  if (gSchedOn) {
    while (gSchedOn && irq_due('b')) {
      reschedule(lock, 'b');
    }
    return 0;
  }
  if (!gDeterministic) {
    const int64_t start = OSGetTime();
    const int64_t until = start + ticks;
    for (;;) {
      const int64_t now = OSGetTime();
      if (now >= until) {
        break;
      }
      const float scale = aurora::time::scale();
      const auto us = std::chrono::microseconds(static_cast<int64_t>(
          static_cast<double>(until - now) * 1e6 / 40500000.0 / (scale > 0.0f ? scale : 1.0)) + 1);
      gBusyWaiting = true;
      gYieldCv.wait_for(lock, us, [] { return !gPendingInterrupts.empty(); });
      gBusyWaiting = false;
      reschedule(lock, 'b');
    }
    return OSGetTime() - start;
  }
  ensure_timer();
  const int64_t start = gVTime;
  int64_t until = gVTime + ticks;
  gIrqDebt.store(0, std::memory_order_relaxed);
  static const bool busyLog = std::getenv("XD_BUSY_LOG") != nullptr;
  int64_t away = 0;
  int switches = 0;
  while (gVTime < until) {
    gBusyUntil = until;
    gBusyWaiting = true;
    gTimerCv.notify_all();
    gYieldCv.wait(lock, [until] { return !gPendingInterrupts.empty() || gVTime >= until; });
    gBusyWaiting = false;
    const int64_t before = gVTime;
    reschedule(lock, 'b');
    away += gVTime - before;
    until += gIrqDebt.exchange(0, std::memory_order_relaxed);
    switches++;
  }
  if (busyLog) {
    std::fprintf(stderr, "[busy] asked %lld spent %lld away %lld wakeups %d\n", static_cast<long long>(ticks),
                 static_cast<long long>(gVTime - start), static_cast<long long>(away), switches);
  }
  return gVTime - start;
}

void PortOS_InterruptCost(int64_t ticks) {
  if (ticks > 0) gIrqDebt.fetch_add(ticks, std::memory_order_relaxed);
}

void PortOS_RaiseInterrupt(void (*handler)(void*), void* arg) {
  std::unique_lock<SchedMutex> lock(gSched);
  const uint32_t site = irq_site(reinterpret_cast<uintptr_t>(handler), false);
  const uint32_t seq = ++gIrqSites[site - 1].seq;
  raise_interrupt(lock, PendingIrq{[handler, arg] { handler(arg); }, site, seq});
}

struct PortIrqNote {
  uint64_t count;
  uint32_t site, seq;
  char kind;
};
struct PortIrqSiteDecl {
  uint64_t rva;
  int alarm;
};

size_t PortOS_TakeIrqNotes(PortIrqNote* out, size_t max) {
  std::lock_guard<SchedMutex> guard(gSched);
  const size_t n = std::min(max, gIrqNotes.size());
  for (size_t i = 0; i < n; i++) {
    out[i] = {gIrqNotes[i].count, gIrqNotes[i].site, gIrqNotes[i].seq, gIrqNotes[i].kind};
  }
  gIrqNotes.erase(gIrqNotes.begin(), gIrqNotes.begin() + static_cast<std::ptrdiff_t>(n));
  return n;
}

int PortOS_IrqSiteInfo(uint32_t site, uint64_t* rva, int* alarm) {
  std::lock_guard<SchedMutex> guard(gSched);
  if (site == 0 || site > gIrqSites.size()) return 0;
  *rva = gIrqSites[site - 1].fn - image_base();
  *alarm = gIrqSites[site - 1].alarm ? 1 : 0;
  return 1;
}

int PortOS_SetIrqSchedule(const PortIrqSiteDecl* sites, size_t siteCount, const PortIrqNote* notes, size_t count) {
  init_time();
  std::unique_lock<SchedMutex> lock(gSched);
  if (!gDeterministic || count == 0 || gSchedOn) return 0;
  gSchedSites.clear();
  for (size_t i = 0; i < siteCount; i++) gSchedSites.push_back({sites[i].rva, sites[i].alarm});
  gSchedSiteTaken.assign(siteCount, false);
  gSchedByOrder = false;
  for (IrqSite& s : gIrqSites) {
    s.recorded = 0;
    irq_map_recorded(s);
  }
  gSchedule.clear();
  for (size_t i = static_cast<size_t>(std::min<uint64_t>(gIrqDelivered, count)); i < count; i++) {
    gSchedule.push_back({notes[i].count, notes[i].site, notes[i].seq, notes[i].kind});
  }
  gSchedOn = true;
  gSchedFailed = false;
  gSchedDelivered = 0;
  ensure_timer();
  gTimerCv.notify_all();
  return 1;
}

int PortOS_IrqScheduleActive(int* failed) {
  std::lock_guard<SchedMutex> guard(gSched);
  if (failed != nullptr) *failed = gSchedFailed ? 1 : 0;
  return gSchedOn ? 1 : 0;
}
uint64_t PortOS_IrqScheduleDelivered(void) {
  std::lock_guard<SchedMutex> guard(gSched);
  return gSchedDelivered;
}

void* OSGetCurrentThread(void) { return current_or_register()->key; }

int OSCreateThread(void* thread, void* (*func)(void*), void* param, void* stack, uint32_t stackSize,
                   int32_t priority, uint16_t attr) {
  (void)stack, (void)stackSize, (void)attr;
  current_or_register();
  if (priority < 0 || priority > 31) {
    return 0;
  }
  std::lock_guard<SchedMutex> guard(gSched);
  auto it = gThreads.find(thread);
  if (it != gThreads.end() && !it->second->dead) {
    fatal("OSCreateThread on a thread that is still alive");
  }
  auto* t = new PThread();
  t->key = thread;
  t->priority = priority;
  t->suspend = 1;
  t->func = func;
  t->param = param;
  gThreads[thread] = t;
  return 1;
}

int32_t OSResumeThread(void* thread) {
  current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  auto it = gThreads.find(thread);
  if (it == gThreads.end()) {
    fatal("OSResumeThread on an unknown thread");
  }
  PThread* t = it->second;
  const int32_t previous = t->suspend;
  if (t->suspend > 0 && --t->suspend == 0) {
    make_ready(t);
    if (!t->started) {
      t->started = true;
      start_game_thread(t);
    }
    reschedule(lock);
  }
  return previous;
}

int32_t OSSuspendThread(void* thread) {
  current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  auto it = gThreads.find(thread);
  if (it == gThreads.end()) {
    fatal("OSSuspendThread on an unknown thread");
  }
  const int32_t previous = it->second->suspend++;
  if (it->second == tSelf) {
    reschedule(lock);
  }
  return previous;
}

void OSYieldThread(void) {
  PThread* self = current_or_register();
  {
    std::unique_lock<SchedMutex> lock(gSched);
    const bool open = tInterruptsEnabled && !tInHandler;
    if (open) {
      irq_point('y');
      run_handlers(lock, 'p');
    }
    make_ready(self);
    PThread* next = pick();
    if (next != nullptr && next != self && next->priority <= self->priority) {
      hand_over(lock, self, next);
    } else if (gSchedOn) {
      if (open) {
        run_handlers(lock, 'w');
      }
    } else if (gDeterministic && tInterruptsEnabled && !tInHandler && gPendingInterrupts.empty()) {
      gYieldWaiting = true;
      gTimerCv.notify_all();
      gYieldCv.wait(lock, [] { return !gPendingInterrupts.empty(); });
      gYieldWaiting = false;
      run_handlers(lock, 'w');
    } else if (!gDeterministic && yield_sleeps() && tInterruptsEnabled && !tInHandler && gPendingInterrupts.empty()) {
      gYieldWaiting = true;
      gYieldCv.wait_for(lock, std::chrono::milliseconds(1), [] { return !gPendingInterrupts.empty(); });
      gYieldWaiting = false;
      run_handlers(lock, 'w');
    }
  }
  PortVI_FrameHookIfDue(1);
}

void OSInitThreadQueue(void* queue) {
  std::lock_guard<SchedMutex> guard(gSched);
  gQueues[queue].clear();
}

void OSSleepThread(void* queue) {
  PThread* self = current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  self->waiting = true;
  gQueues[queue].push_back(self);
  reschedule(lock);
}

void OSWakeupThread(void* queue) {
  current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  auto& q = gQueues[queue];
  for (PThread* t : q) {
    t->waiting = false;
    make_ready(t);
  }
  q.clear();
  if (!tInHandler) {
    reschedule(lock);
  }
}

void OSExitThread(void* value) {
  PThread* self = current_or_register();
  if (self->key == gMainThreadStruct) {
    fatal("the main thread called OSExitThread");
  }
  std::unique_lock<SchedMutex> lock(gSched);
  self->result = value;
  self->dead = true;
  for (PThread* j : self->joiners) {
    j->waiting = false;
    make_ready(j);
  }
  hand_over(lock, self, pick());
  lock.unlock();
#ifdef _WIN32
  ExitThread(0);
#else
  pthread_exit(nullptr);
#endif
}

int OSIsThreadTerminated(void* thread) {
  std::lock_guard<SchedMutex> guard(gSched);
  auto it = gThreads.find(thread);
  return it == gThreads.end() || it->second->dead;
}

int OSJoinThread(void* thread, void** value) {
  PThread* self = current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  auto it = gThreads.find(thread);
  if (it == gThreads.end()) {
    return 0;
  }
  PThread* t = it->second;
  if (!t->dead) {
    self->waiting = true;
    t->joiners.push_back(self);
    reschedule(lock);
  }
  if (value != nullptr) {
    *value = t->result;
  }
  return 1;
}

void* OSSetIdleFunction(void (*func)(void*), void* param, void* stack, uint32_t stackSize) {
  (void)stack, (void)stackSize;
  current_or_register();
  std::unique_lock<SchedMutex> lock(gSched);
  PThread*& idle = gThreads[gIdleThreadStruct];
  if (func == nullptr) {
    if (idle != nullptr) {
      idle->idleEnabled = false;
    }
    return nullptr;
  }
  if (idle != nullptr && idle->started) {
    fatal("OSSetIdleFunction: a second idle function (not used by the game so far)");
  }
  idle = new PThread();
  idle->key = gIdleThreadStruct;
  idle->priority = kIdlePriority;
  idle->idle = true;
  idle->idleEnabled = true;
  idle->func = [](void* p) -> void* {
    auto* fp = static_cast<std::pair<void (*)(void*), void*>*>(p);
    fp->first(fp->second);
    return nullptr;
  };
  idle->param = new std::pair<void (*)(void*), void*>(func, param);
  idle->started = true;
  make_ready(idle);
  start_game_thread(idle);
  return gIdleThreadStruct;
}

void OSCreateAlarm(void* alarm) {
  std::lock_guard<SchedMutex> guard(gSched);
  gAlarms[alarm] = Alarm{};
}

void OSSetAlarm(void* alarm, int64_t ticks, void (*handler)(void*, void*)) {
  std::lock_guard<SchedMutex> guard(gSched);
  ensure_timer();
  alarm_arm(gAlarms[alarm], now_locked() + ticks, 0, handler);
  gTimerCv.notify_one();
}

void OSSetPeriodicAlarm(void* alarm, int64_t start, int64_t period, void (*handler)(void*, void*)) {
  std::lock_guard<SchedMutex> guard(gSched);
  ensure_timer();
  alarm_arm(gAlarms[alarm], start, period, handler);
  gTimerCv.notify_one();
}

void PortOS_SetAlarmAt(void* alarm, int64_t when, void (*handler)(void*, void*)) {
  std::lock_guard<SchedMutex> guard(gSched);
  ensure_timer();
  alarm_arm(gAlarms[alarm], when, 0, handler);
  gTimerCv.notify_one();
}

void OSCancelAlarm(void* alarm) {
  std::lock_guard<SchedMutex> guard(gSched);
  auto it = gAlarms.find(alarm);
  if (it != gAlarms.end()) {
    it->second.armed = false;
  }
}

}

extern "C" int64_t PortTime_Now(void) {
  init_time();
  if (!gDeterministic) {
    return OSGetTime();
  }
  std::lock_guard<SchedMutex> guard(gSched);
  return gVTime;
}
extern "C" bool PortTime_IsDeterministic(void) {
  init_time();
  return gDeterministic;
}

extern "C" void PortTime_SetSpeed(float speed) {
  init_time();
  const float clamped =
      speed < 1.0f ? 1.0f : speed > aurora::time::kMaximumTimeScale ? aurora::time::kMaximumTimeScale : speed;
  std::lock_guard<SchedMutex> lock(gSched);
  if (aurora::time::scale() == clamped) {
    return;
  }
  if (gDeterministic) {
    gVBase = gVTime;
    gRealBase = std::chrono::steady_clock::now();
  }
  aurora::time::set_scale(clamped);
  gTimerCv.notify_all();
}
