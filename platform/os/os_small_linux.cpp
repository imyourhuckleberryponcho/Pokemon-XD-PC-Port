#ifdef __linux__
#include "port_folder_claim.h"
#include "port_window_icon.h"
#include "port_zip.h"

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

extern "C" bool PortFolderClaim_Take(const char* folder) {
  if (folder == nullptr || folder[0] == '\0') return true;
  std::error_code ec;
  std::filesystem::path full = std::filesystem::weakly_canonical(std::filesystem::path(folder), ec);
  if (ec || full.empty()) return true;
  std::string path = full.string();
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const unsigned char c : path) {
    hash = (hash ^ c) * 0x100000001B3ull;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const int length = std::snprintf(address.sun_path + 1, sizeof address.sun_path - 1, "PortFolderClaim-%016llx",
                                   static_cast<unsigned long long>(hash));
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return true;
  const auto size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + length);
  if (bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0) return true;
  if (errno != EADDRINUSE) {
    close(fd);
    return true;
  }
  const char* parent = std::getenv("PORT_RESET_PARENT");
  const long pid = parent != nullptr ? std::strtol(parent, nullptr, 10) : 0;
  if (pid <= 0 || pid == static_cast<long>(getpid())) {
    close(fd);
    return false;
  }
  for (int waited = 0; waited < 2000; ++waited) {
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0) return true;
    if (errno != EADDRINUSE) break;
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      if (bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0) return true;
      break;
    }
    usleep(10000);
  }
  close(fd);
  return false;
}

extern "C" bool PortWindowIcon_Apply(SDL_Window*, const char*) { return false; }
extern "C" bool PortWindowIcon_SetAppId(SDL_Window*, const char*) { return false; }
extern "C" bool PortWindowIcon_OwnRGBA(int, uint8_t*) { return false; }
extern "C" bool PortWindowIcon_ResourceRGBA(int, int, uint8_t*) { return false; }
extern "C" bool PortWindowIcon_SaveResource(int, const char*) { return false; }

namespace port_zip {
bool AsJpeg(const std::string&, int, std::string& jpeg) {
  jpeg.clear();
  return false;
}
}
#endif
