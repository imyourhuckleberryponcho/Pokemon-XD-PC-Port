#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

__attribute__((constructor(101))) static void no_low_memory(void) {
  const unsigned long long piece = 0x1000000ULL;
  unsigned long long taken = 0;
  for (unsigned long long a = 0x10000; a < 0x100000000ULL; a += piece) {
    unsigned long long len = piece;
    if (a == 0x10000) len = piece - 0x10000;
    void* p = mmap((void*)a, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == (void*)a) taken += len;
    else if (p != MAP_FAILED) munmap(p, len);
  }
  fprintf(stderr, "[no_low_preload] %llu MB below 4 GB made unavailable\n", taken >> 20);
}
