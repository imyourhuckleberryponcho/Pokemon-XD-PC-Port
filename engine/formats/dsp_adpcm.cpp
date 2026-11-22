#include "dsp_adpcm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::dsp {

namespace {

uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint32_t Le16(const uint8_t* p) { return p[0] | p[1] << 8; }

int16_t Clamp16(int64_t v) { return static_cast<int16_t>(std::max<int64_t>(-32768, std::min<int64_t>(32767, v))); }

int16_t Step(int nibble, int shift, int c1, int c2, int h1, int h2) {
  const int64_t v = ((static_cast<int64_t>(nibble) * (1 << shift)) * 2048 + 1024 + static_cast<int64_t>(c1) * h1 + static_cast<int64_t>(c2) * h2) >> 11;
  return Clamp16(v);
}

}

bool ReadWav(const std::vector<uint8_t>& f, Pcm& out, std::string& error) {
  if (f.size() < 12 || std::memcmp(f.data(), "RIFF", 4) != 0 || std::memcmp(f.data() + 8, "WAVE", 4) != 0) {
    error = "not a WAV file (no RIFF/WAVE header)";
    return false;
  }
  uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
  const uint8_t* data = nullptr;
  size_t dataSize = 0;
  bool haveFmt = false;
  for (size_t at = 12; at + 8 <= f.size();) {
    const uint32_t size = Le32(&f[at + 4]);
    const size_t body = at + 8;
    const size_t avail = std::min<size_t>(size, f.size() - body);
    if (std::memcmp(&f[at], "fmt ", 4) == 0 && avail >= 16) {
      tag = Le16(&f[body]), channels = Le16(&f[body + 2]), rate = Le32(&f[body + 4]), bits = Le16(&f[body + 14]);
      if (tag == 0xFFFE && avail >= 26) tag = Le16(&f[body + 24]);
      haveFmt = true;
    } else if (std::memcmp(&f[at], "data", 4) == 0) {
      data = &f[body];
      dataSize = avail;
    }
    at = body + size + (size & 1);
  }
  if (!haveFmt || data == nullptr) {
    error = "the WAV file has no fmt or data part";
    return false;
  }
  if ((tag != 1 && tag != 3) || channels == 0 || channels > 8 || rate < 4000 || rate > 192000) {
    error = "the WAV file must be plain PCM (or float) with 1 to 8 channels and a rate from 4000 to 192000 Hz";
    return false;
  }
  if (!((tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && bits == 32))) {
    error = "the WAV file must be 8, 16, 24 or 32-bit PCM, or 32-bit float";
    return false;
  }
  const size_t bytes = bits / 8, frames = dataSize / (bytes * channels);
  if (frames == 0 || frames > 20000000) {
    error = frames == 0 ? "the WAV file holds no sound" : "the WAV file is far too long";
    return false;
  }
  out.rate = rate;
  out.channels.assign(channels, std::vector<int16_t>(frames));
  for (size_t i = 0; i < frames; i++) {
    for (uint32_t c = 0; c < channels; c++) {
      const uint8_t* p = data + (i * channels + c) * bytes;
      int16_t v = 0;
      if (tag == 3) {
        float x;
        std::memcpy(&x, p, 4);
        v = Clamp16(static_cast<int64_t>(std::lround(std::max(-1.0f, std::min(1.0f, x)) * 32767.0f)));
      } else if (bits == 8) {
        v = static_cast<int16_t>((static_cast<int>(p[0]) - 128) * 256);
      } else if (bits == 16) {
        v = static_cast<int16_t>(Le16(p));
      } else if (bits == 24) {
        v = static_cast<int16_t>(Le16(p + 1));
      } else {
        v = static_cast<int16_t>(Le16(p + 2));
      }
      out.channels[c][i] = v;
    }
  }
  return true;
}

Pcm Resample(const Pcm& in, uint32_t rate) {
  if (in.rate == rate || in.channels.empty() || in.channels[0].empty() || rate == 0) return in;
  Pcm out;
  out.rate = rate;
  const size_t n = in.channels[0].size();
  const size_t m = std::max<size_t>(1, static_cast<size_t>(static_cast<uint64_t>(n) * rate / in.rate));
  for (const auto& ch : in.channels) {
    std::vector<int16_t> r(m);
    for (size_t i = 0; i < m; i++) {
      const double at = static_cast<double>(i) * in.rate / rate;
      const size_t a = std::min<size_t>(n - 1, static_cast<size_t>(at)), b = std::min<size_t>(n - 1, a + 1);
      const double t = at - static_cast<double>(a);
      r[i] = Clamp16(std::lround(ch[a] * (1.0 - t) + ch[b] * t));
    }
    out.channels.push_back(std::move(r));
  }
  return out;
}

Encoded Encode(const std::vector<int16_t>& samples) {
  Encoded out;
  const size_t n = samples.size(), frames = (n + 13) / 14;
  out.samples = static_cast<uint32_t>(n);
  if (frames == 0) return out;
  std::vector<int> x(frames * 14, 0);
  for (size_t i = 0; i < n; i++) x[i] = samples[i];
  const auto at = [&](long i) { return i < 0 ? 0 : x[static_cast<size_t>(i)]; };

  struct Point {
    double a1, a2, w;
  };
  std::vector<Point> points(frames);
  for (size_t f = 0; f < frames; f++) {
    double a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0, energy = 0;
    for (long i = static_cast<long>(f) * 14; i < static_cast<long>(f) * 14 + 14; i++) {
      const double s0 = at(i), s1 = at(i - 1), s2 = at(i - 2);
      a11 += s1 * s1, a12 += s1 * s2, a22 += s2 * s2, b1 += s0 * s1, b2 += s0 * s2, energy += s0 * s0;
    }
    const double reg = 1e-3 * (a11 + a22) / 2 + 1.0;
    a11 += reg, a22 += reg;
    const double det = a11 * a22 - a12 * a12;
    double c1 = 0, c2 = 0;
    if (std::fabs(det) > 1e-9) c1 = (b1 * a22 - b2 * a12) / det, c2 = (a11 * b2 - a12 * b1) / det;
    c1 = std::max(-1.99, std::min(1.99, c1));
    c2 = std::max(-0.99, std::min(0.99, c2));
    points[f] = {c1, c2, energy + 1.0};
  }
  double cx[8], cy[8];
  {
    double sw = 0, sx = 0, sy = 0;
    for (const Point& p : points) sw += p.w, sx += p.a1 * p.w, sy += p.a2 * p.w;
    cx[0] = sx / sw, cy[0] = sy / sw;
    for (int k = 1; k < 8; k++) {
      double best = -1;
      size_t bi = 0;
      for (size_t i = 0; i < frames; i++) {
        double d = 1e30;
        for (int j = 0; j < k; j++) d = std::min(d, (points[i].a1 - cx[j]) * (points[i].a1 - cx[j]) + (points[i].a2 - cy[j]) * (points[i].a2 - cy[j]));
        if (d > best) best = d, bi = i;
      }
      cx[k] = points[bi].a1, cy[k] = points[bi].a2;
    }
    for (int round = 0; round < 12; round++) {
      double wx[8] = {}, wy[8] = {}, ww[8] = {};
      for (const Point& p : points) {
        int bj = 0;
        double bd = 1e30;
        for (int j = 0; j < 8; j++) {
          const double d = (p.a1 - cx[j]) * (p.a1 - cx[j]) + (p.a2 - cy[j]) * (p.a2 - cy[j]);
          if (d < bd) bd = d, bj = j;
        }
        wx[bj] += p.a1 * p.w, wy[bj] += p.a2 * p.w, ww[bj] += p.w;
      }
      for (int j = 0; j < 8; j++) {
        if (ww[j] > 0) cx[j] = wx[j] / ww[j], cy[j] = wy[j] / ww[j];
      }
    }
  }
  for (int k = 0; k < 8; k++) {
    out.coefficients[2 * k] = Clamp16(std::lround(cx[k] * 2048.0));
    out.coefficients[2 * k + 1] = Clamp16(std::lround(cy[k] * 2048.0));
  }
  out.frames.assign(frames * 8, 0);
  int h1 = 0, h2 = 0;
  for (size_t f = 0; f < frames; f++) {
    double bestErr = 1e300;
    int bestP = 0, bestS = 0, bestN[14] = {}, bestH1 = 0, bestH2 = 0;
    for (int p = 0; p < 8; p++) {
      const int c1 = out.coefficients[2 * p], c2 = out.coefficients[2 * p + 1];
      for (int s = 0; s <= 14; s++) {
        const int sc = 1 << s;
        int g1 = h1, g2 = h2, nib[14];
        double err = 0;
        for (int i = 0; i < 14; i++) {
          const int target = x[f * 14 + static_cast<size_t>(i)];
          const int64_t pred = (1024 + static_cast<int64_t>(c1) * g1 + static_cast<int64_t>(c2) * g2) >> 11;
          int q = static_cast<int>(std::lround(static_cast<double>(target - pred) / sc));
          q = std::max(-8, std::min(7, q));
          const int v = Step(q, s, c1, c2, g1, g2);
          nib[i] = q;
          err += static_cast<double>(v - target) * (v - target);
          g2 = g1, g1 = v;
        }
        if (err < bestErr) {
          bestErr = err, bestP = p, bestS = s, bestH1 = g1, bestH2 = g2;
          std::memcpy(bestN, nib, sizeof(nib));
        }
        if (err == 0) break;
      }
    }
    uint8_t* d = &out.frames[f * 8];
    d[0] = static_cast<uint8_t>(bestP << 4 | bestS);
    for (int i = 0; i < 14; i += 2) d[1 + i / 2] = static_cast<uint8_t>((bestN[i] & 15) << 4 | (bestN[i + 1] & 15));
    h1 = bestH1, h2 = bestH2;
  }
  return out;
}

std::vector<int16_t> Decode(const Encoded& in) {
  std::vector<int16_t> out;
  int h1 = 0, h2 = 0;
  for (size_t f = 0; f * 8 + 8 <= in.frames.size(); f++) {
    const uint8_t* d = &in.frames[f * 8];
    const int shift = d[0] & 15, p = d[0] >> 4;
    const int c1 = in.coefficients[(p & 7) * 2], c2 = in.coefficients[(p & 7) * 2 + 1];
    for (int i = 0; i < 14 && out.size() < in.samples; i++) {
      int nib = (i % 2 == 0) ? d[1 + i / 2] >> 4 : d[1 + i / 2] & 15;
      if (nib >= 8) nib -= 16;
      const int v = Step(nib, shift, c1, c2, h1, h2);
      out.push_back(static_cast<int16_t>(v));
      h2 = h1, h1 = v;
    }
  }
  return out;
}

}
