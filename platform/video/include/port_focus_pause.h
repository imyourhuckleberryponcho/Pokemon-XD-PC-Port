#ifndef PORT_FOCUS_PAUSE_H
#define PORT_FOCUS_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortFocusPause_Init(uint32_t (*progress)(void), bool testRun);

void PortFocusPause_Frame(void);

#ifdef __cplusplus
}
#endif

#endif
