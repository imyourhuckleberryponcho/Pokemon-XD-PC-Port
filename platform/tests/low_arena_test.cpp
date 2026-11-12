#include "port_low_arena.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#include <new>
#endif

#ifdef LOW_TEST_WITH_MALLOC
extern "C" int PortLowMalloc_SelfTest(const char* tag, unsigned stressMb);
#ifdef __APPLE__
void* operator new(size_t size) {
  void* p = malloc(size == 0 ? 1 : size);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
#endif
#endif

namespace {

int sFailed = 0;
void expect(bool ok, const char* what) {
  if (!ok) {
    ++sFailed;
    printf("FAILED: %s\n", what);
  }
}
void expect_check(const char* where) {
  const int c = PortLow_Check();
  if (c != 0) {
    ++sFailed;
    printf("FAILED: lists not consistent (rule %d) %s\n", c, where);
  }
}

bool low(const void* p, size_t n) { return p != nullptr && reinterpret_cast<uintptr_t>(p) + n <= 0x100000000ull; }
unsigned rnd(unsigned* s) {
  *s = *s * 1664525u + 1013904223u;
  return *s >> 8;
}

struct Torture {
  unsigned seed;
  unsigned ops;
  bool check;
  int bad;
};

void* torture(void* arg) {
  Torture* t = static_cast<Torture*>(arg);
  enum { kSlots = 600 };
  static const size_t kAlign[] = {32, 64, 128, 4096, 65536, 1u << 20};
  void* block[kSlots] = {};
  size_t size[kSlots] = {};
  for (unsigned op = 0; op < t->ops; ++op) {
    const unsigned i = rnd(&t->seed) % kSlots;
    const unsigned kind = rnd(&t->seed) % 100;
    const size_t n = kind < 60 ? rnd(&t->seed) % 300 : kind < 90 ? rnd(&t->seed) % 5000 : kind < 99 ? rnd(&t->seed) % 200000
                                                                                                     : rnd(&t->seed) % (6u << 20);
    if (block[i] != nullptr) {
      const unsigned char v = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(block[i]) >> 4);
      const unsigned char* b = static_cast<unsigned char*>(block[i]);
      for (size_t k = 0; k < size[i]; k += (size[i] > 4096 ? 61 : 1)) {
        if (b[k] != v) {
          t->bad += 1;
          break;
        }
      }
      if (size[i] != 0 && b[size[i] - 1] != v) t->bad += 1;
      if (PortLow_Size(block[i]) < size[i]) t->bad += 1;
      if (rnd(&t->seed) % 3 == 0) {
        void* q = PortLow_Realloc(block[i], n);
        const size_t keep = size[i] < n ? size[i] : n;
        if (q == nullptr) {
          t->bad += 1;
          continue;
        }
        for (size_t k = 0; k < keep; k += (keep > 4096 ? 61 : 1)) {
          if (static_cast<unsigned char*>(q)[k] != v) {
            t->bad += 1;
            break;
          }
        }
        block[i] = q;
      } else {
        PortLow_Free(block[i]);
        block[i] = nullptr;
        continue;
      }
    } else if (rnd(&t->seed) % 8 == 0) {
      const size_t alignment = kAlign[rnd(&t->seed) % 6];
      block[i] = PortLow_Memalign(alignment, n);
      if ((reinterpret_cast<uintptr_t>(block[i]) & (alignment - 1)) != 0) t->bad += 1;
    } else if (rnd(&t->seed) % 8 == 0) {
      block[i] = PortLow_Calloc(n, 1);
      for (size_t k = 0; block[i] != nullptr && k < n; ++k) {
        if (static_cast<char*>(block[i])[k] != 0) {
          t->bad += 1;
          break;
        }
      }
    } else {
      block[i] = PortLow_Malloc(n);
    }
    if (!low(block[i], n) || (reinterpret_cast<uintptr_t>(block[i]) & 15) != 0 || !PortLow_Owns(block[i]) ||
        PortLow_Size(block[i]) < n) {
      t->bad += 1;
      if (block[i] == nullptr) continue;
    }
    size[i] = n;
    memset(block[i], static_cast<unsigned char>(reinterpret_cast<uintptr_t>(block[i]) >> 4), n);
    if (t->check && op % 20000 == 0 && PortLow_Check() != 0) t->bad += 1000;
  }
  for (unsigned i = 0; i < kSlots; ++i) PortLow_Free(block[i]);
  return nullptr;
}

bool child_aborts(int what) {
  fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    const int quiet = dup(2);
    (void)quiet;
    close(2);
    void* p = PortLow_Malloc(100);
    void* q = PortLow_Malloc(100);
    (void)q;
    if (what == 0) {
      PortLow_Free(p);
      PortLow_Free(p);
    } else if (what == 1) {
      PortLow_Free(static_cast<char*>(p) + 16);
    } else if (what == 2) {
      memset(p, 0xEE, 100 + 28);
      PortLow_Free(p);
      PortLow_Free(q);
    } else {
      port_low_test_limit = 1;
      PortLow_Malloc(900u << 20);
    }
    _exit(0);
  }
  int status = 0;
  if (pid < 0 || waitpid(pid, &status, 0) != pid) return false;
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

}

int main() {
  PortLowStats before, st;
  setvbuf(stdout, nullptr, _IONBF, 0);

  {
    static const size_t kSizes[] = {0, 1, 15, 16, 17, 24, 31, 32, 33, 1007, 1008, 1009, 1023, 1024, 1025, 65535, 65536, 1u << 20};
    void* p[18];
    bool ok = true;
    for (int i = 0; i < 18; ++i) {
      p[i] = PortLow_Malloc(kSizes[i]);
      ok = ok && low(p[i], kSizes[i]) && (reinterpret_cast<uintptr_t>(p[i]) & 15) == 0 && PortLow_Size(p[i]) >= kSizes[i] &&
           PortLow_Owns(p[i]);
      if (p[i] != nullptr) memset(p[i], 0xAB, kSizes[i]);
    }
    expect(ok, "blocks of the step sizes");
    expect_check("after the step sizes");
    expect(PortLow_Size(static_cast<char*>(p[9]) + 16) == 0, "an address inside a block is not a block");
    expect(PortLow_Size(&ok) == 0 && !PortLow_Owns(&ok) && !PortLow_Owns(nullptr), "a local variable is not ours");
    for (int i = 0; i < 18; i += 2) PortLow_Free(p[i]);
    expect_check("after freeing every second one");
    for (int i = 1; i < 18; i += 2) PortLow_Free(p[i]);
    expect_check("after freeing the rest");
  }
  PortLow_GetStats(&before);

  {
    Torture t = {0xC0FFEEu, 400000, true, 0};
    torture(&t);
    expect(t.bad == 0, "mixed use on one thread");
    expect_check("after the mixed use");
    PortLow_GetStats(&st);
    expect(st.blocks == before.blocks && st.used == before.used, "everything given back after the mixed use");
  }

  {
    char* a = static_cast<char*>(PortLow_Malloc(40u << 20));
    char* b = static_cast<char*>(PortLow_Malloc(300u << 20));
    expect(low(a, 40u << 20) && low(b, 300u << 20), "a 40 MB and a 300 MB block below 4 GB");
    if (a != nullptr) a[0] = a[(40u << 20) - 1] = 1;
    if (b != nullptr) b[0] = b[(300u << 20) - 1] = 1;
    b = static_cast<char*>(PortLow_Realloc(b, 310u << 20));
    expect(low(b, 310u << 20) && b[0] == 1 && b[(300u << 20) - 1] == 1, "the 300 MB block grown to 310 MB");
    PortLow_Free(a);
    PortLow_Free(b);
    expect_check("after the big blocks");
    PortLow_GetStats(&st);
    expect(st.reserved <= (200u << 20), "the big blocks' address space was given back");
    printf("big blocks: 40 MB at %p, 310 MB at %p; held afterwards %llu MB\n", static_cast<void*>(a), static_cast<void*>(b),
           static_cast<unsigned long long>(st.reserved >> 20));
  }

  {
    Torture t[4];
    pthread_t th[4];
    int bad = 0;
    for (int i = 0; i < 4; ++i) {
      t[i] = Torture{0x9E3779B9u * static_cast<unsigned>(i + 1), 150000, false, 0};
      pthread_create(&th[i], nullptr, torture, &t[i]);
    }
    for (int i = 0; i < 4; ++i) {
      pthread_join(th[i], nullptr);
      bad += t[i].bad;
    }
    expect(bad == 0, "mixed use on four threads");
    expect_check("after four threads");
  }

  {
    enum { kMost = 64 };
    void* p[kMost] = {};
    int got = 0;
    bool allLow = true;
    PortLow_GetStats(&st);
    port_low_fail_returns_null = 1;
    port_low_test_limit = st.reserved + (200u << 20);
    printf("(next line: the expected \"out of memory below 4 GB\" message)\n");
    while (got < kMost) {
      p[got] = PortLow_Malloc(24u << 20);
      if (p[got] == nullptr) break;
      allLow = allLow && low(p[got], 24u << 20);
      ++got;
    }
    expect(got >= 4 && got < kMost && allLow, "a full range refuses the request and hands out nothing high");
    for (int i = 0; i < got; ++i) PortLow_Free(p[i]);
    port_low_test_limit = 0;
    port_low_fail_returns_null = 0;
    void* again = PortLow_Malloc(24u << 20);
    expect(low(again, 24u << 20), "after the refusal, with room again, the same request is answered");
    PortLow_Free(again);
    expect_check("after the full range");
  }

  expect(child_aborts(0), "a block freed twice stops the program");
  expect(child_aborts(1), "freeing an address inside a block stops the program");
  expect(child_aborts(2), "a block written past its end is noticed when freed");
  expect(child_aborts(3), "a request that cannot be answered stops the program");

  PortLow_GetStats(&st);
  printf("low memory after part 1: held %llu MB (most %llu MB) of %llu MB, %llu blocks in use, %llu handed out, "
         "addresses 0x%llx-0x%llx\n",
         static_cast<unsigned long long>(st.reserved >> 20), static_cast<unsigned long long>(st.reservedPeak >> 20),
         static_cast<unsigned long long>(st.windowBytes >> 20), static_cast<unsigned long long>(st.blocks),
         static_cast<unsigned long long>(st.allocations), static_cast<unsigned long long>(st.lowest),
         static_cast<unsigned long long>(st.highest));

#ifdef LOW_TEST_WITH_MALLOC
  expect(PortLowMalloc_SelfTest("[low_arena_test]", 200) == 0, "the self-test through malloc and new");
#ifdef __APPLE__
  {
    malloc_zone_t* systemZone = malloc_default_zone();
    void* high = malloc_zone_malloc(systemZone, 100);
    void* high2 = malloc_zone_malloc(systemZone, 100);
    void* lowBlock = malloc(100);
    memset(high2, 0x33, 100);
    fprintf(stderr, "[low_arena_test] a block of the system's own zone at %p; the zone of a low block %p is \"%s\"; "
            "malloc_size says %zu\n", high, lowBlock,
            malloc_zone_from_ptr(lowBlock) != nullptr ? malloc_get_zone_name(malloc_zone_from_ptr(lowBlock)) : "(none)",
            malloc_size(lowBlock));
    expect(malloc_zone_from_ptr(lowBlock) != nullptr && malloc_zone_from_ptr(lowBlock) != malloc_zone_from_ptr(high),
           "the system knows the low block as a zone's block");
    expect(malloc_size(lowBlock) >= 100, "the system's malloc_size answers for a low block");
    free(high);
    char* moved = static_cast<char*>(realloc(high2, 5000));
    expect(low(moved, 5000) && moved[0] == 0x33 && moved[99] == 0x33, "a system block grown by the program moves below 4 GB");
    free(moved);
    malloc_zone_free(malloc_zone_from_ptr(lowBlock), lowBlock);
    expect(PortLow_Check() == 0, "lists consistent after the crossing blocks");
  }
#endif
#endif

  expect_check("at the end");
  printf(sFailed == 0 ? "low_arena_test: ALL PASSED\n" : "low_arena_test: %d FAILED\n", sFailed);
  return sFailed == 0 ? 0 : 1;
}
