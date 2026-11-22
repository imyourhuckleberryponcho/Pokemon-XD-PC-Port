#include "audio_codec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace engine::audio {

namespace {
std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
}

bool ReadOgg(const std::vector<uint8_t>& file, Pcm& out, LoopPoints& loop, std::string& error) {
  loop = LoopPoints{};
  int err = 0;
  stb_vorbis* v = stb_vorbis_open_memory(file.data(), static_cast<int>(file.size()), &err, nullptr);
  if (v == nullptr) {
    error = "this is not an OGG Vorbis file that can be read (error " + std::to_string(err) + ")";
    return false;
  }
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  const int channels = info.channels;
  const unsigned total = stb_vorbis_stream_length_in_samples(v);
  out = Pcm{};
  out.rate = static_cast<int>(info.sample_rate);
  out.ch.assign(static_cast<size_t>(std::min(channels, 2)), std::vector<float>());
  std::vector<float> buffer(4096 * static_cast<size_t>(channels));
  while (true) {
    const int n = stb_vorbis_get_samples_float_interleaved(v, channels, buffer.data(), static_cast<int>(buffer.size()));
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      for (size_t c = 0; c < out.ch.size(); c++) out.ch[c].push_back(buffer[static_cast<size_t>(i) * static_cast<size_t>(channels) + c]);
    }
  }
  const stb_vorbis_comment comments = stb_vorbis_get_comment(v);
  long long start = -1, length = -1, end = -1;
  for (int i = 0; i < comments.comment_list_length; i++) {
    const std::string c = comments.comment_list[i];
    const size_t eq = c.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Upper(c.substr(0, eq)), value = c.substr(eq + 1);
    const long long number = std::strtoll(value.c_str(), nullptr, 10);
    if (key == "LOOPSTART") start = number;
    else if (key == "LOOPLENGTH") length = number;
    else if (key == "LOOPEND") end = number;
  }
  stb_vorbis_close(v);
  (void)total;
  if (start >= 0 && (length > 0 || end > start)) {
    loop.found = true;
    loop.start = static_cast<uint64_t>(start);
    loop.end = static_cast<uint64_t>(length > 0 ? start + length : end);
    if (loop.end > out.Frames()) loop.end = out.Frames();
  }
  return true;
}

bool ReadSoundFile(const std::vector<uint8_t>& file, Pcm& out, LoopPoints& loop, std::string& error) {
  if (file.size() >= 4 && std::memcmp(file.data(), "RIFF", 4) == 0) return ReadWav(file, out, loop, error);
  if (file.size() >= 4 && std::memcmp(file.data(), "OggS", 4) == 0) return ReadOgg(file, out, loop, error);
  error = "the file is neither a WAV nor an OGG Vorbis file (other formats: convert it to WAV first, for example with Audacity or ffmpeg)";
  return false;
}

}
