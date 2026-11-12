#ifndef PORT_CAPTURE_H
#define PORT_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortCapture_Init(uint32_t (*progress)(void), uint32_t (*field)(void));

void PortCapture_Field(void);

bool PortCapture_WindowSize(int* width, int* height);

#ifdef __cplusplus
}
#endif

#endif
