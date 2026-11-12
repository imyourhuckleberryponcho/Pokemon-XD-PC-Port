#include "crash_report.h"
#include "port_log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <crtdbg.h>
#include <dbghelp.h>
#include <tlhelp32.h>

namespace {

void claim_report() {
  static volatile LONG sClaimed = 0;
  if (InterlockedExchange(&sClaimed, 1) != 0) {
    Sleep(INFINITE);
  }
}

SRWLOCK sPrintLock = SRWLOCK_INIT;
struct PrintLock {
  PrintLock() { AcquireSRWLockExclusive(&sPrintLock); }
  ~PrintLock() { ReleaseSRWLockExclusive(&sPrintLock); }
};

bool init_symbols(HANDLE process) {
  static int done = -1;
  if (done >= 0) return done != 0;
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  char* slash = std::strrchr(exe, 0x5C);
  if (slash != nullptr) *slash = 0;
  done = SymInitialize(process, exe[0] != 0 ? exe : nullptr, TRUE) != FALSE ? 1 : 0;
  return done != 0;
}

void print_stack(CONTEXT context) {
  PrintLock lock;
  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  static bool symbols = false;
  if (!symbols) {
    symbols = init_symbols(process);
  }
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  for (int depth = 0; depth < 48; depth++) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                     SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    const DWORD64 pc = frame.AddrPC.Offset;
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 offset = 0;
    const char* name = SymFromAddr(process, pc, &offset, symbol) ? symbol->Name : "?";
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD column = 0;
    if (SymGetLineFromAddr64(process, pc, &column, &line)) {
      const char* file = line.FileName;
      for (const char* p = file; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/') file = p + 1;
      }
      std::fprintf(stderr, "  #%-2d %s  (%s:%lu)\n", depth, name, file, line.LineNumber);
    } else {
      std::fprintf(stderr, "  #%-2d %s  (0x%llx)\n", depth, name, static_cast<unsigned long long>(pc));
    }
  }
  std::fflush(stderr);
}

constexpr unsigned kReportTimeoutMs = 30000;
PortCrashReportHook sReportHook = nullptr;
volatile LONG sReportRunning = 0;
volatile DWORD sReportThread = 0;

struct ReportJob {
  PortCrashReport report;
};

DWORD WINAPI report_thread(void* param) {
  sReportThread = GetCurrentThreadId();
  const auto* job = static_cast<const ReportJob*>(param);
  sReportHook(&job->report);
  return 0;
}

bool run_report_hook(const char* kind, const char* summary, void* exceptionPointers, unsigned timeoutMs) {
  if (sReportHook == nullptr || InterlockedExchange(&sReportRunning, 1) != 0) {
    return false;
  }
  ReportJob job{{kind, summary, exceptionPointers, GetCurrentThreadId()}};
  bool done = false;
  HANDLE thread = CreateThread(nullptr, 1 << 20, report_thread, &job, 0, nullptr);
  if (thread != nullptr) {
    done = WaitForSingleObject(thread, timeoutMs) == WAIT_OBJECT_0;
    if (!done) {
      std::fprintf(stderr, "*** The %s report didn't finish within %u s; what it saved so far is kept\n", kind,
                   timeoutMs / 1000);
    }
    CloseHandle(thread);
  }
  sReportThread = 0;
  InterlockedExchange(&sReportRunning, 0);
  return done;
}

void describe_crash(const EXCEPTION_POINTERS* info, char* out, size_t size) {
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  char where[512];
  PortCrash_DescribeAddress(record->ExceptionAddress, where, sizeof where);
  if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
    const ULONG_PTR kind = record->ExceptionInformation[0];
    std::snprintf(out, size, "exception 0x%08lX (%s address 0x%llx) in %s", record->ExceptionCode,
                  kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                  static_cast<unsigned long long>(record->ExceptionInformation[1]), where);
  } else {
    std::snprintf(out, size, "exception 0x%08lX in %s", record->ExceptionCode, where);
  }
}

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
  if (sReportThread != 0 && GetCurrentThreadId() == sReportThread) {
    std::fprintf(stderr, "\n*** The crash report itself crashed (exception 0x%08lX); report incomplete\n",
                 info->ExceptionRecord->ExceptionCode);
    ExitThread(1);
  }
  claim_report();
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  std::fflush(stdout);
  std::fprintf(stderr, "\n*** Crash: exception 0x%08lX at 0x%llx", record->ExceptionCode,
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(record->ExceptionAddress)));
  if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
    const ULONG_PTR kind = record->ExceptionInformation[0];
    std::fprintf(stderr, " (%s address 0x%llx)", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                 static_cast<unsigned long long>(record->ExceptionInformation[1]));
  }
  const CONTEXT& c = *info->ContextRecord;
  std::fprintf(stderr,
               "\nRegisters: rax %016llx rbx %016llx rcx %016llx rdx %016llx\n"
               "           rsi %016llx rdi %016llx rbp %016llx rsp %016llx\n"
               "           r8  %016llx r9  %016llx r10 %016llx r11 %016llx\n"
               "           r12 %016llx r13 %016llx r14 %016llx r15 %016llx",
               c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.Rbp, c.Rsp, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14,
               c.R15);
  std::fprintf(stderr, "\nCalls that led here (most recent first):\n");
  CONTEXT context = *info->ContextRecord;
  if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
      record->ExceptionInformation[0] == 8 && context.Rip < 0x10000) {
    std::fprintf(stderr, "  (called a null function pointer; the call was made from the first line below)\n");
    context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
    context.Rsp += 8;
  }
  print_stack(context);
  PortLog_Flush();
  char summary[768];
  describe_crash(info, summary, sizeof summary);
  run_report_hook("crash", summary, info, kReportTimeoutMs);
  PortLog_Flush();
  std::_Exit(3);
}

void on_abort(int) {
  claim_report();
  std::fflush(stdout);
  std::fprintf(stderr, "\n*** Stopped (abort). Calls that led here (most recent first):\n");
  CONTEXT context{};
  RtlCaptureContext(&context);
  print_stack(context);
  PortLog_Flush();
  run_report_hook("stop", "abort() (a failed check)", nullptr, kReportTimeoutMs);
  PortLog_Flush();
  std::_Exit(3);
}

void report_stop(const char* what) {
  claim_report();
  std::fflush(stdout);
  std::fprintf(stderr, "\n*** Stopped (%s). Calls that led here (most recent first):\n", what);
  CONTEXT context{};
  RtlCaptureContext(&context);
  print_stack(context);
  PortLog_Flush();
  run_report_hook("stop", what, nullptr, kReportTimeoutMs);
  PortLog_Flush();
  std::_Exit(3);
}

void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
  report_stop("invalid argument to a C library function");
}

void on_purecall() {
  report_stop("call of a pure virtual function: object already destroyed?");
}

}

extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize) {
  HANDLE process = GetCurrentProcess();
  static bool symbols = false;
  if (!symbols) {
    symbols = init_symbols(process);
  }
  const auto pc = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(address));
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  DWORD64 offset = 0;
  const char* name = SymFromAddr(process, pc, &offset, symbol) ? symbol->Name : "?";
  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD column = 0;
  if (SymGetLineFromAddr64(process, pc, &column, &line)) {
    const char* file = line.FileName;
    for (const char* p = file; *p != '\0'; p++) {
      if (*p == '\\' || *p == '/') file = p + 1;
    }
    std::snprintf(out, outSize, "%s (%s:%lu)", name, file, line.LineNumber);
  } else {
    std::snprintf(out, outSize, "%s (0x%llx)", name, static_cast<unsigned long long>(pc));
  }
}

extern "C" void PortCrash_SetReportHook(PortCrashReportHook hook) { sReportHook = hook; }

extern "C" int PortCrash_RunReportHook(const char* kind, const char* summary, void* exceptionPointers,
                                       unsigned timeoutMs) {
  return run_report_hook(kind, summary, exceptionPointers, timeoutMs) ? 1 : 0;
}

extern "C" int PortCrash_WriteMinidump(const wchar_t* path, const PortCrashReport* report) {
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return 0;
  }
  MINIDUMP_EXCEPTION_INFORMATION exception{};
  const bool hasException = report != nullptr && report->exceptionPointers != nullptr;
  if (hasException) {
    exception.ThreadId = report->threadId;
    exception.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(report->exceptionPointers);
    exception.ClientPointers = FALSE;
  }
  const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                               MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
  MINIDUMP_CALLBACK_INFORMATION callback{};
  callback.CallbackRoutine = [](PVOID, const PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT out) -> BOOL {
    if (in->CallbackType == ModuleCallback &&
        in->Module.BaseOfImage != reinterpret_cast<ULONG64>(GetModuleHandleW(nullptr))) {
      out->ModuleWriteFlags &= ~ModuleWriteDataSeg;
    }
    return TRUE;
  };
  BOOL ok;
  {
    PrintLock lock;
    ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                           hasException ? &exception : nullptr, nullptr, &callback);
  }
  CloseHandle(file);
  return ok ? 1 : 0;
}

namespace {
uintptr_t sWatchAddress = 0;
int sWatchHits = 0;

LONG CALLBACK on_watch(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  info->ContextRecord->Dr6 = 0;
  if (sWatchHits++ < 64) {
    std::fprintf(stderr, "\n*** Watch: write #%d to 0x%llx (now %08x). Calls (most recent first):\n", sWatchHits,
                 static_cast<unsigned long long>(sWatchAddress), *reinterpret_cast<volatile uint32_t*>(sWatchAddress));
    print_stack(*info->ContextRecord);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

struct WatchRequest {
  uintptr_t address;
  int threads;
};

void watch_thread(DWORD id, uintptr_t address) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, id);
  if (thread == nullptr) return;
  SuspendThread(thread);
  CONTEXT c{};
  c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  GetThreadContext(thread, &c);
  c.Dr0 = address;
  c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1 | (1ull << 16) | (3ull << 18);
  SetThreadContext(thread, &c);
  ResumeThread(thread);
  CloseHandle(thread);
}

DWORD WINAPI set_watch(void* param) {
  auto* req = static_cast<WatchRequest*>(param);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return 1;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId()) {
      watch_thread(te.th32ThreadID, req->address);
      req->threads++;
    }
  }
  CloseHandle(snap);
  return 0;
}
}

extern "C" void PortCrash_WatchWrites(uintptr_t address) {
  sWatchAddress = address & ~uintptr_t{3};
  AddVectoredExceptionHandler(1, on_watch);
  WatchRequest req{sWatchAddress, 0};
  HANDLE helper = CreateThread(nullptr, 0, set_watch, &req, 0, nullptr);
  WaitForSingleObject(helper, INFINITE);
  CloseHandle(helper);
  std::fprintf(stderr, "[platform/os] watching writes to 0x%llx on %d threads\n",
               static_cast<unsigned long long>(sWatchAddress), req.threads);
}

namespace {
struct SampleRequest {
  HANDLE thread;
  unsigned afterMs, everyMs, count;
};

DWORD WINAPI sample_thread(void* param) {
  const auto* req = static_cast<const SampleRequest*>(param);
  Sleep(req->afterMs);
  for (unsigned n = 1; n <= req->count; n++) {
    if (SuspendThread(req->thread) == static_cast<DWORD>(-1)) break;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(req->thread, &c)) {
      std::fprintf(stderr, "\n*** Sample %u of the main thread (%u ms in). Calls (most recent first):\n", n,
                   req->afterMs + (n - 1) * req->everyMs);
      print_stack(c);
    }
    ResumeThread(req->thread);
    Sleep(req->everyMs);
  }
  return 0;
}
}

extern "C" void PortCrash_PrintThreadStack(void* thread) {
  HANDLE h = static_cast<HANDLE>(thread);
  if (SuspendThread(h) == static_cast<DWORD>(-1)) return;
  CONTEXT c{};
  c.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(h, &c)) {
    print_stack(c);
  }
  ResumeThread(h);
}

extern "C" void PortCrash_SampleThread(unsigned afterMs, unsigned everyMs, unsigned count) {
  static SampleRequest req;
  HANDLE self = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                  THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
  req = {self, afterMs, everyMs, count};
  CloseHandle(CreateThread(nullptr, 0, sample_thread, &req, 0, nullptr));
}

extern "C" void PortCrash_Install(void) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  SetUnhandledExceptionFilter(on_crash);
  std::signal(SIGABRT, on_abort);
  _set_invalid_parameter_handler(on_invalid_parameter);
  _set_purecall_handler(on_purecall);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

#pragma warning(disable : 4073)
#pragma init_seg(lib)
namespace {
struct EarlyInstall {
  EarlyInstall() { PortCrash_Install(); }
} sEarlyInstall;
}

#else
extern "C" void PortCrash_Install(void) {}
extern "C" void PortCrash_SetReportHook(PortCrashReportHook) {}
extern "C" int PortCrash_RunReportHook(const char*, const char*, void*, unsigned) { return 0; }
extern "C" int PortCrash_WriteMinidump(const wchar_t*, const PortCrashReport*) { return 0; }
#endif
