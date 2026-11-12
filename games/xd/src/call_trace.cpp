#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <port_user_folder.h>
#pragma comment(lib, "dbghelp.lib")

extern "C" {
uint32_t PortVI_GetFlipCount(void);
void* OSGetCurrentThread(void);
void PortXD_GameCodeStart(void);
void PortXD_GameCodeEnd(void);
unsigned char gPortCallTraceOn = 0;
unsigned char gPortCoverOnly = 0;
uintptr_t gPortCoverTable[1u << 17];
volatile long gPortCoverCount = 0;
uintptr_t gPortCoverCodeLo = 0, gPortCoverCodeHi = 0;
void PortCallTrace_Record(uintptr_t inFunction, uintptr_t stackPointer, const uintptr_t* regs);
int PortFuncCost_Init(void);
int PortFuncCost_Resolve(const char* name);
void PortFuncCost_Call(int entry, const uintptr_t* regs);
int PortOS_InHandler(void);
int64_t PortTime_Now(void);
void PortCpuCost_PayDebt(void);
double PortCpuCost_Debt(void);
void PortFuncCost_Statics(const uint8_t* partFound);
void PortFuncCost_TevGlobals(const void* const* toon, const void* const* shadows);
void PortCpuCost_Add(double vsyncs);
void PortCpuCost_SetBase(double vsyncs);
}

namespace {

constexpr size_t kGSthreadEntry = 40;
constexpr size_t kGSthreadName = 72;

uint32_t sFrom = 0, sTo = 0;
std::string sPath;
bool sExit = false;
SRWLOCK sLock = SRWLOCK_INIT;
bool sDone = false;
void** sActiveThread = nullptr;
uintptr_t sHandlersLo = 0, sHandlersHi = 0;

BOOL CALLBACK find_handlers(PSYMBOL_INFO s, ULONG, PVOID) {
  if (s->Tag == 5 && sHandlersLo == 0) {
    sHandlersLo = static_cast<uintptr_t>(s->Address);
    sHandlersHi = sHandlersLo + s->Size;
  }
  return TRUE;
}

std::vector<uint64_t> sRecords;
uint32_t sLastFrame = 0xFFFFFFFF;
DWORD sLastTid = 0;
void* sLastGs = reinterpret_cast<void*>(~uintptr_t(0));
struct ThreadInfo {
  DWORD tid;
  void* osThread;
};
std::vector<ThreadInfo> sThreads;
struct TaskInfo {
  uintptr_t entry;
  std::string name;
};
std::map<uintptr_t, TaskInfo> sTasks;

bool in_game_code(uintptr_t a) {
  return a >= reinterpret_cast<uintptr_t>(&PortXD_GameCodeStart) && a < reinterpret_cast<uintptr_t>(&PortXD_GameCodeEnd);
}

void push(uint32_t a, uint32_t b) { sRecords.push_back(a | (static_cast<uint64_t>(b) << 32)); }

bool sSymbols = false;
void init_symbols() {
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
  char exeDir[MAX_PATH];
  GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
  if (char* slash = strrchr(exeDir, '\\')) {
    *slash = '\0';
  }
  sSymbols = SymInitialize(GetCurrentProcess(), exeDir, TRUE) != FALSE;
  if (!sSymbols) {
    fprintf(stderr, "[call trace] dbghelp: no symbols (%lu); names will be missing\n", GetLastError());
    return;
  }
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  if (SymFromName(GetCurrentProcess(), "_activeThread", sym)) {
    sActiveThread = reinterpret_cast<void**>(sym->Address);
  } else {
    fprintf(stderr, "[call trace] GSthread's _activeThread not found: tasks won't be told apart\n");
  }
  SymEnumSymbols(GetCurrentProcess(), 0, "*!*run_handlers*", find_handlers, nullptr);
  if (sHandlersLo == 0) {
    fprintf(stderr, "[call trace] os_threads.cpp's run_handlers not found: interrupt callbacks not marked\n");
  }
}

const char* source_file(uintptr_t address) {
  IMAGEHLP_LINE64 line = {sizeof(line)};
  DWORD lineDisp = 0;
  if (sSymbols && SymGetLineFromAddr64(GetCurrentProcess(), address, &lineDisp, &line) && line.FileName) {
    return line.FileName;
  }
  return "?";
}

void describe(FILE* f, const char* tag, uintptr_t address) {
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 1023;
  DWORD64 disp = 0;
  const char* name = "?";
  if (sSymbols && SymFromAddr(GetCurrentProcess(), address, &disp, sym)) {
    name = sym->Name;
  }
  fprintf(f, "%s %08llx %s\t%s\n", tag, static_cast<unsigned long long>(address), name, source_file(address));
}

BOOL CALLBACK list_function(PSYMBOL_INFO sym, ULONG, PVOID context) {
  if (sym->Tag == 5 && in_game_code(static_cast<uintptr_t>(sym->Address))) {
    fprintf(static_cast<FILE*>(context), "D %08llx %s\t%s\n", static_cast<unsigned long long>(sym->Address),
            sym->Name, source_file(static_cast<uintptr_t>(sym->Address)));
  }
  return TRUE;
}

void finish() {
  if (sDone) {
    return;
  }
  sDone = true;
  gPortCallTraceOn = 0;
  fprintf(stderr, "[call trace] frame %u: writing %zu records\n", PortVI_GetFlipCount(), sRecords.size());
  std::set<uint32_t> funcs;
  for (uint64_t r : sRecords) {
    if ((r & 0xFFFFFFFF) >= 0x1000) {
      funcs.insert(static_cast<uint32_t>(r));
    }
  }
  const std::string names = sPath + ".txt";
  if (FILE* f = fopen(names.c_str(), "w")) {
    fprintf(f, "# call trace names: frames %u-%u, %zu records, %zu functions\n", sFrom, sTo, sRecords.size(),
            funcs.size());
    for (uint32_t a : funcs) {
      describe(f, "F", a);
    }
    for (size_t i = 0; i < sThreads.size(); i++) {
      fprintf(f, "T %zu %lu %08llx\n", i, sThreads[i].tid,
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(sThreads[i].osThread)));
      describe(f, "TS", reinterpret_cast<uintptr_t>(sThreads[i].osThread));
    }
    for (const auto& [ptr, task] : sTasks) {
      fprintf(f, "G %08llx %08llx %s\n", static_cast<unsigned long long>(ptr),
              static_cast<unsigned long long>(task.entry), task.name.c_str());
    }
    if (sSymbols) {
      const DWORD64 base = SymGetModuleBase64(GetCurrentProcess(), reinterpret_cast<DWORD64>(&PortXD_GameCodeStart));
      SymEnumSymbols(GetCurrentProcess(), base, "*", list_function, f);
    }
    fclose(f);
  }
  const std::string tmp = sPath + ".part";
  if (FILE* f = fopen(tmp.c_str(), "wb")) {
    fwrite("XDCALLT1", 1, 8, f);
    fwrite(&sFrom, 4, 1, f);
    fwrite(&sTo, 4, 1, f);
    fwrite(sRecords.data(), 8, sRecords.size(), f);
    fclose(f);
    MoveFileExA(tmp.c_str(), sPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    fprintf(stderr, "[call trace] frames %u-%u: %zu records, %zu functions -> %s\n", sFrom, sTo, sRecords.size(),
            funcs.size(), sPath.c_str());
  } else {
    fprintf(stderr, "[call trace] can't write %s\n", tmp.c_str());
  }
  fflush(stderr);
  if (sExit) {
    TerminateProcess(GetCurrentProcess(), 0);
  }
}

std::string sCoverPath;
uint32_t sCoverFrom = 0;
bool sTracing = false;
constexpr size_t kCoverSlots = 1u << 17;
std::atomic<bool> sCoverFull{false};
SRWLOCK sCoverWriteLock = SRWLOCK_INIT;
std::map<uintptr_t, std::string> sCoverNames;
std::string sCoverDefined;
uint32_t sCoverWritten = 0xFFFFFFFF;

void cover_add(uintptr_t a) {
  size_t i = static_cast<size_t>((static_cast<uint64_t>(a) * 0x9E3779B97F4A7C15ull) >> 47) & (kCoverSlots - 1);
  for (size_t probe = 0; probe < kCoverSlots; probe++, i = (i + 1) & (kCoverSlots - 1)) {
    auto* slot = reinterpret_cast<volatile LONG64*>(&gPortCoverTable[i]);
    const uintptr_t cur = static_cast<uintptr_t>(*slot);
    if (cur == a) {
      return;
    }
    if (cur == 0) {
      const LONG64 was = InterlockedCompareExchange64(slot, static_cast<LONG64>(a), 0);
      if (was == 0) {
        InterlockedIncrement(&gPortCoverCount);
        return;
      }
      if (static_cast<uintptr_t>(was) == a) {
        return;
      }
    }
  }
  sCoverFull.store(true);
}

std::string line_of(const char* tag, uintptr_t address) {
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 1023;
  DWORD64 disp = 0;
  const char* name = "?";
  if (sSymbols && SymFromAddr(GetCurrentProcess(), address, &disp, sym)) {
    name = sym->Name;
  }
  char head[64];
  snprintf(head, sizeof(head), "%s %08llx ", tag, static_cast<unsigned long long>(address));
  return std::string(head) + name + "\t" + source_file(address) + "\n";
}

BOOL CALLBACK collect_defined(PSYMBOL_INFO sym, ULONG, PVOID) {
  if (sym->Tag == 5 && in_game_code(static_cast<uintptr_t>(sym->Address))) {
    char head[64];
    snprintf(head, sizeof(head), "D %08llx ", static_cast<unsigned long long>(sym->Address));
    sCoverDefined += std::string(head) + sym->Name + "\t" + source_file(static_cast<uintptr_t>(sym->Address)) + "\n";
  }
  return TRUE;
}

void cover_write(bool final) {
  AcquireSRWLockExclusive(&sCoverWriteLock);
  const uint32_t count = static_cast<uint32_t>(gPortCoverCount);
  if (count != sCoverWritten || final) {
    for (size_t i = 0; i < kCoverSlots; i++) {
      const uintptr_t a = *reinterpret_cast<volatile uintptr_t*>(&gPortCoverTable[i]);
      if (a != 0 && sCoverNames.find(a) == sCoverNames.end()) {
        sCoverNames[a] = line_of("F", a);
      }
    }
    if (sCoverDefined.empty() && sSymbols) {
      const DWORD64 base = SymGetModuleBase64(GetCurrentProcess(), reinterpret_cast<DWORD64>(&PortXD_GameCodeStart));
      SymEnumSymbols(GetCurrentProcess(), base, "*", collect_defined, nullptr);
    }
    const std::string tmp = sCoverPath + ".part";
    if (FILE* f = fopen(tmp.c_str(), "w")) {
      fprintf(f, "# call coverage: %zu functions ran, up to game frame %u%s%s\n", sCoverNames.size(),
              PortVI_GetFlipCount(), final ? " (run ended)" : " (run still going)",
              sCoverFull.load() ? " TABLE FULL: some functions missing" : "");
      for (const auto& [a, text] : sCoverNames) {
        fputs(text.c_str(), f);
      }
      fputs(sCoverDefined.c_str(), f);
      fclose(f);
      MoveFileExA(tmp.c_str(), sCoverPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    sCoverWritten = count;
  }
  ReleaseSRWLockExclusive(&sCoverWriteLock);
}

LONG WINAPI cover_on_exception(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  static std::atomic<bool> once{false};
  if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == STATUS_STACK_BUFFER_OVERRUN) &&
      !once.exchange(true)) {
    if (TryAcquireSRWLockExclusive(&sCoverWriteLock)) {
      ReleaseSRWLockExclusive(&sCoverWriteLock);
      cover_write(true);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void cover_writer() {
  for (;;) {
    Sleep(250);
    cover_write(false);
  }
}

void watcher() {
  init_symbols();
  fprintf(stderr, "[call trace] symbols %s, GSthread _activeThread at %p\n", sSymbols ? "loaded" : "MISSING",
          static_cast<void*>(sActiveThread));
  if (!sCoverPath.empty()) {
    std::thread(cover_writer).detach();
  }
  if (!sTracing) {
    if (sCoverFrom != 0) {
      while (PortVI_GetFlipCount() < sCoverFrom) {
        Sleep(1);
      }
      gPortCallTraceOn = 1;
      fprintf(stderr, "[call trace] coverage on at game frame %u\n", PortVI_GetFlipCount());
    }
    return;
  }
  while (PortVI_GetFlipCount() + 2 < sFrom) {
    Sleep(1);
  }
  gPortCallTraceOn = 1;
  fprintf(stderr, "[call trace] on at game frame %u\n", PortVI_GetFlipCount());
}

std::string sCountPath;
bool sCountMode = false;
FILE* sCountFile = nullptr;
SRWLOCK sCountLock = SRWLOCK_INIT;
uintptr_t sCpuStart = 0, sCpuEnd = 0;
uintptr_t sCpuStartHi = 0, sCpuEndHi = 0;
const uint32_t* sPassCounter = nullptr;
constexpr uint32_t kCountSlots = 1u << 15;
uint32_t sCountAddr[kCountSlots];
uint32_t sCountN[kCountSlots];
uint32_t sCountUsed[kCountSlots];
uint32_t sCountUsedN = 0;
constexpr uint32_t kSeenSlots = 1u << 17;
uintptr_t sSeen[kSeenSlots];
uint32_t sSeenN = 0;
bool sCountOverflow = false;
thread_local bool tInCount = false;
uint32_t sCountPasses = 0;

void count_names();

inline uint32_t slot_of(uint64_t a, uint32_t mask) {
  return static_cast<uint32_t>((a * 0x9E3779B97F4A7C15ull) >> 40) & mask;
}

void count_add(uintptr_t a) {
  const uint32_t lo = static_cast<uint32_t>(a);
  uint32_t i = slot_of(lo, kCountSlots - 1);
  for (uint32_t probe = 0; probe < kCountSlots; probe++, i = (i + 1) & (kCountSlots - 1)) {
    if (sCountAddr[i] == lo) {
      sCountN[i]++;
      return;
    }
    if (sCountAddr[i] == 0) {
      sCountAddr[i] = lo;
      sCountN[i] = 1;
      sCountUsed[sCountUsedN++] = i;
      uint32_t j = slot_of(a, kSeenSlots - 1);
      for (uint32_t p2 = 0; p2 < kSeenSlots; p2++, j = (j + 1) & (kSeenSlots - 1)) {
        if (sSeen[j] == a) {
          break;
        }
        if (sSeen[j] == 0) {
          sSeen[j] = a;
          sSeenN++;
          break;
        }
      }
      return;
    }
  }
  sCountOverflow = true;
}

void count_flush(uint32_t pass) {
  if (sCountFile == nullptr) {
    return;
  }
  const uint32_t head[3] = {pass, PortVI_GetFlipCount(), sCountUsedN};
  fwrite(head, 4, 3, sCountFile);
  for (uint32_t k = 0; k < sCountUsedN; k++) {
    const uint32_t i = sCountUsed[k];
    const uint32_t rec[2] = {sCountAddr[i], sCountN[i]};
    fwrite(rec, 4, 2, sCountFile);
    sCountAddr[i] = 0;
    sCountN[i] = 0;
  }
  sCountUsedN = 0;
  if ((++sCountPasses & 255) == 0) {
    fflush(sCountFile);
  }
  if ((sCountPasses & 1023) == 0) {
    count_names();
  }
}

void count_names() {
  const std::string tmp = sCountPath + ".txt.part";
  if (FILE* f = fopen(tmp.c_str(), "w")) {
    fprintf(f, "# call counts names: %u addresses%s\n", sSeenN, sCountOverflow ? " (TABLE FULL: counts missing)" : "");
    for (uint32_t j = 0; j < kSeenSlots; j++) {
      if (sSeen[j] != 0) {
        describe(f, "F", sSeen[j]);
      }
    }
    fclose(f);
    MoveFileExA(tmp.c_str(), (sCountPath + ".txt").c_str(), MOVEFILE_REPLACE_EXISTING);
  }
}

bool find_function(const char* name, uintptr_t* lo, uintptr_t* hi) {
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  if (!sSymbols || !SymFromName(GetCurrentProcess(), name, sym)) {
    return false;
  }
  *lo = static_cast<uintptr_t>(sym->Address);
  *hi = *lo + (sym->Size != 0 ? sym->Size : 64);
  return true;
}

std::string sModelPath;
bool sModelOn = false;
std::unordered_map<std::string, double> sModelCosts;
double sModelDefault = 0.0;
constexpr uint32_t kModelSlots = 1u << 17;
uintptr_t sModelAddr[kModelSlots];
double sModelCost[kModelSlots];

bool pc_only_name(const char* n) {
  return std::strncmp(n, "gc_", 3) == 0 || std::strncmp(n, "port_", 5) == 0 || std::strncmp(n, "Port", 4) == 0 ||
         std::strstr(n, "gc_be_") != nullptr || std::strstr(n, "gc_ptr") != nullptr ||
         std::strstr(n, "gc_bef") != nullptr || std::strstr(n, "::operator ") != nullptr;
}

double model_cost(uintptr_t a) {
  uint32_t j = slot_of(a, kModelSlots - 1);
  for (uint32_t probe = 0; probe < kModelSlots; probe++, j = (j + 1) & (kModelSlots - 1)) {
    if (sModelAddr[j] == a) {
      return sModelCost[j];
    }
    if (sModelAddr[j] == 0) {
      alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 1023;
      DWORD64 disp = 0;
      double c = sModelDefault;
      if (sSymbols && SymFromAddr(GetCurrentProcess(), a, &disp, sym)) {
        if (pc_only_name(sym->Name)) {
          c = 0.0;
        } else {
          auto it = sModelCosts.find(sym->Name);
          if (it != sModelCosts.end()) {
            c = it->second;
          }
        }
      }
      sModelAddr[j] = a;
      sModelCost[j] = c;
      return c;
    }
  }
  return sModelDefault;
}

bool sFuncCostOn = false;
int sFuncEntry[kModelSlots];
uint32_t sCostTraceFrom = 1, sCostTraceTo = 0;
FILE* sCostTrace = nullptr;

void cost_trace(uintptr_t a) {
  const uint32_t pass = sPassCounter != nullptr ? *sPassCounter : 0;
  if (pass < sCostTraceFrom || pass > sCostTraceTo) {
    return;
  }
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 1023;
  DWORD64 disp = 0;
  const char* name = sSymbols && SymFromAddr(GetCurrentProcess(), a, &disp, sym) ? sym->Name : "?";
  fprintf(sCostTrace, "%u %lu %lld %.7f %d %s", pass, GetCurrentThreadId(),
          static_cast<long long>(PortTime_Now()), PortCpuCost_Debt(), PortOS_InHandler(), name);
  void* frames[12] = {};
  const USHORT got = RtlCaptureStackBackTrace(0, 12, frames, nullptr);
  const char* caller = "?";
  for (USHORT i = 0; i + 1 < got; i++) {
    if (reinterpret_cast<uintptr_t>(frames[i]) == a) {
      caller = sSymbols && SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i + 1]), &disp, sym)
                   ? sym->Name
                   : "?";
      break;
    }
  }
  fprintf(sCostTrace, " <- %s\n", caller);
}

int func_entry(uintptr_t a) {
  uint32_t j = slot_of(a, kModelSlots - 1);
  for (uint32_t probe = 0; probe < kModelSlots; probe++, j = (j + 1) & (kModelSlots - 1)) {
    if (sModelAddr[j] == a) {
      return sFuncEntry[j];
    }
    if (sModelAddr[j] == 0) {
      alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 1023;
      DWORD64 disp = 0;
      int e = -1;
      if (sSymbols && SymFromAddr(GetCurrentProcess(), a, &disp, sym) && !pc_only_name(sym->Name)) {
        e = PortFuncCost_Resolve(sym->Name);
      }
      sModelAddr[j] = a;
      sFuncEntry[j] = e;
      return e;
    }
  }
  return -1;
}

void load_model() {
  FILE* f = fopen(sModelPath.c_str(), "r");
  if (f == nullptr) {
    fprintf(stderr, "[cpu model] can't read %s\n", sModelPath.c_str());
    return;
  }
  char line[2048];
  double base = 0.0;
  while (fgets(line, sizeof(line), f) != nullptr) {
    if (line[0] == '#') {
      continue;
    }
    char* tab = strchr(line, '\t');
    if (tab == nullptr) {
      continue;
    }
    *tab = '\0';
    char* name = tab + 1;
    char* end = strchr(name, '\t');
    if (end == nullptr) {
      end = strchr(name, '\n');
    }
    if (end != nullptr) {
      *end = '\0';
    }
    if (strcmp(line, "BASE") == 0) {
      base = strtod(name, nullptr);
    } else if (strcmp(line, "DEFAULT") == 0) {
      sModelDefault = strtod(name, nullptr);
    } else {
      sModelCosts[name] = strtod(line, nullptr);
    }
  }
  fclose(f);
  PortCpuCost_SetBase(base);
  sModelOn = true;
  fprintf(stderr, "[cpu model] %zu functions, base %.4g, others %.3g vsyncs a call (%s)\n", sModelCosts.size(), base,
          sModelDefault, sModelPath.c_str());
}

void count_record(uintptr_t inFunction, const uintptr_t* regs) {
  if (tInCount) {
    return;
  }
  tInCount = true;
  AcquireSRWLockExclusive(&sCountLock);
  if (sCountFile != nullptr) {
    if (inFunction >= sCpuEnd && inFunction < sCpuEndHi) {
      count_add(inFunction);
      count_flush(sPassCounter != nullptr ? *sPassCounter : 0);
    } else if (inFunction >= sCpuStart && inFunction < sCpuStartHi) {
      if (sCountUsedN != 0) {
        count_flush(0xFFFFFFFFu);
      }
      count_add(inFunction);
    } else {
      count_add(inFunction);
    }
  }
  if (sModelOn) {
    PortCpuCost_Add(model_cost(inFunction));
  }
  if (sFuncCostOn) {
    if (sCostTrace != nullptr) {
      cost_trace(inFunction);
    }
    PortFuncCost_Call(func_entry(inFunction), regs);
  }
  ReleaseSRWLockExclusive(&sCountLock);
  tInCount = false;
  PortCpuCost_PayDebt();
}

void count_close() {
  AcquireSRWLockExclusive(&sCountLock);
  if (sCountFile != nullptr) {
    fclose(sCountFile);
    sCountFile = nullptr;
    count_names();
    fprintf(stderr, "[call counts] %u passes, %u functions -> %s\n", sCountPasses, sSeenN,
            sCountPath.c_str());
  }
  ReleaseSRWLockExclusive(&sCountLock);
}

void count_start() {
  init_symbols();
  if (!find_function("GSgfxPerfbarCPUStart", &sCpuStart, &sCpuStartHi) ||
      !find_function("GSgfxPerfbarCPUEnd", &sCpuEnd, &sCpuEndHi)) {
    fprintf(stderr, "[call counts] GSgfxPerfbarCPUStart/End not found: no call counts\n");
    return;
  }
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  if (SymFromName(GetCurrentProcess(), "pokecoloFrameCounter", sym)) {
    sPassCounter = reinterpret_cast<const uint32_t*>(sym->Address);
  }
  if (!sModelPath.empty()) {
    load_model();
  }
  if (PortFuncCost_Init() != 0) {
    if (SymFromName(GetCurrentProcess(), "_partFound", sym)) {
      PortFuncCost_Statics(reinterpret_cast<const uint8_t*>(sym->Address));
    } else {
      fprintf(stderr, "[func cost] _partFound not found: the part-finding callback counts 11 per joint\n");
    }
    if (SymFromName(GetCurrentProcess(), "tobj_toon", sym)) {
      PortFuncCost_TevGlobals(reinterpret_cast<const void* const*>(sym->Address), nullptr);
    }
    if (SymFromName(GetCurrentProcess(), "tobj_shadows", sym)) {
      PortFuncCost_TevGlobals(nullptr, reinterpret_cast<const void* const*>(sym->Address));
    }
    sFuncCostOn = !sModelOn;
    if (const char* r = getenv("XD_FUNC_COST_TRACE"); r != nullptr && r[0] != 0 && sFuncCostOn) {
      char* end = nullptr;
      sCostTraceFrom = static_cast<uint32_t>(strtoul(r, &end, 10));
      sCostTraceTo = end != nullptr && *end == '-' ? static_cast<uint32_t>(strtoul(end + 1, nullptr, 10)) : sCostTraceFrom;
      const char* file = getenv("XD_FUNC_COST_TRACE_FILE");
      sCostTrace = fopen(file != nullptr ? file : "func_cost_trace.txt", "w");
    }
  }
  if (!sCountPath.empty()) {
    AcquireSRWLockExclusive(&sCountLock);
    sCountFile = fopen(sCountPath.c_str(), "wb");
    if (sCountFile != nullptr) {
      fwrite("XDCALLC1", 1, 8, sCountFile);
    }
    ReleaseSRWLockExclusive(&sCountLock);
    if (sCountFile == nullptr) {
      fprintf(stderr, "[call counts] can't write %s\n", sCountPath.c_str());
    } else {
      fprintf(stderr, "[call counts] per CPU frame -> %s\n", sCountPath.c_str());
    }
  }
  gPortCallTraceOn = 1;
}

struct Setup {
  Setup() {
    const char* counts = getenv("XD_CALL_COUNTS");
    const char* model = getenv("XD_CPU_MODEL");
    const char* funcCost = getenv("XD_FUNC_COST");
    if (((counts != nullptr && counts[0] != '\0') || (model != nullptr && model[0] != '\0') ||
         (funcCost != nullptr && funcCost[0] != '\0' && funcCost[0] != '0')) &&
        getenv("XD_CALL_TRACE") == nullptr && getenv("XD_CALL_COVERAGE") == nullptr) {
      sCountPath = counts != nullptr ? counts : "";
      sModelPath = model != nullptr ? model : "";
      sCountMode = true;
      gPortCoverOnly = 0;
      std::thread(count_start).detach();
      atexit(count_close);
      return;
    }
    const char* cover = getenv("XD_CALL_COVERAGE");
    if (cover != nullptr && cover[0] != '\0') {
      sCoverPath = cover;
      const auto lo = reinterpret_cast<uintptr_t>(&PortXD_GameCodeStart);
      const auto hi = reinterpret_cast<uintptr_t>(&PortXD_GameCodeEnd);
      DWORD old = 0;
      if (hi > lo && VirtualProtect(reinterpret_cast<void*>(lo), hi - lo, PAGE_EXECUTE_READWRITE, &old)) {
        gPortCoverCodeLo = lo;
        gPortCoverCodeHi = hi;
      } else {
        fprintf(stderr, "[call trace] coverage: game code not writable (%lu): every call stays traced\n", GetLastError());
      }
      gPortCoverOnly = 1;
      const char* from = getenv("XD_CALL_COVERAGE_FROM");
      sCoverFrom = from != nullptr ? static_cast<uint32_t>(strtoul(from, nullptr, 10)) : 0;
      if (sCoverFrom == 0) {
        gPortCallTraceOn = 1;
      }
      fprintf(stderr, "[call trace] coverage (which game functions run) -> %s\n", sCoverPath.c_str());
      atexit([] { cover_write(true); });
      AddVectoredExceptionHandler(1, cover_on_exception);
    }
    const char* range = getenv("XD_CALL_TRACE");
    if (range == nullptr || range[0] == '\0') {
      if (!sCoverPath.empty()) {
        std::thread(watcher).detach();
      }
      return;
    }
    sTracing = true;
    gPortCoverOnly = 0;
    char* end = nullptr;
    sFrom = static_cast<uint32_t>(strtoul(range, &end, 10));
    sTo = (end != nullptr && *end == '-') ? static_cast<uint32_t>(strtoul(end + 1, nullptr, 10)) : sFrom;
    if (sTo < sFrom) {
      sTo = sFrom;
    }
    const char* path = getenv("XD_CALL_TRACE_FILE");
    if (path != nullptr && path[0] != '\0') {
      sPath = path;
    } else {
      std::string dir = std::string(PortUser_FolderUtf8("Pokemon XD")) + "\\calltrace";
      CreateDirectoryA(dir.c_str(), nullptr);
      sPath = dir + "\\trace_" + std::to_string(sFrom) + "_" + std::to_string(sTo) + ".bin";
    }
    const char* exitAfter = getenv("XD_CALL_TRACE_EXIT");
    sExit = exitAfter != nullptr && exitAfter[0] == '1';
    sRecords.reserve(1 << 22);
    fprintf(stderr, "[call trace] game frames %u-%u -> %s\n", sFrom, sTo, sPath.c_str());
    std::thread(watcher).detach();
    atexit([] {
      AcquireSRWLockExclusive(&sLock);
      finish();
      ReleaseSRWLockExclusive(&sLock);
    });
  }
} sSetup;

}

extern "C" void PortCallTrace_Record(uintptr_t inFunction, uintptr_t stackPointer, const uintptr_t* regs) {
  if (sCountMode) {
    count_record(inFunction, regs);
    return;
  }
  if (!sCoverPath.empty()) {
    cover_add(inFunction);
    if (!sTracing) {
      return;
    }
  }
  const uint32_t frame = PortVI_GetFlipCount();
  if (frame < sFrom) {
    return;
  }
  void* frames[4] = {};
  const USHORT got = RtlCaptureStackBackTrace(0, 4, frames, nullptr);
  const bool fromOutside = got < 4 || !in_game_code(reinterpret_cast<uintptr_t>(frames[3]));
  bool fromInterrupt = false;
  if (fromOutside && sHandlersLo != 0) {
    void* more[24] = {};
    const USHORT n = RtlCaptureStackBackTrace(2, 24, more, nullptr);
    for (USHORT i = 0; i < n && !fromInterrupt; i++) {
      const auto a = reinterpret_cast<uintptr_t>(more[i]);
      fromInterrupt = a >= sHandlersLo && a < sHandlersHi;
    }
  }
  AcquireSRWLockExclusive(&sLock);
  if (sDone) {
    ReleaseSRWLockExclusive(&sLock);
    return;
  }
  if (frame > sTo) {
    finish();
    ReleaseSRWLockExclusive(&sLock);
    return;
  }
  if (frame != sLastFrame) {
    push(1, frame);
    sLastFrame = frame;
  }
  const DWORD tid = GetCurrentThreadId();
  if (tid != sLastTid) {
    size_t i = 0;
    while (i < sThreads.size() && sThreads[i].tid != tid) {
      i++;
    }
    if (i == sThreads.size()) {
      sThreads.push_back({tid, OSGetCurrentThread()});
    }
    push(2, static_cast<uint32_t>(i));
    sLastTid = tid;
  }
  void* gs = sActiveThread != nullptr ? *sActiveThread : nullptr;
  if (gs != sLastGs) {
    uintptr_t entry = 0;
    if (gs != nullptr) {
      entry = *reinterpret_cast<uintptr_t*>(static_cast<char*>(gs) + kGSthreadEntry);
      if (!in_game_code(entry)) {
        entry = 0;
      }
      if (sTasks.find(reinterpret_cast<uintptr_t>(gs)) == sTasks.end()) {
        char name[0x21] = {};
        memcpy(name, static_cast<char*>(gs) + kGSthreadName, 0x20);
        for (char& c : name) {
          if (c != '\0' && (c < 0x20 || c > 0x7E)) {
            c = '?';
          }
        }
        sTasks[reinterpret_cast<uintptr_t>(gs)] = {entry, name};
      }
    }
    push(3, static_cast<uint32_t>(entry));
    push(4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gs)));
    sLastGs = gs;
  }
  push(static_cast<uint32_t>(inFunction),
       (static_cast<uint32_t>(stackPointer) & ~7u) | (fromOutside ? 1u : 0u) | (fromInterrupt ? 2u : 0u));
  ReleaseSRWLockExclusive(&sLock);
}
