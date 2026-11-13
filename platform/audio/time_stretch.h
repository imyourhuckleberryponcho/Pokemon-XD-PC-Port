#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace port_audio {

class TimeStretch {
public:
  static constexpr int kFrame = 1024;
  static constexpr int kHop = kFrame / 2;
  static constexpr int kSearch = 256;

  TimeStretch() {
    for (int i = 0; i < kFrame; i++) {
      window_[i] = 0.5f - 0.5f * std::cos(6.283185307179586f * static_cast<float>(i) / kFrame);
    }
  }

  void Push(const int16_t* samples, size_t frames, double factor, std::vector<int16_t>& out) {
    in_.insert(in_.end(), samples, samples + frames * 2);
    const auto have = [&] { return static_cast<int64_t>(in_.size() / 2); };
    while (true) {
      const int64_t centre = static_cast<int64_t>(inPos_);
      if (centre + kSearch + kFrame > have()) break;
      int64_t best = centre;
      if (started_) best = BestStart(std::max<int64_t>(centre - kSearch, 0), centre + kSearch);
      for (int i = 0; i < kHop; i++) {
        for (int c = 0; c < 2; c++) {
          const float v = tail_[i * 2 + c] + in_[static_cast<size_t>((best + i) * 2 + c)] * window_[i];
          out.push_back(Clamp(v));
        }
      }
      for (int i = 0; i < kHop; i++) {
        for (int c = 0; c < 2; c++) {
          tail_[i * 2 + c] = in_[static_cast<size_t>((best + kHop + i) * 2 + c)] * window_[kHop + i];
        }
        follow_[i] = Mono(best + kHop + i);
      }
      started_ = true;
      inPos_ += kHop * factor;
      const int64_t drop = std::max<int64_t>(0, std::min<int64_t>(static_cast<int64_t>(inPos_) - kSearch, have()));
      if (drop > 0) {
        in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(drop * 2));
        inPos_ -= static_cast<double>(drop);
      }
    }
  }

  void Flush(std::vector<int16_t>& out) {
    if (started_) {
      for (int i = 0; i < kHop * 2; i++) out.push_back(Clamp(tail_[i]));
    }
    Reset();
  }

  void Reset() {
    in_.clear();
    inPos_ = 0.0;
    started_ = false;
    std::fill(std::begin(tail_), std::end(tail_), 0.f);
  }

  bool Active() const { return started_ || !in_.empty(); }

private:
  float Mono(int64_t frame) const {
    const size_t i = static_cast<size_t>(frame * 2);
    return 0.5f * (static_cast<float>(in_[i]) + static_cast<float>(in_[i + 1]));
  }

  static int16_t Clamp(float v) {
    const float r = std::round(v);
    return static_cast<int16_t>(r > 32767.f ? 32767.f : r < -32768.f ? -32768.f : r);
  }

  int64_t BestStart(int64_t lo, int64_t hi) const {
    const auto score = [&](int64_t k, int step) {
      double c = 0.0, e = 1e-9;
      for (int i = 0; i < kHop; i += step) {
        const float v = Mono(k + i);
        c += static_cast<double>(v) * follow_[i];
        e += static_cast<double>(v) * v;
      }
      return c / std::sqrt(e);
    };
    int64_t best = lo;
    double bestScore = -1e300;
    for (int64_t k = lo; k <= hi; k += 2) {
      const double s = score(k, 2);
      if (s > bestScore) bestScore = s, best = k;
    }
    const int64_t centre = best;
    bestScore = -1e300;
    for (int64_t k = std::max(lo, centre - 1); k <= std::min(hi, centre + 1); k++) {
      const double s = score(k, 1);
      if (s > bestScore) bestScore = s, best = k;
    }
    return best;
  }

  std::vector<int16_t> in_;
  double inPos_ = 0.0;
  bool started_ = false;
  float window_[kFrame];
  float tail_[kHop * 2] = {};
  float follow_[kHop] = {};
};

}
