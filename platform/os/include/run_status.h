#ifndef PORT_RUN_STATUS_H
#define PORT_RUN_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortRunStatus_Start(const char* appFolder, const char* label, uint32_t (*progress)(void),
                         unsigned stuckSeconds, bool exitWhenStuck);

void PortRunStatus_SetNote(const char* note);

void PortRunStatus_SetWaitingForPlayer(bool waiting);

void PortRunStatus_MessageWindow(bool open);

#ifdef __cplusplus
}
#endif

#endif
