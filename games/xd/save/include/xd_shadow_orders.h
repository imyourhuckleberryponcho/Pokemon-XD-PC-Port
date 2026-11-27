#ifndef XD_SHADOW_ORDERS_H
#define XD_SHADOW_ORDERS_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace xd::save {

constexpr uint32_t kFirstFreeShadowRow = 84;
constexpr uint32_t kLastDiscShadowRow = 127;
constexpr uint32_t kFirstExtraShadowRow = 128;
constexpr uint32_t kExtraShadowRows = 256;
constexpr uint32_t kLastShadowRow = kFirstExtraShadowRow + kExtraShadowRows - 1;
constexpr uint16_t kShadowRush = 357;
constexpr uint16_t kFirstShadowMove = 356, kLastShadowMove = 373;

struct ShadowOrder {
  uint32_t row = 0;
  uint16_t species = 0;
  int level = 1;
  int gauge = 0;
  uint16_t moves[4] = {};
  int revers = 0;
};

inline std::string FormatShadowOrder(const ShadowOrder& o) {
  std::string s = std::to_string(o.row) + ":" + std::to_string(o.species) + ":" + std::to_string(o.level) + ":" + std::to_string(o.gauge) + ":";
  for (int i = 0; i < 4; i++) s += (i > 0 ? "," : "") + std::to_string(o.moves[i]);
  return s + ":" + std::to_string(o.revers);
}

inline bool ParseShadowOrder(const std::string& text, ShadowOrder& o) {
  std::vector<std::string> part;
  size_t at = 0;
  for (;;) {
    const size_t c = text.find(':', at);
    part.push_back(text.substr(at, c == std::string::npos ? std::string::npos : c - at));
    if (c == std::string::npos) break;
    at = c + 1;
  }
  if (part.size() != 6) return false;
  const auto number = [](const std::string& v, long& out) {
    if (v.empty() || v.size() > 9 || v.find_first_not_of("0123456789") != std::string::npos) return false;
    out = std::atol(v.c_str());
    return true;
  };
  long row, species, level, gauge, revers;
  if (!number(part[0], row) || !number(part[1], species) || !number(part[2], level) || !number(part[3], gauge) || !number(part[5], revers)) return false;
  ShadowOrder r;
  size_t from = 0;
  for (int i = 0; i < 4; i++) {
    const size_t c = part[4].find(',', from);
    long m;
    if (!number(part[4].substr(from, c == std::string::npos ? std::string::npos : c - from), m) || m > 0xFFFF) return false;
    r.moves[i] = static_cast<uint16_t>(m);
    if (c == std::string::npos) {
      if (i != 3) return false;
      break;
    }
    from = c + 1;
  }
  if (row < 1 || row > 4000 || species < 1 || species > 0xFFFF || level < 1 || level > 100 || gauge < 1 || gauge > 0xFFFF || revers > 4) return false;
  r.row = static_cast<uint32_t>(row);
  r.species = static_cast<uint16_t>(species);
  r.level = static_cast<int>(level);
  r.gauge = static_cast<int>(gauge);
  r.revers = static_cast<int>(revers);
  o = r;
  return true;
}

}

#endif
