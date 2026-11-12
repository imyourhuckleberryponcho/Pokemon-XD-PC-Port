#ifndef PORT_MUSYX_SDK_TYPES_H
#define PORT_MUSYX_SDK_TYPES_H

#include <stdint.h>

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
#ifdef _MSC_VER
typedef signed long s32;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef signed long long s64;
#else
typedef signed int s32;
typedef unsigned int u32;
typedef uint64_t u64;
typedef int64_t s64;
#endif
typedef float f32;
typedef double f64;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef int BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(n) __declspec(align(n))
#endif

#endif
