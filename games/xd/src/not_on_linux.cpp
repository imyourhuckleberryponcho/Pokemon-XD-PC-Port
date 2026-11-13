#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void not_yet(const char* what) {
  std::fprintf(stderr, "[games/xd] %s: not on Linux yet\n", what);
  std::fflush(stderr);
}

bool asked(const char* variable) {
  const char* v = std::getenv(variable);
  return v != nullptr && v[0] != '\0';
}

}

extern "C" {

#ifdef __APPLE__
void PortXD_StateDumpStart(void) {
  if (asked("XD_STATE_PROBES")) not_yet("XD_STATE_PROBES (game variables by name)");
  if (asked("XD_RNG_SEED")) not_yet("XD_RNG_SEED");
}
#endif

}
