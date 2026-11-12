#ifndef PORT_MUSYX_SDK_AI_H
#define PORT_MUSYX_SDK_AI_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AIDCallback)(void);

AIDCallback AIRegisterDMACallback(AIDCallback callback);
void AIInitDMA(u32 start_addr, u32 length);
void AIStartDMA(void);
void AIStopDMA(void);

#ifdef __cplusplus
}
#endif

#endif
