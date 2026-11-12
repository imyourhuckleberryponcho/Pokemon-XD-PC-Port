#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static int sFailures = 0;
static int sGlobal = 1;

static bool low(const void* p) { return reinterpret_cast<uintptr_t>(p) < 0x100000000ull; }

static void report(const char* what, const void* p, bool mustBeLow) {
  const bool ok = !mustBeLow || low(p);
  std::printf("%-46s %18p  %s\n", what, p, low(p) ? "below 4 GB" : (mustBeLow ? "HIGH  <-- FAILED" : "high"));
  if (!ok) {
    ++sFailures;
  }
}

static void* map_at(uintptr_t addr, size_t size, int prot, int fd) {
  void* p = mmap(reinterpret_cast<void*>(addr), size, prot,
                 (fd >= 0 ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS) | MAP_FIXED_NOREPLACE, fd, 0);
  if (p == MAP_FAILED || p != reinterpret_cast<void*>(addr)) {
    std::printf("  mmap at %#zx (%zu MB) failed\n", static_cast<size_t>(addr), size >> 20);
    ++sFailures;
    return nullptr;
  }
  return p;
}

static void* thread_main(void* arg) {
  int local = 0;
  report(static_cast<const char*>(arg), &local, std::strstr(static_cast<const char*>(arg), "low stack") != nullptr);
  void* m = std::malloc(64);
  report("  malloc(64) on that thread", m, false);
  std::free(m);
  return nullptr;
}

static ucontext_t sMainContext, sFiberContext;
static void fiber_main() {
  int local = 0;
  report("fiber (ucontext) on a MAP_32BIT stack", &local, true);
  swapcontext(&sFiberContext, &sMainContext);
}

int main() {
  const bool tuned = std::getenv("LOW_MALLOC") != nullptr;
  if (tuned) {
    mallopt(M_MMAP_THRESHOLD, 1 << 30);
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_TOP_PAD, 64 << 20);
  }
  std::printf("malloc tuning (LOW_MALLOC): %s\n", tuned ? "on" : "off");
  report("function (program image)", reinterpret_cast<const void*>(&main), true);
  report("global variable", &sGlobal, true);
  int local = 0;
  report("main thread's stack", &local, false);
  void* small = std::malloc(64);
  void* big = std::malloc(8 << 20);
  report("malloc(64)", small, tuned);
  report("malloc(8 MB)", big, tuned);
  report("sbrk(0) (end of the brk heap)", sbrk(0), true);

  const size_t mem1Size = 0x01800000;
  const int fd = memfd_create("xd_mem1", 0);
  if (fd < 0 || ftruncate(fd, mem1Size) != 0) {
    std::printf("memfd_create failed\n");
    return 1;
  }
  auto* mem1 = static_cast<uint8_t*>(map_at(0x80000000, mem1Size, PROT_READ | PROT_WRITE, fd));
  auto* mem1u = static_cast<uint8_t*>(map_at(0xC0000000, mem1Size, PROT_READ | PROT_WRITE, fd));
  if (mem1 != nullptr && mem1u != nullptr) {
    mem1[0x1234] = 0x5A;
    const bool same = mem1u[0x1234] == 0x5A;
    std::printf("%-46s %s\n", "0x80000000 and 0xC0000000 are the same memory", same ? "yes" : "NO  <-- FAILED");
    sFailures += same ? 0 : 1;
  }
  map_at(0xE0000000, 0x4000, PROT_READ | PROT_WRITE, -1);
  void* lowBlock = map_at(0x70000000, 0x10000000, PROT_NONE, -1);
  if (lowBlock != nullptr) {
    const bool ok = mprotect(lowBlock, 0x10000, PROT_READ | PROT_WRITE) == 0;
    if (ok) {
      std::memset(lowBlock, 1, 0x10000);
    }
    std::printf("%-46s %s\n", "256 MB at 0x70000000, first 64 KB usable", ok ? "yes" : "NO  <-- FAILED");
    sFailures += ok ? 0 : 1;
  }

  pthread_t t;
  static char plain[] = "thread, ordinary stack";
  pthread_create(&t, nullptr, thread_main, plain);
  pthread_join(t, nullptr);
  const size_t stackSize = 1 << 20;
  void* stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_STACK, -1, 0);
  if (stack == MAP_FAILED) {
    std::printf("MAP_32BIT stack failed\n");
    ++sFailures;
  } else {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stackSize);
    static char lowName[] = "thread on a MAP_32BIT low stack";
    pthread_create(&t, &attr, thread_main, lowName);
    pthread_join(t, nullptr);
    getcontext(&sFiberContext);
    sFiberContext.uc_stack.ss_sp = stack;
    sFiberContext.uc_stack.ss_size = stackSize;
    sFiberContext.uc_link = &sMainContext;
    makecontext(&sFiberContext, fiber_main, 0);
    swapcontext(&sMainContext, &sFiberContext);
  }
  std::printf("%s\n", sFailures == 0 ? "OK: every piece of the low-memory layout worked" : "FAILED");
  return sFailures == 0 ? 0 : 1;
}
#else
int main() { return 0; }
#endif
