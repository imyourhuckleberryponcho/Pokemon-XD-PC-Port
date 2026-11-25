#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
#include "port_folder_claim.h"
#ifdef __APPLE__
#include "port_window_icon.h"
#include "port_zip.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#endif

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace {
void WriteHolder(int fd) {
  char text[96];
  const int n = std::snprintf(text, sizeof text, "%ld %lld\n", static_cast<long>(getpid()), static_cast<long long>(time(nullptr)));
  if (n > 0 && ftruncate(fd, 0) == 0) {
    const ssize_t written = pwrite(fd, text, static_cast<size_t>(n), 0);
    (void)written;
  }
}

std::string ExeOf(long pid) {
#ifdef __APPLE__
  char buffer[4096];
  const int n = proc_pidpath(static_cast<int>(pid), buffer, sizeof buffer);
  return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
#else
  char link[64], target[1024];
  std::snprintf(link, sizeof link, "/proc/%ld/exe", pid);
  const ssize_t n = readlink(link, target, sizeof target - 1);
  return n > 0 ? std::string(target, static_cast<size_t>(n)) : std::string();
#endif
}

std::string LockName(const char* folder) {
  std::error_code ec;
  std::filesystem::path full = std::filesystem::weakly_canonical(std::filesystem::path(folder), ec);
  if (ec || full.empty()) return {};
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
  return name;
}
}

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
  if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
    WriteHolder(fd);
    return true;
  }
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
  for (int waited = 0; waited < 6000; ++waited) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
      WriteHolder(fd);
      return true;
    }
    if (errno != EWOULDBLOCK) break;
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        WriteHolder(fd);
        return true;
      }
      break;
    }
    usleep(10000);
  }
  PortFolderHolder holder;
  if (kill(static_cast<pid_t>(pid), 0) == 0 && PortFolderClaim_Holder(folder, &holder) && holder.pid == static_cast<unsigned long>(pid) &&
      !ExeOf(pid).empty() && ExeOf(pid) == ExeOf(getpid())) {
    std::fprintf(stderr, "[card] the copy that started this one (process %ld) is still closing after 60 s: ending it\n", pid);
    kill(static_cast<pid_t>(pid), SIGKILL);
    for (int waited = 0; waited < 1000; ++waited) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        WriteHolder(fd);
        return true;
      }
      usleep(10000);
    }
  }
  close(fd);
  return false;
}

extern "C" bool PortFolderClaim_Holder(const char* folder, PortFolderHolder* out) {
  if (out == nullptr) return false;
  *out = PortFolderHolder{};
  if (folder == nullptr || folder[0] == '\0') return false;
  const std::string name = LockName(folder);
  if (name.empty()) return false;
  const int fd = open(name.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  if (flock(fd, LOCK_SH | LOCK_NB) == 0) {
    close(fd);
    return false;
  }
  char text[96] = {};
  const ssize_t n = pread(fd, text, sizeof text - 1, 0);
  close(fd);
  long pid = 0;
  long long started = 0;
  if (n > 0 && std::sscanf(text, "%ld %lld", &pid, &started) >= 1 && pid > 0) {
    out->pid = static_cast<unsigned long>(pid);
    out->started = started;
    const std::string program = ExeOf(pid);
    std::snprintf(out->program, sizeof out->program, "%s", program.c_str());
    out->alive = kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
    out->ours = out->alive && !program.empty() && program == ExeOf(getpid());
  }
  return true;
}

extern "C" bool PortFolderClaim_EndHolder(const PortFolderHolder* holder) {
  if (holder == nullptr || holder->pid == 0 || !holder->alive || !holder->ours) return false;
  const pid_t pid = static_cast<pid_t>(holder->pid);
  if (ExeOf(pid) != std::string(holder->program)) return false;
  kill(pid, SIGKILL);
  for (int waited = 0; waited < 1000; ++waited) {
    if (kill(pid, 0) != 0 && errno == ESRCH) return true;
    usleep(10000);
  }
  return false;
}

extern "C" bool PortFolderClaim_IsFree(const char* folder) {
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
  const bool free = flock(fd, LOCK_EX | LOCK_NB) == 0;
  close(fd);
  return free;
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
