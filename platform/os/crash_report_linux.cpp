#if defined(__linux__) || defined(__APPLE__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "crash_report.h"
#include "port_log.h"
#include "port_seh.h"

#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {

constexpr unsigned kReportTimeoutMs = 60000;

std::atomic<int> sReporting{0};
std::atomic<int> sReportRunning{0};
std::atomic<long> sReportThread{0};
PortCrashReportHook sReportHook = nullptr;

#ifdef __APPLE__
enum { REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_RBP, REG_RSP, REG_R8, REG_R9, REG_R10, REG_R11,
       REG_R12, REG_R13, REG_R14, REG_R15, REG_RIP, REG_ERR, REG_COUNT };
typedef unsigned long long greg_t;
const greg_t* mac_gregs(const ucontext_t* uc, greg_t* r) {
  const auto& s = uc->uc_mcontext->__ss;
  const greg_t v[REG_COUNT] = {s.__rax, s.__rbx, s.__rcx, s.__rdx, s.__rsi, s.__rdi, s.__rbp, s.__rsp, s.__r8, s.__r9,
                               s.__r10, s.__r11, s.__r12, s.__r13, s.__r14, s.__r15, s.__rip, uc->uc_mcontext->__es.__err};
  for (int i = 0; i < REG_COUNT; ++i) r[i] = v[i];
  return r;
}
#define PORT_GREGS(uc) mac_gregs(uc, macRegisters)
#elif defined(__ANDROID__) && defined(__aarch64__)
enum { REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_RBP, REG_RSP, REG_R8, REG_R9, REG_R10, REG_R11,
       REG_R12, REG_R13, REG_R14, REG_R15, REG_RIP, REG_ERR, REG_COUNT };
typedef unsigned long long greg_t;
const greg_t* arm_gregs(const ucontext_t* uc, greg_t* r) {
  const auto& m = uc->uc_mcontext;
  const greg_t v[REG_COUNT] = {m.regs[0], m.regs[1], m.regs[2], m.regs[3], m.regs[4], m.regs[5], m.regs[29], m.sp,
                               m.regs[8], m.regs[9], m.regs[10], m.regs[11], m.regs[12], m.regs[13], m.regs[14],
                               m.regs[15], m.pc, 0};
  for (int i = 0; i < REG_COUNT; ++i) r[i] = v[i];
  return r;
}
#define PORT_GREGS(uc) arm_gregs(uc, macRegisters)
#else
#define PORT_GREGS(uc) ((uc)->uc_mcontext.gregs)
#endif

long thread_id() { return static_cast<long>(syscall(SYS_gettid)); }

void claim_report() {
  if (sReporting.exchange(1) != 0) {
    for (;;) {
      pause();
    }
  }
}

void print_frames(void* const* frames, int count) {
  for (int i = 0; i < count; i++) {
    char name[512];
    PortCrash_DescribeAddress(frames[i], name, sizeof name);
    std::fprintf(stderr, "  %s\n", name);
  }
}

struct ReportJob {
  PortCrashReport report;
  std::atomic<int> done{0};
};

void* report_thread(void* param) {
  sReportThread.store(thread_id());
  auto* job = static_cast<ReportJob*>(param);
  sReportHook(&job->report);
  job->done.store(1);
  return nullptr;
}

bool run_report_hook(const char* kind, const char* summary, void* exceptionPointers, unsigned timeoutMs) {
  if (sReportHook == nullptr || sReportRunning.exchange(1) != 0) {
    return false;
  }
  static ReportJob job;
  job.report = PortCrashReport{kind, summary, exceptionPointers, static_cast<unsigned long>(thread_id())};
  job.done.store(0);
  bool done = false;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1 << 20);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t thread;
  if (pthread_create(&thread, &attr, report_thread, &job) == 0) {
    for (unsigned waited = 0; waited < timeoutMs && !(done = job.done.load() != 0); waited += 5) {
      const timespec nap{0, 5000000};
      nanosleep(&nap, nullptr);
    }
    if (!done) {
      std::fprintf(stderr, "*** The %s report didn't finish within %u s; what it saved so far is kept\n", kind,
                   timeoutMs / 1000);
    }
  }
  pthread_attr_destroy(&attr);
  sReportThread.store(0);
  sReportRunning.store(0);
  return done;
}

const char* signal_name(int sig) {
  switch (sig) {
  case SIGSEGV: return "SIGSEGV (bad memory access)";
  case SIGBUS: return "SIGBUS (bad memory access)";
  case SIGILL: return "SIGILL (illegal instruction)";
  case SIGFPE: return "SIGFPE (arithmetic error, e.g. integer division by zero)";
  default: return "signal";
  }
}

thread_local PortSehFrame* tSehTop = nullptr;

void on_crash(int sig, siginfo_t* info, void* contextRaw) {
  if (PortSehFrame* frame = tSehTop; frame != nullptr) {
    siglongjmp(frame->env, 1);
  }
  if (sReportThread.load() != 0 && thread_id() == sReportThread.load()) {
    std::fprintf(stderr, "\n*** The crash report itself crashed (signal %d); report incomplete\n", sig);
    for (;;) {
      pause();
    }
  }
  claim_report();
  const auto* uc = static_cast<const ucontext_t*>(contextRaw);
#if defined(__APPLE__) || (defined(__ANDROID__) && defined(__aarch64__))
  greg_t macRegisters[REG_COUNT];
#endif
  const greg_t* r = PORT_GREGS(uc);
  const auto rip = static_cast<uintptr_t>(r[REG_RIP]);
  const auto fault = reinterpret_cast<uintptr_t>(info->si_addr);
  const bool memory = sig == SIGSEGV || sig == SIGBUS;
  const char* access = !memory ? "" : (r[REG_ERR] & 16) ? "executing" : (r[REG_ERR] & 2) ? "writing" : "reading";
  std::fflush(stdout);
  std::fprintf(stderr, "\n*** Crash: %s at 0x%" PRIxPTR, signal_name(sig), rip);
  if (memory) {
    std::fprintf(stderr, " (%s address 0x%" PRIxPTR ")", access, fault);
  }
  std::fprintf(stderr,
               "\nRegisters: rax %016llx rbx %016llx rcx %016llx rdx %016llx\n"
               "           rsi %016llx rdi %016llx rbp %016llx rsp %016llx\n"
               "           r8  %016llx r9  %016llx r10 %016llx r11 %016llx\n"
               "           r12 %016llx r13 %016llx r14 %016llx r15 %016llx",
               r[REG_RAX], r[REG_RBX], r[REG_RCX], r[REG_RDX], r[REG_RSI], r[REG_RDI], r[REG_RBP], r[REG_RSP], r[REG_R8],
               r[REG_R9], r[REG_R10], r[REG_R11], r[REG_R12], r[REG_R13], r[REG_R14], r[REG_R15]);
  std::fprintf(stderr, "\nCalls that led here (most recent first):\n");
  if (memory && (r[REG_ERR] & 16) && rip < 0x10000) {
    std::fprintf(stderr, "  (called a null function pointer; the call was made from the first line below)\n");
  }
  void* frames[64];
  const int count = backtrace(frames, 64);
  int first = 0;
  for (int i = 0; i < count; i++) {
    if (reinterpret_cast<uintptr_t>(frames[i]) == rip) {
      first = i;
      break;
    }
  }
  print_frames(frames + first, count - first);
  PortLog_Flush();
  char where[512], summary[768];
  PortCrash_DescribeAddress(reinterpret_cast<const void*>(rip), where, sizeof where);
  if (memory) {
    std::snprintf(summary, sizeof summary, "%s (%s address 0x%" PRIxPTR ") in %s", signal_name(sig), access, fault,
                  where);
  } else {
    std::snprintf(summary, sizeof summary, "%s in %s", signal_name(sig), where);
  }
  run_report_hook("crash", summary, nullptr, kReportTimeoutMs);
  PortLog_Flush();
  _exit(3);
}

void on_abort(int) {
  claim_report();
  std::fflush(stdout);
  std::fprintf(stderr, "\n*** Stopped (abort). Calls that led here (most recent first):\n");
  void* frames[64];
  const int count = backtrace(frames, 64);
  print_frames(frames, count);
  PortLog_Flush();
  run_report_hook("stop", "abort() (a failed check)", nullptr, kReportTimeoutMs);
  PortLog_Flush();
  _exit(3);
}

void not_yet(const char* what) {
  std::fprintf(stderr, "[crash report] %s: not on Linux yet\n", what);
}

#ifdef __linux__
constexpr int kSnapFrames = 40;
struct ThreadSnap {
  std::atomic<long> want{0};
  std::atomic<int> ready{0};
  int count = 0;
  void* frames[kSnapFrames];
};
ThreadSnap sSnap;

void on_snapshot(int) {
  if (sSnap.want.load() != thread_id()) return;
  sSnap.count = backtrace(sSnap.frames, kSnapFrames);
  sSnap.ready.store(1);
}

void print_snap_frame(void* address) {
  Dl_info info{};
  const auto a = reinterpret_cast<uintptr_t>(address);
  if (dladdr(address, &info) == 0) {
    std::fprintf(stderr, "    0x%" PRIxPTR "\n", a);
    return;
  }
  const char* module = info.dli_fname != nullptr ? info.dli_fname : "?";
  const char* slash = std::strrchr(module, '/');
  if (slash != nullptr) module = slash + 1;
  const uintptr_t off = a - reinterpret_cast<uintptr_t>(info.dli_fbase);
  if (info.dli_sname != nullptr) {
    std::fprintf(stderr, "    %s+0x%" PRIxPTR " (%s+0x%" PRIxPTR ")\n", info.dli_sname,
                 a - reinterpret_cast<uintptr_t>(info.dli_saddr), module, off);
  } else {
    std::fprintf(stderr, "    (%s+0x%" PRIxPTR ")\n", module, off);
  }
}

void thread_facts(long tid, char* name, size_t nameSize, char* state, unsigned long* ticks) {
  std::snprintf(name, nameSize, "?");
  *state = '?';
  *ticks = 0;
  char path[64];
  std::snprintf(path, sizeof path, "/proc/self/task/%ld/stat", tid);
  if (std::FILE* f = std::fopen(path, "r")) {
    char line[512];
    if (std::fgets(line, sizeof line, f) != nullptr) {
      const char* open = std::strchr(line, '(');
      const char* close = std::strrchr(line, ')');
      if (open != nullptr && close != nullptr && close > open) {
        std::snprintf(name, nameSize, "%.*s", static_cast<int>(close - open - 1), open + 1);
        unsigned long ut = 0, st = 0;
        char s = '?';
        long d[10];
        if (std::sscanf(close + 2, "%c %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %lu %lu", &s, &d[0], &d[1], &d[2],
                        &d[3], &d[4], &d[5], &d[6], &d[7], &d[8], &d[9], &ut, &st) == 13) {
          *state = s;
          *ticks = ut + st;
        }
      }
    }
    std::fclose(f);
  }
}
#endif

}

extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  Dl_info info{};
  const auto a = reinterpret_cast<uintptr_t>(address);
  if (dladdr(address, &info) != 0 && info.dli_sname != nullptr) {
    std::snprintf(out, outSize, "%s+0x%" PRIxPTR, info.dli_sname, a - reinterpret_cast<uintptr_t>(info.dli_saddr));
  } else if (info.dli_fname != nullptr) {
    const char* slash = std::strrchr(info.dli_fname, '/');
    std::snprintf(out, outSize, "0x%" PRIxPTR " (%s+0x%" PRIxPTR ")", a, slash != nullptr ? slash + 1 : info.dli_fname,
                  a - reinterpret_cast<uintptr_t>(info.dli_fbase));
  } else {
    std::snprintf(out, outSize, "0x%" PRIxPTR, a);
  }
}

extern "C" void PortCrash_SetReportHook(PortCrashReportHook hook) { sReportHook = hook; }

extern "C" int PortCrash_RunReportHook(const char* kind, const char* summary, void* exceptionPointers,
                                       unsigned timeoutMs) {
  return run_report_hook(kind, summary, exceptionPointers, timeoutMs) ? 1 : 0;
}

extern "C" int PortCrash_WriteMinidump(const wchar_t*, const PortCrashReport*) { return 0; }

extern "C" void PortCrash_WatchWrites(uintptr_t) { not_yet("XD_WATCH (watching writes to an address)"); }
extern "C" void PortCrash_SampleThread(unsigned, unsigned, unsigned) { not_yet("XD_SAMPLE (sampling a thread's stack)"); }
extern "C" void PortCrash_PrintThreadStack(void*) { not_yet("another thread's call stack"); }

extern "C" void PortCrash_PrintAllThreadStacks(const char* title) {
#ifdef __linux__
  static std::atomic<int> busy{0};
  if (busy.exchange(1) != 0) return;
  static int sig = 0;
  if (sig == 0) {
    sig = SIGRTMIN + 2;
    struct sigaction sa {};
    sa.sa_handler = on_snapshot;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
  }
  std::fprintf(stderr, "[watchdog] %s\n", title != nullptr ? title : "every thread's calls (most recent first):");
  const long self = thread_id();
  long tids[256];
  int n = 0;
  if (DIR* d = opendir("/proc/self/task")) {
    while (const dirent* e = readdir(d)) {
      const long t = std::atol(e->d_name);
      if (t > 0 && n < 256) tids[n++] = t;
    }
    closedir(d);
  }
  std::fprintf(stderr, "[watchdog] %d threads\n", n);
  for (int i = 0; i < n; i++) {
    char name[64], state;
    unsigned long ticks;
    thread_facts(tids[i], name, sizeof name, &state, &ticks);
    std::fprintf(stderr, "  thread %ld \"%s\" state %c, cpu %lu ticks%s\n", tids[i], name, state, ticks,
                 tids[i] == self ? " (the watchdog itself)" : "");
    if (tids[i] == self) continue;
    sSnap.ready.store(0);
    sSnap.count = 0;
    sSnap.want.store(tids[i]);
    if (syscall(SYS_tgkill, static_cast<long>(getpid()), tids[i], sig) != 0) {
      std::fprintf(stderr, "    (could not be asked)\n");
      continue;
    }
    for (int waited = 0; waited < 400 && sSnap.ready.load() == 0; waited++) {
      const timespec nap{0, 1000000};
      nanosleep(&nap, nullptr);
    }
    sSnap.want.store(0);
    if (sSnap.ready.load() == 0) {
      std::fprintf(stderr, "    (no answer within 0.4 s)\n");
      continue;
    }
    for (int f = 0; f < sSnap.count; f++) print_snap_frame(sSnap.frames[f]);
  }
  busy.store(0);
#else
  not_yet("every thread's call stack");
#endif
}

PortSehFrame::PortSehFrame() : previous(tSehTop) { tSehTop = this; }
PortSehFrame::~PortSehFrame() { tSehTop = previous; }
extern "C" PortSehFrame* PortSeh_Top(void) { return tSehTop; }

extern "C" void PortCrash_Install(void) {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  static char altStack[64 * 1024];
  stack_t ss{};
  ss.ss_sp = altStack;
  ss.ss_size = sizeof altStack;
  sigaltstack(&ss, nullptr);
  struct sigaction sa {};
  sa.sa_sigaction = on_crash;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
    sigaction(sig, &sa, nullptr);
  }
  struct sigaction ab {};
  ab.sa_handler = on_abort;
  ab.sa_flags = SA_ONSTACK;
  sigemptyset(&ab.sa_mask);
  sigaction(SIGABRT, &ab, nullptr);
}

namespace {
__attribute__((constructor(101))) void early_install() { PortCrash_Install(); }
}
#endif
