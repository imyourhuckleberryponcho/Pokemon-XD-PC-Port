#include "../compat/mac/port_mac_from_chars.h"

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static long sChecked = 0, sFailed = 0;

template <typename T> static bool same_value(T a, T b) {
  if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
  return std::memcmp(&a, &b, sizeof a) == 0;
}

template <typename T> static void check(const std::string& s, const char* type) {
  T mine = static_cast<T>(123.25), real = static_cast<T>(123.25);
  const char* first = s.data();
  const char* last = s.data() + s.size();
  const auto a = port_mac::from_chars(first, last, mine);
  const auto b = std::from_chars(first, last, real);
  ++sChecked;
  if (a.ptr != b.ptr || a.ec != b.ec || !same_value(mine, real)) {
    if (++sFailed <= 40) {
      std::fprintf(stderr, "DIFFERENT (%s) \"%s\": stand-in stop %td ec %d value %.17g | std stop %td ec %d value %.17g\n",
                   type, s.c_str(), a.ptr - first, static_cast<int>(a.ec), static_cast<double>(mine), b.ptr - first,
                   static_cast<int>(b.ec), static_cast<double>(real));
    }
  }
}

static void both(const std::string& s) {
  check<double>(s, "double");
  check<float>(s, "float");
}

int main() {
  const char* locale = nullptr;
  for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "nl_NL.UTF-8", "es_ES.UTF-8", "C.UTF-8"}) {
    if ((locale = std::setlocale(LC_ALL, name)) != nullptr) break;
  }
  std::printf("locale of the process: %s (decimal point '%s')\n", locale != nullptr ? locale : "C",
              std::localeconv()->decimal_point);
  const std::vector<std::string> fixed = {
      "", " ", "0", "-0", "+0", "+1", " 1", "\t1", "\n1", "1 ", "1", "-1", "1.5", "-1.5", ".5", "-.5", "5.", "-5.", ".",
      "-", "--1", "-+1", "+-1", "1,5", "1.5.5", "1e", "1e+", "1e-", "1e5", "1E5", "1e+5", "1e-5", "1e05", "1.e5", ".5e1",
      "e5", "1e5.5", "1e5e5", "0x10", "0X10", "0x", "0x1p3", "-0x1p3", "1x", "x1", "1.0x", "1e2x", "inf", "INF", "Inf",
      "-inf", "+inf", "infinity", "INFINITY", "-Infinity", "infinit", "infx", "in", "i", "nan", "NAN", "NaN", "-nan",
      "+nan", "nan(", "nan()", "nan(1)", "nan(abc)", "nan(a b)", "nan(abc", "nanx", "na", "n", "1e308", "1.7976931348623157e308",
      "1.7976931348623158e308", "1.7976931348623159e308", "1.8e308", "1e309", "-1e309", "1e400", "-1e400", "1e4000",
      "2.2250738585072014e-308", "2.2250738585072011e-308", "2.2250738585072009e-308", "1e-308", "1e-310", "4.9e-324",
      "5e-324", "4.9406564584124654e-324", "2.5e-324", "2.4703282292062328e-324", "2.4703282292062327e-324", "2e-324",
      "1e-324", "1e-325", "1e-400", "-1e-400", "1e-4000", "0e400", "0e-400", "0.0e9999", "-0.0e-9999",
      "3.4028234e38", "3.4028235e38", "3.4028236e38", "3.4028235677973366e38", "3.5e38", "1e39", "-1e39",
      "1.17549435e-38", "1.1754942e-38", "1e-39", "1.4e-45", "1.401298464324817e-45", "7.1e-46", "7.0064923216240854e-46",
      "7.006492321624085e-46", "7e-46", "1e-46", "-1e-46", "1e-50",
      "0.1", "0.2", "0.3", "0.30000000000000004", "9007199254740993", "9007199254740992.5", "16777217", "16777216.5",
      "123456789012345678901234567890", "0.000000000000000000000000000001", "1.00000000000000011102230246251565404",
      "1.00000000000000011102230246251565405", "8.5", "0.7", "1.25", "100", "-100", "00012", "-00.5", "1e+00005",
      "1e000000000000000000001", "1e-000000000000000000001", "1e99999999999999999999", "1e-99999999999999999999",
      "0e99999999999999999999", "12345.6789abc", "12 34", "1\0002", "1.5f", "1.5F", "1.5L", "1d5", "1p5", "１"};
  for (const std::string& s : fixed) both(s);
  both(std::string("1\0" "2", 3));
  both(std::string("\0" "1", 2));

  std::mt19937_64 rng(20261110);
  const std::string alphabet = "0123456789012345678901234567890123456789....eeEE--++ xXinfINFnaNA()";
  for (int i = 0; i < 400000; ++i) {
    std::string s;
    const int length = 1 + static_cast<int>(rng() % 14);
    for (int k = 0; k < length; ++k) s += alphabet[rng() % alphabet.size()];
    both(s);
  }
  for (int i = 0; i < 300000; ++i) {
    char text[64];
    const int digits = 1 + static_cast<int>(rng() % 19);
    std::string mantissa;
    for (int k = 0; k < digits; ++k) mantissa += static_cast<char>('0' + rng() % 10);
    if (digits > 1 && rng() % 2) mantissa.insert(1 + rng() % (digits - 1), ".");
    const int exponent = static_cast<int>(rng() % 700) - 350;
    std::snprintf(text, sizeof text, "%s%s%s%d", (rng() % 4 == 0) ? "-" : "", mantissa.c_str(), (rng() % 2) ? "e" : "E",
                  exponent);
    both(text);
  }
  for (int i = 0; i < 100000; ++i) {
    uint64_t bits = rng();
    if (i % 4 == 0) bits &= 0x000FFFFFFFFFFFFFull;
    if (i % 4 == 1) bits = (bits & 0x800FFFFFFFFFFFFFull) | 0x7FE0000000000000ull;
    double d;
    std::memcpy(&d, &bits, sizeof d);
    if (std::isnan(d) || std::isinf(d)) continue;
    char text[64];
    std::snprintf(text, sizeof text, "%.17g", d);
    for (char& c : text) {
      if (c == ',') c = '.';
    }
    both(text);
  }
  std::printf("mac_from_chars_test: %ld comparisons with std::from_chars, %ld different\n", sChecked, sFailed);
  return sFailed == 0 ? 0 : 1;
}
