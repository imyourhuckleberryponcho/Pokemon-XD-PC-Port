#ifndef PLATFORM_DISC_OPEN_H
#define PLATFORM_DISC_OPEN_H

#include <nod.h>

#ifdef __ANDROID__
#include <SDL3/SDL_iostream.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#endif

#ifndef __ANDROID__
#define PortDisc_NodOpen nod_disc_open
#else
inline NodResult PortDisc_NodOpen(const char* path, const NodDiscOptions* options, NodHandle** out) {
  if (path != nullptr && std::strncmp(path, "content://", 10) == 0) {
    static std::mutex ioLock;
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (io == nullptr) {
      return NOD_RESULT_ERR_IO;
    }
    NodDiscStream stream{};
    stream.user_data = io;
    stream.read_at = [](void* user, uint64_t offset, void* buf, size_t len) -> int64_t {
      SDL_IOStream* s = static_cast<SDL_IOStream*>(user);
      std::lock_guard<std::mutex> lock(ioLock);
      if (offset > static_cast<uint64_t>(std::numeric_limits<Sint64>::max()) ||
          SDL_SeekIO(s, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0) {
        return -1;
      }
      size_t total = 0;
      while (total < len) {
        const size_t n = SDL_ReadIO(s, static_cast<uint8_t*>(buf) + total, len - total);
        if (n == 0) {
          break;
        }
        total += n;
      }
      return static_cast<int64_t>(total);
    };
    stream.stream_len = [](void* user) -> int64_t {
      const Sint64 size = SDL_GetIOSize(static_cast<SDL_IOStream*>(user));
      return size < 0 ? -1 : static_cast<int64_t>(size);
    };
    stream.close = [](void* user) { SDL_CloseIO(static_cast<SDL_IOStream*>(user)); };
    return nod_disc_open_stream(&stream, options, out);
  }
  return nod_disc_open(path, options, out);
}
#endif

#endif
