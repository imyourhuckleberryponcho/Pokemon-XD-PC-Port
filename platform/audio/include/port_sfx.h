#ifndef PORT_SFX_H
#define PORT_SFX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortSfx_NoteGroup(const void* project, uint16_t groupId, const void* sampleDir, const void* pool);

bool PortSfx_Play(uint16_t fxId, uint8_t vol, uint8_t pan);

void PortSfx_SetVolume(float gain);

bool PortSfx_WriteWav(uint16_t fxId, uint8_t vol, uint8_t pan, const char* path);

#ifdef __cplusplus
}
#endif

#endif
