#include <dolphin/types.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
void menuFaceInit(s32 n);

void PortXD_MenuFaceInit(s32 n) {
  if (const char* v = std::getenv("XD_FACE_CACHE"); v != nullptr && v[0] != '\0') {
    const long want = std::strtol(v, nullptr, 10);
    if (want >= 3 && want < n) {
      std::fprintf(stderr, "[faces] test switch XD_FACE_CACHE: %ld face slots instead of %d\n", want, static_cast<int>(n));
      n = static_cast<s32>(want);
    }
  }
  menuFaceInit(n);
}
}
