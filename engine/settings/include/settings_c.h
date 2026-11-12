#ifndef ENGINE_SETTINGS_C_H
#define ENGINE_SETTINGS_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const int32_t* EngineSettings_IntSlot(const char* id);

const float* EngineSettings_FloatSlot(const char* id);

int32_t EngineSettings_ChoiceIndex(const char* id, const char* choiceId);

int EngineSettings_IsOriginal(void);

size_t EngineSettings_Summary(char* buffer, size_t size);

size_t EngineSettings_ReplayText(char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
