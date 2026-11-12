#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
#include "port_folder_claim.h"
#ifdef __APPLE__
#include "port_window_icon.h"
#include "port_zip.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
  const char* tmp = std::getenv("TMPDIR");
  char name[1100];
  std::snprintf(name, sizeof name, "%s/PortFolderClaim-%016llx.lock", (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp",
                static_cast<unsigned long long>(hash));
  const int fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return true;
  if (flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
  if (errno != EWOULDBLOCK) {
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
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
    if (errno != EWOULDBLOCK) break;
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
      break;
    }
    usleep(10000);
  }
  close(fd);
  return false;
}

#ifdef __APPLE__
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
#endif
