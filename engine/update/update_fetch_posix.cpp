#include "update_internal.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

bool FilePathOf(const std::string& url, fs::path& out) {
  if (url.rfind("file:///", 0) != 0) {
    return false;
  }
  std::string path;
  for (size_t i = 7; i < url.size(); i++) {
    if (url[i] == '%' && i + 2 < url.size()) {
      path += static_cast<char>(std::strtol(url.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      path += url[i];
    }
  }
  out = fs::path(path);
  return true;
}

bool CopyLimited(const fs::path& from, const fs::path& to, uint64_t limit, const FetchOptions& options,
                 std::string& error) {
  std::ifstream in(from, std::ios::binary);
  if (!in) {
    error = "the file isn't there: " + detail::Utf8(from);
    return false;
  }
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "a file couldn't be written: " + detail::Utf8(to);
    return false;
  }
  std::error_code ec;
  const uint64_t total = fs::file_size(from, ec);
  char buffer[1 << 16];
  uint64_t done = 0;
  while (in.read(buffer, sizeof buffer) || in.gcount() > 0) {
    done += static_cast<uint64_t>(in.gcount());
    if (done > limit) {
      error = "the download is larger than expected";
      return false;
    }
    out.write(buffer, in.gcount());
    if (options.progress && !options.progress(done, total)) {
      error = "stopped";
      return false;
    }
  }
  out.close();
  return !out.fail();
}

fs::path TempFile() {
  static std::atomic<int> counter{0};
  std::error_code ec;
  fs::path dir = fs::temp_directory_path(ec);
  if (ec) {
    dir = "/tmp";
  }
  return dir / ("xd-update-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + ".tmp");
}

bool Curl(const std::string& url, const fs::path& file, const FetchOptions& options, uint64_t limit,
          std::string& error) {
  const std::string maxSize = std::to_string(limit);
  const std::string connect = std::to_string(std::max(1, options.timeoutMs / 1000));
  const std::string uaText = options.userAgent;
  const std::string outFile = detail::Utf8(file);
  const std::vector<std::string> args = {"curl",          "--disable",     "--silent",     "--show-error",
                                         "--fail",        "--location",    "--proto",      "=http,https",
                                         "--proto-redir", "=http,https",   "--max-redirs", "5",
                                         "--noproxy",     "127.0.0.1,localhost", "--connect-timeout", connect,
                                         "--speed-limit", "1",             "--speed-time", connect,
                                         "--max-filesize", maxSize,        "-A",           uaText,
                                         "-o",            outFile,         url};
  int errPipe[2];
  if (::pipe(errPipe) != 0) {
    error = "downloading isn't possible (no pipe)";
    return false;
  }
  std::vector<char*> argv;
  for (const std::string& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);
  const pid_t child = ::fork();
  if (child < 0) {
    ::close(errPipe[0]);
    ::close(errPipe[1]);
    error = "downloading isn't possible (couldn't start curl)";
    return false;
  }
  if (child == 0) {
    ::close(errPipe[0]);
    ::dup2(errPipe[1], 2);
    const int null = ::open("/dev/null", O_RDWR);
    if (null >= 0) {
      ::dup2(null, 0);
      ::dup2(null, 1);
    }
    ::execvp("curl", argv.data());
    const char message[] = "curl is not installed";
    if (::write(2, message, sizeof message - 1) < 0) {
    }
    ::_exit(127);
  }
  ::close(errPipe[1]);
  ::fcntl(errPipe[0], F_SETFL, O_NONBLOCK);
  std::string errText;
  int status = 0;
  bool stopped = false, tooBig = false;
  for (;;) {
    char chunk[512];
    const ssize_t n = ::read(errPipe[0], chunk, sizeof chunk);
    if (n > 0 && errText.size() < 2000) {
      errText.append(chunk, static_cast<size_t>(n));
    }
    const pid_t done = ::waitpid(child, &status, WNOHANG);
    std::error_code ec;
    const uint64_t have = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
    if (have > limit) {
      tooBig = true;
    }
    if (done == child) {
      break;
    }
    if (tooBig || (options.progress && !options.progress(have, 0))) {
      stopped = !tooBig;
      ::kill(child, SIGKILL);
      ::waitpid(child, &status, 0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
  }
  char rest[512];
  const ssize_t n = ::read(errPipe[0], rest, sizeof rest);
  if (n > 0 && errText.size() < 2000) {
    errText.append(rest, static_cast<size_t>(n));
  }
  ::close(errPipe[0]);
  std::error_code ec;
  if (WIFEXITED(status) && WEXITSTATUS(status) == 63) {
    tooBig = true;
  }
  if (tooBig) {
    error = "the download is larger than expected";
  } else if (stopped) {
    error = "stopped";
  } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return true;
  } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
    error = "downloading needs the program \"curl\" (it comes with macOS; on Linux install it with your package manager)";
  } else {
    while (!errText.empty() && (errText.back() == '\n' || errText.back() == '\r')) {
      errText.pop_back();
    }
    const size_t newline = errText.find('\n');
    if (newline != std::string::npos) {
      errText.resize(newline);
    }
    error = errText.empty() ? "the download failed" : errText;
  }
  fs::remove(file, ec);
  return false;
}

}

bool FetchToFile(const std::string& url, const fs::path& file, const FetchOptions& options, std::string& error) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path part = fs::path(file).concat(".download");
  fs::remove(part, ec);
  fs::remove(file, ec);
  bool ok;
  fs::path local;
  if (FilePathOf(url, local)) {
    ok = CopyLimited(local, part, options.maxBytes, options, error);
  } else if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
    ok = Curl(url, part, options, options.maxBytes, error);
  } else {
    error = "the address isn't one this program downloads from";
    return false;
  }
  if (!ok) {
    fs::remove(part, ec);
    return false;
  }
  fs::rename(part, file, ec);
  if (ec) {
    fs::remove(part, ec);
    error = "the download couldn't be saved: " + ec.message();
    return false;
  }
  return true;
}

bool FetchToString(const std::string& url, const FetchOptions& options, std::string& out, std::string& error) {
  out.clear();
  const fs::path temp = TempFile();
  FetchOptions small = options;
  bool ok;
  fs::path local;
  if (FilePathOf(url, local)) {
    ok = CopyLimited(local, temp, options.maxBytes, small, error);
  } else if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
    ok = Curl(url, temp, small, options.maxBytes, error);
  } else {
    error = "the address isn't one this program downloads from";
    return false;
  }
  std::error_code ec;
  if (ok) {
    std::ifstream in(temp, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  fs::remove(temp, ec);
  return ok;
}

}
