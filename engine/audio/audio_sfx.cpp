#include "audio_sfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace engine::audio::sfx {

namespace {

uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
int16_t Be16s(const uint8_t* p) { return static_cast<int16_t>(Be16(p)); }
void Put32(uint8_t* p, uint32_t x) { p[0] = static_cast<uint8_t>(x >> 24), p[1] = static_cast<uint8_t>(x >> 16), p[2] = static_cast<uint8_t>(x >> 8), p[3] = static_cast<uint8_t>(x); }
void Put16(uint8_t* p, uint16_t x) { p[0] = static_cast<uint8_t>(x >> 8), p[1] = static_cast<uint8_t>(x); }

bool CountEntries(const std::vector<uint8_t>& sdir, size_t& n) {
  n = 0;
  for (;;) {
    const size_t at = n * kSdirEntryBytes;
    if (at + 2 > sdir.size()) return false;
    if (Be16(&sdir[at]) == 0xFFFF) return true;
    if (at + kSdirEntryBytes > sdir.size()) return false;
    n++;
  }
}

const Sample* Find(const std::vector<Sample>& all, uint16_t id) {
  for (const Sample& s : all) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

}

bool ParseSdir(const std::vector<uint8_t>& sdir, std::vector<Sample>& out, std::string& error) {
  out.clear();
  size_t n = 0;
  if (!CountEntries(sdir, n)) {
    error = "this is not a sample directory (no closing entry)";
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    const uint8_t* e = &sdir[i * kSdirEntryBytes];
    Sample s;
    s.entry = i;
    s.id = Be16(e);
    s.offset = Be32(e + 4);
    s.info = Be32(e + 0x0C);
    s.type = static_cast<uint8_t>(Be32(e + 0x10) >> 24);
    s.length = Be32(e + 0x10) & 0xFFFFFF;
    s.loopOffset = Be32(e + 0x14);
    s.loopLength = Be32(e + 0x18);
    s.extra = Be32(e + 0x1C);
    out.push_back(s);
  }
  return true;
}

bool ParseEffects(const std::vector<uint8_t>& proj, const std::vector<uint8_t>& pool, std::vector<Effect>& out, std::string& error) {
  out.clear();
  if (pool.size() < 16) {
    error = "the macro pool is too small";
    return false;
  }
  const uint32_t macroOff = Be32(pool.data());
  std::vector<std::pair<uint16_t, std::vector<uint16_t>>> macros;
  for (size_t o = macroOff; o + 8 <= pool.size();) {
    const uint32_t next = Be32(&pool[o]);
    if (next == 0xFFFFFFFFu) break;
    if (next < 8 || o + next > pool.size()) {
      error = "the macro pool is damaged";
      return false;
    }
    std::vector<uint16_t> samples;
    for (size_t s = o + 8; s + 8 <= o + next; s += 8) {
      const uint32_t p0 = Be32(&pool[s]);
      if ((p0 & 0xFF) == 0x10) {
        const uint16_t id = static_cast<uint16_t>(p0 >> 8);
        if (std::find(samples.begin(), samples.end(), id) == samples.end()) samples.push_back(id);
      }
    }
    macros.emplace_back(Be16(&pool[o + 4]), std::move(samples));
    o += next;
  }
  for (size_t g = 0; g + 0x28 <= proj.size();) {
    const uint32_t next = Be32(&proj[g]);
    const size_t table = g + 0 + Be32(&proj[g + 0x1C]);
    const size_t tableAbs = Be32(&proj[g + 0x1C]);
    (void)table;
    if (tableAbs + 4 <= proj.size()) {
      const uint16_t num = Be16(&proj[tableAbs]);
      for (uint16_t k = 0; k < num && tableAbs + 4 + 10u * (k + 1) <= proj.size(); k++) {
        const uint8_t* t = &proj[tableAbs + 4 + 10u * k];
        Effect e;
        e.fx = Be16(t), e.macro = Be16(t + 2), e.maxVoices = t[4], e.priority = t[5], e.volume = t[6];
        for (const auto& m : macros) {
          if (m.first == e.macro) e.samples = m.second;
        }
        out.push_back(std::move(e));
      }
    }
    if (next == 0xFFFFFFFFu || next == 0 || g + next >= proj.size()) break;
    g += next;
  }
  if (out.empty()) {
    error = "no effects found in the project";
    return false;
  }
  return true;
}

namespace {

bool ReadCoefs(const std::vector<uint8_t>& sdir, const Sample& s, AdpcmChannel& ch, std::string& error) {
  if (s.extra == 0 || static_cast<size_t>(s.extra) + kAdpcmInfoBytes > sdir.size()) {
    error = "the sample's coefficient block is outside the directory";
    return false;
  }
  const uint8_t* p = &sdir[s.extra];
  for (int i = 0; i < 16; i++) ch.coef[i] = Be16s(p + 8 + 2 * i);
  ch.ps = p[2], ch.loopPs = p[3], ch.loopHist1 = Be16s(p + 4), ch.loopHist2 = Be16s(p + 6);
  ch.hist1 = ch.hist2 = 0;
  return true;
}

}

bool DecodeSample(const std::vector<uint8_t>& sdir, const std::vector<uint8_t>& samp, uint16_t id, Pcm& out, LoopPoints& loop, std::string& error) {
  std::vector<Sample> all;
  if (!ParseSdir(sdir, all, error)) return false;
  const Sample* s = Find(all, id);
  if (s == nullptr) {
    error = "there is no sample " + std::to_string(id);
    return false;
  }
  if (s->type != 0) {
    error = "sample " + std::to_string(id) + " is not DSP-ADPCM (type " + std::to_string(s->type) + ")";
    return false;
  }
  AdpcmChannel ch;
  if (!ReadCoefs(sdir, *s, ch, error)) return false;
  if (static_cast<uint64_t>(s->offset) + s->Bytes() > samp.size()) {
    error = "the sample lies outside the sample file";
    return false;
  }
  out = Pcm{};
  out.rate = static_cast<int>(s->Rate());
  const std::vector<int16_t> pcm = AdpcmDecode(&samp[s->offset], s->Bytes(), s->length, ch);
  out.ch.assign(1, std::vector<float>(pcm.size()));
  for (size_t i = 0; i < pcm.size(); i++) out.ch[0][i] = static_cast<float>(pcm[i]) / 32768.0f;
  loop = LoopPoints{};
  if (s->loopLength > 0) loop.found = true, loop.start = s->loopOffset, loop.end = static_cast<uint64_t>(s->loopOffset) + s->loopLength;
  return true;
}

bool ReplaceSample(std::vector<uint8_t>& sdir, std::vector<uint8_t>& samp, uint16_t id, const Pcm& sound, const LoopPoints& fileLoop,
                   const ReplaceOptions& options, ReplaceResult& result, std::string& error) {
  result = ReplaceResult{};
  std::vector<Sample> all;
  if (!ParseSdir(sdir, all, error)) return false;
  const Sample* found = Find(all, id);
  if (found == nullptr) {
    error = "there is no sample " + std::to_string(id) + " in this bank";
    return false;
  }
  const Sample old = *found;
  if (old.type != 0) {
    error = "sample " + std::to_string(id) + " is not DSP-ADPCM (type " + std::to_string(old.type) + "); only that kind can be replaced";
    return false;
  }
  if (sound.Frames() == 0 || sound.ch.empty()) {
    error = "the sound file is empty";
    return false;
  }
  if (old.extra == 0 || static_cast<size_t>(old.extra) + kAdpcmInfoBytes > sdir.size()) {
    error = "the sample's coefficient block is outside the directory";
    return false;
  }
  const uint32_t rate = options.rate != 0 ? options.rate : (old.Rate() != 0 ? old.Rate() : 32000);
  std::vector<float> mono(sound.Frames(), 0.0f);
  for (const auto& c : sound.ch) {
    for (size_t i = 0; i < mono.size(); i++) mono[i] += c[i] / static_cast<float>(sound.ch.size());
  }
  const double ratio = static_cast<double>(rate) / static_cast<double>(sound.rate);
  if (std::fabs(ratio - 1.0) > 1e-9) mono = Resample(mono, ratio);
  if (mono.size() > 0xFFFFFF - 14) {
    error = "the sound is too long for a sound effect";
    return false;
  }
  bool loops = false;
  uint64_t loopStart = 0, loopEnd = 0;
  if (options.loop == ReplaceOptions::Loop::On || (options.loop == ReplaceOptions::Loop::Keep && (old.loopLength > 0 || fileLoop.found))) {
    loops = true;
    if (options.loopEnd != 0) {
      loopStart = static_cast<uint64_t>(std::llround(static_cast<double>(options.loopStart) * ratio));
      loopEnd = static_cast<uint64_t>(std::llround(static_cast<double>(options.loopEnd) * ratio));
    } else if (fileLoop.found) {
      loopStart = static_cast<uint64_t>(std::llround(static_cast<double>(fileLoop.start) * ratio));
      loopEnd = static_cast<uint64_t>(std::llround(static_cast<double>(fileLoop.end) * ratio));
    } else {
      loopStart = 0, loopEnd = mono.size();
    }
    loopEnd = std::min<uint64_t>(std::max<uint64_t>(loopEnd, 1), mono.size());
    loopStart = loopStart / kAdpcmFrameSamples * kAdpcmFrameSamples;
    if (loopStart >= loopEnd) loopStart = 0;
    if (loopEnd < mono.size()) {
      mono.resize(loopEnd);
      result.notes.push_back("the sound after the loop's end is cut off (the game loops back at the end)");
    }
  }
  std::vector<int16_t> pcm(mono.size());
  float peak = 0;
  for (size_t i = 0; i < mono.size(); i++) {
    const double v = static_cast<double>(mono[i]) * options.volume * 32768.0;
    pcm[i] = static_cast<int16_t>(std::lround(std::clamp(v, -32768.0, 32767.0)));
    peak = std::max(peak, std::fabs(mono[i]));
  }
  if (static_cast<double>(peak) * options.volume > 1.0) result.notes.push_back("the sound is louder than the format allows and was clipped (lower \"volume\" or the file's level)");
  AdpcmChannel ch;
  std::vector<uint8_t> frames;
  AdpcmEncode(pcm, loops ? static_cast<long long>(loopStart) : -1, ch, frames);
  const uint32_t length = static_cast<uint32_t>(pcm.size());
  const size_t newBytes = frames.size();
  const size_t oldBytes = old.Bytes();
  size_t at = old.offset;
  bool inPlace = true;
  if (newBytes > oldBytes) {
    size_t room = oldBytes;
    uint32_t nextStart = 0xFFFFFFFFu;
    for (const Sample& s : all) {
      if (s.offset > old.offset && s.offset < nextStart) nextStart = s.offset;
    }
    if (nextStart != 0xFFFFFFFFu) room = std::max<size_t>(oldBytes, nextStart - old.offset);
    if (newBytes > room) {
      inPlace = false;
      at = (samp.size() + 31) / 32 * 32;
      const size_t grow = at + newBytes - samp.size();
      if (grow > options.maxGrow) {
        error = "the new sound takes " + std::to_string(newBytes) + " bytes in the game's format, the old one " + std::to_string(oldBytes) +
                "; that would grow the sample file by " + std::to_string(grow) + " bytes (at most " + std::to_string(options.maxGrow) + " allowed). Make the sound shorter";
        return false;
      }
    }
  }
  if (inPlace) {
    if (at + std::max(newBytes, oldBytes) > samp.size()) samp.resize(at + std::max(newBytes, oldBytes), 0);
    std::memset(&samp[at], 0, std::max(newBytes, oldBytes));
    std::memcpy(&samp[at], frames.data(), newBytes);
  } else {
    result.grewBytes = at + newBytes - samp.size();
    samp.resize(at + newBytes, 0);
    std::memcpy(&samp[at], frames.data(), newBytes);
  }
  uint8_t* x = &sdir[old.extra];
  Put16(x, 8);
  x[2] = frames[0];
  x[3] = loops ? static_cast<uint8_t>(ch.loopPs) : frames[0];
  Put16(x + 4, static_cast<uint16_t>(loops ? ch.loopHist1 : 0));
  Put16(x + 6, static_cast<uint16_t>(loops ? ch.loopHist2 : 0));
  for (int i = 0; i < 16; i++) Put16(x + 8 + 2 * i, static_cast<uint16_t>(ch.coef[i]));
  uint8_t* e = &sdir[old.entry * kSdirEntryBytes];
  Put32(e + 4, static_cast<uint32_t>(at));
  Put32(e + 0x0C, (old.info & 0xFF000000u) | rate);
  Put32(e + 0x10, length);
  Put32(e + 0x14, loops ? static_cast<uint32_t>(loopStart) : 0);
  Put32(e + 0x18, loops ? static_cast<uint32_t>(loopEnd - loopStart) : 0);
  result.inPlace = inPlace;
  result.offset = static_cast<uint32_t>(at);
  result.samples = length;
  char buf[200];
  std::snprintf(buf, sizeof buf, "%.2f seconds at %u Hz (the old one: %.2f seconds)%s", static_cast<double>(length) / rate, rate,
                static_cast<double>(old.length) / std::max<uint32_t>(old.Rate(), 1), loops ? ", looping" : "");
  result.notes.insert(result.notes.begin(), buf);
  if (!inPlace) result.notes.push_back("it is longer than the old sound, so it was put at the end of the sample file (" + std::to_string(result.grewBytes) + " bytes more)");
  if (old.loopLength > 0 && !loops) result.notes.push_back("the old sound loops (the game repeats it while the effect lasts); yours plays once");
  return true;
}

}
