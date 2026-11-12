#ifndef PORT_AUDIO_CATEGORIES_H
#define PORT_AUDIO_CATEGORIES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PORT_AUDIO_MUSIC = 0,
  PORT_AUDIO_EFFECTS = 1,
  PORT_AUDIO_CRIES = 2,
  PORT_AUDIO_CATEGORY_COUNT = 3
};

void PortAudio_SetCategoryVolume(int category, float gain);
float PortAudio_GetCategoryVolume(int category);

typedef int (*PortAudioFxClassifier)(uint16_t fxId);
typedef int (*PortAudioStreamClassifier)(void* update, uint32_t user, uint32_t streamId);
void PortAudio_SetCategoryClassifiers(PortAudioFxClassifier fx, PortAudioStreamClassifier stream);

void PortAudio_GetCategoryCounts(uint32_t out[PORT_AUDIO_CATEGORY_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
