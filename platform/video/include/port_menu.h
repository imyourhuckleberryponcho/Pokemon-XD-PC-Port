#ifndef PORT_MENU_H
#define PORT_MENU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortMenuInput {
  uint16_t held;
  uint16_t pressed;
  uint16_t repeat;
} PortMenuInput;

typedef struct PortMenuPage {
  const char* name;
  void (*open)(void* ctx);
  bool (*frame)(void* ctx, const PortMenuInput* input);
  void (*close)(void* ctx);
  void* ctx;
} PortMenuPage;

void PortMenu_Init(const char* appFolder, uint32_t (*progress)(void));

bool PortMenu_Open(const PortMenuPage* page);

void PortMenu_SetScriptPage(const PortMenuPage* page);

void PortMenu_Frame(void (*pump)(void));

void PortMenu_LatchHeld(void);

bool PortMenu_Active(void);
bool PortMenu_Pending(void);

void PortMenu_PictureRect(float* x, float* y, float* scale);

#ifdef __cplusplus
}
#endif

#endif
