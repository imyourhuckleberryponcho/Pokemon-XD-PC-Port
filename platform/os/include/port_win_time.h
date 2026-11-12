#ifndef PORT_WIN_TIME_H
#define PORT_WIN_TIME_H
#ifndef _WIN32
#include <stdint.h>
#include <time.h>

typedef unsigned long DWORD;
typedef union PortLargeInteger {
  int64_t QuadPart;
} LARGE_INTEGER;

static inline int QueryPerformanceCounter(LARGE_INTEGER* out) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  out->QuadPart = (int64_t)t.tv_sec * 1000000000LL + (int64_t)t.tv_nsec;
  return 1;
}
static inline int QueryPerformanceFrequency(LARGE_INTEGER* out) {
  out->QuadPart = 1000000000LL;
  return 1;
}
static inline void Sleep(DWORD ms) {
  struct timespec t;
  t.tv_sec = (time_t)(ms / 1000);
  t.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&t, 0);
}
static inline void YieldProcessor(void) { __builtin_ia32_pause(); }
#endif
#endif
