#include "randomizer.h"

#include <cstdio>

namespace engine::randomizer {

namespace {

uint64_t splitmix(uint64_t& x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

uint32_t bits_for(uint32_t choices) {
  uint32_t b = 0;
  while ((1u << b) < choices) b++;
  return b;
}

uint16_t check16(const std::vector<uint8_t>& bytes, std::string_view prefix) {
  uint32_t h = 2166136261u;
  for (char c : prefix) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  for (uint8_t b : bytes) h = (h ^ b) * 16777619u;
  return static_cast<uint16_t>(h ^ (h >> 16));
}

struct BitWriter {
  std::vector<uint8_t> bytes;
  uint32_t n = 0;
  void Put(uint64_t v, uint32_t bits) {
    for (uint32_t i = 0; i < bits; i++) {
      if (n % 8 == 0) bytes.push_back(0);
      if ((v >> (bits - 1 - i)) & 1) bytes.back() |= static_cast<uint8_t>(0x80 >> (n % 8));
      n++;
    }
  }
};
struct BitReader {
  const std::vector<uint8_t>& bytes;
  uint32_t n = 0;
  bool Get(uint32_t bits, uint64_t& v) {
    v = 0;
    for (uint32_t i = 0; i < bits; i++, n++) {
      if (n / 8 >= bytes.size()) return false;
      v = (v << 1) | ((bytes[n / 8] >> (7 - n % 8)) & 1);
    }
    return true;
  }
};

}

Rng::Rng(uint64_t seed) {
  for (uint64_t& s : s_) s = splitmix(seed);
}

Rng Rng::Stream(uint64_t seed, std::string_view category) {
  uint64_t h = 1469598103934665603ull;
  for (char c : category) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
  return Rng(seed ^ rotl(h, 17));
}

uint64_t Rng::Next() {
  const uint64_t result = rotl(s_[1] * 5, 7) * 9;
  const uint64_t t = s_[1] << 17;
  s_[2] ^= s_[0];
  s_[3] ^= s_[1];
  s_[1] ^= s_[2];
  s_[0] ^= s_[3];
  s_[2] ^= t;
  s_[3] = rotl(s_[3], 45);
  return result;
}

uint32_t Rng::Below(uint32_t n) {
  if (n <= 1) return 0;
  const uint64_t limit = UINT64_MAX - UINT64_MAX % n;
  uint64_t x;
  do x = Next(); while (x >= limit);
  return static_cast<uint32_t>(x % n);
}

bool Rng::Chance(uint32_t percent) { return Below(100) < percent; }

int Schema::Find(std::string_view id) const {
  for (size_t i = 0; i < switches.size(); i++) {
    if (switches[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

uint32_t Settings::Get(const Schema& s, std::string_view id) const {
  const int i = s.Find(id);
  return i >= 0 && static_cast<size_t>(i) < values.size() ? values[i] : 0;
}

void Settings::Set(const Schema& s, std::string_view id, uint32_t v) {
  const int i = s.Find(id);
  if (i < 0) return;
  if (values.size() < s.switches.size()) values.resize(s.switches.size(), 0);
  values[i] = v < s.switches[i].Choices() ? v : 0;
}

uint32_t Schema::CodeVersion(const Settings& s) const {
  uint32_t version = 1;
  for (size_t i = 0; i < switches.size(); i++) {
    const uint32_t v = i < s.values.size() ? s.values[i] : 0;
    if (v != 0 && v < switches[i].Choices() && switches[i].since > version) version = switches[i].since;
  }
  return version < this->version ? version : this->version;
}

std::string EncodeCode(const Schema& schema, const Settings& s) {
  const uint32_t version = schema.CodeVersion(s);
  BitWriter w;
  w.Put(version, 8);
  w.Put(s.seed, 64);
  for (size_t i = 0; i < schema.switches.size(); i++) {
    if (schema.switches[i].since > version) continue;
    const uint32_t v = i < s.values.size() ? s.values[i] : 0;
    w.Put(v < schema.switches[i].Choices() ? v : 0, bits_for(schema.switches[i].Choices()));
  }
  while (w.n % 8 != 0) w.Put(0, 1);
  const uint16_t check = check16(w.bytes, schema.prefix);
  w.Put(check, 16);
  std::string body;
  BitReader r{w.bytes};
  const uint32_t chars = (w.n + 4) / 5;
  for (uint32_t c = 0; c < chars; c++) {
    uint64_t v = 0;
    for (int b = 0; b < 5; b++) {
      uint64_t bit = 0;
      if (!r.Get(1, bit)) bit = 0;
      v = (v << 1) | bit;
    }
    body += kAlphabet[v];
  }
  std::string out = schema.prefix + std::to_string(version);
  for (size_t i = 0; i < body.size(); i++) {
    if (i % 4 == 0) out += '-';
    out += body[i];
  }
  return out;
}

bool DecodeCode(const Schema& schema, std::string_view code, Settings& out, bool& seedOnly, std::string& error) {
  std::string clean;
  for (char c : code) {
    if (c == '-' || c == ' ' || c == '\t') continue;
    clean += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }
  seedOnly = false;
  if (clean.empty()) {
    error = "the code is empty";
    return false;
  }
  bool digits = true;
  for (char c : clean) digits &= (c >= '0' && c <= '9');
  if (digits) {
    if (clean.size() > 20) {
      error = "the seed number is too long";
      return false;
    }
    uint64_t seed = 0;
    for (char c : clean) {
      const uint64_t d = static_cast<uint64_t>(c - '0');
      if (seed > (UINT64_MAX - d) / 10) {
        error = "the seed number is too big";
        return false;
      }
      seed = seed * 10 + d;
    }
    out.seed = seed;
    if (out.values.size() < schema.switches.size()) out.values.resize(schema.switches.size(), 0);
    seedOnly = true;
    return true;
  }
  std::string prefix = schema.prefix;
  for (char& c : prefix) c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  if (clean.compare(0, prefix.size(), prefix) != 0) {
    error = "this is not a code of this game's randomizer (it should start with " + schema.prefix + ")";
    return false;
  }
  size_t pos = prefix.size();
  uint32_t version = 0;
  const size_t dash = code.find('-');
  std::string head;
  for (char c : code.substr(0, dash)) {
    if (c != ' ' && c != '\t') head += c;
  }
  for (size_t i = prefix.size(); i < head.size() && i < prefix.size() + 4; i++) {
    if (head[i] < '0' || head[i] > '9') break;
    version = version * 10 + static_cast<uint32_t>(head[i] - '0');
    pos++;
  }
  if (dash == std::string_view::npos || pos == prefix.size() || version == 0 || version > schema.version) {
    error = "the code was made by randomizer version " + std::to_string(version) + "; this build knows versions 1 to " +
            std::to_string(schema.version);
    return false;
  }
  BitWriter w;
  for (; pos < clean.size(); pos++) {
    const char* p = nullptr;
    for (const char* a = kAlphabet; *a; a++) {
      if (*a == clean[pos]) p = a;
    }
    if (p == nullptr) {
      error = std::string("the code has a character that codes never use: ") + clean[pos];
      return false;
    }
    w.Put(static_cast<uint64_t>(p - kAlphabet), 5);
  }
  BitReader r{w.bytes};
  uint64_t v = 0;
  Settings s;
  bool ok = r.Get(8, v) && v == version;
  ok = ok && r.Get(64, s.seed);
  for (size_t i = 0; ok && i < schema.switches.size(); i++) {
    if (schema.switches[i].since > version) {
      s.values.push_back(0);
      continue;
    }
    ok = r.Get(bits_for(schema.switches[i].Choices()), v);
    if (ok && v >= schema.switches[i].Choices()) ok = false;
    s.values.push_back(static_cast<uint32_t>(v));
  }
  if (ok) {
    while (r.n % 8 != 0) r.Get(1, v);
    std::vector<uint8_t> payload(w.bytes.begin(), w.bytes.begin() + r.n / 8);
    ok = r.Get(16, v) && v == check16(payload, schema.prefix);
    ok = ok && (clean.size() - prefix.size() - std::to_string(version).size()) == (r.n + 4) / 5;
    while (ok && r.n < w.n) ok = r.Get(1, v) && v == 0;
  }
  if (!ok) {
    error = "the code is mistyped (its check doesn't match)";
    return false;
  }
  out = s;
  return true;
}

size_t ModOutput::Count() const {
  size_t n = 0;
  for (const auto& [t, entries] : tables) {
    for (const auto& [e, fields] : entries) n += fields.size();
  }
  return n;
}

std::string ModOutput::ToJson(const std::string& note) const {
  std::string out = "{\n  \"_note\": \"";
  for (char c : note) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += "\"";
  for (const auto& [table, entries] : tables) {
    out += ",\n  \"" + table + "\": {";
    bool firstEntry = true;
    for (const auto& [entry, fields] : entries) {
      out += firstEntry ? "\n" : ",\n";
      firstEntry = false;
      out += "    \"#" + std::to_string(entry) + "\": {";
      bool firstField = true;
      for (const auto& [field, value] : fields) {
        out += firstField ? " " : ", ";
        firstField = false;
        out += "\"" + field + "\": " + std::to_string(value);
      }
      out += " }";
    }
    out += "\n  }";
  }
  out += "\n}\n";
  return out;
}

uint32_t PickSimilar(Rng& rng, const std::vector<uint32_t>& pool, const std::vector<uint32_t>& strength, uint32_t target,
                     uint32_t percent, const std::vector<bool>* used) {
  auto free = [&](uint32_t x) { return used == nullptr || x >= used->size() || !(*used)[x]; };
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t p = percent; p <= percent + 1000; p += 10) {
      std::vector<uint32_t> fit;
      for (uint32_t x : pool) {
        const uint32_t st = x < strength.size() ? strength[x] : 0;
        const uint64_t lo = target * static_cast<uint64_t>(p > 100 ? 0 : 100 - p) / 100;
        const uint64_t hi = target * static_cast<uint64_t>(100 + p) / 100;
        if (st >= lo && st <= hi && (pass == 1 || free(x))) fit.push_back(x);
      }
      if (!fit.empty()) return fit[rng.Below(static_cast<uint32_t>(fit.size()))];
    }
  }
  return pool[rng.Below(static_cast<uint32_t>(pool.size()))];
}

uint32_t ScaleLevel(uint32_t level, uint32_t percent, uint32_t maxLevel) {
  if (level == 0 || percent == 0) return level;
  if (level >= maxLevel) return level;
  uint64_t now = (static_cast<uint64_t>(level) * (100 + percent) + 50) / 100;
  if (now <= level) now = level + 1;
  return now > maxLevel ? maxLevel : static_cast<uint32_t>(now);
}

uint32_t LevelForStrength(uint32_t level, uint32_t oldStrength, uint32_t nowStrength, uint32_t maxLevel) {
  if (level == 0 || oldStrength == 0 || nowStrength == 0 || oldStrength == nowStrength) return level;
  uint64_t now = (static_cast<uint64_t>(level) * oldStrength + nowStrength / 2) / nowStrength;
  const uint64_t lo = (static_cast<uint64_t>(level) * 3 + 3) / 4, hi = static_cast<uint64_t>(level) * 5 / 4;
  if (now < lo) now = lo;
  if (now > hi) now = hi;
  if (now < 1) now = 1;
  if (now > maxLevel) now = maxLevel > level ? maxLevel : level;
  return static_cast<uint32_t>(now);
}

}
