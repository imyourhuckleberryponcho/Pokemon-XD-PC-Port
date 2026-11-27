#ifndef PORT_ANDROID_FD_H
#define PORT_ANDROID_FD_H

#ifdef __ANDROID__
#include <SDL3/SDL_iostream.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

inline std::string PortAndroid_ResolveContent(const std::string& uri, int& fd) {
  fd = -1;
  if (uri.compare(0, 10, "content://") != 0) {
    return uri;
  }
  SDL_IOStream* io = SDL_IOFromFile(uri.c_str(), "rb");
  if (io == nullptr) {
    return std::string();
  }
  std::string result;
  const SDL_PropertiesID props = SDL_GetIOProperties(io);
  std::FILE* fp = props != 0 ? static_cast<std::FILE*>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, nullptr)) : nullptr;
  if (fp != nullptr) {
    const int copy = dup(fileno(fp));
    if (copy >= 0) {
      fd = copy;
      result = "/proc/self/fd/" + std::to_string(copy);
    }
  }
  SDL_CloseIO(io);
  return result;
}
inline void PortAndroid_CloseResolved(int fd) {
  if (fd >= 0) {
    close(fd);
  }
}
#endif

#endif
