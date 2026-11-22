#define _USE_MATH_DEFINES
#include <audio_sfx.h>
#include <audio_stream.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ea = engine::audio;

static int failures = 0;
#define CHECK(cond, what)                                            \
  do {                                                               \
    const bool ok = (cond);                                          \
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);              \
    if (!ok) failures++;                                             \
  } while (0)

namespace {

ea::Pcm Sine(int rate, double seconds, double freq, double amp, double freq2 = 0) {
  ea::Pcm p;
  p.rate = rate;
  const size_t n = static_cast<size_t>(rate * seconds);
  p.ch.assign(2, std::vector<float>(n));
  for (size_t i = 0; i < n; i++) {
    const double t = static_cast<double>(i) / rate;
    p.ch[0][i] = static_cast<float>(amp * std::sin(2 * M_PI * freq * t) + (freq2 > 0 ? 0.2 * amp * std::sin(2 * M_PI * freq2 * t) : 0));
    p.ch[1][i] = static_cast<float>(amp * std::sin(2 * M_PI * freq * t + 1.0));
  }
  return p;
}

double SnrDb(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
  double sig = 0, err = 0;
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = from; i < n; i++) {
    sig += static_cast<double>(a[i]) * a[i];
    err += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
  }
  return 10 * std::log10((sig + 1e-12) / (err + 1e-12));
}

double Frequency(const std::vector<float>& x, size_t from, size_t to, int rate) {
  size_t crossings = 0;
  double first = -1, last = -1;
  for (size_t i = from + 1; i < to && i < x.size(); i++) {
    if (x[i - 1] < 0 && x[i] >= 0) {
      const double at = static_cast<double>(i - 1) + (0 - x[i - 1]) / (x[i] - x[i - 1]);
      if (first < 0) first = at;
      last = at;
      crossings++;
    }
  }
  return crossings < 2 ? 0 : (static_cast<double>(crossings - 1) / (last - first)) * rate;
}

void TestWav() {
  ea::Pcm p = Sine(22050, 0.5, 300, 0.4);
  ea::LoopPoints loop;
  loop.found = true, loop.start = 100, loop.end = 5000;
  const std::vector<uint8_t> file = ea::WriteWav(p, &loop);
  ea::Pcm back;
  ea::LoopPoints loop2;
  std::string error;
  CHECK(ea::ReadWav(file, back, loop2, error), "a WAV written is read back");
  CHECK(back.rate == 22050 && back.Frames() == p.Frames() && back.Channels() == 2, "WAV: rate, length, channels");
  CHECK(SnrDb(p.ch[0], back.ch[0]) > 70, "WAV: 16-bit round trip is accurate");
  CHECK(loop2.found && loop2.start == 100 && loop2.end == 5000, "WAV: the smpl loop is written and read");
  std::vector<uint8_t> bad = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'X', 'X', 'X', 'X'};
  CHECK(!ea::ReadWav(bad, back, loop2, error) && !error.empty(), "WAV: a wrong file is refused with a reason");
  ea::Pcm any;
  CHECK(!ea::ReadSoundFile({'h', 'e', 'l', 'l', 'o'}, any, loop2, error) && error.find("neither") != std::string::npos, "a file that is neither WAV nor OGG is refused");
  std::vector<uint8_t> w24 = file;
  (void)w24;
}

void TestResample() {
  const ea::Pcm p = Sine(32000, 1.0, 500, 0.5);
  const std::vector<float> up = ea::Resample(p.ch[0], 1.5);
  CHECK(up.size() == 48000, "resample: 1.5x gives 1.5x the samples");
  CHECK(std::fabs(Frequency(up, 2000, 46000, 48000) - 500) < 2, "resample up: the pitch is kept");
  const std::vector<float> down = ea::Resample(p.ch[0], 0.7);
  CHECK(std::fabs(Frequency(down, 1000, 22000, 22400) - 500) < 3, "resample down: the pitch is kept");
}

void TestAdpcm() {
  const ea::Pcm p = Sine(32000, 0.6, 440, 0.5, 1330);
  std::vector<int16_t> pcm(p.Frames());
  for (size_t i = 0; i < pcm.size(); i++) pcm[i] = static_cast<int16_t>(std::lround(p.ch[0][i] * 32767));
  ea::AdpcmChannel ch;
  std::vector<uint8_t> frames;
  ea::AdpcmEncode(pcm, -1, ch, frames);
  CHECK(frames.size() == ea::AdpcmFrames(pcm.size()) * 8, "adpcm: 8 bytes per 14 samples");
  const std::vector<int16_t> back = ea::AdpcmDecode(frames.data(), frames.size(), pcm.size(), ch);
  std::vector<float> a(pcm.size()), b(back.size());
  for (size_t i = 0; i < pcm.size(); i++) a[i] = pcm[i], b[i] = back[i];
  const double snr = SnrDb(a, b);
  std::printf("     adpcm SNR %.1f dB\n", snr);
  CHECK(back.size() == pcm.size() && snr > 30, "adpcm: decoding what was encoded gives the sound back (SNR above 30 dB)");
  std::vector<int16_t> noise(20000);
  uint32_t seed = 12345;
  for (auto& s : noise) seed = seed * 1664525u + 1013904223u, s = static_cast<int16_t>((static_cast<int>(seed >> 16) & 0x3FFF) - 0x2000);
  ea::AdpcmChannel nc;
  std::vector<uint8_t> nf;
  ea::AdpcmEncode(noise, -1, nc, nf);
  const std::vector<int16_t> nb = ea::AdpcmDecode(nf.data(), nf.size(), noise.size(), nc);
  std::vector<float> na(noise.begin(), noise.end()), nb2(nb.begin(), nb.end());
  CHECK(SnrDb(na, nb2) > 10, "adpcm: noise survives with a sensible error (no blow-up)");
  std::vector<int16_t> zero(1000, 0);
  ea::AdpcmChannel zc;
  std::vector<uint8_t> zf;
  ea::AdpcmEncode(zero, -1, zc, zf);
  const std::vector<int16_t> zb = ea::AdpcmDecode(zf.data(), zf.size(), zero.size(), zc);
  bool allZero = true;
  for (int16_t s : zb) allZero &= s == 0;
  CHECK(allZero, "adpcm: silence stays silence");
}

void TestPlainStream() {
  const ea::Pcm p = Sine(32000, 1.5, 440, 0.5, 880);
  ea::BuildOptions o;
  ea::BuildResult r;
  std::string error;
  CHECK(ea::BuildStream(p, o, r, error), "stream: a plain sound builds");
  CHECK(r.header.size() == 192 && std::memcmp(r.header.data(), "I_SF", 4) == 0, "stream: 192-byte I_SF header");
  CHECK(r.body.size() % 0x10000 == 0 && r.body.size() == r.info.blocks * 0x10000, "stream: body is whole blocks of two channels");
  CHECK(!r.info.loop && r.info.samples == p.Frames() && r.info.rate == 32000, "stream: no loop, the sample count and rate are the sound's");
  const uint32_t last = 2 + (p.Frames() - 1) + (p.Frames() - 1) / 14 * 2;
  const auto be32 = [&](size_t at) { return static_cast<uint32_t>(r.header[at]) << 24 | r.header[at + 1] << 16 | r.header[at + 2] << 8 | r.header[at + 3]; };
  CHECK(be32(0x10) == last + 1 && be32(0x20) == 2 && be32(0x24) == last && be32(0x28) == last, "stream: nibble addresses as in the games' streams");
  ea::Pcm back;
  ea::LoopPoints loop;
  ea::StreamInfo info;
  CHECK(ea::DecodeStream(r.header, r.body, back, loop, info, error), "stream: it decodes");
  CHECK(back.Frames() == p.Frames() && SnrDb(p.ch[0], back.ch[0]) > 28 && SnrDb(p.ch[1], back.ch[1]) > 28, "stream: both channels come back (SNR above 28 dB)");
  ea::Pcm mono = Sine(44100, 0.5, 300, 0.4);
  mono.ch.resize(1);
  ea::BuildResult rm;
  CHECK(ea::BuildStream(mono, o, rm, error) && rm.info.rate == 44100 && rm.info.channels == 2, "stream: mono becomes two channels, a 44.1 kHz rate is kept");
  ea::BuildOptions cry;
  cry.maxBlocks = 1;
  const ea::Pcm longer = Sine(32000, 5.0, 300, 0.4);
  ea::BuildResult rc;
  CHECK(ea::BuildStream(longer, cry, rc, error) && rc.info.blocks == 1 && rc.info.samples == ea::kStreamBlockSamples && !rc.notes.empty(), "stream: a sound longer than the slot is cut");
  ea::StreamInfo bi;
  ea::AdpcmChannel bc[2];
  std::vector<uint8_t> junk(192, 0);
  CHECK(!ea::ParseStreamHeader(junk, bi, bc, error), "stream: a header without I_SF is refused");
}

void TestLoopStream() {
  const ea::Pcm p = Sine(32000, 2.9, 440, 0.5);
  ea::BuildOptions o;
  o.loop = true;
  o.loopStart = static_cast<uint64_t>(0.7 * 32000);
  o.loopEnd = static_cast<uint64_t>(2.9 * 32000);
  ea::BuildResult r;
  std::string error;
  CHECK(ea::BuildStream(p, o, r, error), "loop: a looping sound builds");
  CHECK(r.info.loop && r.info.loopStartSample % ea::kStreamBlockSamples == 0 && r.info.loopStartSample > 0, "loop: the loop starts on a block boundary");
  CHECK(r.info.samples == r.info.blocks * ea::kStreamBlockSamples && r.info.loopEndSample == r.info.samples, "loop: the stream is whole blocks and loops to its end");
  const auto be32 = [&](size_t at) { return static_cast<uint32_t>(r.header[at]) << 24 | r.header[at + 1] << 16 | r.header[at + 2] << 8 | r.header[at + 3]; };
  CHECK(be32(0x10) == r.info.blocks * 0x10000 && be32(0x1C) == 1 && be32(0x24) == r.info.blocks * 0x10000 - 1, "loop: header words as in the games' looping streams");
  ea::Pcm back;
  ea::LoopPoints loop;
  ea::StreamInfo info;
  CHECK(ea::DecodeStream(r.header, r.body, back, loop, info, error), "loop: it decodes");
  CHECK(loop.found && loop.start == r.info.loopStartSample, "loop: the decoded loop points are the block start");
  const double f = Frequency(back.ch[0], static_cast<size_t>(loop.start) + 100, back.Frames() - 100, back.rate);
  std::printf("     loop: stream rate %d Hz, played pitch %.2f Hz (want 440), %u blocks\n", back.rate, f, info.blocks);
  CHECK(std::fabs(f - 440) < 2.5, "loop: played at the stream's rate, the pitch is still 440 Hz");
  const float seam = std::fabs(back.ch[0][back.Frames() - 1] - back.ch[0][static_cast<size_t>(loop.start)]);
  std::printf("     loop: seam jump %.4f\n", seam);
  CHECK(seam < 0.15f, "loop: no click at the seam (the last sample leads into the first of the loop)");
  ea::BuildOptions t = o;
  t.trimIntro = true;
  ea::BuildResult rt;
  CHECK(ea::BuildStream(p, t, rt, error) && rt.info.loopStartSample % ea::kStreamBlockSamples == 0 && rt.info.blocks <= r.info.blocks, "loop: --trim-intro lands the loop on a block too");
  ea::BuildOptions s;
  s.loop = true;
  s.loopStart = 0;
  s.loopEnd = 3200;
  ea::BuildResult rs;
  CHECK(ea::BuildStream(p, s, rs, error) && rs.info.blocks >= 1 && rs.info.loopStartSample == 0, "loop: a very short loop builds (repeated inside the stream)");
  ea::BuildOptions bad = o;
  bad.loopStart = 5000, bad.loopEnd = 4000;
  CHECK(!ea::BuildStream(p, bad, rs, error) && !error.empty(), "loop: a start after the end is refused");
}

}

void TestSfx() {
  namespace es = engine::audio::sfx;
  const size_t n = 3;
  std::vector<uint8_t> sdir(n * 32 + 4 + n * 40, 0);
  std::vector<uint8_t> samp;
  auto put32 = [&](size_t at, uint32_t x) { sdir[at] = x >> 24, sdir[at + 1] = x >> 16, sdir[at + 2] = x >> 8, sdir[at + 3] = x; };
  uint32_t offset = 0;
  for (size_t i = 0; i < n; i++) {
    ea::Pcm tone = Sine(22050, 0.1 + 0.1 * i, 440.0 * (i + 1), 0.5);
    std::vector<int16_t> pcm(tone.Frames());
    for (size_t k = 0; k < pcm.size(); k++) pcm[k] = static_cast<int16_t>(tone.ch[0][k] * 32000);
    ea::AdpcmChannel ch;
    std::vector<uint8_t> frames;
    ea::AdpcmEncode(pcm, -1, ch, frames);
    const size_t e = i * 32;
    sdir[e] = 0, sdir[e + 1] = static_cast<uint8_t>(10 + i);
    put32(e + 4, offset);
    put32(e + 0x0C, 0x3C000000u | 22050);
    put32(e + 0x10, static_cast<uint32_t>(pcm.size()));
    const size_t x = n * 32 + 4 + i * 40;
    put32(e + 0x1C, static_cast<uint32_t>(x));
    sdir[x + 1] = 8;
    for (int k = 0; k < 16; k++) sdir[x + 8 + 2 * k] = static_cast<uint8_t>(ch.coef[k] >> 8), sdir[x + 9 + 2 * k] = static_cast<uint8_t>(ch.coef[k]);
    samp.insert(samp.end(), frames.begin(), frames.end());
    while (samp.size() % 32) samp.push_back(0);
    offset = static_cast<uint32_t>(samp.size());
  }
  sdir[n * 32] = 0xFF, sdir[n * 32 + 1] = 0xFF;
  std::vector<es::Sample> all;
  std::string error;
  CHECK(es::ParseSdir(sdir, all, error) && all.size() == 3 && all[1].id == 11 && all[1].Rate() == 22050 && all[1].Key() == 60, "sfx: the directory is read");
  const size_t before = samp.size();
  ea::Pcm shortTone = Sine(44100, 0.05, 880.0, 0.4);
  shortTone.ch.resize(1);
  es::ReplaceResult r;
  ea::LoopPoints none;
  CHECK(es::ReplaceSample(sdir, samp, 12, shortTone, none, es::ReplaceOptions{}, r, error) && r.inPlace && samp.size() == before, "sfx: a shorter sound goes into the old room, the file does not grow");
  ea::Pcm back;
  ea::LoopPoints loop;
  CHECK(es::DecodeSample(sdir, samp, 12, back, loop, error) && back.rate == 22050 && !loop.found, "sfx: the replaced sample decodes, at the old rate, without a loop");
  ea::Pcm ref = Sine(22050, 0.05, 880.0, 0.4);
  CHECK(back.Frames() >= ref.Frames() - 2 && SnrDb(ref.ch[0], back.ch[0], 200) > 18.0, "sfx: the replaced sample sounds like the new tone");
  ea::Pcm longTone = Sine(22050, 0.6, 330.0, 0.4);
  CHECK(es::ReplaceSample(sdir, samp, 10, longTone, none, es::ReplaceOptions{}, r, error) && !r.inPlace && samp.size() > before, "sfx: a longer sound goes to the end of the file");
  CHECK(es::DecodeSample(sdir, samp, 10, back, loop, error) && back.Frames() == longTone.Frames(), "sfx: the longer sample decodes in full");
  es::ReplaceOptions tight;
  tight.maxGrow = 100;
  const std::vector<uint8_t> keepDir = sdir, keepSamp = samp;
  CHECK(!es::ReplaceSample(sdir, samp, 11, Sine(22050, 2.0, 200.0, 0.4), none, tight, r, error) && sdir == keepDir && samp == keepSamp, "sfx: a sound that would grow the file too much is refused, nothing changes");
  CHECK(!es::ReplaceSample(sdir, samp, 99, shortTone, none, es::ReplaceOptions{}, r, error), "sfx: an unknown sample is refused");
}

int main() {
  TestWav();
  TestResample();
  TestAdpcm();
  TestPlainStream();
  TestLoopStream();
  TestSfx();
  std::printf(failures == 0 ? "ALL OK\n" : "FAILURES: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
