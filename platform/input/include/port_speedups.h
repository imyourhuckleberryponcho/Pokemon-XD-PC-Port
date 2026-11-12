#ifndef PORT_SPEEDUPS_H
#define PORT_SPEEDUPS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortSpeedups_HandleEvent(const void* sdlEvent);

void PortSpeedups_Frame(bool gameWants);

void PortSpeedups_Field(uint32_t field);

bool PortSpeedups_FastForwarding(void);

void PortSpeedups_Used(const char* what);

const char* PortSpeedups_Report(void);

#ifdef __cplusplus
}
#endif

#endif
