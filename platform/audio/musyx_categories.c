#define hwSetVolume PortMusyX_StreamSetVolume
#define streamHandle PortMusyX_StreamHandleOriginal
#include "musyx/runtime/stream.c"
#undef hwSetVolume
#undef streamHandle

#include "musyx/dspvoice.h"
#ifdef PORT_MUSYX_LINUX_LONG_H
#undef size_t
#endif
#include <port_audio_categories.h>

#include <stdio.h>
#include <stdlib.h>

void hwSetVolume(u32 v, u8 table, float vol, u32 pan, u32 span, float auxa, float auxb);
void streamHandle();
void PortMusyX_SynthSetVolume(u32 v, u8 table, float vol, u32 pan, u32 span, float auxa, float auxb);

#define kVoices 64

typedef struct {
  u8 set;
  u8 stream;
  u8 table;
  u8 category;
  float vol;
  u32 pan;
  u32 span;
  float auxa;
  float auxb;
} VoiceVolume;

static VoiceVolume sVoices[kVoices];
static long sApplied = 0;
static long sGhostGeneration = 0;
static u32 sCounts[PORT_AUDIO_CATEGORY_COUNT];

PORT_MUSYX_STATE_END
static volatile float sGain[PORT_AUDIO_CATEGORY_COUNT] = {1.f, 1.f, 1.f};
static volatile long sGeneration = 0;
static PortAudioFxClassifier sFxClassifier = NULL;
static PortAudioStreamClassifier sStreamClassifier = NULL;
static int sLog = -1;
static volatile float sGhostGain[4] = {1.f, 1.f, 1.f, 1.f};
PORT_MUSYX_STATE_BEGIN

static const char* category_name(int c) {
  return c == PORT_AUDIO_MUSIC ? "music" : c == PORT_AUDIO_EFFECTS ? "effects" : "cries";
}

static int valid_category(int c) {
  return c >= 0 && c < PORT_AUDIO_CATEGORY_COUNT ? c : PORT_AUDIO_EFFECTS;
}

static u16 scaled(u16 level, float g) {
  return (u16)((float)level * g + 0.5f);
}

static void apply(u32 v, const VoiceVolume* a) {
  const float g = sGain[a->category] * sGhostGain[a->category] * (a->stream ? sGhostGain[3] : 1.f);
  hwSetVolume(v, a->table, a->vol, a->pan, a->span, a->auxa, a->auxb);
  if (g != 1.f) {
    DSPvoice* d = &dspVoice[v];
    d->volL = scaled(d->volL, g);
    d->volR = scaled(d->volR, g);
    d->volS = scaled(d->volS, g);
    d->volLa = scaled(d->volLa, g);
    d->volRa = scaled(d->volRa, g);
    d->volSa = scaled(d->volSa, g);
    d->volLb = scaled(d->volLb, g);
    d->volRb = scaled(d->volRb, g);
    d->volSb = scaled(d->volSb, g);
    d->changed[0] |= 7;
  }
}

static void set_volume(u32 v, int category, u32 what, u8 table, float vol, u32 pan, u32 span, float auxa,
                       float auxb) {
  VoiceVolume* a;
  if (v >= kVoices) {
    hwSetVolume(v, table, vol, pan, span, auxa, auxb);
    return;
  }
  a = &sVoices[v];
  if (sLog < 0) {
    const char* e = getenv("XD_AUDIO_CATEGORY_LOG");
    sLog = e != NULL && e[0] == '1';
  }
  if (sLog && (!a->set || a->category != category)) {
    fprintf(stderr, "[audio] voice %u: %s (%s 0x%X)\n", (unsigned)v, category_name(category),
            what == 0xFFFFFFFFu ? "stream" : "sound", (unsigned)(what & 0xFFFF));
  }
  a->set = 1;
  a->stream = what == 0xFFFFFFFFu;
  a->table = table;
  a->category = (u8)category;
  a->vol = vol;
  a->pan = pan;
  a->span = span;
  a->auxa = auxa;
  a->auxb = auxb;
  sCounts[category]++;
  apply(v, a);
}

void PortMusyX_SynthSetVolume(u32 v, u8 table, float vol, u32 pan, u32 span, float auxa, float auxb) {
  int category = PORT_AUDIO_MUSIC;
  u32 what = 0;
  if (v < synthInfo.voiceNum) {
    const SYNTH_VOICE* sv = &synthVoice[v];
    what = (u32)sv->allocId;
    if (sv->fxFlag) {
      category = PORT_AUDIO_EFFECTS;
      if ((what & 0x80000000u) != 0 && what != 0xFFFFFFFFu && sFxClassifier != NULL) {
        category = valid_category(sFxClassifier((uint16_t)(what & 0xFFFF)));
      }
    }
  }
  set_volume(v, category, what & 0x7FFFFFFFu, table, vol, pan, span, auxa, auxb);
}

void PortMusyX_StreamSetVolume(u32 v, u8 table, float vol, u32 pan, u32 span, float auxa, float auxb) {
  int category = PORT_AUDIO_MUSIC;
  u32 i;
  for (i = 0; i < synthInfo.voiceNum && i < 64; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    if (si->state != 0 && si->voice == v) {
      if (sStreamClassifier != NULL) {
        category = valid_category(sStreamClassifier((void*)si->updateFunction, si->user, (uint32_t)si->stid));
      }
      break;
    }
  }
  set_volume(v, category, 0xFFFFFFFFu, table, vol, pan, span, auxa, auxb);
}

void streamHandle() {
  long gen;
  u32 v;
  PortMusyX_StreamHandleOriginal();
  gen = sGeneration + sGhostGeneration;
  if (gen == sApplied) return;
  sApplied = gen;
  for (v = 0; v < kVoices && v < synthInfo.voiceNum; ++v) {
    if (sVoices[v].set && hwIsActive(v)) {
      apply(v, &sVoices[v]);
    }
  }
}

void PortAudio_SetCategoryVolume(int category, float gain) {
  if (category < 0 || category >= PORT_AUDIO_CATEGORY_COUNT) return;
  if (!(gain >= 0.f)) gain = 0.f;
  if (gain > 1.f) gain = 1.f;
  if (sGain[category] == gain) return;
  sGain[category] = gain;
  sGeneration++;
}

float PortAudio_GetCategoryVolume(int category) {
  return category >= 0 && category < PORT_AUDIO_CATEGORY_COUNT ? sGain[category] : 1.f;
}

void PortAudio_SetCategoryClassifiers(PortAudioFxClassifier fx, PortAudioStreamClassifier stream) {
  sFxClassifier = fx;
  sStreamClassifier = stream;
}

void PortAudio_GetCategoryCounts(uint32_t out[PORT_AUDIO_CATEGORY_COUNT]) {
  int c;
  for (c = 0; c < PORT_AUDIO_CATEGORY_COUNT; ++c) out[c] = sCounts[c];
}

void PortMusyX_GhostSetGains(const float gains[4]) {
  int i, changed = 0;
  for (i = 0; i < 4; ++i) {
    if (sGhostGain[i] != gains[i]) {
      sGhostGain[i] = gains[i];
      changed = 1;
    }
  }
  if (changed) sGhostGeneration++;
}

static u32 ghost_stream_update(void* buffer1, u32 len1, void* buffer2, u32 len2, u32 user) {
  (void)buffer1, (void)len1, (void)buffer2, (void)len2, (void)user;
  return 0;
}

int PortMusyX_GhostIsolateStreams(void) {
  int i, playing = 0;
  for (i = 0; i < 64; ++i) {
    if (streamInfo[i].state != 0) {
      streamInfo[i].updateFunction = ghost_stream_update;
      playing += streamInfo[i].state == 2;
    }
  }
  return playing;
}

double PortMusyX_GhostStreamRunway(void) {
  double best = 1e9;
  u32 i;
  for (i = 0; i < 64 && i < synthInfo.voiceNum; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    u32 cpos, ahead;
    if (si->state != 2 || si->size == 0 || si->frq == 0) continue;
    if (si->voice < kVoices && sVoices[si->voice].set && sVoices[si->voice].category != PORT_AUDIO_MUSIC) continue;
    cpos = hwGetPos(si->voice);
    if (si->type == 1) cpos = (cpos / 14) * 14;
    ahead = (si->last + si->size - cpos) % si->size;
    if (ahead == 0) ahead = si->size;
    if ((double)ahead / si->frq < best) best = (double)ahead / si->frq;
  }
  return best;
}
