#ifndef PORT_FREEZE_H
#define PORT_FREEZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortFreeze_Capture(void);

bool PortFreeze_Ready(int* waitHooks);

bool PortFreeze_Begin(void);

const uint8_t* PortFreeze_Picture(size_t* size);

void PortFreeze_DrawPicture(void);

void PortFreeze_Write(const void* data, size_t size);

void PortFreeze_Present(void (*pump)(void));

void PortFreeze_End(void);

bool PortFreeze_Active(void);

uint32_t PortFreeze_PictureCount(size_t* partial);
bool PortFreeze_RedrawBegin(void);
void PortFreeze_RedrawEnd(void);

#ifdef __cplusplus
}
#endif

#endif
