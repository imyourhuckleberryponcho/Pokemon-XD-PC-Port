#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <math.h>

#include <dolphin/ppc_math.h>

double __frsqrte(double x) { return frsqrte(x); }
float __frsqrtes(float x) { return (float)frsqrte((double)x); }

double __fabs(double x) { return fabs(x); }
float __fabsf(float x) { return fabsf(x); }
double __fnabs(double x) { return -fabs(x); }

double __fsel(double a, double b, double c) { return a >= 0.0 ? b : c; }

int __cntlzw(unsigned int x) {
#ifdef _MSC_VER
  unsigned long index;
  return _BitScanReverse(&index, x) ? 31 - (int)index : 32;
#else
  return x != 0 ? __builtin_clz(x) : 32;
#endif
}
