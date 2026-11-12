#ifndef PORT_MAC_FROM_CHARS_H
#define PORT_MAC_FROM_CHARS_H
#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
#include <stdlib.h>
#ifdef __APPLE__
#include <xlocale.h>
#else
#include <locale.h>
#endif

#include <cerrno>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace port_mac {
#ifdef __APPLE__
inline locale_t c_locale() { return nullptr; }
#else
inline locale_t c_locale() {
  static const locale_t c = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
  return c;
}
#endif
inline double parse_c_locale(const char* text, char** end, double) { return strtod_l(text, end, c_locale()); }
inline float parse_c_locale(const char* text, char** end, float) { return strtof_l(text, end, c_locale()); }

template <typename T>
std::from_chars_result from_chars(const char* first, const char* last, T& out) {
  if (first == last || *first == '+' || *first == ' ' || (*first >= '\t' && *first <= '\r')) {
    return {first, std::errc::invalid_argument};
  }
  for (const char* p = first; p != last; ++p) {
    if (*p == 'x' || *p == 'X') {
      last = p;
      break;
    }
  }
  const std::string copy(first, last);
  char* end = nullptr;
  errno = 0;
  const T value = parse_c_locale(copy.c_str(), &end, T{});
  if (end == copy.c_str()) {
    return {first, std::errc::invalid_argument};
  }
  const char* stop = first + (end - copy.c_str());
  if (errno == ERANGE && (std::isinf(value) || value == T{})) {
    return {stop, std::errc::result_out_of_range};
  }
  out = value;
  return {stop, std::errc()};
}
}
#endif
#endif
