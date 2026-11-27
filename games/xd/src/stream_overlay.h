#ifndef XD_STREAM_OVERLAY_H
#define XD_STREAM_OVERLAY_H

#include <cstdint>
#include <string>

namespace xd::stream {

void Frame();
void Shutdown();

inline std::string JsonString(const std::string& s) {
  std::string out = "\"";
  static const char* hex = "0123456789abcdef";
  auto u = [&](uint32_t c) {
    out += "\\u";
    out += hex[(c >> 12) & 15];
    out += hex[(c >> 8) & 15];
    out += hex[(c >> 4) & 15];
    out += hex[c & 15];
  };
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xF0 && i + 3 < s.size() + 0 && (c & 0xF8) == 0xF0) { cp = c & 7; n = 4; }
    else if (c >= 0xE0 && (c & 0xF0) == 0xE0) { cp = c & 15; n = 3; }
    else if (c >= 0xC0 && (c & 0xE0) == 0xC0) { cp = c & 31; n = 2; }
    else if (c >= 0x80) { cp = 0xFFFD; n = 1; }
    if (n > 1) {
      if (i + n > s.size()) { cp = 0xFFFD; n = 1; }
      else {
        for (size_t k = 1; k < n; k++) {
          const unsigned char cc = static_cast<unsigned char>(s[i + k]);
          if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; n = 1; break; }
          cp = (cp << 6) | (cc & 0x3F);
        }
      }
    }
    i += n;
    if (cp == '\\' || cp == '"' || cp == '/' || cp == '<' || cp == '>' || cp == '&' || cp == '\'' || cp < 0x20 || cp >= 0x7F) {
      if (cp >= 0x10000 && cp <= 0x10FFFF) {
        const uint32_t v = cp - 0x10000;
        u(0xD800 + (v >> 10));
        u(0xDC00 + (v & 0x3FF));
      } else {
        u(cp > 0x10FFFF ? 0xFFFD : cp);
      }
    } else {
      out += static_cast<char>(cp);
    }
  }
  out += '"';
  return out;
}

}

#endif
