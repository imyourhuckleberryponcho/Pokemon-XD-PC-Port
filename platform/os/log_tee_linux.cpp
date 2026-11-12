#if defined(__linux__) || defined(__APPLE__)
#include "port_log.h"
#include "port_user_folder.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int kPipeSize = 1 << 20;
constexpr long long kMaxLogBytes = 256LL << 20;
constexpr int kFlushWaitMs = 1000;

struct Stream {
  int read = -1;
  int write = -1;
  int original = -1;
  std::atomic<long> sent{0};
  std::atomic<long> seen{0};
};

Stream sStreams[2];
bool sStarted = false;
std::mutex* sFileLock = new std::mutex();
int sFile = -1;
long long sWritten = 0;
std::string* sPath = new std::string();

void WriteAll(int fd, const char* data, size_t size) {
  while (size > 0) {
    const ssize_t done = write(fd, data, size);
    if (done < 0 && errno == EINTR) {
      continue;
    }
    if (done <= 0) {
      return;
    }
    data += done;
    size -= static_cast<size_t>(done);
  }
}

void AppendToLog(const char* data, size_t size) {
  std::lock_guard<std::mutex> guard(*sFileLock);
  if (sWritten < kMaxLogBytes) {
    WriteAll(sFile, data, size);
    sWritten += static_cast<long long>(size);
    if (sWritten >= kMaxLogBytes) {
      static const char kFull[] = "\n(log too long: nothing more is written to this file)\n";
      WriteAll(sFile, kFull, sizeof kFull - 1);
    }
  }
}

void* Pump(void* param) {
  Stream& s = *static_cast<Stream*>(param);
  static thread_local char buffer[64 * 1024];
  for (;;) {
    const ssize_t n = read(s.read, buffer, sizeof buffer);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return nullptr;
    }
    size_t kept = 0;
    long markers = 0;
    for (ssize_t i = 0; i < n; i++) {
      if (buffer[i] == '\0') {
        markers++;
      } else {
        buffer[kept++] = buffer[i];
      }
    }
    if (kept > 0) {
      if (s.original >= 0) {
        WriteAll(s.original, buffer, kept);
      }
      AppendToLog(buffer, kept);
    }
    if (markers > 0) {
      s.seen.fetch_add(markers);
    }
  }
}

void Prune(const fs::path& folder, const std::string& prefix, size_t keep) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const std::string name = entry.path().filename().string();
    if (entry.is_regular_file(ec) && name.rfind(prefix, 0) == 0 && entry.path().extension() == ".log") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (size_t i = 0; i + keep < files.size(); i++) {
    fs::remove(files[i], ec);
  }
}

bool Redirect(int fd, Stream& s) {
  int ends[2];
  if (pipe(ends) != 0) {
    return false;
  }
#ifdef F_SETPIPE_SZ
  fcntl(ends[1], F_SETPIPE_SZ, kPipeSize);
#endif
  const int original = dup(fd);
  if (dup2(ends[1], fd) < 0) {
    close(ends[0]);
    close(ends[1]);
    if (original >= 0) close(original);
    return false;
  }
  close(ends[1]);
  s.read = ends[0];
  s.write = fd;
  s.original = original;
  pthread_t thread;
  if (pthread_create(&thread, nullptr, Pump, &s) != 0) {
    dup2(original, fd);
    return false;
  }
  pthread_detach(thread);
  return true;
}

bool SameTarget() {
  struct stat a {}, b {};
  return fstat(1, &a) == 0 && fstat(2, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

long long NowMs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

}

extern "C" bool PortLog_Start(const char* appFolder, const char* kind, size_t keep) {
  if (sStarted) {
    return true;
  }
  const fs::path folder = PortUser_Folder(appFolder) / "logs";
  std::error_code ec;
  fs::create_directories(folder, ec);
  const std::string prefix = std::string(kind) + "_";
  Prune(folder, prefix, keep > 0 ? keep - 1 : 0);

  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local);
  const fs::path path = folder / (prefix + stamp + "_p" + std::to_string(getpid()) + ".log");
  sFile = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (sFile < 0) {
    std::fprintf(stderr, "[log] could not create %s; this run has no log file\n", path.c_str());
    return false;
  }
  std::fflush(stdout);
  std::fflush(stderr);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  bool redirected = false;
  if (SameTarget()) {
    redirected = Redirect(1, sStreams[0]) && dup2(1, 2) == 2;
  } else {
    redirected = Redirect(1, sStreams[0]) && Redirect(2, sStreams[1]);
  }
  if (!redirected) {
    std::fprintf(stderr, "[log] could not redirect the program's output; the log stays incomplete\n");
  }
  *sPath = path.string();
  sStarted = true;
  std::atexit(PortLog_Flush);
  return true;
}

extern "C" void PortLog_Flush(void) {
  if (!sStarted) {
    return;
  }
  long target[2] = {0, 0};
  for (int i = 0; i < 2; i++) {
    Stream& s = sStreams[i];
    if (s.write >= 0) {
      target[i] = s.sent.fetch_add(1) + 1;
      WriteAll(s.write, "", 1);
    }
  }
  const long long deadline = NowMs() + kFlushWaitMs;
  for (int i = 0; i < 2; i++) {
    while (sStreams[i].write >= 0 && sStreams[i].seen.load() < target[i] && NowMs() < deadline) {
      const timespec nap{0, 1000000};
      nanosleep(&nap, nullptr);
    }
  }
  fsync(sFile);
}

extern "C" const char* PortLog_Path(void) { return sPath->c_str(); }

extern "C" bool PortLog_CopyTail(const wchar_t* dest, size_t maxBytes) {
  if (!sStarted) {
    return false;
  }
  PortLog_Flush();
  std::FILE* in = std::fopen(sPath->c_str(), "rb");
  if (in == nullptr) {
    return false;
  }
  fseeko(in, 0, SEEK_END);
  const long long size = ftello(in);
  const long long start = size > static_cast<long long>(maxBytes) ? size - static_cast<long long>(maxBytes) : 0;
  std::string text(static_cast<size_t>(size - start), '\0');
  fseeko(in, start, SEEK_SET);
  text.resize(std::fread(text.data(), 1, text.size(), in));
  std::fclose(in);
  std::string head;
  if (start > 0) {
    const size_t newline = text.find('\n');
    text.erase(0, newline == std::string::npos ? 0 : newline + 1);
    head = "(the start of this log is left out: the whole log is " + *sPath + ")\n";
  }
  std::FILE* out = std::fopen(fs::path(dest).c_str(), "wb");
  if (out == nullptr) {
    return false;
  }
  std::fwrite(head.data(), 1, head.size(), out);
  std::fwrite(text.data(), 1, text.size(), out);
  std::fclose(out);
  return true;
}
#endif
