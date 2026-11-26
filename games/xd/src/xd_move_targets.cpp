#include "xd_move_targets.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

struct Fix {
  const char* move;
  const char* field;
  int value;
};

const Fix kGen4[] = {
    {"Miracle Eye", "rangeId", 0}, {"Embargo", "rangeId", 0}, {"Psycho Shift", "rangeId", 0}, {"Gastro Acid", "rangeId", 0},
    {"Power Swap", "rangeId", 0}, {"Guard Swap", "rangeId", 0}, {"Worry Seed", "rangeId", 0}, {"Heart Swap", "rangeId", 0},
    {"Defog", "rangeId", 0}, {"Aura Sphere", "rangeId", 0}, {"Magnet Bomb", "rangeId", 0}, {"Air Slash", "rangeId", 0},
    {"Mud Bomb", "rangeId", 0}, {"Mirror Shot", "rangeId", 0}, {"Psycho Cut", "rangeId", 0}, {"Stone Edge", "rangeId", 0},
    {"Attack Order", "rangeId", 0},
    {"Dark Void", "rangeId", 4}, {"Toxic Spikes", "rangeId", 7},
    {"Bug Buzz", "bouon", 1}, {"Chatter", "bouon", 1}, {"Dark Void", "bouon", 0},
    {"Chatter", "yubiwohuru", 0}, {"Chatter", "monomane", 0}, {"Chatter", "nekonote", 0}, {"Chatter", "negoto", 0},
    {"Metal Burst", "yubiwohuru", 1}, {"Metal Burst", "nekonote", 1}, {"Me First", "monomane", 1}, {"Copycat", "monomane", 1},
    {"Shadow Force", "hidenFlag", 0},
};

const Fix kGen57[] = {
    {"Sticky Web", "rangeId", 7},
};

bool IsSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

size_t StringEnd(const std::string& t, size_t i) {
  for (size_t p = i + 1; p < t.size(); p++) {
    if (t[p] == '\\') p++;
    else if (t[p] == '"') return p;
  }
  return std::string::npos;
}

}

namespace xd::movetargets {

int SetField(std::string& text, const std::string& move, const std::string& field, int value) {
  const std::string quoted = "\"" + move + "\"";
  const std::string number = std::to_string(value);
  for (size_t at = text.find(quoted); at != std::string::npos; at = text.find(quoted, at + 1)) {
    size_t p = at + quoted.size();
    while (p < text.size() && IsSpace(text[p])) p++;
    if (p >= text.size() || text[p] != ':') continue;
    p++;
    while (p < text.size() && IsSpace(text[p])) p++;
    if (p >= text.size() || text[p] != '{') continue;
    const size_t open = p;
    int depth = 0;
    size_t close = std::string::npos;
    size_t valueAt = std::string::npos, valueEnd = std::string::npos;
    for (size_t i = open; i < text.size(); i++) {
      const char c = text[i];
      if (c == '"') {
        const size_t e = StringEnd(text, i);
        if (e == std::string::npos) return 0;
        if (depth == 1 && valueAt == std::string::npos && text.compare(i + 1, e - i - 1, field) == 0 && e - i - 1 == field.size()) {
          size_t q = e + 1;
          while (q < text.size() && IsSpace(text[q])) q++;
          if (q < text.size() && text[q] == ':') {
            q++;
            while (q < text.size() && IsSpace(text[q])) q++;
            size_t r = q;
            while (r < text.size() && text[r] != ',' && text[r] != '}' && !IsSpace(text[r])) r++;
            valueAt = q;
            valueEnd = r;
          }
        }
        i = e;
      } else if (c == '{' || c == '[') {
        depth++;
      } else if (c == '}' || c == ']') {
        depth--;
        if (depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) return 0;
    if (valueAt != std::string::npos) {
      if (text.compare(valueAt, valueEnd - valueAt, number) == 0) return 0;
      text.replace(valueAt, valueEnd - valueAt, number);
      return 1;
    }
    text.insert(open + 1, " \"" + field + "\": " + number + ",");
    return 1;
  }
  return 0;
}

int PatchPackMoves(std::string& text, bool gen57) {
  int changed = 0;
  if (gen57) {
    for (const Fix& f : kGen57) changed += SetField(text, f.move, f.field, f.value);
  } else {
    for (const Fix& f : kGen4) changed += SetField(text, f.move, f.field, f.value);
  }
  return changed;
}

}
