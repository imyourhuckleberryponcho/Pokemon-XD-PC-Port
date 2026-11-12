#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)

#include "port_low_arena.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

extern "C" int PortLowMalloc_Mode(void);

namespace {

const uintptr_t kFourGb = 0x100000000ull;
bool low(const void* p, size_t size) { return p != nullptr && reinterpret_cast<uintptr_t>(p) + size <= kFourGb; }

void* thread_block(void*) { return malloc(3000); }

struct Stress {
  unsigned seed;
  uint64_t bytesWanted;
  uint64_t bytes, count;
  int high, damaged, failed;
};

unsigned rnd(unsigned* s) {
  *s = *s * 1664525u + 1013904223u;
  return *s >> 8;
}

unsigned char pattern(const void* p, size_t size) { return static_cast<unsigned char>((reinterpret_cast<uintptr_t>(p) >> 4) ^ size); }

void fill(void* p, size_t size) {
  unsigned char* b = static_cast<unsigned char*>(p);
  const unsigned char v = pattern(p, size);
  if (size <= 512) {
    memset(b, v, size);
  } else {
    memset(b, v, 256);
    memset(b + size - 256, v, 256);
  }
}
bool intact(const void* p, size_t size, unsigned char v) {
  const unsigned char* b = static_cast<const unsigned char*>(p);
  const size_t edge = size <= 512 ? size : 256;
  for (size_t i = 0; i < edge; ++i) {
    if (b[i] != v || b[size - 1 - i] != v) return false;
  }
  return true;
}

void* stress_thread(void* arg) {
  Stress* s = static_cast<Stress*>(arg);
  enum { kSlots = 384 };
  void* block[kSlots] = {};
  size_t size[kSlots] = {};
  unsigned char mark[kSlots] = {};
  while (s->bytes < s->bytesWanted || s->count < 150000) {
    const unsigned i = rnd(&s->seed) % kSlots;
    const unsigned kind = rnd(&s->seed) % 100;
    size_t n = kind < 70 ? 1 + rnd(&s->seed) % 512 : kind < 95 ? 1 + rnd(&s->seed) % 65536 : 1 + rnd(&s->seed) % (3u << 20);
    if (s->bytes >= s->bytesWanted) n = 1 + n % 2048;
    if (block[i] != nullptr) {
      if (!intact(block[i], size[i], mark[i])) s->damaged += 1;
      if (rnd(&s->seed) % 4 == 0) {
        const size_t keep = size[i] < n ? size[i] : n;
        const size_t edge = keep < 256 ? keep : 256;
        void* q = realloc(block[i], n);
        if (q == nullptr) {
          s->failed += 1;
          continue;
        }
        for (size_t k = 0; k < edge; ++k) {
          if (static_cast<unsigned char*>(q)[k] != mark[i]) {
            s->damaged += 1;
            break;
          }
        }
        block[i] = q;
      } else {
        free(block[i]);
        block[i] = nullptr;
        continue;
      }
    } else {
      const unsigned how = rnd(&s->seed) % 16;
      if (how == 0) {
        const size_t alignment = static_cast<size_t>(32) << (rnd(&s->seed) % 8);
        if (posix_memalign(&block[i], alignment, n) != 0) block[i] = nullptr;
        if (block[i] != nullptr && (reinterpret_cast<uintptr_t>(block[i]) & (alignment - 1)) != 0) s->damaged += 1;
      } else if (how == 1) {
        block[i] = calloc(1, n);
        if (block[i] != nullptr && (static_cast<char*>(block[i])[0] != 0 || static_cast<char*>(block[i])[n - 1] != 0)) s->damaged += 1;
      } else if (how == 2) {
        block[i] = ::operator new(n);
      } else {
        block[i] = malloc(n);
      }
      if (block[i] == nullptr) {
        s->failed += 1;
        continue;
      }
    }
    if (!low(block[i], n)) s->high += 1;
    size[i] = n;
    mark[i] = pattern(block[i], n);
    fill(block[i], n);
    s->bytes += n;
    s->count += 1;
  }
  for (unsigned i = 0; i < kSlots; ++i) {
    if (block[i] != nullptr && !intact(block[i], size[i], mark[i])) s->damaged += 1;
    free(block[i]);
  }
  return nullptr;
}

double now_ms() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

}

extern "C" int PortLowMalloc_SelfTest(const char* tag, unsigned stressMb) {
  int problems = 0;
  const int mode = PortLowMalloc_Mode();
  fprintf(stderr, "%s: low memory: %s\n", tag,
          mode == 1 ? "the program's own malloc and new are answered below 4 GB; the system's libraries keep the system's memory"
          : mode == 2 ? "XD_LOW_MALLOC=all: every malloc of the process is answered below 4 GB, the system's libraries' too"
                      : "XD_LOW_MALLOC=all was asked for but the system did NOT take the low zone as its first");
  if (mode == -2) ++problems;

  {
    int* viaNew = new int[100];
    std::vector<int> list(5000, 7);
    std::string text(300, 'x');
    char* copy = strdup("a text copied by strdup");
    void* aligned = nullptr;
    void* fromThread = nullptr;
    pthread_t thread;
    if (posix_memalign(&aligned, 64, 1000) != 0) aligned = nullptr;
    if (pthread_create(&thread, nullptr, thread_block, nullptr) == 0) pthread_join(thread, &fromThread);
    const bool ok = low(viaNew, 400) && low(list.data(), 20000) && low(text.data(), 300) && low(copy, 24) &&
                    low(aligned, 1000) && (reinterpret_cast<uintptr_t>(aligned) & 63) == 0 && low(fromThread, 3000);
    fprintf(stderr, "%s: low memory: operator new at %p, a std::vector's storage at %p, a std::string's at %p, strdup at %p, "
            "an aligned (64) block at %p, a block made on a second thread at %p; all below 4 GB: %s\n",
            tag, static_cast<void*>(viaNew), static_cast<void*>(list.data()), static_cast<const void*>(text.data()),
            static_cast<void*>(copy), aligned, fromThread, ok ? "yes" : "NO");
    if (!ok) ++problems;
    delete[] viaNew;
    free(copy);
    free(aligned);
    free(fromThread);
  }

  {
    char* small = static_cast<char*>(malloc(100));
    bool kept = false, ok = false;
    void* first = small;
    char* grown = nullptr;
    if (small != nullptr) {
      memset(small, 0x5A, 100);
      grown = static_cast<char*>(realloc(small, 5u << 20));
      if (grown != nullptr) {
        kept = true;
        for (int i = 0; i < 100; ++i) kept = kept && grown[i] == 0x5A;
        grown[(5u << 20) - 1] = 1;
      }
    }
    ok = kept && low(first, 100) && low(grown, 5u << 20);
    fprintf(stderr, "%s: low memory: realloc: 100 bytes at %p grown to 5 MB at %p, contents kept: %s; both below 4 GB: %s\n",
            tag, first, static_cast<void*>(grown), kept ? "yes" : "NO", ok ? "yes" : "NO");
    if (!ok) ++problems;
    free(grown);
  }

  if (stressMb != 0) {
    enum { kThreads = 4 };
    Stress s[kThreads];
    pthread_t t[kThreads];
    bool made[kThreads] = {};
    uint64_t bytes = 0, count = 0;
    int high = 0, damaged = 0, failed = 0, started = 0;
    const double start = now_ms();
    for (int i = 0; i < kThreads; ++i) {
      s[i] = Stress{0x1234567u * static_cast<unsigned>(i + 1), (static_cast<uint64_t>(stressMb) << 20) / kThreads, 0, 0, 0, 0, 0};
      made[i] = pthread_create(&t[i], nullptr, stress_thread, &s[i]) == 0;
      if (made[i]) ++started;
    }
    for (int i = 0; i < kThreads; ++i) {
      if (made[i]) pthread_join(t[i], nullptr);
      bytes += s[i].bytes;
      count += s[i].count;
      high += s[i].high;
      damaged += s[i].damaged;
      failed += s[i].failed;
    }
    const double took = now_ms() - start;
    const int check = PortLow_Check();
    const bool ok = started == kThreads && high == 0 && damaged == 0 && failed == 0 && check == 0;
    fprintf(stderr, "%s: low memory: stress: %d threads, %llu blocks (malloc, calloc, realloc, aligned, new), %llu MB handed out "
            "in all, %.0f ms; blocks above 4 GB: %d; blocks with damaged contents: %d; requests refused: %d; "
            "block lists consistent: %s (%d); verdict: %s\n",
            tag, started, static_cast<unsigned long long>(count), static_cast<unsigned long long>(bytes >> 20), took, high,
            damaged, failed, check == 0 ? "yes" : "NO", check, ok ? "OK" : "FAILED");
    if (!ok) ++problems;
  }

  {
    PortLowStats st;
    PortLow_GetStats(&st);
    fprintf(stderr, "%s: low memory: address space held now %llu MB in %llu pieces (most so far: %llu MB) of %llu MB that can be "
            "used; in blocks now %llu KB in %llu blocks (most so far: %llu MB); %llu blocks handed out since the start; "
            "lowest address held 0x%llx, highest 0x%llx\n",
            tag, static_cast<unsigned long long>(st.reserved >> 20), static_cast<unsigned long long>(st.segments),
            static_cast<unsigned long long>(st.reservedPeak >> 20), static_cast<unsigned long long>(st.windowBytes >> 20),
            static_cast<unsigned long long>(st.used >> 10), static_cast<unsigned long long>(st.blocks),
            static_cast<unsigned long long>(st.usedPeak >> 20), static_cast<unsigned long long>(st.allocations),
            static_cast<unsigned long long>(st.lowest), static_cast<unsigned long long>(st.highest));
    if (st.highest > kFourGb) ++problems;
  }
  fprintf(stderr, "%s: low memory self-test: %s\n", tag, problems == 0 ? "OK" : "PROBLEM");
  return problems;
}

#endif
