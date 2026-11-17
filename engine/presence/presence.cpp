#include "presence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace engine::presence {

using Clock = std::chrono::steady_clock;

std::string EscapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (const unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  return o;
}

std::string Clean(const std::string& s, size_t maxBytes) {
  std::string o;
  for (const unsigned char c : s) o += (c < 0x20 || c == 0x7f) ? ' ' : static_cast<char>(c);
  if (o.size() > maxBytes) {
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(o[n]) & 0xC0) == 0x80) n--;
    o.resize(n);
  }
  return o;
}

std::string BuildHandshake(const std::string& clientId) {
  return "{\"v\":1,\"client_id\":\"" + EscapeJson(clientId) + "\"}";
}

static std::string Str(const char* key, const std::string& v) {
  return std::string("\"") + key + "\":\"" + EscapeJson(v) + "\"";
}

std::string BuildSetActivity(const Activity& a, uint32_t pid, uint32_t nonce) {
  std::string act = "{";
  bool first = true;
  auto add = [&](const std::string& piece) {
    if (!first) act += ",";
    act += piece;
    first = false;
  };
  if (!a.details.empty()) add(Str("details", Clean(a.details)));
  if (!a.state.empty()) add(Str("state", Clean(a.state)));
  if (a.startSeconds > 0) add("\"timestamps\":{\"start\":" + std::to_string(a.startSeconds) + "}");
  if (!a.largeImage.empty() || !a.smallImage.empty()) {
    std::string as = "\"assets\":{";
    bool f = true;
    auto addA = [&](const char* k, const std::string& v) {
      if (v.empty()) return;
      if (!f) as += ",";
      as += Str(k, Clean(v));
      f = false;
    };
    addA("large_image", a.largeImage);
    addA("large_text", a.largeText);
    addA("small_image", a.smallImage);
    addA("small_text", a.smallText);
    as += "}";
    add(as);
  }
  act += "}";
  return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":" + act +
         "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
}

std::string BuildClearActivity(uint32_t pid, uint32_t nonce) {
  return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + "},\"nonce\":\"" +
         std::to_string(nonce) + "\"}";
}

namespace {

constexpr uint32_t kOpHandshake = 0, kOpFrame = 1, kOpClose = 2, kOpPing = 3, kOpPong = 4;
constexpr uint32_t kMaxPayload = 64 * 1024;

#ifdef _WIN32
class Link {
 public:
  ~Link() { Close(); }
  bool Open(const std::string& path) {
    for (int attempt = 0; attempt < 3; attempt++) {
      h_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (h_ != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY) break;
      WaitNamedPipeA(path.c_str(), 300);
    }
    if (h_ == INVALID_HANDLE_VALUE) {
      lastError_ = GetLastError();
      h_ = nullptr;
      return false;
    }
    ev_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (ev_ == nullptr) {
      Close();
      return false;
    }
    return true;
  }
  void Close() {
    if (h_ != nullptr) {
      CancelIoEx(h_, nullptr);
      CloseHandle(h_);
      h_ = nullptr;
    }
    if (ev_ != nullptr) {
      CloseHandle(ev_);
      ev_ = nullptr;
    }
  }
  bool IsOpen() const { return h_ != nullptr; }
  int Write(const void* data, size_t n, int timeoutMs) { return Io(true, const_cast<void*>(data), n, timeoutMs); }
  int Read(void* data, size_t n, int timeoutMs) { return Io(false, data, n, timeoutMs); }

 private:
  int Io(bool write, void* data, size_t n, int timeoutMs) {
    if (h_ == nullptr) return -1;
    if (!write && timeoutMs == 0) {
      DWORD avail = 0;
      if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) return -1;
      if (avail == 0) return 0;
    }
    OVERLAPPED ov{};
    ov.hEvent = ev_;
    ResetEvent(ev_);
    DWORD done = 0;
    BOOL ok = write ? WriteFile(h_, data, static_cast<DWORD>(n), &done, &ov)
                    : ReadFile(h_, data, static_cast<DWORD>(n), &done, &ov);
    if (!ok) {
      if (GetLastError() != ERROR_IO_PENDING) return -1;
      const DWORD w = WaitForSingleObject(ev_, static_cast<DWORD>(timeoutMs < 0 ? 0 : timeoutMs));
      if (w != WAIT_OBJECT_0) {
        CancelIoEx(h_, &ov);
        GetOverlappedResult(h_, &ov, &done, TRUE);
        return done > 0 ? static_cast<int>(done) : 0;
      }
      if (!GetOverlappedResult(h_, &ov, &done, FALSE)) return -1;
    }
    return done > 0 ? static_cast<int>(done) : -1;
  }
  HANDLE h_ = nullptr, ev_ = nullptr;
 public:
  unsigned long lastError_ = 0;
};

std::vector<std::string> Candidates(const std::string& override) {
  std::vector<std::string> v;
  if (!override.empty()) {
    v.push_back(override.rfind("\\\\", 0) == 0 ? override : "\\\\.\\pipe\\" + override);
    return v;
  }
  for (int i = 0; i < 10; i++) v.push_back("\\\\.\\pipe\\discord-ipc-" + std::to_string(i));
  return v;
}
uint32_t CurrentPid() { return GetCurrentProcessId(); }
#else
class Link {
 public:
  ~Link() { Close(); }
  bool Open(const std::string& path) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) return false;
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    ::fcntl(fd_, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
      lastError_ = static_cast<unsigned long>(errno);
      Close();
      return false;
    }
    return true;
  }
  void Close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }
  bool IsOpen() const { return fd_ >= 0; }
  int Write(const void* data, size_t n, int timeoutMs) {
    if (fd_ < 0) return -1;
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    ssize_t r = ::send(fd_, data, n, flags);
    if (r >= 0) return static_cast<int>(r);
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
    pollfd p{fd_, POLLOUT, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return 0;
    r = ::send(fd_, data, n, flags);
    return r >= 0 ? static_cast<int>(r) : ((errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1);
  }
  int Read(void* data, size_t n, int timeoutMs) {
    if (fd_ < 0) return -1;
    pollfd p{fd_, POLLIN, 0};
    const int pr = ::poll(&p, 1, timeoutMs);
    if (pr == 0) return 0;
    if (pr < 0) return errno == EINTR ? 0 : -1;
    const ssize_t r = ::recv(fd_, data, n, 0);
    if (r > 0) return static_cast<int>(r);
    if (r == 0) return -1;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
  }

 private:
  int fd_ = -1;
 public:
  unsigned long lastError_ = 0;
};

std::vector<std::string> Candidates(const std::string& override) {
  std::vector<std::string> v;
  if (!override.empty()) {
    if (override.find('/') != std::string::npos) {
      v.push_back(override);
    } else {
      const char* t = std::getenv("TMPDIR");
      v.push_back(std::string(t != nullptr && t[0] != '\0' ? t : "/tmp") + "/" + override);
    }
    return v;
  }
  std::vector<std::string> dirs;
  auto addDir = [&](const char* d) {
    if (d == nullptr || d[0] == '\0') return;
    std::string s = d;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    for (const std::string& e : dirs)
      if (e == s) return;
    dirs.push_back(s);
  };
  addDir(std::getenv("XDG_RUNTIME_DIR"));
  addDir(std::getenv("TMPDIR"));
  addDir(std::getenv("TMP"));
  addDir(std::getenv("TEMP"));
  addDir("/tmp");
  for (const std::string& d : dirs) {
    const std::vector<std::string> bases{d, d + "/snap.discord", d + "/snap.discord-canary",
                                         d + "/app/com.discordapp.Discord", d + "/app/com.discordapp.DiscordCanary",
                                         d + "/app/com.discordapp.DiscordPTB"};
    for (const std::string& b : bases)
      for (int i = 0; i < 10; i++) v.push_back(b + "/discord-ipc-" + std::to_string(i));
  }
  return v;
}
uint32_t CurrentPid() { return static_cast<uint32_t>(::getpid()); }
#endif

void Put32(uint8_t* p, uint32_t v) {
  p[0] = v & 255;
  p[1] = (v >> 8) & 255;
  p[2] = (v >> 16) & 255;
  p[3] = (v >> 24) & 255;
}
uint32_t Get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

}

struct Client::Impl {
  Config cfg;
  std::mutex m;
  std::condition_variable cv;
  bool stop = false, finished = false;
  std::optional<Activity> desired;
  std::atomic<bool> connected{false};
  std::thread th;

  Link link;
  bool sentValid = false;
  std::optional<Activity> sent;
  Clock::time_point lastSend{}, nextTry{};
  bool everSent = false;
  int fails = 0;
  uint32_t nonce = 0;

  void Log(const char* what, const std::string& more = {}) const {
    if (cfg.log) std::fprintf(stderr, "[presence] %s%s%s\n", what, more.empty() ? "" : ": ", more.c_str());
  }

  bool WriteAll(const uint8_t* d, size_t n) {
    const auto end = Clock::now() + std::chrono::milliseconds(cfg.ioTimeoutMs);
    size_t done = 0;
    while (done < n) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
      if (left <= 0) return false;
      const int r = link.Write(d + done, n - done, static_cast<int>(left));
      if (r < 0) return false;
      done += static_cast<size_t>(r);
    }
    return true;
  }
  bool ReadAll(uint8_t* d, size_t n, int timeoutMs) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t done = 0;
    while (done < n) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
      if (left <= 0) return false;
      const int r = link.Read(d + done, n - done, static_cast<int>(left));
      if (r < 0) return false;
      done += static_cast<size_t>(r);
    }
    return true;
  }
  bool SendFrame(uint32_t op, const std::string& json) {
    std::vector<uint8_t> b(8 + json.size());
    Put32(b.data(), op);
    Put32(b.data() + 4, static_cast<uint32_t>(json.size()));
    std::memcpy(b.data() + 8, json.data(), json.size());
    return WriteAll(b.data(), b.size());
  }
  int ReadFrame(uint32_t& op, std::string& body, int waitMs) {
    uint8_t h[8];
    const int r = link.Read(h, 1, waitMs);
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (!ReadAll(h + 1, 7, cfg.ioTimeoutMs)) return -1;
    op = Get32(h);
    const uint32_t len = Get32(h + 4);
    if (len > kMaxPayload) return -1;
    body.assign(len, '\0');
    if (len > 0 && !ReadAll(reinterpret_cast<uint8_t*>(&body[0]), len, cfg.ioTimeoutMs)) return -1;
    return 1;
  }
  void Drop(const char* why) {
    link.Close();
    connected = false;
    sentValid = false;
    sent.reset();
    Log("disconnected", why);
  }

  bool TryConnect() {
    for (const std::string& path : Candidates(cfg.endpointOverride)) {
      if (!link.Open(path)) {
        if (cfg.log && !cfg.endpointOverride.empty()) Log("cannot open", path + " (error " + std::to_string(link.lastError_) + ")");
        continue;
      }
      Log("connecting", path);
      if (SendFrame(kOpHandshake, BuildHandshake(cfg.clientId))) {
        const auto end = Clock::now() + std::chrono::milliseconds(cfg.ioTimeoutMs);
        while (Clock::now() < end) {
          uint32_t op = 0;
          std::string body;
          const int r = ReadFrame(op, body, cfg.ioTimeoutMs);
          if (r <= 0) break;
          if (op == kOpClose) break;
          if (op == kOpFrame && body.find("READY") != std::string::npos) {
            connected = true;
            sentValid = false;
            Log("ready");
            return true;
          }
        }
      }
      link.Close();
    }
    return false;
  }

  void Pump() {
    for (int i = 0; i < 8; i++) {
      uint32_t op = 0;
      std::string body;
      const int r = ReadFrame(op, body, 0);
      if (r == 0) return;
      if (r < 0 || op == kOpClose) {
        Drop("closed by the other side");
        return;
      }
      if (op == kOpPing && !SendFrame(kOpPong, body)) {
        Drop("write failed");
        return;
      }
    }
  }

  bool Send(const std::optional<Activity>& a) {
    const std::string json = a ? BuildSetActivity(*a, CurrentPid(), ++nonce) : BuildClearActivity(CurrentPid(), ++nonce);
    if (!SendFrame(kOpFrame, json)) {
      Drop("write failed");
      return false;
    }
    sent = a;
    sentValid = true;
    lastSend = Clock::now();
    everSent = true;
    Log(a ? "activity sent" : "activity cleared", a ? a->details + " / " + a->state : std::string());
    uint32_t op = 0;
    std::string body;
    for (int i = 0; i < 4; i++) {
      const int r = ReadFrame(op, body, i == 0 ? cfg.ioTimeoutMs : 0);
      if (r == 0) break;
      if (r < 0 || op == kOpClose) {
        Drop("closed after a send");
        return false;
      }
      if (op == kOpPing) SendFrame(kOpPong, body);
      if (op == kOpFrame && body.find("\"evt\":\"ERROR\"") != std::string::npos) Log("refused", body.substr(0, 200));
      if (op == kOpFrame && body.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos) break;
    }
    return true;
  }

  void Run() {
    for (;;) {
      std::optional<Activity> want;
      bool stopping;
      {
        std::unique_lock<std::mutex> lk(m);
        if (!stop) cv.wait_for(lk, std::chrono::milliseconds(200));
        stopping = stop;
        want = desired;
      }
      const auto now = Clock::now();
      if (stopping) {
        if (connected && link.IsOpen()) Send(std::nullopt);
        link.Close();
        connected = false;
        break;
      }
      if (!connected) {
        if (now >= nextTry) {
          if (TryConnect()) {
            fails = 0;
          } else {
            fails++;
            const int ms = fails >= cfg.maxQuickRetries ? cfg.retryLongIntervalMs : cfg.retryIntervalMs;
            nextTry = Clock::now() + std::chrono::milliseconds(ms);
            if (fails == 1) Log("no chat program found; trying again later");
          }
        }
        if (!connected) continue;
      }
      Pump();
      if (!connected) {
        nextTry = Clock::now() + std::chrono::milliseconds(cfg.retryIntervalMs < 3000 ? cfg.retryIntervalMs : 3000);
        continue;
      }
      const bool differs = !sentValid ? want.has_value() : (sent != want);
      const bool allowed = !everSent || now - lastSend >= std::chrono::milliseconds(cfg.minIntervalMs);
      if (differs && allowed) Send(want);
    }
    std::lock_guard<std::mutex> lk(m);
    finished = true;
    cv.notify_all();
  }
};

Client::Client() = default;
Client::~Client() { Stop(); }

void Client::Start(const Config& config) {
  if (impl_) return;
  try {
    auto impl = std::make_shared<Impl>();
    impl->cfg = config;
    impl->th = std::thread([impl] {
      try {
        impl->Run();
      } catch (...) {
        std::lock_guard<std::mutex> lk(impl->m);
        impl->finished = true;
        impl->cv.notify_all();
      }
    });
    impl_ = impl;
  } catch (...) {
    impl_.reset();
  }
}

void Client::Set(const Activity& a) {
  if (!impl_) return;
  std::lock_guard<std::mutex> lk(impl_->m);
  impl_->desired = a;
}

void Client::Clear() {
  if (!impl_) return;
  std::lock_guard<std::mutex> lk(impl_->m);
  impl_->desired.reset();
}

void Client::Stop() {
  if (!impl_) return;
  std::shared_ptr<Impl> impl = std::move(impl_);
  impl_.reset();
  bool done;
  {
    std::unique_lock<std::mutex> lk(impl->m);
    impl->stop = true;
    impl->cv.notify_all();
    done = impl->cv.wait_for(lk, std::chrono::milliseconds(impl->cfg.stopWaitMs), [&] { return impl->finished; });
  }
  if (impl->th.joinable()) {
    if (done) impl->th.join();
    else impl->th.detach();
  }
}

bool Client::Running() const { return impl_ != nullptr; }
bool Client::Connected() const { return impl_ && impl_->connected.load(); }

}
