#ifndef PORT_WINDOW_ICON_H
#define PORT_WINDOW_ICON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;

bool PortWindowIcon_Apply(struct SDL_Window* window, const char* icoPathUtf8);

bool PortWindowIcon_SetAppId(struct SDL_Window* window, const char* appId);

bool PortWindowIcon_OwnRGBA(int size, uint8_t* rgba);
bool PortWindowIcon_ResourceRGBA(int resource, int size, uint8_t* rgba);
bool PortWindowIcon_SaveResource(int resource, const char* icoPathUtf8);

#ifdef __cplusplus
}
#endif

#endif
