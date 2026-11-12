#if defined(__linux__) || defined(__APPLE__)
#include "run_status.h"

#include "crash_report.h"
#include "port_log.h"
#include "port_user_folder.h"

#include <pthread.h>
#ifdef __APPLE__
#include <crt_externs.h>
#endif
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace {
fs::path sFile;
std::string sLabel;
std::string sStarted;
uint32_t (*sProgress)(void) = nullptr;
unsigned sStuckSeconds = 0;
bool sExitWhenStuck = false;
std::mutex sNoteLock;
std::string sNote;
std::atomic<bool> sStopped{false};
std::atomic<bool> sWaitingForPlayer{false};
std::atomic<int> sMessageWindows{0};

std::string Now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char text[32];
  std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &tm);
  return text;
}

std::string CommandLine() {
  std::string out;
#ifdef __APPLE__
  {
    char** own = *_NSGetArgv();
    const int count = *_NSGetArgc();
    for (int i = 0; i < count; ++i) {
      if (i != 0) out += ' ';
      out += own[i];
    }
    return out;
  }
#endif
  if (std::FILE* f = std::fopen("/proc/self/cmdline", "rb")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) {
      out.append(buf, n);
    }
    std::fclose(f);
  }
  while (!out.empty() && out.back() == '\0') out.pop_back();
  for (char& c : out) {
    if (c == '\0') c = ' ';
  }
  return out;
}

unsigned long long TickMs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<unsigned long long>(t.tv_sec) * 1000ull + static_cast<unsigned long long>(t.tv_nsec) / 1000000ull;
}

void SleepMs(unsigned ms) {
  const timespec nap{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
  nanosleep(&nap, nullptr);
}

void Write(uint32_t progress, unsigned stillSeconds, const char* state) {
  std::string note;
  {
    std::lock_guard<std::mutex> lock(sNoteLock);
    note = sNote;
  }
  const fs::path tmp = fs::path(sFile).replace_extension(".tmp");
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) return;
  std::fprintf(f, "pid %lu\nlabel %s\nstarted %s\nupdated %s\nstate %s\nframe %u\nstill_seconds %u\nnote %s\nlog %s\ncommand %s\n",
               static_cast<unsigned long>(getpid()), sLabel.c_str(), sStarted.c_str(), Now().c_str(), state, progress,
               stillSeconds, note.c_str(), PortLog_Path(), CommandLine().c_str());
  std::fclose(f);
  std::error_code ec;
  fs::rename(tmp, sFile, ec);
}

void* Watch(void*) {
  uint32_t last = sProgress();
  unsigned long long lastMove = TickMs();
  bool reported = false;
  while (!sStopped.load()) {
    SleepMs(2000);
    if (sStopped.load()) break;
    const uint32_t now = sProgress();
    const unsigned long long tick = TickMs();
    if (sWaitingForPlayer.load() || sMessageWindows.load() > 0) {
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
      std::fprintf(stderr, "\n[watchdog] STUCK: no new game frame for %u s (at frame %u). (The threads' call stacks "
                           "are not printed on Linux and Mac yet.)\n",
                   still, now);
      PortLog_Flush();
      if (sExitWhenStuck) {
        std::fprintf(stderr, "[watchdog] test run: ending with exit code 124\n");
        PortLog_Flush();
        Write(now, still, "STUCK (ended)");
        _exit(124);
      }
      char summary[128];
      std::snprintf(summary, sizeof summary, "no new game frame for %u s (at frame %u)", still, now);
      PortCrash_RunReportHook("stuck", summary, nullptr, 60000);
      PortLog_Flush();
    }
  }
  return nullptr;
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
  const fs::path folder = PortUser_Folder(appFolder) / "running";
  std::error_code ec;
  fs::create_directories(folder, ec);
  sFile = folder / ("p" + std::to_string(getpid()) + ".txt");
  sLabel = label != nullptr ? label : "";
  sStarted = Now();
  sProgress = progress;
  sStuckSeconds = stuckSeconds;
  sExitWhenStuck = exitWhenStuck;
  Write(progress(), 0, "starting");
  std::atexit(Remove);
  pthread_t thread;
  if (pthread_create(&thread, nullptr, Watch, nullptr) == 0) {
    pthread_detach(thread);
  }
}

extern "C" void PortRunStatus_SetWaitingForPlayer(bool waiting) { sWaitingForPlayer.store(waiting); }
extern "C" void PortRunStatus_MessageWindow(bool open) { sMessageWindows.fetch_add(open ? 1 : -1); }

extern "C" void PortRunStatus_SetNote(const char* note) {
  std::lock_guard<std::mutex> lock(sNoteLock);
  sNote = note != nullptr ? note : "";
}
#endif
