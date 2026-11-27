#include <gsthread_port.h>
#ifdef _WIN32
#include <gsthread_coro.h>
#include <cstdlib>
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" uint32_t PortVI_GetFlipCount(void) { return 0; }
extern "C" void PortCrash_DescribeAddress(const void*, char* out, size_t outSize) {
  if (outSize > 0) out[0] = 0;
}

namespace {

struct FakeThread {
  const char* name;
  bool alive = true;
  uint32_t result = 0;
};

std::vector<FakeThread*> sList;
FakeThread* sActive = nullptr;
std::string sLog;

void exit_func(uint32_t result) {
  sActive->result = result;
  sActive->alive = false;
  PortGSthread_SwitchToScheduler();
}

void gsthread_switch() { PortGSthread_SwitchToScheduler(); }

uintptr_t task_a(uintptr_t arg, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                 uintptr_t, uintptr_t, uintptr_t) {
  for (int i = 0; i < 3; i++) {
    sLog += "A" + std::to_string(i) + " ";
    gsthread_switch();
  }
  return 100 + arg;
}

uintptr_t task_b(uintptr_t ptr, uintptr_t x, uintptr_t y, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                 uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  int* counter = reinterpret_cast<int*>(ptr);
  for (int i = 0; i < 2; i++) {
    *counter += static_cast<int>(x + y);
    sLog += "B" + std::to_string(i) + " ";
    gsthread_switch();
  }
  return 7;
}

int recurse(int n) {
  volatile char pad[256];
  pad[0] = static_cast<char>(n);
  return n == 0 ? pad[0] : recurse(n - 1) + 1;
}
uintptr_t task_c(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                 uintptr_t, uintptr_t, uintptr_t) {
  sLog += "C" + std::to_string(recurse(500)) + " ";
  gsthread_switch();
  sLog += "Cend ";
  return 3;
}

void execute_pass() {
  for (FakeThread* t : sList) {
    if (!t->alive) {
      continue;
    }
    sActive = t;
    PortGSthread_Run(t);
    sActive = nullptr;
    if (!t->alive) {
      PortGSthread_Destroy(t);
    }
  }
}

#ifdef _WIN32
int gTaskSum = 0;
uintptr_t task_d(uintptr_t id, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                 uintptr_t, uintptr_t, uintptr_t) {
  volatile int big[2000];
  for (int i = 0; i < 2000; i++) big[i] = static_cast<int>(id * 1000 + i);
  double d = 0.5 * static_cast<double>(id);
  for (int step = 0; step < 6; step++) {
    d = d * 1.5 + step;
    big[step] += step;
    gTaskSum += static_cast<int>(big[step] + big[1999]) + static_cast<int>(d);
    sLog += "D" + std::to_string(id) + "." + std::to_string(static_cast<int>(d)) + " ";
    gsthread_switch();
  }
  return 9;
}

bool roundtrip() {
  FakeThread d1{"D1"}, d2{"D2"};
  sList = {&d1, &d2};
  sLog.clear();
  gTaskSum = 0;
  PortGSthread_Create(&d1, reinterpret_cast<void*>(&task_d), exit_func, 0x1000);
  PortGSthread_SetArg(&d1, 0, 1);
  PortGSthread_Create(&d2, reinterpret_cast<void*>(&task_d), exit_func, 0x1000);
  PortGSthread_SetArg(&d2, 0, 2);
  execute_pass();
  execute_pass();
  gscoro::Snapshot snap;
  static std::string logAtSave, firstRun;
  static int sumAtSave, tries = 0;
  volatile int mainLocal = 1234;
  logAtSave = sLog;
  sumAtSave = gTaskSum;
  const int r = gscoro::Take(snap);
  if (r == 0) {
    for (int i = 0; i < 5; i++) execute_pass();
    firstRun = sLog;
    mainLocal = 99;
    sLog = logAtSave;
    gTaskSum = sumAtSave;
    tries++;
    gscoro::Restore(snap);
  }
  bool ok = true;
  auto check = [&](bool cond, const char* what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    ok &= cond;
  };
  check(r == 1, "Take returned 1 after Restore");
  check(mainLocal == 1234, "the main stack's local is as it was at the save");
  check(sLog == logAtSave, "port state put back");
  for (int i = 0; i < 5; i++) execute_pass();
  std::printf("first : %s\nsecond: %s\n", firstRun.c_str(), sLog.c_str());
  check(sLog == firstRun, "the passes after the restore equal the first run's");
  check(!d1.alive && !d2.alive, "tasks ended");
  return ok;
}
#endif
}

int main() {
#ifdef _WIN32
  if (const char* c = std::getenv("GS_COROUTINES"); c != nullptr && c[0] == '1') {
    gscoro::UseCoroutines([](size_t n, size_t) -> void* { return VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); });
    std::printf("coroutines on%c", 10);
  }
#endif
  FakeThread a{"A"}, b{"B"}, c{"C"};
  int counter = 0;
  sList = {&a, &b, &c};
  PortGSthread_Create(&a, reinterpret_cast<void*>(&task_a), exit_func, 0x1000);
  PortGSthread_SetArg(&a, 0, 5);
  PortGSthread_Create(&b, reinterpret_cast<void*>(&task_b), exit_func, 0x1000);
  PortGSthread_SetArg(&b, 0, reinterpret_cast<uintptr_t>(&counter));
  PortGSthread_SetArg(&b, 1, 2);
  PortGSthread_SetArg(&b, 2, 3);
  PortGSthread_Create(&c, reinterpret_cast<void*>(&task_c), exit_func, 0x1000);

  for (int pass = 0; pass < 6 && (a.alive || b.alive || c.alive); pass++) {
    sLog += "| ";
    execute_pass();
  }

  const std::string expected = "| A0 B0 C500 | A1 B1 Cend | A2 | ";
  bool ok = true;
  auto check = [&](bool cond, const char* what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    ok &= cond;
  };
  std::printf("run order: %s\n", sLog.c_str());
  check(sLog == expected, "tasks ran in GSthread's order, one step per pass");
  check(a.result == 105 && b.result == 7 && c.result == 3, "results reached the exit function");
  check(counter == 10, "a pointer start-up value arrived intact (64-bit)");
  check(!a.alive && !b.alive && !c.alive, "all tasks ended");
#ifdef _WIN32
  if (gscoro::On()) ok &= roundtrip();
#endif
  return ok ? 0 : 1;
}
