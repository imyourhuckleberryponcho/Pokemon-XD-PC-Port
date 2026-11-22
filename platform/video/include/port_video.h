#ifndef PORT_VIDEO_H
#define PORT_VIDEO_H

#include <aurora/aurora.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortVideo_Configure(AuroraConfig* config);

void PortVideo_UseWindowResolution(void);

float PortVideo_ResolutionScale(void);

float PortVideo_WideScale(void);

void PortVideo_ApplySettings(void);

float PortVideo_MonitorHz(void);

void PortVideo_PresentsComing(unsigned count, bool firstPaced);

void PortVideo_SetSettingsSaver(void (*save)(void));

void PortVideo_Start(void);

void PortVideo_HandleEvent(const void* sdlEvent);

void PortVideo_SetFastForward(bool on);

#ifdef __cplusplus
}
#endif

#endif
