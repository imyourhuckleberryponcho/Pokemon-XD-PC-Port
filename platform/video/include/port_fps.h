#ifndef PORT_FPS_H
#define PORT_FPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortFps_Frame(uint32_t gamePictures, uint32_t fields);

void PortVideo_Notice(const char* text, double seconds);

#ifdef __cplusplus
}
#endif

#endif
