#include "../audio/time_stretch.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, double got) {
  std::printf("%s %s (%g)\n", ok ? "ok  " : "FAIL", what, got);
  if (!ok) gFailures++;
}

constexpr double kRate = 32028.0;

double Frequency(const std::vector<int16_t>& s) {
  int crossings = 0;
  for (size_t i = 2; i + 1 < s.size(); i += 2) {
    if (s[i - 2] < 0 && s[i] >= 0) crossings++;
  }
  return crossings / (s.size() / 2 / kRate);
}

}

int main() {
  const double tone = 440.0, amp = 12000.0;
  const size_t frames = static_cast<size_t>(kRate * 4);
  std::vector<int16_t> in(frames * 2);
  for (size_t i = 0; i < frames; i++) {
    const double v = amp * std::sin(2 * 3.141592653589793 * tone * i / kRate);
    in[i * 2] = static_cast<int16_t>(v);
    in[i * 2 + 1] = static_cast<int16_t>(v * 0.5);
  }
  for (int factor : {2, 4, 8, 16}) {
    port_audio::TimeStretch ts;
    std::vector<int16_t> out;
    for (size_t at = 0; at < frames; at += 160) {
      std::vector<int16_t> part;
      ts.Push(in.data() + at * 2, std::min<size_t>(160, frames - at), factor, part);
      out.insert(out.end(), part.begin(), part.end());
    }
    const size_t before = out.size();
    ts.Flush(out);
    std::printf("-- %dx: %zu frames in, %zu out\n", factor, frames, out.size() / 2);
    const double want = static_cast<double>(frames) / factor;
    const double got = static_cast<double>(out.size() / 2);
    Check(std::fabs(got - want) < port_audio::TimeStretch::kFrame * 2, "length is the input's / factor", got);
    std::vector<int16_t> mid(out.begin() + 4096, out.begin() + static_cast<std::ptrdiff_t>(before) - 4096);
    const double f = Frequency(mid);
    Check(std::fabs(f - tone) < tone * 0.02, "same pitch as the input", f);
    int peak = 0, jump = 0;
    for (size_t i = 0; i < mid.size(); i += 2) {
      peak = std::max(peak, std::abs(static_cast<int>(mid[i])));
      if (i >= 2) jump = std::max(jump, std::abs(mid[i] - mid[i - 2]));
    }
    Check(peak <= amp * 1.02, "never louder than the input", peak);
    Check(peak >= amp * 0.85, "not quieter either (pieces line up)", peak);
    Check(jump < 1200, "no clicks between pieces", jump);
    int lastAbs = 0;
    for (size_t i = out.size() - 32; i < out.size(); i += 2) lastAbs = std::max(lastAbs, std::abs(static_cast<int>(out[i])));
    Check(lastAbs < amp * 0.1, "the end fades out (no cut)", lastAbs);
    Check(!ts.Active(), "forgotten after the flush", 0);
  }
  std::printf(gFailures ? "FAILED: %d\n" : "all passed\n", gFailures);
  return gFailures ? 1 : 0;
}
