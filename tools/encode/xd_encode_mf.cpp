#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {
#define CHECK(expr)                                                                          \
  do {                                                                                       \
    HRESULT hr_ = (expr);                                                                    \
    if (FAILED(hr_)) {                                                                       \
      std::fprintf(stderr, "xd_encode: %s failed (0x%08lx)\n", #expr, (unsigned long)hr_);   \
      return 2;                                                                              \
    }                                                                                        \
  } while (0)

struct Wav {
  uint32_t rate = 0, channels = 0, bits = 0;
  std::vector<uint8_t> data;
};

bool ReadWav(const std::string& path, Wav& w) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> all;
  static uint8_t buf[1 << 16];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) all.insert(all.end(), buf, buf + n);
  std::fclose(f);
  if (all.size() < 44 || std::memcmp(all.data(), "RIFF", 4) != 0) return false;
  size_t at = 12;
  while (at + 8 <= all.size()) {
    uint32_t len;
    std::memcpy(&len, &all[at + 4], 4);
    if (std::memcmp(&all[at], "fmt ", 4) == 0 && at + 8 + 16 <= all.size()) {
      uint16_t ch, bits;
      uint32_t rate;
      std::memcpy(&ch, &all[at + 10], 2);
      std::memcpy(&rate, &all[at + 12], 4);
      std::memcpy(&bits, &all[at + 22], 2);
      w.channels = ch;
      w.rate = rate;
      w.bits = bits;
    } else if (std::memcmp(&all[at], "data", 4) == 0) {
      const size_t end = std::min<size_t>(all.size(), at + 8 + len);
      w.data.assign(all.begin() + static_cast<std::ptrdiff_t>(at + 8), all.begin() + static_cast<std::ptrdiff_t>(end));
      return w.rate != 0;
    }
    at += 8 + len + (len & 1);
  }
  return false;
}

IMFMediaBuffer* MakeBuffer(const uint8_t* data, DWORD size) {
  IMFMediaBuffer* b = nullptr;
  if (FAILED(MFCreateMemoryBuffer(size, &b))) return nullptr;
  BYTE* p = nullptr;
  if (FAILED(b->Lock(&p, nullptr, nullptr))) {
    b->Release();
    return nullptr;
  }
  std::memcpy(p, data, size);
  b->Unlock();
  b->SetCurrentLength(size);
  return b;
}
}

int main(int argc, char** argv) {
  int w = 0, h = 0;
  double fps = 60000.0 / 1001.0;
  std::string audio, out;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    if (k == "--size") std::sscanf(argv[i + 1], "%dx%d", &w, &h);
    else if (k == "--fps") fps = std::atof(argv[i + 1]);
    else if (k == "--audio") audio = argv[i + 1];
    else if (k == "--out") out = argv[i + 1];
  }
  if (w <= 0 || h <= 0 || out.empty()) {
    std::fprintf(stderr, "usage: xd_encode --size WxH [--fps N] [--audio clip.wav] --out video.mp4 < frames.rgba\n");
    return 1;
  }
  _setmode(_fileno(stdin), _O_BINARY);
  CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  CHECK(MFStartup(MF_VERSION));
  const UINT32 fpsNum = static_cast<UINT32>(fps * 1001.0 + 0.5), fpsDen = 1001;
  const std::wstring wout(out.begin(), out.end());
  IMFAttributes* attrs = nullptr;
  CHECK(MFCreateAttributes(&attrs, 2));
  attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
  IMFSinkWriter* writer = nullptr;
  CHECK(MFCreateSinkWriterFromURL(wout.c_str(), nullptr, attrs, &writer));
  IMFMediaType *vout = nullptr, *vin = nullptr;
  CHECK(MFCreateMediaType(&vout));
  vout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  vout->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  vout->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(w) * static_cast<UINT32>(h) * 6);
  vout->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(vout, MF_MT_FRAME_SIZE, w, h);
  MFSetAttributeRatio(vout, MF_MT_FRAME_RATE, fpsNum, fpsDen);
  MFSetAttributeRatio(vout, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  DWORD vStream = 0;
  CHECK(writer->AddStream(vout, &vStream));
  CHECK(MFCreateMediaType(&vin));
  vin->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  vin->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  vin->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(vin, MF_MT_FRAME_SIZE, w, h);
  MFSetAttributeRatio(vin, MF_MT_FRAME_RATE, fpsNum, fpsDen);
  MFSetAttributeRatio(vin, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  CHECK(writer->SetInputMediaType(vStream, vin, nullptr));
  const bool wantAudio = !audio.empty();
  DWORD aStream = 0;
  uint32_t aRate = 48000, aCh = 2;
  if (wantAudio) {
    Wav probe;
    if (ReadWav(audio, probe)) {
      aRate = probe.rate;
      aCh = probe.channels;
    }
    IMFMediaType *aout = nullptr, *ain = nullptr;
    CHECK(MFCreateMediaType(&aout));
    aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
    aout->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    aout->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    CHECK(writer->AddStream(aout, &aStream));
    CHECK(MFCreateMediaType(&ain));
    ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, aRate);
    ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, aCh);
    ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, aCh * 2);
    ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, aRate * aCh * 2);
    CHECK(writer->SetInputMediaType(aStream, ain, nullptr));
  }
  CHECK(writer->BeginWriting());
  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
  std::vector<uint8_t> rgba(frameBytes), flipped(frameBytes);
  const LONGLONG frameDur = static_cast<LONGLONG>(10000000.0 * fpsDen / fpsNum);
  LONGLONG t = 0;
  uint64_t frames = 0;
  while (std::fread(rgba.data(), 1, frameBytes, stdin) == frameBytes) {
    for (int y = 0; y < h; y++) {
      const uint8_t* src = &rgba[static_cast<size_t>(y) * static_cast<size_t>(w) * 4];
      uint8_t* dst = &flipped[static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4];
      for (int x = 0; x < w; x++) {
        dst[x * 4 + 0] = src[x * 4 + 2];
        dst[x * 4 + 1] = src[x * 4 + 1];
        dst[x * 4 + 2] = src[x * 4 + 0];
        dst[x * 4 + 3] = 255;
      }
    }
    IMFMediaBuffer* buf = MakeBuffer(flipped.data(), static_cast<DWORD>(frameBytes));
    IMFSample* sample = nullptr;
    if (!buf || FAILED(MFCreateSample(&sample))) return 2;
    sample->AddBuffer(buf);
    sample->SetSampleTime(t);
    sample->SetSampleDuration(frameDur);
    CHECK(writer->WriteSample(vStream, sample));
    sample->Release();
    buf->Release();
    t += frameDur;
    frames++;
  }
  Wav wav;
  if (wantAudio && ReadWav(audio, wav)) {
    const size_t bytesPerSec = static_cast<size_t>(wav.rate) * wav.channels * 2, chunk = bytesPerSec / 2;
    LONGLONG at = 0;
    for (size_t pos = 0; pos < wav.data.size(); pos += chunk) {
      const DWORD n = static_cast<DWORD>(std::min(chunk, wav.data.size() - pos));
      IMFMediaBuffer* buf = MakeBuffer(&wav.data[pos], n);
      IMFSample* sample = nullptr;
      if (!buf || FAILED(MFCreateSample(&sample))) return 2;
      sample->AddBuffer(buf);
      sample->SetSampleTime(at);
      const LONGLONG dur = static_cast<LONGLONG>(10000000.0 * n / static_cast<double>(bytesPerSec));
      sample->SetSampleDuration(dur);
      CHECK(writer->WriteSample(aStream, sample));
      sample->Release();
      buf->Release();
      at += dur;
    }
  } else if (wantAudio) {
    std::fprintf(stderr, "xd_encode: the sound file %s could not be read: no sound track\n", audio.c_str());
  }
  CHECK(writer->Finalize());
  writer->Release();
  MFShutdown();
  std::fprintf(stderr, "xd_encode: %llu frames written to %s\n", static_cast<unsigned long long>(frames), out.c_str());
  return frames > 0 ? 0 : 3;
}
