#ifndef ENGINE_AUDIO_CODEC_H
#define ENGINE_AUDIO_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::audio {

struct Pcm {
  int rate = 32000;
  std::vector<std::vector<float>> ch;
  size_t Frames() const { return ch.empty() ? 0 : ch[0].size(); }
  int Channels() const { return static_cast<int>(ch.size()); }
};

struct LoopPoints {
  bool found = false;
  uint64_t start = 0, end = 0;
};

bool ReadWav(const std::vector<uint8_t>& file, Pcm& out, LoopPoints& loop, std::string& error);
std::vector<uint8_t> WriteWav(const Pcm& pcm, const LoopPoints* loop);

bool ReadOgg(const std::vector<uint8_t>& file, Pcm& out, LoopPoints& loop, std::string& error);

bool ReadSoundFile(const std::vector<uint8_t>& file, Pcm& out, LoopPoints& loop, std::string& error);

std::vector<float> Resample(const std::vector<float>& in, double ratio, size_t outFrames = 0);

struct AdpcmChannel {
  int16_t coef[16] = {};
  int16_t gain = 0;
  int16_t ps = 0;
  int16_t hist1 = 0, hist2 = 0;
  int16_t loopPs = 0;
  int16_t loopHist1 = 0, loopHist2 = 0;
};
constexpr size_t kAdpcmFrameBytes = 8, kAdpcmFrameSamples = 14;
inline size_t AdpcmFrames(size_t samples) { return (samples + kAdpcmFrameSamples - 1) / kAdpcmFrameSamples; }

std::vector<int16_t> AdpcmDecode(const uint8_t* frames, size_t frameBytes, size_t samples, const AdpcmChannel& channel, bool fromLoop = false);

void AdpcmEncode(const std::vector<int16_t>& pcm, long long loopSample, AdpcmChannel& channel, std::vector<uint8_t>& frames);

}

#endif
