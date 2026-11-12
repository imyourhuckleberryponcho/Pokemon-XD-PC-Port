#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t dspTasksBooted;
  uint32_t dspInitialised;
  uint32_t dspYields;
  uint64_t dspInstructions;
  uint32_t aiBlocks;
  uint32_t aiBlocksDropped;
  uint32_t aiNonSilentBlocks;
  uint32_t aiUnderruns;
  uint32_t outShortTimes;
  uint32_t outShortBytes;
  uint64_t aiHandlerNs;
  uint64_t dspNs;
  uint32_t reserveMs;
  uint32_t reserveGrown;
  uint32_t longestWaitMs;
} PortAudioStats;

void PortAudio_GetStats(PortAudioStats* out);

void PortAudio_SetFastForward(int factor);

void PortAudio_SetVolume(float gain);

void PortAudio_SetPaused(bool paused);

int PortDsp_UseCoefDump(const uint8_t* bytes, size_t size);

#ifdef __cplusplus
}
#endif

#endif
