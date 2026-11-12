#ifndef PORT_MUSYX_SDK_OSCACHE_H
#define PORT_MUSYX_SDK_OSCACHE_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void DCInvalidateRange(void* addr, u32 nBytes);
void DCFlushRange(void* addr, u32 nBytes);
void DCStoreRange(void* addr, u32 nBytes);
void DCFlushRangeNoSync(void* addr, u32 nBytes);
void DCStoreRangeNoSync(void* addr, u32 nBytes);

#ifdef __cplusplus
}
#endif

#endif
