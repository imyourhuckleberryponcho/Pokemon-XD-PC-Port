#include "musyx/musyx.h"

#include <string.h>

__declspec(align(32)) char dspSlave[0x1EC0];
unsigned short dspSlaveLength = 0x1EC0;

void* AudioTest_Alloc(unsigned int size);

static void* test_malloc(size_t size) { return AudioTest_Alloc((unsigned int)size); }
static void test_free(void* p) { (void)p; }

static SND_AUX_REVERBHI sReverb;

int AudioTest_StartMusyX(void) {
  SND_HOOKS hooks = {test_malloc, test_free};
  s32 result;
  sndSetHooks(&hooks);
  result = sndInit(0x40, 0x30, 0x10, 1, 2, 0x5FC000);
  if (result != 0) {
    return (int)result;
  }
  memset(&sReverb, 0, sizeof(sReverb));
  sReverb.coloration = 0.5f;
  sReverb.mix = 0.5f;
  sReverb.time = 2.0f;
  sReverb.damping = 0.5f;
  sReverb.preDelay = 0.0f;
  sReverb.crosstalk = 0.0f;
  sndAuxCallbackPrepareReverbHI(&sReverb);
  sndSetAuxProcessingCallbacks(0, sndAuxCallbackReverbHI, &sReverb, 0xFF, 0, 0, 0, 0xFF, 0);
  return 0;
}
