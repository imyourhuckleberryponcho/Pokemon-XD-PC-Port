#include "run_status.h"

#include "crash_report.h"
#include "port_log.h"
#include "port_user_folder.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace fs = std::filesystem;

namespace {
fs::path sFile;
std::string sLabel;
std::string sStarted;
uint32_t (*sProgress)(void) = nullptr;
unsigned sStuckSeconds = 0;
bool sExitWhenStuck = false;
HANDLE sMainThread = nullptr;
std::mutex sNoteLock;
std::string sNote;
std::atomic<bool> sStopped{false};
std::atomic<bool> sWaitingForPlayer{false};

std::string Now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char text[32];
  std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &tm);
  return text;
}

std::string CommandLineUtf8() {
  const wchar_t* line = GetCommandLineW();
  const int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, line, -1, out.data(), n, nullptr, nullptr);
  return out;
}

void Write(uint32_t progress, unsigned stillSeconds, const char* state) {
  std::string note;
  {
    std::lock_guard<std::mutex> lock(sNoteLock);
    note = sNote;
  }
  const fs::path tmp = fs::path(sFile).replace_extension(L".tmp");
  FILE* f = _wfopen(tmp.c_str(), L"wb");
  if (f == nullptr) return;
  std::fprintf(f, "pid %lu\nlabel %s\nstarted %s\nupdated %s\nstate %s\nframe %u\nstill_seconds %u\nnote %s\nlog %s\ncommand %s\n",
               GetCurrentProcessId(), sLabel.c_str(), sStarted.c_str(), Now().c_str(), state, progress, stillSeconds,
               note.c_str(), PortLog_Path(), CommandLineUtf8().c_str());
  std::fclose(f);
  MoveFileExW(tmp.c_str(), sFile.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void PrintOtherThreads() {
  const DWORD self = GetCurrentThreadId();
  const DWORD mainId = GetThreadId(sMainThread);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te{};
  te.dwSize = sizeof te;
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self || te.th32ThreadID == mainId) {
      continue;
    }
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                          te.th32ThreadID);
    if (h == nullptr) continue;
    char name[128] = "";
    PWSTR wide = nullptr;
    if (SUCCEEDED(GetThreadDescription(h, &wide)) && wide != nullptr) {
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof name, nullptr, nullptr);
      LocalFree(wide);
    }
    FILETIME created, exited, kernel, user;
    double cpuMs = 0.0;
    if (GetThreadTimes(h, &created, &exited, &kernel, &user)) {
      const auto ticks = [](const FILETIME& t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
      cpuMs = static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
    }
    std::fprintf(stderr, "[watchdog] thread %lu%s%s%s (CPU %.0f ms):\n", te.th32ThreadID, name[0] ? " \"" : "", name,
                 name[0] ? "\"" : "", cpuMs);
    PortCrash_PrintThreadStack(h);
    CloseHandle(h);
  }
  CloseHandle(snap);
}

DWORD WINAPI Watch(void*) {
  uint32_t last = sProgress();
  ULONGLONG lastMove = GetTickCount64();
  bool reported = false;
  while (!sStopped.load()) {
    Sleep(2000);
    if (sStopped.load()) break;
    const uint32_t now = sProgress();
    const ULONGLONG tick = GetTickCount64();
    if (sWaitingForPlayer.load()) {
      lastMove = tick;
      Write(now, 0, "waiting for the player");
      continue;
    }
    if (now != last) {
      last = now;
      lastMove = tick;
      if (reported) {
        std::fprintf(stderr, "[watchdog] moving again at frame %u\n", now);
        reported = false;
      }
    }
    const unsigned still = static_cast<unsigned>((tick - lastMove) / 1000);
    const bool stuck = sStuckSeconds != 0 && still >= sStuckSeconds;
    Write(now, still, stuck ? "STUCK" : (still >= 6 ? "slow" : "running"));
    if (stuck && !reported) {
      reported = true;
      std::fprintf(stderr, "\n[watchdog] STUCK: no new game frame for %u s (at frame %u). The main thread is in:\n",
                   still, now);
      PortCrash_PrintThreadStack(sMainThread);
      PrintOtherThreads();
      PortLog_Flush();
      Sleep(2000);
      std::fprintf(stderr, "[watchdog] 2 s later, the main thread:\n");
      PortCrash_PrintThreadStack(sMainThread);
      PrintOtherThreads();
      PortLog_Flush();
      if (sExitWhenStuck) {
        std::fprintf(stderr, "[watchdog] test run: ending with exit code 124\n");
        PortLog_Flush();
        Write(now, still, "STUCK (ended)");
        TerminateProcess(GetCurrentProcess(), 124);
      }
      char summary[128];
      std::snprintf(summary, sizeof summary, "no new game frame for %u s (at frame %u)", still, now);
      PortCrash_RunReportHook("stuck", summary, nullptr, 60000);
      PortLog_Flush();
    }
  }
  return 0;
}

void Remove() {
  sStopped.store(true);
  std::error_code ec;
  fs::remove(sFile, ec);
}
}

extern "C" void PortRunStatus_Start(const char* appFolder, const char* label, uint32_t (*progress)(void),
                                    unsigned stuckSeconds, bool exitWhenStuck) {
  static bool started = false;
  if (started || progress == nullptr) return;
  started = true;
  const fs::path folder = PortUser_Folder(appFolder) / L"running";
  std::error_code ec;
  fs::create_directories(folder, ec);
  sFile = folder / (L"p" + std::to_wstring(GetCurrentProcessId()) + L".txt");
  sLabel = label != nullptr ? label : "";
  sStarted = Now();
  sProgress = progress;
  sStuckSeconds = stuckSeconds;
  sExitWhenStuck = exitWhenStuck;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sMainThread,
                  THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
  Write(progress(), 0, "starting");
  std::atexit(Remove);
  CloseHandle(CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr));
}

extern "C" void PortRunStatus_SetWaitingForPlayer(bool waiting) { sWaitingForPlayer.store(waiting); }

extern "C" void PortRunStatus_SetNote(const char* note) {
  std::lock_guard<std::mutex> lock(sNoteLock);
  sNote = note != nullptr ? note : "";
}
