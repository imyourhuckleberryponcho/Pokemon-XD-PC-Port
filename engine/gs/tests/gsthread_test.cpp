#include <gsthread_port.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

}

int main() {
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
  return ok ? 0 : 1;
}
