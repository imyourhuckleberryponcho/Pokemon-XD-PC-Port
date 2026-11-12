#include <port_sfx.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

extern "C" void* ARGetStorageAddress(void);
extern "C" uint32_t ARGetSize(void);

namespace {

constexpr int kRate = 32000;
constexpr double kMaxSeconds = 3.0;
constexpr double kTickMs = 8000.0 / 6144.0;
constexpr float kMix = 0.5f;

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct Group {
  const uint8_t* project;
  const uint8_t* group;
  uint16_t id;
  const uint8_t* sdir;
  const uint8_t* pool;
};
std::mutex sLock;
std::vector<Group> sGroups;
std::map<uint32_t, std::vector<int16_t>> sCache;
bool sLog = false;
float sGain = 1.f;
SDL_AudioStream* sStream = nullptr;
bool sNoOutput = false;

const uint8_t* FindFx(uint16_t fxId) {
  for (const Group& g : sGroups) {
    if (Be16(g.group + 6) != 1) continue;
    const uint8_t* fd = g.project + Be32(g.group + 0x1C);
    const uint16_t n = Be16(fd);
    for (uint16_t i = 0; i < n; i++) {
      const uint8_t* fx = fd + 4 + i * 10;
      if (Be16(fx) == fxId) return fx;
    }
  }
  return nullptr;
}

const uint8_t* FindMacro(uint16_t id) {
  for (const Group& g : sGroups) {
    if (g.pool == nullptr) continue;
    const uint8_t* m = g.pool + Be32(g.pool);
    for (int guard = 0; guard < 4096; guard++) {
      const uint32_t next = Be32(m);
      if (next == 0xFFFFFFFF) break;
      if (Be16(m + 4) == id) return m + 8;
      m += next;
    }
  }
  return nullptr;
}

struct Sample {
  const uint8_t* data = nullptr;
  uint32_t length = 0, loop = 0, loopLength = 0;
  uint32_t rate = 32000;
  uint8_t baseKey = 60;
  uint8_t type = 0;
  int16_t coef[8][2] = {};
  uint8_t initialPS = 0;
};

bool FindSample(uint16_t id, Sample& out) {
  const uint8_t* aram = static_cast<const uint8_t*>(ARGetStorageAddress());
  const uint32_t aramSize = aram != nullptr ? ARGetSize() : 0;
  for (const Group& g : sGroups) {
    for (const uint8_t* s = g.sdir; Be16(s) != 0xFFFF; s += 0x20) {
      if (Be16(s) != id) continue;
      uint16_t ref;
      std::memcpy(&ref, s + 2, 2);
      if (ref == 0 || ref == 0xFFFF) continue;
      uint32_t addr;
      std::memcpy(&addr, s + 8, 4);
      const uint32_t info = Be32(s + 0xC), len = Be32(s + 0x10);
      out.length = len & 0xFFFFFF;
      out.type = static_cast<uint8_t>(len >> 24);
      out.loop = Be32(s + 0x14);
      out.loopLength = Be32(s + 0x18);
      out.rate = info & 0xFFFFFF;
      out.baseKey = static_cast<uint8_t>(info >> 24);
      const uint32_t extra = Be32(s + 0x1C);
      const uint32_t bytes = (out.length + 13) / 14 * 8;
      if (aram == nullptr || addr >= aramSize || bytes > aramSize - addr) return false;
      out.data = aram + addr;
      if (extra != 0) {
        const uint8_t* a = g.sdir + extra;
        out.initialPS = a[2];
        for (int k = 0; k < 8; k++) {
          out.coef[k][0] = static_cast<int16_t>(Be16(a + 8 + k * 4));
          out.coef[k][1] = static_cast<int16_t>(Be16(a + 10 + k * 4));
        }
      }
      return true;
    }
  }
  return false;
}

std::vector<int16_t> Decode(const Sample& s) {
  std::vector<int16_t> out;
  out.reserve(s.length);
  int32_t h1 = 0, h2 = 0;
  for (uint32_t i = 0; i < s.length; i++) {
    const uint32_t frame = i / 14, k = i % 14;
    const uint8_t* f = s.data + frame * 8;
    const int ps = f[0], scale = 1 << (ps & 0xF), pred = (ps >> 4) & 7;
    const uint8_t b = f[1 + k / 2];
    int nib = (k & 1) ? (b & 0xF) : (b >> 4);
    if (nib >= 8) nib -= 16;
    int32_t v = ((nib * scale) << 11) + s.coef[pred][0] * h1 + s.coef[pred][1] * h2;
    v = (v + 1024) >> 11;
    v = std::clamp(v, -32768, 32767);
    h2 = h1, h1 = v;
    out.push_back(static_cast<int16_t>(v));
  }
  return out;
}

float VolumeGain(int v) {
  const float x = std::clamp(v, 0, 127) / 127.f;
  return x * x;
}

struct Voice {
  std::vector<int16_t> pcm;
  Sample smp;
  bool playing = false;
  double pos = 0;
  int note = 60, detune = 0;
  double fixedHz = 0;
  float volume = 1.f;
  int pan = 64;
  double aMs = 0, dMs = 0, sLevel = 1, rMs = 5;
  bool keyOff = false;
  double keyOffAt = 0;
  double startAt = 0;
};

double PitchStep(const Voice& v) {
  const double hz = v.fixedHz > 0
                        ? v.fixedHz
                        : v.smp.rate * std::pow(2.0, (v.note - v.smp.baseKey + v.detune / 100.0) / 12.0);
  return hz / kRate;
}

void RenderSpan(Voice& v, std::vector<int16_t>& out, double& t, double seconds) {
  const int n = static_cast<int>(seconds * kRate + 0.5);
  const double step = PitchStep(v);
  const float pl = std::cos(static_cast<float>(v.pan) / 127.f * 1.5707964f);
  const float pr = std::sin(static_cast<float>(v.pan) / 127.f * 1.5707964f);
  for (int i = 0; i < n; i++, t += 1.0 / kRate) {
    float s = 0.f;
    if (v.playing && !v.pcm.empty()) {
      const size_t at = static_cast<size_t>(v.pos);
      if (at + 1 >= v.pcm.size()) {
        if (v.smp.loopLength > 0) {
          v.pos = v.smp.loop + std::fmod(v.pos - v.smp.loop, static_cast<double>(v.smp.loopLength));
        } else {
          v.playing = false;
        }
      }
      if (v.playing) {
        const size_t a = static_cast<size_t>(v.pos);
        const double frac = v.pos - a;
        const float x0 = v.pcm[a], x1 = a + 1 < v.pcm.size() ? v.pcm[a + 1] : 0.f;
        s = static_cast<float>(x0 + (x1 - x0) * frac);
        v.pos += step;
        const double age = (t - v.startAt) * 1000.0;
        double env;
        if (age < v.aMs) env = age / v.aMs;
        else if (age < v.aMs + v.dMs) env = 1.0 - (1.0 - v.sLevel) * (age - v.aMs) / v.dMs;
        else env = v.sLevel;
        if (v.keyOff) {
          const double since = (t - v.keyOffAt) * 1000.0;
          env *= since >= v.rMs ? 0.0 : 1.0 - since / v.rMs;
          if (since >= v.rMs) v.playing = false;
        }
        s *= static_cast<float>(env) * v.volume;
      }
    }
    out.push_back(static_cast<int16_t>(std::clamp(s * pl * kMix, -32768.f, 32767.f)));
    out.push_back(static_cast<int16_t>(std::clamp(s * pr * kMix, -32768.f, 32767.f)));
  }
}

bool Render(uint16_t fxId, uint8_t vol, uint8_t pan, std::vector<int16_t>& out) {
  const uint8_t* fx = FindFx(fxId);
  if (fx == nullptr) {
    std::fprintf(stderr, "[port sfx] sound effect %u: not in the loaded sound groups\n", fxId);
    return false;
  }
  const uint16_t macroId = Be16(fx + 2);
  const uint8_t* step = FindMacro(macroId);
  if (step == nullptr) {
    std::fprintf(stderr, "[port sfx] sound effect %u: its macro %u is not loaded\n", fxId, macroId);
    return false;
  }
  Voice v;
  v.note = fx[8];
  v.volume = VolumeGain(vol == 0xFF ? fx[6] : vol);
  v.pan = pan == 0xFF ? fx[7] : pan;
  double t = 0;
  bool ended = false;
  if (sLog) std::fprintf(stderr, "[port sfx] fx %u: macro %u key %u volume %u pan %u\n", fxId, macroId, fx[8], fx[6], fx[7]);
  for (int guard = 0; guard < 256 && !ended && t < kMaxSeconds; guard++, step += 8) {
    const uint32_t p0 = Be32(step), p1 = Be32(step + 4);
    const uint8_t cmd = p0 & 0x7F;
    if (sLog) std::fprintf(stderr, "[port sfx]   step %02X %08X %08X\n", cmd, p0, p1);
    auto wait = [&](bool keyOffWait, bool sampleEndWait, uint32_t time, bool ms) {
      double sec;
      if (time == 0xFFFF) {
        sec = kMaxSeconds;
      } else {
        sec = (ms ? time : time * kTickMs) / 1000.0;
      }
      if ((sampleEndWait || keyOffWait) && v.playing && v.smp.loopLength == 0) {
        const double left = (v.pcm.size() - v.pos) / PitchStep(v) / kRate;
        sec = std::min(sec, left);
      }
      if (!v.playing && sampleEndWait) sec = 0;
      RenderSpan(v, out, t, std::min(sec, kMaxSeconds - t));
    };
    switch (cmd) {
    case 0x00:
    case 0x01:
      ended = true;
      break;
    case 0x04:
    case 0x07:
      if ((p1 >> 16) != 0) {
        if ((p0 >> 16) & 1) std::fprintf(stderr, "[port sfx] fx %u: a random wait (taken as its full time)\n", fxId);
        wait(((p0 >> 8) & 1) != 0, ((p0 >> 24) & 1) != 0, p1 >> 16, cmd == 0x07 || ((p1 >> 8) & 1) != 0);
      }
      break;
    case 0x10: {
      const uint16_t sid = static_cast<uint16_t>(p0 >> 8);
      if (!FindSample(sid, v.smp)) {
        std::fprintf(stderr, "[port sfx] fx %u: sample %u not found in ARAM\n", fxId, sid);
        return false;
      }
      if (v.smp.type != 0) {
        std::fprintf(stderr, "[port sfx] fx %u: sample %u has a kind (%u) the port doesn't decode\n", fxId, sid, v.smp.type);
        return false;
      }
      v.pcm = Decode(v.smp);
      v.pos = (p0 >> 24) == 0 ? std::min<double>(p1, v.pcm.empty() ? 0 : v.pcm.size() - 1) : 0;
      v.playing = true;
      v.startAt = t;
      break;
    }
    case 0x11:
      v.playing = false;
      break;
    case 0x12:
      if (!v.keyOff) v.keyOff = true, v.keyOffAt = t;
      break;
    case 0x0C: {
      const uint16_t id = static_cast<uint16_t>(p0 >> 8);
      const uint8_t* m = nullptr;
      for (const Group& g : sGroups) {
        if (g.pool == nullptr) continue;
        const uint8_t* c = g.pool + Be32(g.pool + 4);
        for (int k = 0; k < 4096 && Be32(c) != 0xFFFFFFFF; k++, c += Be32(c)) {
          if (Be16(c + 4) == id) {
            m = c + 8;
            break;
          }
        }
        if (m != nullptr) break;
      }
      if (m != nullptr && (p0 >> 24) == 0) {
        auto le16 = [](const uint8_t* q) { return static_cast<uint16_t>(q[0] | q[1] << 8); };
        v.aMs = le16(m), v.dMs = le16(m + 2), v.sLevel = std::min(le16(m + 4) * 8, 0x7FFF) / 32767.0, v.rMs = le16(m + 6);
      } else if (m != nullptr) {
        std::fprintf(stderr, "[port sfx] fx %u: a DLS ADSR (taken as none)\n", fxId);
      }
      break;
    }
    case 0x0D: {
      const int scale = (p0 >> 8) & 0xFF, add = (p0 >> 16) & 0xFF;
      const bool fromOrg = ((p1 >> 8) & 0xFF) != 0;
      const float vol7 = std::sqrt(v.volume) * 127.f;
      const float org = static_cast<float>(vol == 0xFF ? fx[6] : vol);
      float nv = (fromOrg ? org : vol7) * scale / 127.f + add;
      v.volume = VolumeGain(static_cast<int>(std::min(nv, 127.f)));
      break;
    }
    case 0x0E:
      v.pan = (p0 >> 8) & 0x7F;
      break;
    case 0x18:
      v.note = ((p0 >> 24) & 0xFF) == 0 ? v.note + static_cast<int8_t>(p0 >> 8) : fx[8] + static_cast<int8_t>(p0 >> 8);
      v.note = std::clamp(v.note, 0, 127);
      v.detune = static_cast<int8_t>(p0 >> 16);
      wait(false, false, p1 >> 16, ((p1 >> 8) & 1) != 0);
      break;
    case 0x19:
      v.note = (p0 >> 8) & 0x7F;
      v.detune = static_cast<int8_t>(p0 >> 16);
      wait(false, false, p1 >> 16, ((p1 >> 8) & 1) != 0);
      break;
    case 0x1F:
      v.fixedHz = (p0 >> 8) & 0xFFFFFF;
      break;
    case 0x40:
      if (((p0 >> 8) & 0xFF) == 7) {
        const float pct = static_cast<float>(static_cast<int16_t>(p0 >> 16)) / 100.f;
        const float vol7 = std::sqrt(v.volume) * 127.f;
        v.volume = VolumeGain(static_cast<int>(std::lround(std::clamp(vol7 * pct, 0.f, 127.f))));
      }
      break;
    case 0x2A:
      break;
    case 0x36: case 0x37: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x30: case 0x31: case 0x38:
    case 0x39: case 0x33: case 0x34: case 0x35: case 0x59: case 0x5A:
      break;
    default:
      std::fprintf(stderr, "[port sfx] fx %u: macro step %02X not followed (skipped)\n", fxId, cmd);
      break;
    }
  }
  if (!ended && v.playing && t < kMaxSeconds) {
    const double left = v.smp.loopLength == 0 ? (v.pcm.size() - v.pos) / PitchStep(v) / kRate : 0.0;
    RenderSpan(v, out, t, std::min(left, kMaxSeconds - t));
  }
  if (v.playing && v.keyOff == false && v.smp.loopLength > 0) {
    const size_t fade = std::min<size_t>(out.size() / 2, kRate / 50);
    for (size_t i = 0; i < fade; i++) {
      const float g = static_cast<float>(i) / fade;
      out[out.size() - 2 * (fade - i)] = static_cast<int16_t>(out[out.size() - 2 * (fade - i)] * (1.f - g));
      out[out.size() - 2 * (fade - i) + 1] = static_cast<int16_t>(out[out.size() - 2 * (fade - i) + 1] * (1.f - g));
    }
  }
  if (sLog) std::fprintf(stderr, "[port sfx] fx %u: %.3f s\n", fxId, out.size() / 2.0 / kRate);
  return true;
}

const std::vector<int16_t>* Rendered(uint16_t fxId, uint8_t vol, uint8_t pan) {
  const uint32_t key = static_cast<uint32_t>(fxId) << 16 | vol << 8 | pan;
  auto it = sCache.find(key);
  if (it != sCache.end()) return it->second.empty() ? nullptr : &it->second;
  std::vector<int16_t> pcm;
  if (!Render(fxId, vol, pan, pcm)) pcm.clear();
  return sCache.emplace(key, std::move(pcm)).first->second.empty() ? nullptr : &sCache[key];
}

bool OpenOutput() {
  if (sStream != nullptr) return true;
  if (sNoOutput) return false;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    sNoOutput = true;
    return false;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, kRate};
  sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (sStream == nullptr) {
    std::fprintf(stderr, "[port sfx] no sound output for the menu sounds (%s)\n", SDL_GetError());
    sNoOutput = true;
    return false;
  }
  SDL_SetAudioStreamGain(sStream, sGain);
  SDL_ResumeAudioStreamDevice(sStream);
  return true;
}

}

extern "C" {

void PortSfx_NoteGroup(const void* project, uint16_t groupId, const void* sampleDir, const void* pool) {
  if (project == nullptr || sampleDir == nullptr) return;
  std::lock_guard<std::mutex> lock(sLock);
  if (const char* l = std::getenv("XD_SFX_LOG")) sLog = l[0] == '1';
  const uint8_t* g = static_cast<const uint8_t*>(project);
  for (int guard = 0; guard < 1024; guard++) {
    const uint32_t next = Be32(g);
    if (next == 0xFFFFFFFF) return;
    if (Be16(g + 4) == groupId) {
      sGroups.push_back({static_cast<const uint8_t*>(project), g, groupId, static_cast<const uint8_t*>(sampleDir),
                         static_cast<const uint8_t*>(pool)});
      if (sLog) std::fprintf(stderr, "[port sfx] group %u (type %u) noted\n", groupId, Be16(g + 6));
      return;
    }
    g = static_cast<const uint8_t*>(project) + next;
  }
}

bool PortSfx_Play(uint16_t fxId, uint8_t vol, uint8_t pan) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::vector<int16_t>* pcm = Rendered(fxId, vol, pan);
  if (pcm == nullptr || !OpenOutput()) return false;
  SDL_ClearAudioStream(sStream);
  SDL_PutAudioStreamData(sStream, pcm->data(), static_cast<int>(pcm->size() * sizeof(int16_t)));
  return true;
}

void PortSfx_SetVolume(float gain) {
  std::lock_guard<std::mutex> lock(sLock);
  sGain = std::clamp(gain, 0.f, 1.f);
  if (sStream != nullptr) SDL_SetAudioStreamGain(sStream, sGain);
}

bool PortSfx_WriteWav(uint16_t fxId, uint8_t vol, uint8_t pan, const char* path) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::vector<int16_t>* pcm = Rendered(fxId, vol, pan);
  if (pcm == nullptr) return false;
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  const uint32_t data = static_cast<uint32_t>(pcm->size() * 2);
  auto u32 = [&](uint32_t x) { std::fwrite(&x, 4, 1, f); };
  auto u16 = [&](uint16_t x) { std::fwrite(&x, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + data);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16), u16(1), u16(2), u32(kRate), u32(kRate * 4), u16(4), u16(16);
  std::fwrite("data", 1, 4, f);
  u32(data);
  std::fwrite(pcm->data(), 2, pcm->size(), f);
  std::fclose(f);
  return true;
}

}
