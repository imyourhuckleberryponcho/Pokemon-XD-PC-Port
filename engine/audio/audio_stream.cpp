#include "audio_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace engine::audio {

namespace {

uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
int16_t Be16s(const uint8_t* p) { return static_cast<int16_t>(static_cast<uint16_t>(p[0] << 8 | p[1])); }
void PutBe32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = static_cast<uint8_t>(x >> 24), v[at + 1] = static_cast<uint8_t>(x >> 16), v[at + 2] = static_cast<uint8_t>(x >> 8), v[at + 3] = static_cast<uint8_t>(x);
}
void PutBe16(std::vector<uint8_t>& v, size_t at, int16_t x) { v[at] = static_cast<uint8_t>(static_cast<uint16_t>(x) >> 8), v[at + 1] = static_cast<uint8_t>(x); }

std::string Fmt(const char* format, double a, double b = 0, double c = 0) {
  char buf[256];
  std::snprintf(buf, sizeof buf, format, a, b, c);
  return buf;
}

int16_t ToS16(float x) {
  const long v = std::lround(x * 32768.0f);
  return static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

}

bool ParseStreamHeader(const std::vector<uint8_t>& h, StreamInfo& info, AdpcmChannel ch[2], std::string& error) {
  if (h.size() != kStreamHeaderBytes || std::memcmp(h.data(), "I_SF", 4) != 0) {
    error = "this is not a sound stream header (192 bytes starting with I_SF)";
    return false;
  }
  info = StreamInfo{};
  info.version = Be32(&h[4]);
  info.rate = Be32(&h[8]);
  info.samples = Be32(&h[0x0C]);
  info.channels = Be32(&h[0x14]);
  if (Be32(&h[0x18]) != kStreamBlockBytes || info.channels < 1 || info.channels > 2 || info.rate == 0) {
    error = "the stream header has a block size or channel count this tool does not know";
    return false;
  }
  info.loop = Be32(&h[0x1C]) != 0;
  info.loopStartNibble = Be32(&h[0x20]);
  info.loopEndNibble = Be32(&h[0x24]);
  const size_t bytes = AdpcmFrames(info.samples) * kAdpcmFrameBytes;
  info.blocks = static_cast<uint32_t>((bytes + kStreamBlockBytes - 1) / kStreamBlockBytes);
  if (info.loop) {
    info.loopStartSample = static_cast<uint64_t>((info.loopStartNibble - 2) >> 16) * kStreamBlockSamples;
    info.loopEndSample = static_cast<uint64_t>((info.loopEndNibble + 0xFFFF) >> 16) * kStreamBlockSamples;
  }
  for (uint32_t c = 0; c < info.channels; c++) {
    const uint8_t* p = &h[0x40 + 0x40 * c];
    for (int i = 0; i < 16; i++) ch[c].coef[i] = Be16s(p + 2 * i);
    ch[c].gain = Be16s(p + 32), ch[c].ps = Be16s(p + 34), ch[c].hist1 = Be16s(p + 36), ch[c].hist2 = Be16s(p + 38);
    ch[c].loopPs = Be16s(p + 40), ch[c].loopHist1 = Be16s(p + 42), ch[c].loopHist2 = Be16s(p + 44);
  }
  return true;
}

bool DecodeStream(const std::vector<uint8_t>& header, const std::vector<uint8_t>& body, Pcm& out, LoopPoints& loop, StreamInfo& info, std::string& error) {
  AdpcmChannel ch[2];
  if (!ParseStreamHeader(header, info, ch, error)) return false;
  const size_t need = static_cast<size_t>(info.blocks) * info.channels * kStreamBlockBytes;
  const size_t bytesPerChannel = AdpcmFrames(info.samples) * kAdpcmFrameBytes;
  if (body.size() + kStreamBlockBytes * info.channels < need) {
    error = "the stream body is shorter than its header says (" + std::to_string(body.size()) + " bytes, needs " + std::to_string(need) + ")";
    return false;
  }
  out = Pcm{};
  out.rate = static_cast<int>(info.rate);
  loop = LoopPoints{};
  for (uint32_t c = 0; c < info.channels; c++) {
    std::vector<uint8_t> frames;
    frames.reserve(bytesPerChannel);
    for (uint32_t b = 0; b < info.blocks && frames.size() < bytesPerChannel; b++) {
      const size_t at = (static_cast<size_t>(b) * info.channels + c) * kStreamBlockBytes;
      if (at >= body.size()) break;
      const size_t take = std::min({kStreamBlockBytes, body.size() - at, bytesPerChannel - frames.size()});
      frames.insert(frames.end(), body.begin() + static_cast<long>(at), body.begin() + static_cast<long>(at + take));
    }
    const std::vector<int16_t> pcm = AdpcmDecode(frames.data(), frames.size(), info.samples, ch[c]);
    std::vector<float> f(pcm.size());
    for (size_t i = 0; i < pcm.size(); i++) f[i] = static_cast<float>(pcm[i]) / 32768.f;
    out.ch.push_back(std::move(f));
  }
  if (info.loop) {
    loop.found = true;
    loop.start = info.loopStartSample;
    loop.end = std::min<uint64_t>(info.loopEndSample, out.Frames());
  }
  return true;
}

bool BuildStream(const Pcm& input, const BuildOptions& o, BuildResult& r, std::string& error) {
  r = BuildResult{};
  if (input.ch.empty() || input.Frames() == 0) {
    error = "the sound file is empty";
    return false;
  }
  Pcm in = input;
  if (in.ch.size() == 1) in.ch.push_back(in.ch[0]);
  const size_t BS = kStreamBlockSamples;
  size_t n = in.Frames();
  uint32_t rate = static_cast<uint32_t>(in.rate);
  const uint32_t workRate = o.forceRate != 0 ? o.forceRate : (o.loop || rate < 8000 || rate > 48000) ? 32000 : rate;
  if (workRate != rate) {
    const double ratio = static_cast<double>(workRate) / rate;
    for (auto& c : in.ch) c = Resample(c, ratio);
    r.notes.push_back(Fmt("the sound was converted from %.0f Hz to %.0f Hz", rate, workRate));
    n = in.Frames();
    rate = workRate;
  }
  uint64_t loopStart = o.loopStart, loopEnd = o.loopEnd;
  if (workRate != static_cast<uint32_t>(input.rate)) {
    const double ratio = static_cast<double>(workRate) / input.rate;
    loopStart = static_cast<uint64_t>(std::llround(static_cast<double>(loopStart) * ratio));
    loopEnd = static_cast<uint64_t>(std::llround(static_cast<double>(loopEnd) * ratio));
  }
  size_t streamSamples = 0, loopStartStream = 0;
  uint32_t streamRate = rate;
  if (!o.loop) {
    streamSamples = n;
    if (o.maxBlocks != 0 && AdpcmFrames(n) * kAdpcmFrameBytes > o.maxBlocks * kStreamBlockBytes) {
      streamSamples = o.maxBlocks * BS;
      r.notes.push_back(Fmt("the sound was cut at %.2f seconds (the longest this slot holds) of %.2f", static_cast<double>(streamSamples) / rate, static_cast<double>(n) / rate));
    }
    for (auto& c : in.ch) c.resize(streamSamples);
  } else {
    if (loopEnd == 0 || loopEnd > n) loopEnd = n;
    if (loopStart >= loopEnd) {
      error = "the loop start is not before the loop end";
      return false;
    }
    const size_t len = static_cast<size_t>(loopEnd - loopStart), intro = static_cast<size_t>(loopStart);
    size_t bestM = 1, bestK = 1;
    double bestF = 0, bestDist = 1e30;
    for (size_t m = 1; m <= 16 && m * len <= static_cast<size_t>(rate) * 600; m++) {
      const size_t k = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(m * len) / BS)));
      const double f = static_cast<double>(k * BS) / static_cast<double>(m * len);
      const double dist = std::fabs(std::log(f));
      if (dist < bestDist - 1e-9) bestDist = dist, bestM = m, bestK = k, bestF = f;
      if (dist < 0.03) break;
    }
    Pcm s;
    s.rate = in.rate;
    for (auto& c : in.ch) {
      std::vector<float> v(c.begin(), c.begin() + static_cast<long>(loopEnd));
      for (size_t i = 1; i < bestM; i++) v.insert(v.end(), c.begin() + static_cast<long>(loopStart), c.begin() + static_cast<long>(loopEnd));
      s.ch.push_back(std::move(v));
    }
    const size_t introOut = static_cast<size_t>(std::llround(static_cast<double>(intro) * bestF)), loopOut = bestK * BS;
    for (auto& c : s.ch) c = Resample(c, bestF, introOut + loopOut);
    size_t front = 0, cut = 0;
    if (introOut % BS != 0) {
      if (o.trimIntro) cut = introOut % BS;
      else front = BS - introOut % BS;
    }
    for (auto& c : s.ch) {
      std::vector<float> v(front, 0.f);
      v.insert(v.end(), c.begin() + static_cast<long>(cut), c.end());
      c = std::move(v);
    }
    in = std::move(s);
    streamSamples = in.Frames();
    loopStartStream = introOut + front - cut;
    streamRate = static_cast<uint32_t>(std::llround(static_cast<double>(rate) * bestF));
    if (std::fabs(bestF - 1) > 0.0005) {
      r.notes.push_back(Fmt("so the loop is a whole number of blocks the game can repeat, it is played at %.0f Hz instead of %.0f Hz (the same tune and speed: the loop was resampled by %.4f)", streamRate, rate, bestF));
    }
    if (bestM > 1) r.notes.push_back(Fmt("the loop is short, so it is repeated %.0f times inside the stream", static_cast<double>(bestM)));
    if (front > 0) r.notes.push_back(Fmt("%.2f seconds of silence were put before the music so the loop starts where the game can jump back to", static_cast<double>(front) / streamRate));
    if (cut > 0) r.notes.push_back(Fmt("%.2f seconds were cut from the start so the loop starts where the game can jump back to", static_cast<double>(cut) / streamRate));
    if (o.maxBlocks != 0 && streamSamples / BS > o.maxBlocks) {
      error = "the sound needs " + std::to_string(streamSamples / BS) + " blocks but this slot holds " + std::to_string(o.maxBlocks);
      return false;
    }
  }
  const size_t frames = AdpcmFrames(streamSamples);
  const size_t blocks = (frames * kAdpcmFrameBytes + kStreamBlockBytes - 1) / kStreamBlockBytes;
  AdpcmChannel ac[2];
  std::vector<uint8_t> data[2];
  for (int c = 0; c < 2; c++) {
    std::vector<int16_t> pcm(streamSamples);
    for (size_t i = 0; i < streamSamples; i++) pcm[i] = ToS16(in.ch[static_cast<size_t>(c)][i]);
    AdpcmEncode(pcm, o.loop ? static_cast<long long>(loopStartStream) : -1, ac[c], data[c]);
  }
  r.body.assign(blocks * 2 * kStreamBlockBytes, 0);
  for (size_t b = 0; b < blocks; b++) {
    for (size_t c = 0; c < 2; c++) {
      const size_t from = b * kStreamBlockBytes, take = std::min(kStreamBlockBytes, data[c].size() - std::min(data[c].size(), from));
      if (take > 0) std::memcpy(&r.body[(b * 2 + c) * kStreamBlockBytes], &data[c][from], take);
    }
  }
  r.header.assign(kStreamHeaderBytes, 0);
  std::memcpy(r.header.data(), "I_SF", 4);
  PutBe32(r.header, 4, o.version);
  PutBe32(r.header, 8, streamRate);
  PutBe32(r.header, 0x0C, static_cast<uint32_t>(o.loop ? blocks * BS : streamSamples));
  PutBe32(r.header, 0x14, 2);
  PutBe32(r.header, 0x18, static_cast<uint32_t>(kStreamBlockBytes));
  if (o.loop) {
    PutBe32(r.header, 0x10, static_cast<uint32_t>(blocks * 0x10000));
    PutBe32(r.header, 0x1C, 1);
    PutBe32(r.header, 0x20, static_cast<uint32_t>(loopStartStream / kAdpcmFrameSamples * 16 + 2));
    PutBe32(r.header, 0x24, static_cast<uint32_t>(blocks * 0x10000 - 1));
    PutBe32(r.header, 0x28, static_cast<uint32_t>(blocks * 0x10000 - 1));
  } else {
    const uint32_t last = static_cast<uint32_t>(2 + (streamSamples - 1) + (streamSamples - 1) / kAdpcmFrameSamples * 2);
    PutBe32(r.header, 0x10, last + 1);
    PutBe32(r.header, 0x20, 2);
    PutBe32(r.header, 0x24, last);
    PutBe32(r.header, 0x28, last);
  }
  for (size_t c = 0; c < 2; c++) {
    const size_t at = 0x40 + 0x40 * c;
    for (size_t i = 0; i < 16; i++) PutBe16(r.header, at + 2 * i, ac[c].coef[i]);
    PutBe16(r.header, at + 32, ac[c].gain), PutBe16(r.header, at + 34, ac[c].ps), PutBe16(r.header, at + 36, ac[c].hist1), PutBe16(r.header, at + 38, ac[c].hist2);
    PutBe16(r.header, at + 40, ac[c].loopPs), PutBe16(r.header, at + 42, ac[c].loopHist1), PutBe16(r.header, at + 44, ac[c].loopHist2);
  }
  AdpcmChannel back[2];
  std::string ignore;
  ParseStreamHeader(r.header, r.info, back, ignore);
  r.notes.push_back(Fmt("%.1f seconds, %.0f blocks of 0x8000 bytes", static_cast<double>(streamSamples) / streamRate, static_cast<double>(blocks)));
  return true;
}

}
