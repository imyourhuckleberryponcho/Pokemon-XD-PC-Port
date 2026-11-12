#include <musyx/musyx.h>
#include <musyx/dspvoice.h>
#include <musyx/hardware.h>
#include <musyx/sal.h>
#include <musyx/synth.h>
#include <musyx/voice.h>

#ifdef PORT_MUSYX_LINUX_LONG_H
#undef size_t
#endif
#include <port_menu_music.h>

#include <stdio.h>
#include <string.h>

PORT_MUSYX_STATE_END

#ifdef _MSC_VER
#pragma section(".mxstate$a", read, write)
#pragma section(".mxstate$z", read, write)
__declspec(allocate(".mxstate$a")) unsigned char PortMusyX_StateBegin[16] = {1};
__declspec(allocate(".mxstate$z")) unsigned char PortMusyX_StateEnd[16] = {1};
#else
#ifdef __APPLE__
extern unsigned char __start_mxstate[] __asm("section$start$__DATA$mxstate");
extern unsigned char __stop_mxstate[] __asm("section$end$__DATA$mxstate");
#else
extern unsigned char __start_mxstate[];
extern unsigned char __stop_mxstate[];
#endif
#endif

void PortMusyX_GhostSetGains(const float gains[4]);
int PortMusyX_GhostIsolateStreams(void);

static volatile unsigned long sRefused = 0;

static void state_block(void** begin, size_t* size) {
#ifdef _MSC_VER
  *begin = PortMusyX_StateBegin;
  *size = (size_t)(PortMusyX_StateEnd + sizeof(PortMusyX_StateEnd) - PortMusyX_StateBegin);
#else
  *begin = __start_mxstate;
  *size = (size_t)(__stop_mxstate - __start_mxstate);
#endif
}

static size_t aux_user_size(SND_AUX_CALLBACK handler) {
  if (handler == (SND_AUX_CALLBACK)sndAuxCallbackReverbHI) return sizeof(SND_AUX_REVERBHI);
  return 0;
}

static int can_run(char* why, size_t whySize) {
  u32 s;
  if (!sndActive) {
    snprintf(why, whySize, "the sound engine isn't running");
    return 0;
  }
  for (s = 0; s < 8; ++s) {
    if (dspStudio[s].auxAHandler != NULL && aux_user_size(dspStudio[s].auxAHandler) == 0) {
      snprintf(why, whySize, "studio %u's aux A effect is the game's own code", (unsigned)s);
      return 0;
    }
    if (dspStudio[s].auxBHandler != NULL && aux_user_size(dspStudio[s].auxBHandler) == 0) {
      snprintf(why, whySize, "studio %u's aux B effect is the game's own code", (unsigned)s);
      return 0;
    }
  }
  if (vs.callback != NULL) {
    snprintf(why, whySize, "virtual samples (fed by the game's code) are in use");
    return 0;
  }
  return 1;
}

static size_t extra_ranges(PortMenuMusicRange* out, size_t max) {
  size_t n = 0;
  u32 s;
  for (s = 0; s < 8; ++s) {
    const size_t a = dspStudio[s].auxAHandler != NULL ? aux_user_size(dspStudio[s].auxAHandler) : 0;
    const size_t b = dspStudio[s].auxBHandler != NULL ? aux_user_size(dspStudio[s].auxBHandler) : 0;
    if (a != 0 && dspStudio[s].auxAUser != NULL && n < max) {
      out[n].address = dspStudio[s].auxAUser;
      out[n++].size = a;
    }
    if (b != 0 && dspStudio[s].auxBUser != NULL && n < max) {
      out[n].address = dspStudio[s].auxBUser;
      out[n++].size = b;
    }
  }
  return n;
}

static void* refuse_malloc(size_t size) {
  (void)size;
  sRefused++;
  return NULL;
}

static void refuse_free(void* p) {
  (void)p;
  sRefused++;
}

static void isolate(void) {
  synthMessageCallback = NULL;
  salHooks.malloc = refuse_malloc;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  salHooks.mallocPhysical = refuse_malloc;
#endif
  salHooks.free = refuse_free;
  PortMusyX_GhostIsolateStreams();
}

static uint32_t refused_calls(void) { return (uint32_t)sRefused; }

double PortMusyX_GhostStreamRunway(void);

static const PortMenuMusicEngine kEngine = {state_block, can_run,       extra_ranges,
                                            isolate,     PortMusyX_GhostSetGains, refused_calls,
                                            PortMusyX_GhostStreamRunway};

void PortMusyX_GhostInstall(void) { PortMenuMusic_SetEngine(&kEngine); }
