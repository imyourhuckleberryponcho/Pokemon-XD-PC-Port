#ifndef PORT_FRAME_RATE_H
#define PORT_FRAME_RATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortFrameRate_Init(uint32_t (*field)(void));

void PortFrameRate_BeforePresent(void);

void PortFrameRate_AfterBegin(void);

bool PortFrameRate_ShowingBetween(void);

uint32_t PortFrameRate_BetweenCount(void);

#ifdef __cplusplus
}
#endif

#endif
