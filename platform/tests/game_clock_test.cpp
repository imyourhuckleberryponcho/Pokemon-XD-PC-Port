#include <cstdint>
#include <cstdio>

extern "C" {
int64_t PortTime_GameGetTime(void);
int64_t PortTime_GameGetRealTime(void);
uint32_t PortTime_GameGetTick(void);
void PortTime_SetGameReadHook(int64_t (*hook)(int64_t live, const void* caller));
void PortTime_SetGameClockFilter(int64_t (*filter)(int64_t pcClock));

int64_t sFakeNow = 0x123400000000LL;
int64_t OSGetTime(void) { return sFakeNow += 1000; }
int64_t PortTime_Now(void) { return OSGetTime(); }
bool PortTime_IsDeterministic(void) { return false; }
}

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

const void* sLastCaller = nullptr;
int64_t sLastLive = 0;
int sCalls = 0;
int64_t Hook(int64_t live, const void* caller) {
  sLastCaller = caller;
  sLastLive = live;
  sCalls++;
  return 0x0000000500000007LL;
}

#ifndef _MSC_VER
#define __declspec(x) __attribute__((x))
#endif
__declspec(noinline) const void* ReadOnce() {
  PortTime_GameGetTime();
  return sLastCaller;
}
}

int main() {
  check(PortTime_GameGetTime() == sFakeNow, "no hook: the clock unchanged");
  check(PortTime_GameGetTick() == static_cast<uint32_t>(sFakeNow & 0xFFFFFFFF), "OSGetTick: its low 32 bits");

  PortTime_SetGameReadHook(Hook);
  check(PortTime_GameGetTime() == 0x0000000500000007LL && sLastLive == sFakeNow && sCalls == 1,
        "hook: sees the live clock and decides the value");
  check(PortTime_GameGetTick() == 7u, "OSGetTick goes through the hook too");
  const void* places[3] = {ReadOnce(), ReadOnce(), ReadOnce()};
  check(places[0] != nullptr && places[0] == places[1] && places[1] == places[2], "one call: the same place each time");
  PortTime_GameGetTime();
  const void* other = sLastCaller;
  check(other != places[0], "another call: another place");

  PortTime_SetGameReadHook(nullptr);
  check(PortTime_GameGetTime() == sFakeNow && sCalls == 6, "hook removed: the clock unchanged again");

  static int64_t sField = 0;
  sField = sFakeNow + 50000;
  PortTime_SetGameClockFilter([](int64_t) { return sField; });
  const int64_t a = PortTime_GameGetTime(), b = PortTime_GameGetTime(), c = PortTime_GameGetTime();
  check(a == sField && b == sField + 1 && c == sField + 2, "filter: its value, reads within it one tick apart");
  sField += 675675;
  check(PortTime_GameGetTime() == sField, "filter: the next field's time exactly");
  const int64_t before = sField;
  sField -= 1000000;
  check(PortTime_GameGetTime() >= before, "filter: never backwards");
  check(PortTime_GameGetRealTime() == sFakeNow, "real-time reads (play time, save date): the PC's clock");
  PortTime_SetGameClockFilter(nullptr);
  check(PortTime_GameGetTime() == sFakeNow, "filter removed: the PC's clock again");

  std::printf(ok ? "all passed\n" : "FAILED\n");
  return ok ? 0 : 1;
}
