#ifndef ENGINE_AUDIO_STREAM_H
#define ENGINE_AUDIO_STREAM_H

#include "audio_codec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::audio {

constexpr size_t kStreamHeaderBytes = 192;
constexpr size_t kStreamBlockBytes = 0x8000;
constexpr size_t kStreamBlockSamples = kStreamBlockBytes / 8 * 14;

struct StreamInfo {
  uint32_t rate = 32000;
  uint32_t samples = 0;
  uint32_t channels = 2;
  bool loop = false;
  uint32_t loopStartNibble = 0;
  uint32_t loopEndNibble = 0;
  uint32_t blocks = 0;
  uint64_t loopStartSample = 0;
  uint64_t loopEndSample = 0;
  uint32_t version = 0x20040109;
};

bool ParseStreamHeader(const std::vector<uint8_t>& header, StreamInfo& info, AdpcmChannel channels[2], std::string& error);

bool DecodeStream(const std::vector<uint8_t>& header, const std::vector<uint8_t>& body, Pcm& out, LoopPoints& loop, StreamInfo& info, std::string& error);

struct BuildOptions {
  bool loop = false;
  uint64_t loopStart = 0, loopEnd = 0;
  bool trimIntro = false;
  size_t maxBlocks = 0;
  uint32_t version = 0x20040109;
  uint32_t forceRate = 0;
};

struct BuildResult {
  std::vector<uint8_t> header, body;
  StreamInfo info;
  std::vector<std::string> notes;
};

bool BuildStream(const Pcm& in, const BuildOptions& options, BuildResult& out, std::string& error);

}

#endif
