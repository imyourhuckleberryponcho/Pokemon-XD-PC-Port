#ifndef PORT_ANDROID_LAYOUT_H
#define PORT_ANDROID_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void PortAndroid_LayoutInit(void);
uintptr_t PortAndroid_LowBlockBase(void);
uintptr_t PortAndroid_LowBlockSize(void);
const char* PortAndroid_LargestGaps(void);
void* PortAndroid_StackTake(size_t len, int prot);
int PortAndroid_StackGive(void* addr, size_t len);
int PortAndroid_InStackRange(const void* p);
unsigned PortAndroid_ArenaWindows(uintptr_t (*out)[2], unsigned max);
const char* PortAndroid_LayoutReport(void);

int PortAndroid_FixedRangeReserved(uintptr_t at);
const char* PortAndroid_FixedRangeOwners(void);
const char* PortAndroid_RangeOwners(uintptr_t at, uintptr_t size);
void PortAndroid_WriteStartFailure(const char* folder, const char* message);

#ifdef __cplusplus
}
#endif
#endif
