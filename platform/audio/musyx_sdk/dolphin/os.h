

#ifndef PORT_MUSYX_SDK_OS_H
#define PORT_MUSYX_SDK_OS_H

#include <dolphin/types.h>
#include <dolphin/os/OSCache.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL OSDisableInterrupts(void);
BOOL OSEnableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL level);

typedef u32 OSTick;
typedef s64 OSTime;
OSTick OSGetTick(void);
OSTime OSGetTime(void);






OSTick PortMusyX_GetTick(void);
#define OSGetTick PortMusyX_GetTick


#define __OSBusClock (*(volatile u32*)(uintptr_t)0x800000F8u)
#define OS_TIMER_CLOCK (__OSBusClock / 4)
#define OSTicksToMicroseconds(ticks) ((ticks) * 8 / (OS_TIMER_CLOCK / 125000))


u32 OSCachedToPhysical(void* caddr);
void* OSCachedToUncached(void* caddr);
void* OSPhysicalToCached(u32 paddr);


typedef struct OSThreadQueue {
  struct OSThread* head;
  struct OSThread* tail;
} OSThreadQueue;
void OSInitThreadQueue(OSThreadQueue* queue);
void OSSleepThread(OSThreadQueue* queue);
void OSWakeupThread(OSThreadQueue* queue);

void OSReport(const char* msg, ...);
void OSPanic(const char* file, int line, const char* msg, ...);

#ifdef __cplusplus
}
#endif

#endif
