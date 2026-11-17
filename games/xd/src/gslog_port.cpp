#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void GSlogWrite(const char* fmt, ...) {
  char text[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof text, fmt != nullptr ? fmt : "", args);
  va_end(args);
  static const bool all = std::getenv("XD_GSLOG") != nullptr && std::getenv("XD_GSLOG")[0] == '1';
  static unsigned overflow = 0, failure = 0;
  bool show = all;
  if (std::strstr(text, "overflow") != nullptr) show = show || overflow++ < 20 || overflow % 600 == 0;
  else if (std::strstr(text, "FAIL") != nullptr || std::strstr(text, "failure") != nullptr) show = show || failure++ < 20 || failure % 600 == 0;
  if (!show) return;
  size_t n = std::strlen(text);
  while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;
  std::fprintf(stderr, "[gslog] %s\n", text);
}

extern "C" void (*PortXD_GSlogWritefSink)(const char* text);
namespace {
void GslogfToLog(const char* text) {
  char line[512];
  std::snprintf(line, sizeof line, "%s", text != nullptr ? text : "");
  size_t n = std::strlen(line);
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
  std::fprintf(stderr, "[gslogf] %s\n", line);
}
struct GslogfInit {
  GslogfInit() {
    const char* v = std::getenv("XD_GSLOGF");
    if (v != nullptr && v[0] == '1') PortXD_GSlogWritefSink = GslogfToLog;
  }
} gGslogfInit;
}
