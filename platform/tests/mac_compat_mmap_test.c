







#define _GNU_SOURCE
#include "../os/include/port_mac_compat.h"

#include <string.h>

int port_test_mac_has_reserve = 0;
int port_test_mac_has_stack_reserve = 0;
static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                        \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

int main(void) {
  const size_t k64 = 0x10000;

  void* a = mmap((void*)0x50000000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(a == (void*)0x50000000u);
  if (a != MAP_FAILED) memset(a, 0x11, k64);
  void* b = mmap((void*)0x50000000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(b == MAP_FAILED);
  CHECK(a != MAP_FAILED && *(unsigned char*)a == 0x11);

  void* c = mmap((void*)0x50008000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(c == MAP_FAILED);
  void* d = mmap((void*)0x50010000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(d == (void*)0x50010000u);



  void* reserved = (mmap)((void*)0x70000000u, 0x10000000u, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  CHECK(reserved == (void*)0x70000000u);
  void* e = mmap((void*)0x70000000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(e == MAP_FAILED);
  port_test_mac_has_reserve = 1;
  e = mmap((void*)0x70000000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  CHECK(e == (void*)0x70000000u);
  if (e != MAP_FAILED) {
    *(volatile unsigned char*)e = 0x22;
    CHECK(*(volatile unsigned char*)e == 0x22);
  }
  CHECK(port_mac_in_reserve(0x80000000u, 0x01800000u) && port_mac_in_reserve(0xC0000000u, 0x01800000u) &&
        port_mac_in_reserve(0xE0000000u, 0x4000u) && port_mac_in_reserve(0x7FFF0000u, 0x10000u));
  CHECK(!port_mac_in_reserve(0xE0000000u, 0x10000u) && !port_mac_in_reserve(0x81800000u, 0x1000u) &&
        !port_mac_in_reserve(0x6FFFF000u, 0x2000u) && !port_mac_in_reserve(0x50000000u, 0x1000u));

  CHECK(mmap((void*)0x50000000u, k64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED);
  port_test_mac_has_reserve = 0;


  {
    uintptr_t got[12];
    for (int i = 0; i < 12; ++i) {
      const size_t size = i % 2 ? (8u << 20) + 4096 : 0x24000;
      void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_STACK, -1, 0);
      CHECK(p != MAP_FAILED);
      got[i] = (uintptr_t)p;
      CHECK(got[i] >= 0x10000000u && got[i] + size <= 0x3F000000u);
      if (p != MAP_FAILED) memset(p, i, size);
      for (int k = 0; k < i; ++k) CHECK(got[k] != got[i]);
    }

    void* in_the_way = (mmap)((void*)0x20000000u, 0x100000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK(in_the_way == (void*)0x20000000u);
    memset(in_the_way, 0x33, 0x100000);
    for (int i = 0; i < 40; ++i) {
      void* p = mmap(NULL, 16u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
      CHECK(p != MAP_FAILED);
      if (p != MAP_FAILED) memset(p, 0, 4096);
      CHECK((uintptr_t)p + (16u << 20) <= 0x20000000u || (uintptr_t)p >= 0x20100000u);
    }
    CHECK(*(unsigned char*)in_the_way == 0x33);
  }


  {
    const size_t size = 1u << 20;
    const int fd = memfd_create("mem1", 0);
    CHECK(fd >= 0);
    CHECK(ftruncate(fd, (off_t)size) == 0);
    unsigned char* first = (unsigned char*)mmap((void*)0x52000000u, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    unsigned char* second = (unsigned char*)mmap((void*)0x56000000u, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    CHECK(first == (unsigned char*)0x52000000u && second == (unsigned char*)0x56000000u);
    if (first != MAP_FAILED && second != MAP_FAILED) {
      first[0x3000] = 0x5A;
      second[size - 1] = 0xA5;
      CHECK(second[0x3000] == 0x5A && first[size - 1] == 0xA5);
    }

    const int fd2 = memfd_create("mem1", 0);
    CHECK(fd2 >= 0 && fd2 != fd);
  }





  {
    const uintptr_t base = PORT_MAC_STK_BASE, end = PORT_MAC_STK_BASE + PORT_MAC_STK_SIZE;
    void* whole = (mmap)((void*)base, PORT_MAC_STK_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
    CHECK(whole == (void*)base);
    port_test_mac_has_stack_reserve = 1;
    enum { kLive = 40 };
    unsigned char* live[kLive] = {0};
    size_t liveSize[kLive] = {0};
    unsigned seed = 12345;
    int bad = 0;
    for (int i = 0; i < 20000 && bad == 0; ++i) {
      seed = seed * 1103515245u + 12345u;
      const int slot = (int)((seed >> 16) % kLive);
      if (live[slot] != NULL) {
        if (live[slot][liveSize[slot] - 1] != (unsigned char)slot) bad = 1;
        if (munmap(live[slot], liveSize[slot]) != 0) bad = 2;
        live[slot] = NULL;
      }
      const size_t size = (seed >> 8) % 5 == 0 ? (8u << 20) + 0x4000 : 0x100000 + 0x4000;
      unsigned char* p = (unsigned char*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_STACK, -1, 0);
      if (p == MAP_FAILED) { bad = 3; break; }
      if ((uintptr_t)p < base + PORT_MAC_STK_UNIT || (uintptr_t)p + size > end || ((uintptr_t)p & 0xFFFF) != 0) bad = 4;
      if (p[0] != 0 || p[size - 1] != 0 || p[0x4000] != 0) bad = 5;
      for (int k = 0; k < kLive; ++k) {
        if (live[k] != NULL && p < live[k] + liveSize[k] && live[k] < p + size) bad = 6;
      }
      mprotect(p, 0x4000, PROT_NONE);
      memset(p + 0x4000, slot, size - 0x4000);
      live[slot] = p;
      liveSize[slot] = size;
    }
    if (bad != 0) fprintf(stderr, "stack range: problem %d\n", bad);
    CHECK(bad == 0);

    for (int k = 0; k < kLive; ++k) {
      if (live[k] != NULL) CHECK(munmap(live[k], liveSize[k]) == 0);
    }
    void* big[64];
    int got = 0;
    while (got < 64) {
      void* p = mmap(NULL, 16u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
      if (p == MAP_FAILED) break;
      big[got++] = p;
    }
    CHECK(got == 31);
    CHECK(munmap(big[7], 16u << 20) == 0);
    CHECK(mmap(NULL, 16u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0) == big[7]);

    void* other = (mmap)(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(other != MAP_FAILED && munmap(other, 0x10000) == 0);
    port_test_mac_has_stack_reserve = 0;
  }
  if (sFailures == 0) printf("mac_compat_mmap_test: all passed\n");
  return sFailures == 0 ? 0 : 1;
}
