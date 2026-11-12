#include <port_audio_categories.h>
#include <settings.h>

#include <dolphin/types.h>
#include <game/pxdvs/GSAPI/GSsnd/sndData.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/relglobal.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" u32 _sndStreamCallback(void* buf1, u32 len1, void* buf2, u32 len2, u32 user);

namespace {

struct StreamHead {
  void* master;
  void* child;
  s32 state;
  u32 stid;
  void* buf;
  u32 bufSize;
  u32 bufBytes;
  u32 x1c;
  u32 x20;
  u32 x24;
  u32 fsysId;
  u32 x2c;
  u32 sndId;
};

struct Tables {
  std::mutex lock;
  const void* builtFor = nullptr;
  std::vector<uint8_t> crySound;
  std::vector<uint8_t> cryFx;
};
Tables sTables;
std::atomic<bool> sLayoutWarned{false};

const SndData* SoundEntry(uint32_t id) {
  if (snd_data == nullptr || snd_data_number == nullptr || id >= *snd_data_number) return nullptr;
  return static_cast<const SndData*>(snd_data) + id;
}

void BuildTables() {
  if (pokemon_data == nullptr || pokemon_data_number == nullptr || snd_data == nullptr ||
      snd_data_number == nullptr) {
    return;
  }
  if (sTables.builtFor == pokemon_data) return;
  const uint32_t sounds = *snd_data_number;
  std::vector<uint8_t> crySound(sounds, 0), cryFx(0x10000, 0);
  auto mark = [&](uint32_t id) {
    if (id == 0 || id >= sounds) return;
    crySound[id] = 1;
    const SndData* d = SoundEntry(id);
    if (d != nullptr && d->se && !d->stream) cryFx[d->fxid] = 1;
  };
  for (uint32_t i = 0; i < *pokemon_data_number; i++) mark(pokemon_data[i].voice);
  if (snd_test_nakigoe != nullptr && snd_test_nakigoe_number != nullptr) {
    for (uint32_t i = 0; i < *snd_test_nakigoe_number; i++) mark(snd_test_nakigoe[i]);
  }
  sTables.crySound.swap(crySound);
  sTables.cryFx.swap(cryFx);
  sTables.builtFor = pokemon_data;
  const char* log = std::getenv("XD_AUDIO_CATEGORY_LOG");
  if (log != nullptr && log[0] == '1') {
    uint32_t cries = 0, cryStreams = 0, streams = 0, seStreams = 0, effects = 0, songs = 0, env = 0;
    for (uint32_t id = 0; id < sounds; id++) {
      const SndData* d = SoundEntry(id);
      cries += sTables.crySound[id];
      cryStreams += sTables.crySound[id] && d->stream;
      streams += d->stream && !d->se;
      seStreams += d->stream && d->se;
      effects += !d->stream && d->se;
      songs += !d->stream && !d->se;
      env += d->env;
    }
    std::fprintf(stderr,
                 "[audio] sound categories: %u sounds; cries %u (%u streamed); music streams %u, effect streams %u, "
                 "effects %u, songs %u; environment %u\n",
                 sounds, cries, cryStreams, streams, seStreams, effects, songs, env);
  }
}

int ClassifyFx(uint16_t fxId) {
  std::lock_guard<std::mutex> g(sTables.lock);
  BuildTables();
  return !sTables.cryFx.empty() && sTables.cryFx[fxId] ? PORT_AUDIO_CRIES : PORT_AUDIO_EFFECTS;
}

int ClassifyStream(void* update, uint32_t user, uint32_t streamId) {
  if (update != reinterpret_cast<void*>(&_sndStreamCallback) || user == 0) {
    return PORT_AUDIO_MUSIC;
  }
  const StreamHead* s = reinterpret_cast<const StreamHead*>(static_cast<uintptr_t>(user));
  if (s->stid != streamId) {
    if (!sLayoutWarned.exchange(true)) {
      std::fprintf(stderr, "[audio] GSsnd's stream layout looks different (games/xd/src/audio_categories.cpp "
                           "StreamHead): its streams count as music\n");
    }
    return PORT_AUDIO_MUSIC;
  }
  const uint32_t id = s->sndId;
  const SndData* d = SoundEntry(id);
  if (d == nullptr) return PORT_AUDIO_MUSIC;
  {
    std::lock_guard<std::mutex> g(sTables.lock);
    BuildTables();
    if (id < sTables.crySound.size() && sTables.crySound[id]) return PORT_AUDIO_CRIES;
  }
  return d->se ? PORT_AUDIO_EFFECTS : PORT_AUDIO_MUSIC;
}

struct VolumeSetting {
  const char* id;
  int category;
};
constexpr VolumeSetting kVolumes[] = {
    {"audio.music_volume", PORT_AUDIO_MUSIC},
    {"audio.sfx_volume", PORT_AUDIO_EFFECTS},
    {"audio.cry_volume", PORT_AUDIO_CRIES},
};

}

extern "C" {

void PortMusyX_GhostInstall(void);

void PortXD_AudioCategoriesInit(void) {
  PortAudio_SetCategoryClassifiers(ClassifyFx, ClassifyStream);
  PortMusyX_GhostInstall();
  for (const VolumeSetting& v : kVolumes) {
    PortAudio_SetCategoryVolume(v.category, static_cast<float>(engine::settings::Global().Int(v.id).Get()) / 100.f);
  }
}

int PortXD_AudioCategoryVolumeChanged(const char* id, int percent) {
  for (const VolumeSetting& v : kVolumes) {
    if (std::strcmp(v.id, id) == 0) {
      PortAudio_SetCategoryVolume(v.category, static_cast<float>(percent) / 100.f);
      return 1;
    }
  }
  return 0;
}

}
