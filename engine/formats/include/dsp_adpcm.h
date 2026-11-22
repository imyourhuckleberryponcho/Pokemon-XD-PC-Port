#ifndef ENGINE_DSP_ADPCM_H
#define ENGINE_DSP_ADPCM_H

#include <cstdint>
#include <string>
#include <vector>

namespace engine::dsp {

struct Pcm {
  uint32_t rate = 0;
  std::vector<std::vector<int16_t>> channels;
};

bool ReadWav(const std::vector<uint8_t>& file, Pcm& out, std::string& error);

Pcm Resample(const Pcm& in, uint32_t rate);

struct Encoded {
  int16_t coefficients[16] = {};
  std::vector<uint8_t> frames;
  uint32_t samples = 0;
};

Encoded Encode(const std::vector<int16_t>& samples);

std::vector<int16_t> Decode(const Encoded& in);

}

#endif
