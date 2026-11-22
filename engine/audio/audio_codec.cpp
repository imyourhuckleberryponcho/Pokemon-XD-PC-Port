#include "audio_codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::audio {

namespace {

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24; }
void PutLe16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x & 255), v.push_back((x >> 8) & 255); }
void PutLe32(std::vector<uint8_t>& v, uint32_t x) { PutLe16(v, x & 0xFFFF), PutLe16(v, x >> 16); }

int16_t Clamp16(long long v) { return static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

}

bool ReadWav(const std::vector<uint8_t>& f, Pcm& out, LoopPoints& loop, std::string& error) {
  loop = LoopPoints{};
  if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0) {
    error = "this is not a WAV file (no RIFF/WAVE header)";
    return false;
  }
  int format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  const uint8_t* data = nullptr;
  size_t dataSize = 0;
  for (size_t pos = 12; pos + 8 <= f.size();) {
    const uint8_t* c = f.data() + pos;
    size_t size = Le32(c + 4);
    const size_t body = pos + 8;
    if (body + size > f.size()) size = f.size() - body;
    if (std::memcmp(c, "fmt ", 4) == 0 && size >= 16) {
      format = Le16(c + 8);
      channels = Le16(c + 10);
      rate = Le32(c + 12);
      bits = Le16(c + 22);
      if (format == 0xFFFE && size >= 26) format = Le16(c + 32);
    } else if (std::memcmp(c, "data", 4) == 0) {
      data = f.data() + body;
      dataSize = size;
    } else if (std::memcmp(c, "smpl", 4) == 0 && size >= 36 + 24 && Le32(c + 8 + 28) >= 1) {
      loop.found = true;
      loop.start = Le32(c + 8 + 36 + 8);
      loop.end = static_cast<uint64_t>(Le32(c + 8 + 36 + 12)) + 1;
    }
    pos = body + size + (size & 1);
  }
  if (data == nullptr || channels < 1 || rate == 0) {
    error = "the WAV file has no sound data or no format chunk";
    return false;
  }
  const bool isFloat = format == 3;
  if (!(format == 1 || format == 3) || !(bits == 16 || bits == 24 || bits == 32 || (isFloat && bits == 64))) {
    error = "only 16, 24 and 32-bit PCM and 32-bit float WAV files are read (this one is format " + std::to_string(format) + ", " + std::to_string(bits) + " bits)";
    return false;
  }
  const size_t bytes = static_cast<size_t>(bits / 8), frames = dataSize / (bytes * static_cast<size_t>(channels));
  out = Pcm{};
  out.rate = static_cast<int>(rate);
  out.ch.assign(static_cast<size_t>(std::min(channels, 2)), std::vector<float>(frames));
  for (size_t i = 0; i < frames; i++) {
    for (int c = 0; c < static_cast<int>(out.ch.size()); c++) {
      const uint8_t* p = data + (i * static_cast<size_t>(channels) + static_cast<size_t>(c)) * bytes;
      float v;
      if (isFloat) {
        if (bits == 32) {
          float x;
          std::memcpy(&x, p, 4);
          v = x;
        } else {
          double x;
          std::memcpy(&x, p, 8);
          v = static_cast<float>(x);
        }
      } else if (bits == 16) {
        v = static_cast<int16_t>(Le16(p)) / 32768.f;
      } else if (bits == 24) {
        const int32_t x = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 24)) >> 8;
        v = static_cast<float>(x) / 8388608.f;
      } else {
        v = static_cast<float>(static_cast<int32_t>(Le32(p)) / 2147483648.0);
      }
      out.ch[static_cast<size_t>(c)][i] = v;
    }
  }
  if (loop.found && loop.end > frames) loop.end = frames;
  return true;
}

std::vector<uint8_t> WriteWav(const Pcm& pcm, const LoopPoints* loop) {
  const size_t n = pcm.Frames();
  const uint32_t channels = static_cast<uint32_t>(std::max(1, pcm.Channels()));
  const bool withLoop = loop != nullptr && loop->found;
  std::vector<uint8_t> v;
  const uint32_t dataBytes = static_cast<uint32_t>(n * channels * 2);
  const uint32_t smplBytes = withLoop ? 8 + 36 + 24 : 0;
  v.insert(v.end(), {'R', 'I', 'F', 'F'});
  PutLe32(v, 4 + 8 + 16 + smplBytes + 8 + dataBytes);
  v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  PutLe32(v, 16);
  PutLe16(v, 1), PutLe16(v, channels), PutLe32(v, static_cast<uint32_t>(pcm.rate)), PutLe32(v, static_cast<uint32_t>(pcm.rate) * channels * 2);
  PutLe16(v, channels * 2), PutLe16(v, 16);
  if (withLoop) {
    v.insert(v.end(), {'s', 'm', 'p', 'l'});
    PutLe32(v, 36 + 24);
    for (int i = 0; i < 7; i++) PutLe32(v, i == 2 ? static_cast<uint32_t>(1e9 / std::max(1, pcm.rate)) : 0);
    PutLe32(v, 1), PutLe32(v, 0);
    PutLe32(v, 0), PutLe32(v, 0), PutLe32(v, static_cast<uint32_t>(loop->start)), PutLe32(v, static_cast<uint32_t>(loop->end > 0 ? loop->end - 1 : 0)), PutLe32(v, 0), PutLe32(v, 0);
  }
  v.insert(v.end(), {'d', 'a', 't', 'a'});
  PutLe32(v, dataBytes);
  for (size_t i = 0; i < n; i++) {
    for (uint32_t c = 0; c < channels; c++) {
      const float x = pcm.ch[c][i] * 32768.f;
      PutLe16(v, static_cast<uint16_t>(Clamp16(static_cast<long long>(std::lround(x)))));
    }
  }
  return v;
}

std::vector<float> Resample(const std::vector<float>& in, double ratio, size_t outFrames) {
  if (in.empty() || ratio <= 0) return {};
  const size_t n = outFrames != 0 ? outFrames : static_cast<size_t>(std::llround(static_cast<double>(in.size()) * ratio));
  std::vector<float> out(n);
  if (std::fabs(ratio - 1.0) < 1e-12 && n == in.size()) return in;
  const double cutoff = std::min(1.0, ratio);
  const int half = 24;
  const double span = half / cutoff;
  const double pi = 3.14159265358979323846;
  for (size_t j = 0; j < n; j++) {
    const double x = static_cast<double>(j) / ratio;
    const long long lo = static_cast<long long>(std::ceil(x - span)), hi = static_cast<long long>(std::floor(x + span));
    double acc = 0, wsum = 0;
    for (long long i = lo; i <= hi; i++) {
      const double d = x - static_cast<double>(i);
      const double t = d * cutoff;
      const double sinc = std::fabs(t) < 1e-12 ? 1.0 : std::sin(pi * t) / (pi * t);
      const double u = d / span;
      const double w = 0.42 + 0.5 * std::cos(pi * u) + 0.08 * std::cos(2 * pi * u);
      const double k = sinc * w * cutoff;
      wsum += k;
      if (i >= 0 && i < static_cast<long long>(in.size())) acc += k * in[static_cast<size_t>(i)];
    }
    out[j] = static_cast<float>(wsum != 0 ? acc / wsum : 0);
  }
  return out;
}

std::vector<int16_t> AdpcmDecode(const uint8_t* frames, size_t frameBytes, size_t samples, const AdpcmChannel& ch, bool fromLoop) {
  std::vector<int16_t> out;
  out.reserve(samples);
  int h1 = fromLoop ? ch.loopHist1 : ch.hist1, h2 = fromLoop ? ch.loopHist2 : ch.hist2;
  for (size_t f = 0; out.size() < samples && (f + 1) * kAdpcmFrameBytes <= frameBytes; f++) {
    const uint8_t* p = frames + f * kAdpcmFrameBytes;
    const int predictor = (p[0] >> 4) & 7, scale = p[0] & 15;
    const int c0 = ch.coef[predictor * 2], c1 = ch.coef[predictor * 2 + 1];
    for (size_t i = 0; i < kAdpcmFrameSamples && out.size() < samples; i++) {
      int nib = (i & 1) ? (p[1 + i / 2] & 15) : (p[1 + i / 2] >> 4);
      if (nib >= 8) nib -= 16;
      const long long s = ((static_cast<long long>(nib) * (1LL << scale)) * 2048 + 1024 + static_cast<long long>(c0) * h1 + static_cast<long long>(c1) * h2) >> 11;
      const int16_t v = Clamp16(s);
      out.push_back(v);
      h2 = h1;
      h1 = v;
    }
  }
  return out;
}

namespace {

struct Stats {
  double r00 = 0, r01 = 0, r11 = 0, p0 = 0, p1 = 0, yy = 0;
};

bool Solve(const Stats& s, double& a, double& b) {
  const double reg = 1e-3 * (s.r00 + s.r11) + 1e-9;
  const double m00 = s.r00 + reg, m11 = s.r11 + reg, m01 = s.r01;
  const double det = m00 * m11 - m01 * m01;
  if (std::fabs(det) < 1e-12) return false;
  a = (s.p0 * m11 - s.p1 * m01) / det;
  b = (s.p1 * m00 - s.p0 * m01) / det;
  return true;
}

double ErrorOf(const Stats& s, double a, double b) { return s.yy - 2 * (a * s.p0 + b * s.p1) + a * a * s.r00 + 2 * a * b * s.r01 + b * b * s.r11; }

}

void AdpcmEncode(const std::vector<int16_t>& pcm, long long loopSample, AdpcmChannel& ch, std::vector<uint8_t>& frames) {
  const size_t n = pcm.size(), nf = AdpcmFrames(n);
  const auto X = [&](long long i) -> double { return i >= 0 && i < static_cast<long long>(n) ? pcm[static_cast<size_t>(i)] : 0.0; };
  std::vector<Stats> st(nf);
  std::vector<double> la(nf, 0), lb(nf, 0);
  std::vector<size_t> active;
  for (size_t f = 0; f < nf; f++) {
    Stats& s = st[f];
    for (size_t i = 0; i < kAdpcmFrameSamples; i++) {
      const long long k = static_cast<long long>(f * kAdpcmFrameSamples + i);
      const double x = X(k), x1 = X(k - 1), x2 = X(k - 2);
      s.r00 += x1 * x1, s.r01 += x1 * x2, s.r11 += x2 * x2, s.p0 += x * x1, s.p1 += x * x2, s.yy += x * x;
    }
    if (s.yy > 0) {
      active.push_back(f);
      Solve(s, la[f], lb[f]);
    }
  }
  double ca[8] = {}, cb[8] = {};
  if (!active.empty()) {
    std::vector<size_t> order = active;
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return la[x] < la[y]; });
    for (int k = 0; k < 8; k++) {
      const size_t f = order[std::min(order.size() - 1, (order.size() * static_cast<size_t>(2 * k + 1)) / 16)];
      ca[k] = la[f], cb[k] = lb[f];
    }
    std::vector<int> member(nf, 0);
    for (int iter = 0; iter < 10; iter++) {
      Stats sum[8];
      int count[8] = {};
      for (size_t f : active) {
        int best = 0;
        double bestErr = 1e300;
        for (int k = 0; k < 8; k++) {
          const double e = ErrorOf(st[f], ca[k], cb[k]);
          if (e < bestErr) bestErr = e, best = k;
        }
        member[f] = best;
        Stats& t = sum[best];
        t.r00 += st[f].r00, t.r01 += st[f].r01, t.r11 += st[f].r11, t.p0 += st[f].p0, t.p1 += st[f].p1, t.yy += st[f].yy;
        count[best]++;
      }
      for (int k = 0; k < 8; k++) {
        double a, b;
        if (count[k] > 0 && Solve(sum[k], a, b)) {
          ca[k] = a, cb[k] = b;
        } else {
          size_t worst = active[0];
          double worstErr = -1;
          for (size_t f : active) {
            const double e = ErrorOf(st[f], ca[member[f]], cb[member[f]]);
            if (e > worstErr) worstErr = e, worst = f;
          }
          ca[k] = la[worst], cb[k] = lb[worst];
        }
      }
    }
  }
  for (int k = 0; k < 8; k++) {
    ch.coef[2 * k] = Clamp16(std::llround(std::clamp(ca[k], -15.99, 15.99) * 2048));
    ch.coef[2 * k + 1] = Clamp16(std::llround(std::clamp(cb[k], -15.99, 15.99) * 2048));
  }
  if (active.empty()) {
    for (int k = 0; k < 8; k++) ch.coef[2 * k] = static_cast<int16_t>(0x0400 + 0x0100 * k), ch.coef[2 * k + 1] = 0;
  }
  frames.assign(nf * kAdpcmFrameBytes, 0);
  int h1 = 0, h2 = 0;
  ch.gain = 0, ch.hist1 = 0, ch.hist2 = 0;
  const long long loopFrame = loopSample >= 0 ? loopSample / static_cast<long long>(kAdpcmFrameSamples) : -1;
  for (size_t f = 0; f < nf; f++) {
    if (static_cast<long long>(f) == loopFrame) ch.loopHist1 = static_cast<int16_t>(h1), ch.loopHist2 = static_cast<int16_t>(h2);
    double x[kAdpcmFrameSamples];
    for (size_t i = 0; i < kAdpcmFrameSamples; i++) x[i] = X(static_cast<long long>(f * kAdpcmFrameSamples + i));
    double bestErr = 1e300;
    int bestP = 0, bestS = 0, bestH1 = 0, bestH2 = 0;
    int bestNib[kAdpcmFrameSamples] = {};
    for (int p = 0; p < 8; p++) {
      const int c0 = ch.coef[2 * p], c1 = ch.coef[2 * p + 1];
      double emax = 0;
      {
        double y1 = h1, y2 = h2;
        for (size_t i = 0; i < kAdpcmFrameSamples; i++) {
          const double pred = (1024.0 + c0 * y1 + c1 * y2) / 2048.0;
          emax = std::max(emax, std::fabs(x[i] - pred));
          y2 = y1, y1 = x[i];
        }
      }
      int s0 = 0;
      while (s0 < 12 && 7.0 * (1 << s0) < emax) s0++;
      for (int scale = s0; scale <= std::min(12, s0 + 1); scale++) {
        int y1 = h1, y2 = h2, nib[kAdpcmFrameSamples];
        double err = 0;
        for (size_t i = 0; i < kAdpcmFrameSamples; i++) {
          const long long predBits = 1024 + static_cast<long long>(c0) * y1 + static_cast<long long>(c1) * y2;
          const double target = (x[i] * 2048.0 - static_cast<double>(predBits)) / (2048.0 * static_cast<double>(1 << scale));
          int q = static_cast<int>(std::lround(target));
          q = std::clamp(q, -8, 7);
          const int16_t s = Clamp16((((static_cast<long long>(q) * (1LL << scale)) * 2048) + predBits) >> 11);
          err += (x[i] - s) * (x[i] - s);
          nib[i] = q;
          y2 = y1, y1 = s;
        }
        if (err < bestErr) {
          bestErr = err, bestP = p, bestS = scale, bestH1 = y1, bestH2 = y2;
          std::memcpy(bestNib, nib, sizeof nib);
        }
      }
    }
    uint8_t* out = &frames[f * kAdpcmFrameBytes];
    out[0] = static_cast<uint8_t>(bestP << 4 | bestS);
    for (size_t i = 0; i < kAdpcmFrameSamples; i += 2) out[1 + i / 2] = static_cast<uint8_t>((bestNib[i] & 15) << 4 | (bestNib[i + 1] & 15));
    h1 = bestH1, h2 = bestH2;
  }
  ch.ps = nf > 0 ? frames[0] : 0;
  if (loopFrame < 0 || loopFrame >= static_cast<long long>(nf)) {
    ch.loopPs = ch.ps, ch.loopHist1 = 0, ch.loopHist2 = 0;
  } else {
    ch.loopPs = frames[static_cast<size_t>(loopFrame) * kAdpcmFrameBytes];
  }
}

}
