#ifndef PORT_MUSYX_SDK_ARQ_H
#define PORT_MUSYX_SDK_ARQ_H

#include <dolphin/ar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARQ_TYPE_MRAM_TO_ARAM ARAM_DIR_MRAM_TO_ARAM
#define ARQ_TYPE_ARAM_TO_MRAM ARAM_DIR_ARAM_TO_MRAM
#define ARQ_PRIORITY_LOW 0
#define ARQ_PRIORITY_HIGH 1

typedef void (*ARQCallback)(uintptr_t pointerToARQRequest);

typedef struct ARQRequest {
  struct ARQRequest* next;
  u32 owner;
  u32 type;
  u32 priority;
  u32 source;
  u32 dest;
  u32 length;
  ARQCallback callback;
} ARQRequest;

void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
                    u32 length, ARQCallback callback);
u32 ARQGetChunkSize(void);

#ifdef __cplusplus
}
#endif

#endif
