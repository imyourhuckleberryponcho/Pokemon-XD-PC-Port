#ifndef ENGINE_AUDIO_SFX_H
#define ENGINE_AUDIO_SFX_H

#include "audio_codec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::audio::sfx {

constexpr size_t kSdirEntryBytes = 32;
constexpr size_t kAdpcmInfoBytes = 40;

struct Sample {
  uint16_t id = 0;
  uint32_t offset = 0;
  uint32_t info = 0;
  uint32_t length = 0;
  uint8_t type = 0;
  uint32_t loopOffset = 0, loopLength = 0;
  uint32_t extra = 0;
  size_t entry = 0;
  uint32_t Rate() const { return info & 0xFFFFFF; }
  uint32_t Key() const { return info >> 24; }
  uint32_t Bytes() const { return (length + 13) / 14 * 8; }
};

bool ParseSdir(const std::vector<uint8_t>& sdir, std::vector<Sample>& out, std::string& error);

struct Effect {
  uint16_t fx = 0;
  uint16_t macro = 0;
  std::vector<uint16_t> samples;
  uint8_t volume = 0, priority = 0, maxVoices = 0;
};
bool ParseEffects(const std::vector<uint8_t>& proj, const std::vector<uint8_t>& pool, std::vector<Effect>& out, std::string& error);

bool DecodeSample(const std::vector<uint8_t>& sdir, const std::vector<uint8_t>& samp, uint16_t id, Pcm& out, LoopPoints& loop, std::string& error);

struct ReplaceOptions {
  enum class Loop { Keep, On, Off } loop = Loop::Keep;
  uint64_t loopStart = 0, loopEnd = 0;
  uint32_t rate = 0;
  double volume = 1.0;
  size_t maxGrow = 256 * 1024;
};
struct ReplaceResult {
  bool inPlace = false;
  uint32_t offset = 0;
  uint32_t samples = 0;
  size_t grewBytes = 0;
  std::vector<std::string> notes;
};
bool ReplaceSample(std::vector<uint8_t>& sdir, std::vector<uint8_t>& samp, uint16_t id, const Pcm& sound, const LoopPoints& fileLoop,
                   const ReplaceOptions& options, ReplaceResult& result, std::string& error);

}

#endif
