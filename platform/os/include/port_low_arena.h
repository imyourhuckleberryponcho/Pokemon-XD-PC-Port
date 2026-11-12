#ifndef PORT_LOW_ARENA_H
#define PORT_LOW_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* PortLow_Malloc(size_t size);
void* PortLow_Calloc(size_t count, size_t size);
void* PortLow_Realloc(void* ptr, size_t size);
void* PortLow_Memalign(size_t alignment, size_t size);
void PortLow_Free(void* ptr);

int PortLow_Owns(const void* ptr);
size_t PortLow_Size(const void* ptr);

typedef struct PortLowStats {
  uint64_t reserved;
  uint64_t reservedPeak;
  uint64_t used;
  uint64_t usedPeak;
  uint64_t blocks;
  uint64_t allocations;
  uint64_t segments;
  uint64_t windowBytes;
  uint64_t lowest, highest;
} PortLowStats;
void PortLow_GetStats(PortLowStats* out);

int PortLow_Check(void);

void PortLow_Lock(void);
void PortLow_Unlock(void);
void PortLow_ReinitLock(void);

extern int port_low_fail_returns_null;
extern uint64_t port_low_test_limit;

#ifdef __cplusplus
}
#endif
#endif
