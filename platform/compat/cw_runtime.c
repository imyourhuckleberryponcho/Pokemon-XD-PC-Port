#include <stdint.h>

int32_t __float_nan[] = {0x7FFFFFFF};
int32_t __float_huge[] = {0x7F800000};
int32_t __float_min[] = {0x00800000};
int32_t __float_epsilon[] = {0x34000000};

#ifndef _WIN32
int stricmp(const char* a, const char* b) {
  for (;; a++, b++) {
    int x = (unsigned char)*a, y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z') {
      x += 'a' - 'A';
    }
    if (y >= 'A' && y <= 'Z') {
      y += 'a' - 'A';
    }
    if (x != y || x == 0) {
      return x - y;
    }
  }
}
#endif
