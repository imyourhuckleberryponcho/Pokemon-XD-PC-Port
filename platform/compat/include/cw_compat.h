#ifndef CW_COMPAT_H
#define CW_COMPAT_H

#include <malloc.h>

#define _ALLOW_KEYWORD_MACROS 1
#define asm

#ifdef _MSC_VER
#define __attribute__(x)
#endif

#if defined(__cplusplus) && !defined(__clang__) && !defined(__GNUC__)
#define __typeof__(x) decltype(x)
#endif

typedef unsigned int uint;

#ifdef _MSC_VER
#define __alloca _alloca
#else
#define __alloca __builtin_alloca
#endif

#ifdef __cplusplus
extern "C" {
#endif
double __frsqrte(double x);
float __frsqrtes(float x);
#if defined(__cplusplus) && !defined(_MSC_VER)
#include "cw_fabs_linux.h"
#else
double __fabs(double x);
float __fabsf(float x);
#endif
double __fnabs(double x);
double __fsel(double a, double b, double c);
int __cntlzw(unsigned int x);
#ifdef __cplusplus
}
#endif

#endif
