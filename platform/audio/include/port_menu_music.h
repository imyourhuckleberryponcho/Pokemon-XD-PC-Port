#ifndef PORT_MENU_MUSIC_H
#define PORT_MENU_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PORT_MENU_MUSIC_STOP = 0,
  PORT_MENU_MUSIC_FADE = 1,
  PORT_MENU_MUSIC_KEEP = 2,
};

typedef struct {
  void* address;
  size_t size;
} PortMenuMusicRange;

typedef struct {
  void (*stateBlock)(void** begin, size_t* size);
  int (*canRun)(char* why, size_t whySize);
  size_t (*extraRanges)(PortMenuMusicRange* out, size_t max);
  void (*isolate)(void);
  void (*setGains)(const float gains[4]);
  uint32_t (*refusedCalls)(void);
  double (*streamRunway)(void);
} PortMenuMusicEngine;

void PortMenuMusic_SetEngine(const PortMenuMusicEngine* engine);

void PortMenuMusic_Begin(int mode);
void PortMenuMusic_End(void);

void PortMenuMusic_SetNext(int mode);
int PortMenuMusic_TakeNext(void);

int PortMenuMusic_ModeFromName(const char* name, int fallback);

#ifdef __cplusplus
}
#endif

#endif
