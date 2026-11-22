#ifndef PORT_PHOTO_H
#define PORT_PHOTO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortPhoto_Init(const char* appFolder, uint32_t (*progress)(void));

void PortPhoto_HandleEvent(const void* sdlEvent);

void PortPhoto_Frame(void (*pump)(void));

bool PortPhoto_Active(void);

void PortPhoto_Request(bool open);
bool PortPhoto_Enabled(void);

void PortPhoto_PadAResumes(bool on);

void PortPhoto_LiveSet(bool on);
bool PortPhoto_LiveActive(void);
bool PortPhoto_LiveBefore(void);
bool PortPhoto_LiveAfter(void);

#ifdef __cplusplus
}
#endif

#endif
