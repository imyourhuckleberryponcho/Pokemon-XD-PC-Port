

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "port_low_arena.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

static int sFailed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++sFailed; } } while (0)

int main() {
  const size_t kSize = (size_t)1 << 30;
  void* r = mmap(nullptr, kSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (r == MAP_FAILED) {
    std::printf("cannot reserve 1 GB\n");
    return 2;
  }
  const uintptr_t lo = (uintptr_t)r, hi = lo + kSize;
  PortLow_UseRange(lo, hi);
  port_low_fail_returns_null = 1;


  std::vector<std::pair<unsigned char*, size_t>> v;
  for (int i = 0; i < 5000; ++i) {
    const size_t n = (size_t)(1 + (i * 7919) % 20000) + ((i % 50 == 0) ? ((size_t)3 << 20) : 0);
    unsigned char* p = (unsigned char*)PortLow_Malloc(n);
    CHECK(p != nullptr);
    if (p == nullptr) break;
    CHECK(((uintptr_t)p & 15u) == 0);
    CHECK((uintptr_t)p >= lo && (uintptr_t)p + n <= hi);
    CHECK(PortLow_Owns(p));
    std::memset(p, i & 255, n);
    v.push_back({p, n});
  }
  CHECK(PortLow_Check() == 0);
  PortLowStats st;
  PortLow_GetStats(&st);
  CHECK(st.reserved > 0 && st.lowest >= lo && st.highest <= hi);
  CHECK(st.windowBytes == 0 || st.windowBytes == kSize);


  for (size_t i = 0; i < v.size(); ++i) {
    for (size_t k = 0; k < v[i].second; k += 997) {
      if (v[i].first[k] != (unsigned char)(i & 255)) { CHECK(!"content changed"); break; }
    }
  }
  for (size_t i = 0; i < v.size(); i += 2) { PortLow_Free(v[i].first); v[i].first = nullptr; }
  CHECK(PortLow_Check() == 0);
  for (int i = 0; i < 2000; ++i) {
    void* p = PortLow_Malloc((size_t)(16 + i * 13));
    CHECK(p != nullptr && PortLow_Owns(p));
    PortLow_Free(p);
  }

  unsigned char* q = (unsigned char*)PortLow_Malloc(100);
  std::memset(q, 0x5A, 100);
  q = (unsigned char*)PortLow_Realloc(q, 100000);
  CHECK(q != nullptr && q[0] == 0x5A && q[99] == 0x5A);
  PortLow_Free(q);
  for (auto& e : v) if (e.first) PortLow_Free(e.first);
  CHECK(PortLow_Check() == 0);


  int local = 0;
  CHECK(!PortLow_Owns(&local));
  CHECK(!PortLow_Owns(nullptr));


  void* big = PortLow_Malloc((size_t)200 << 20);
  CHECK(big != nullptr);
  PortLow_Free(big);
  void* big2 = PortLow_Malloc((size_t)200 << 20);
  CHECK(big2 != nullptr && (uintptr_t)big2 >= lo && (uintptr_t)big2 < hi);
  PortLow_Free(big2);
  void* other = mmap(nullptr, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(other != MAP_FAILED && ((uintptr_t)other < lo || (uintptr_t)other >= hi));


  void* huge = PortLow_Malloc((size_t)600 << 20);
  void* huge2 = PortLow_Malloc((size_t)600 << 20);
  CHECK(huge != nullptr && huge2 == nullptr);
  PortLow_Free(huge);
  CHECK(PortLow_Check() == 0);

  std::printf(sFailed == 0 ? "low_arena_range_test: OK\n" : "low_arena_range_test: %d FAILED\n", sFailed);
  return sFailed == 0 ? 0 : 1;
}
