#include "musyx/hardware.h"
#include "musyx/stream.h"
#include <stdlib.h>
#include <string.h>
void PortMusyX_StreamSetVolume(u32 v, u8 table, float vol, u32 pan, u32 span, float auxa, float auxb);
typedef size_t PortMusyXSize;
void PortMusyX_StreamFlush(void* base, u32 offset, u32 bytes, u8 hwStreamHandle, void (*callback)(PortMusyXSize), u32 user);
#define hwSetVolume PortMusyX_StreamSetVolume
#define streamHandle PortMusyX_StreamHandleOriginal
#define hwFlushStream PortMusyX_StreamFlush
#include "musyx/runtime/stream.c"
#undef hwSetVolume
#undef streamHandle
#undef hwFlushStream

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
static volatile float sFfGain[PORT_AUDIO_CATEGORY_COUNT] = {1.f, 1.f, 1.f};
static volatile long sFfGhostWorld = 0;
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
  const float g = sGain[a->category] * sGhostGain[a->category] * (a->stream ? sGhostGain[3] : 1.f) *
                  (sFfGhostWorld ? 1.f : sFfGain[a->category]);
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

static void feed_observe(void);

void streamHandle() {
  long gen;
  u32 v;
  PortMusyX_StreamHandleOriginal();
  feed_observe();
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

void PortMusyX_SetFfMix(float music, float effects, float cries) {
  if (sFfGain[0] == music && sFfGain[1] == effects && sFfGain[2] == cries) return;
  sFfGain[0] = music;
  sFfGain[1] = effects;
  sFfGain[2] = cries;
  sGeneration++;
}

void PortMusyX_FfGhostWorld(int on) { sFfGhostWorld = on; }

PORT_MUSYX_STATE_END
typedef struct {
  u32 samples;
  u32 off[2], n[2];
  u8* data[2];
} FeedChunk;
typedef struct {
  FeedChunk* q;
  size_t head, tail, cap;
  u32 stid, last;
  int valid;
} FeedSlot;
static FeedSlot sFeed[64];
typedef struct {
  u8 vol, pan, span, auxa, auxb, orgPan, orgSPan;
  u32 stid;
  int valid;
} RealMix;
static RealMix sRealMix[64];
static int sFeedRecord = 0;
static volatile long sFfFeedServe = 0;

static void feed_clear_slot(FeedSlot* f) {
  size_t i;
  for (i = f->head; i < f->tail; ++i) {
    free(f->q[i].data[0]);
    free(f->q[i].data[1]);
  }
  free(f->q);
  memset(f, 0, sizeof(*f));
}

void PortMusyX_FfFeedRecord(int on) {
  int i;
  for (i = 0; i < 64; ++i) feed_clear_slot(&sFeed[i]);
  sFeedRecord = on;
  if (!on) return;
  for (i = 0; i < 64 && i < (int)synthInfo.voiceNum; ++i) {
    if (streamInfo[i].state == 2) {
      sFeed[i].stid = (u32)streamInfo[i].stid;
      sFeed[i].last = streamInfo[i].last;
      sFeed[i].valid = 1;
    }
  }
}

void PortMusyX_FfReapply(void) { sApplied = -1; }

void PortMusyX_FfSyncMix(void) {
  int i;
  for (i = 0; i < 64 && i < (int)synthInfo.voiceNum; ++i) {
    STREAM_INFO* si = &streamInfo[i];
    const RealMix* m = &sRealMix[i];
    if (si->state != 2 || !m->valid || m->stid != (u32)si->stid) continue;
    if (si->vol == m->vol && si->pan == m->pan && si->span == m->span && si->auxa == m->auxa && si->auxb == m->auxb)
      continue;
    si->vol = m->vol, si->pan = m->pan, si->span = m->span, si->auxa = m->auxa, si->auxb = m->auxb;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 4)
    si->orgPan = m->orgPan, si->orgSPan = m->orgSPan;
#endif
    SetHWMix(si);
  }
}

void PortMusyX_FfFeedServe(int on) { sFfFeedServe = on; }

static u32 feed_byte_start(const STREAM_INFO* si, u32 sample) {
  return si->type == 1 ? (sample / 14) * 8 : sample * 2;
}
static u32 feed_byte_end(const STREAM_INFO* si, u32 sample) {
  return si->type == 1 ? ((sample + 13) / 14) * 8 : sample * 2;
}

static void feed_observe(void) {
  int i;
  if (!sFeedRecord || sFfGhostWorld) return;
  for (i = 0; i < 64 && i < (int)synthInfo.voiceNum; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    FeedSlot* f = &sFeed[i];
    if (si->state == 2) {
      RealMix* m = &sRealMix[i];
      m->vol = si->vol, m->pan = si->pan, m->span = si->span, m->auxa = si->auxa, m->auxb = si->auxb;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 4)
      m->orgPan = si->orgPan, m->orgSPan = si->orgSPan;
#endif
      m->stid = (u32)si->stid;
      m->valid = 1;
    } else {
      sRealMix[i].valid = 0;
    }
    if (si->state != 2 || si->size == 0) {
      if (f->valid) feed_clear_slot(f);
      continue;
    }
    if (!f->valid || f->stid != (u32)si->stid) {
      feed_clear_slot(f);
      f->stid = (u32)si->stid;
      f->last = si->last;
      f->valid = 1;
      continue;
    }
    if (si->last != f->last) {
      FeedChunk c;
      u32 a = f->last, b = si->last, e;
      memset(&c, 0, sizeof(c));
      c.samples = b >= a ? b - a : si->size - a + b;
      c.off[0] = feed_byte_start(si, a);
      if (b > a) {
        e = feed_byte_end(si, b);
        c.n[0] = (e > si->bytes ? si->bytes : e) - c.off[0];
      } else {
        c.n[0] = si->bytes - c.off[0];
        c.off[1] = 0;
        e = feed_byte_end(si, b);
        c.n[1] = e > si->bytes ? si->bytes : e;
      }
      c.data[0] = (u8*)malloc(c.n[0] ? c.n[0] : 1);
      c.data[1] = (u8*)malloc(c.n[1] ? c.n[1] : 1);
      if (c.data[0] == NULL || c.data[1] == NULL) {
        free(c.data[0]);
        free(c.data[1]);
      } else {
        memcpy(c.data[0], (u8*)si->buffer + c.off[0], c.n[0]);
        if (c.n[1]) memcpy(c.data[1], (u8*)si->buffer + c.off[1], c.n[1]);
        if (f->tail == f->cap) {
          if (f->head > 0) {
            memmove(f->q, f->q + f->head, (f->tail - f->head) * sizeof(FeedChunk));
            f->tail -= f->head;
            f->head = 0;
          } else {
            size_t nc = f->cap ? f->cap * 2 : 64;
            FeedChunk* nq = (FeedChunk*)realloc(f->q, nc * sizeof(FeedChunk));
            if (nq == NULL) {
              free(c.data[0]);
              free(c.data[1]);
              f->last = si->last;
              continue;
            }
            f->q = nq;
            f->cap = nc;
          }
        }
        f->q[f->tail++] = c;
        { static int n = 0; if (c.samples > 5000 && n++ < 12 && getenv("XD_FF_MUSIC_DEBUG")) fprintf(stderr, "[ff debug] recorded slot %d: %u samples, %u+%u bytes at %u\n", i, (unsigned)c.samples, (unsigned)c.n[0], (unsigned)c.n[1], (unsigned)c.off[0]); }
      }
      f->last = si->last;
    }
  }
}

static u32 feed_serve(void* buffer1, u32 len1, void* buffer2, u32 len2, u32 user) {
  int i;
  u32 total = 0, room = len1 + (buffer2 != NULL ? len2 : 0);
  { static int n = 0; if (0) fprintf(stderr, "[ff debug] serve asked len1 %u len2 %u user %x\n", (unsigned)len1, (unsigned)len2, (unsigned)user); }
  (void)buffer1;
  for (i = 0; i < 64; ++i) {
    STREAM_INFO* si = &streamInfo[i];
    FeedSlot* f = &sFeed[i];
    if (si->state == 0 || si->user != user) continue;
    while (f->head < f->tail && total + f->q[f->head].samples <= room) {
      FeedChunk* c = &f->q[f->head];
      memcpy((u8*)si->buffer + c->off[0], c->data[0], c->n[0]);
      if (c->n[1]) memcpy((u8*)si->buffer + c->off[1], c->data[1], c->n[1]);
      static int sNoAram = -1;
      if (sNoAram < 0) sNoAram = getenv("XD_FF_MUSIC_NO_ARAM") != NULL && getenv("XD_FF_MUSIC_NO_ARAM")[0] == '1';
      if (!sNoAram) {
        const u32 unit = si->type == 1 ? 14 : 1, bytesPer = si->type == 1 ? 8 : 2;
        sndStreamARAMUpdate((u32)si->stid, c->off[0] / bytesPer * unit, c->n[0] / bytesPer * unit, c->off[1] / bytesPer * unit,
                            c->n[1] / bytesPer * unit);
      }
      total += c->samples;
      { static int n = 0; if (c->samples > 5000 && n++ < 400 && getenv("XD_FF_MUSIC_DEBUG")) fprintf(stderr, "[ff debug] served slot %d: %u samples at byte %u (room %u, copy last %u cpos %u)\n", i, (unsigned)c->samples, (unsigned)c->off[0], (unsigned)room, (unsigned)si->last, (unsigned)hwGetPos(si->voice)); }
      free(c->data[0]);
      free(c->data[1]);
      f->head++;
    }
    break;
  }
  return total;
}

double PortMusyX_FfFeedSeconds(void) {
  double best = 1e9;
  u32 i;
  for (i = 0; i < 64 && i < synthInfo.voiceNum; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    const FeedSlot* f = &sFeed[i];
    u32 cpos, ahead, queued = 0;
    size_t k;
    if (si->state != 2 || si->size == 0 || si->frq == 0) continue;
    if (si->voice < kVoices && sVoices[si->voice].set && sVoices[si->voice].category != PORT_AUDIO_MUSIC) continue;
    for (k = f->head; k < f->tail; ++k) queued += f->q[k].samples;
    cpos = hwGetPos(si->voice);
    if (si->type == 1) cpos = (cpos / 14) * 14;
    ahead = (si->last + si->size - cpos) % si->size;
    if (ahead == 0) ahead = si->size;
    if ((double)(ahead + queued) / si->frq < best) best = (double)(ahead + queued) / si->frq;
  }
  return best;
}

uint32_t PortMusyX_FfStreamsPlaying(void) {
  u32 i, h = 2166136261u;
  for (i = 0; i < 64 && i < synthInfo.voiceNum; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    if (si->state != 2 || si->voice >= kVoices) continue;
    if (sVoices[si->voice].set && sVoices[si->voice].category != PORT_AUDIO_MUSIC) continue;
    h = (h ^ i) * 16777619u;
    h = (h ^ (u32)si->stid) * 16777619u;
  }
  return h;
}

void PortMusyX_FfDebugStreams(void) {
  u32 i;
  for (i = 0; i < 64 && i < synthInfo.voiceNum; ++i) {
    const STREAM_INFO* si = &streamInfo[i];
    const FeedSlot* f = &sFeed[i];
    if (si->state == 0) continue;
    if (i < 2 && si->state == 2) {
      extern void* ARGetStorageAddress(void);
      PortMusyXSize alen = 0, aram = aramGetStreamBufferAddress(si->hwStreamHandle, &alen);
      const u8* host = (const u8*)ARGetStorageAddress();
      u32 hm[2] = {2166136261u, 2166136261u}, ha[2] = {2166136261u, 2166136261u}, k, half = si->bytes / 2;
      for (k = 0; k < si->bytes; k += 5) {
        hm[k >= half] = (hm[k >= half] ^ ((const u8*)si->buffer)[k]) * 16777619u;
        ha[k >= half] = (ha[k >= half] ^ host[aram + k]) * 16777619u;
      }
      fprintf(stderr, "[ff debug]   ring %u: buffer halves %08x %08x, aram halves %08x %08x (aram %zx len %zu, bytes %u)\n", (unsigned)i, hm[0], hm[1], ha[0], ha[1], (size_t)aram, (size_t)alen, (unsigned)si->bytes);
    }
    fprintf(stderr, "[ff debug]   stream %u state %u type %u voice %u cat %d set %d last %u cpos %u active %d chunks %d size %u\n", (unsigned)i,
            (unsigned)si->state, (unsigned)si->type, (unsigned)si->voice,
            si->voice < kVoices ? (int)sVoices[si->voice].category : -1, si->voice < kVoices ? (int)sVoices[si->voice].set : -1,
            (unsigned)si->last, si->voice < kVoices ? (unsigned)hwGetPos(si->voice) : 0u, si->voice < kVoices ? (int)hwIsActive(si->voice) : -1, (int)(f->tail - f->head), (unsigned)si->size);
  }
}

extern void* ARGetStorageAddress(void);

void PortMusyX_StreamFlush(void* base, u32 offset, u32 bytes, u8 hwStreamHandle, void (*callback)(PortMusyXSize), u32 user) {
  if (sFfGhostWorld) {
    PortMusyXSize len = 0;
    PortMusyXSize aram = aramGetStreamBufferAddress(hwStreamHandle, &len);
    u8* host = (u8*)ARGetStorageAddress();
    bytes += (offset & 31);
    offset &= ~31u;
    bytes = (bytes + 31) & ~31u;
    if (host != NULL && aram != 0) memcpy(host + aram + offset, (u8*)base + offset, bytes);
    if (callback != NULL) callback(user);
    return;
  }
  hwFlushStream(base, offset, bytes, hwStreamHandle, callback, user);
}

PORT_MUSYX_STATE_BEGIN

static u32 ghost_stream_update(void* buffer1, u32 len1, void* buffer2, u32 len2, u32 user) {
  if (sFfFeedServe) return feed_serve(buffer1, len1, buffer2, len2, user);
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
    if (getenv("XD_FF_MUSIC_DEBUG")) fprintf(stderr, "[ff debug] stream %u buffer %p type %u size %u bytes %u last %u voice %u\n", (unsigned)i, (void*)si->buffer, (unsigned)si->type, (unsigned)si->size, (unsigned)si->bytes, (unsigned)si->last, (unsigned)si->voice);
    if (si->voice < kVoices && sVoices[si->voice].set && sVoices[si->voice].category != PORT_AUDIO_MUSIC) continue;
    cpos = hwGetPos(si->voice);
    if (si->type == 1) cpos = (cpos / 14) * 14;
    ahead = (si->last + si->size - cpos) % si->size;
    if (ahead == 0) ahead = si->size;
    if ((double)ahead / si->frq < best) best = (double)ahead / si->frq;
  }
  return best;
}
