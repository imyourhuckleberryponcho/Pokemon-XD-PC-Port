#ifndef PRESENCE_FAKE_SERVER_H
#define PRESENCE_FAKE_SERVER_H
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using Clock = std::chrono::steady_clock;
static void Sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class Conn {
 public:
  ~Conn() { Close(); }
#ifdef _WIN32
  HANDLE h = nullptr;
  HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  int Read(void* d, size_t n, int ms) {
    OVERLAPPED ov{};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD got = 0;
    if (!ReadFile(h, d, static_cast<DWORD>(n), &got, &ov)) {
      if (GetLastError() != ERROR_IO_PENDING) return -1;
      if (WaitForSingleObject(ev, ms) != WAIT_OBJECT_0) {
        CancelIoEx(h, &ov);
        GetOverlappedResult(h, &ov, &got, TRUE);
        return got > 0 ? static_cast<int>(got) : 0;
      }
      if (!GetOverlappedResult(h, &ov, &got, FALSE)) return -1;
    }
    return got > 0 ? static_cast<int>(got) : -1;
  }
  bool Write(const void* d, size_t n) {
    OVERLAPPED ov{};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD got = 0;
    if (!WriteFile(h, d, static_cast<DWORD>(n), &got, &ov)) {
      if (GetLastError() != ERROR_IO_PENDING) return false;
      if (WaitForSingleObject(ev, 2000) != WAIT_OBJECT_0) {
        CancelIoEx(h, &ov);
        return false;
      }
      if (!GetOverlappedResult(h, &ov, &got, FALSE)) return false;
    }
    return true;
  }
  void Close() {
    if (h != nullptr) {
      DisconnectNamedPipe(h);
      CloseHandle(h);
      h = nullptr;
    }
  }
#else
  int fd = -1;
  int Read(void* d, size_t n, int ms) {
    pollfd p{fd, POLLIN, 0};
    const int r = ::poll(&p, 1, ms);
    if (r == 0) return 0;
    if (r < 0) return -1;
    const ssize_t g = ::recv(fd, d, n, 0);
    return g > 0 ? static_cast<int>(g) : -1;
  }
  bool Write(const void* d, size_t n) { return ::send(fd, d, n, MSG_NOSIGNAL) == static_cast<ssize_t>(n); }
  void Close() {
    if (fd >= 0) ::close(fd);
    fd = -1;
  }
#endif
};

enum class Mode { Normal, Stall, DropAfterFirstSet };

class FakeServer {
 public:
  explicit FakeServer(Mode m = Mode::Normal) : mode(m) {
    static int counter = 0;
#ifdef _WIN32
    name = "xdpresence_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter);
#else
    name = "/tmp/xdpresence_test_" + std::to_string(getpid()) + "_" + std::to_string(++counter) + ".sock";
#endif
  }
  ~FakeServer() { Stop(); }
  void Start() {
    stop = false;
#ifndef _WIN32
    ::unlink(name.c_str());
    lfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, name.c_str(), sizeof a.sun_path - 1);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(lfd, 4) != 0) std::printf("fake server: bind failed\n");
#endif
    th = std::thread([this] { Loop(); });
  }
  void Stop() {
    stop = true;
    if (th.joinable()) th.join();
#ifndef _WIN32
    if (lfd >= 0) ::close(lfd);
    lfd = -1;
    ::unlink(name.c_str());
#endif
  }
  std::vector<std::pair<uint32_t, std::string>> Frames() {
    std::lock_guard<std::mutex> lk(m);
    return frames;
  }
  int Count(const std::string& needle) {
    int n = 0;
    for (auto& f : Frames())
      if (f.second.find(needle) != std::string::npos) n++;
    return n;
  }
  std::atomic<int> connections{0};
  std::atomic<Mode> mode;
  std::string name;

 private:
  bool Accept(Conn& c) {
#ifdef _WIN32
    HANDLE p = CreateNamedPipeA(("\\\\.\\pipe\\" + name).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
    if (p == INVALID_HANDLE_VALUE) {
      Sleep_ms(50);
      return false;
    }
    OVERLAPPED ov{};
    ov.hEvent = c.ev;
    ResetEvent(c.ev);
    if (!ConnectNamedPipe(p, &ov)) {
      const DWORD e = GetLastError();
      if (e == ERROR_IO_PENDING) {
        while (!stop) {
          if (WaitForSingleObject(c.ev, 50) == WAIT_OBJECT_0) break;
        }
        if (stop) {
          CancelIoEx(p, &ov);
          CloseHandle(p);
          return false;
        }
      } else if (e != ERROR_PIPE_CONNECTED) {
        CloseHandle(p);
        return false;
      }
    }
    c.h = p;
    return true;
#else
    pollfd p{lfd, POLLIN, 0};
    if (::poll(&p, 1, 50) <= 0) return false;
    c.fd = ::accept(lfd, nullptr, nullptr);
    return c.fd >= 0;
#endif
  }
  static void Put(uint8_t* p, uint32_t v) {
    p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255;
  }
  bool Reply(Conn& c, uint32_t op, const std::string& j) {
    std::vector<uint8_t> b(8 + j.size());
    Put(b.data(), op);
    Put(b.data() + 4, static_cast<uint32_t>(j.size()));
    std::memcpy(b.data() + 8, j.data(), j.size());
    return c.Write(b.data(), b.size());
  }
  bool ReadN(Conn& c, uint8_t* d, size_t n) {
    size_t got = 0;
    while (got < n && !stop) {
      const int r = c.Read(d + got, n - got, 100);
      if (r < 0) return false;
      got += static_cast<size_t>(r);
    }
    return got == n;
  }
  void Serve(Conn& c) {
    connections++;
    int sets = 0;
    while (!stop) {
      if (mode == Mode::Stall) {
        Sleep_ms(20);
        continue;
      }
      uint8_t h[8];
      if (!ReadN(c, h, 8)) return;
      const uint32_t op = h[0] | (h[1] << 8) | (h[2] << 16) | (static_cast<uint32_t>(h[3]) << 24);
      const uint32_t len = h[4] | (h[5] << 8) | (h[6] << 16) | (static_cast<uint32_t>(h[7]) << 24);
      std::string body(len, '\0');
      if (len > 0 && !ReadN(c, reinterpret_cast<uint8_t*>(&body[0]), len)) return;
      {
        std::lock_guard<std::mutex> lk(m);
        frames.emplace_back(op, body);
      }
      if (op == 0) {
        if (!Reply(c, 1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1}}")) return;
      } else if (op == 1) {
        sets++;
        if (!Reply(c, 1, "{\"cmd\":\"SET_ACTIVITY\",\"evt\":null,\"data\":{}}")) return;
        if (mode == Mode::DropAfterFirstSet && sets == 1) return;
      }
    }
  }
  void Loop() {
    std::vector<std::thread> servers;
    while (!stop) {
      auto c = std::make_shared<Conn>();
      if (!Accept(*c)) continue;
      servers.emplace_back([this, c] {
        Serve(*c);
        c->Close();
      });
    }
    for (auto& t : servers) t.join();
  }
  std::atomic<bool> stop{false};
  std::thread th;
  std::mutex m;
  std::vector<std::pair<uint32_t, std::string>> frames;
#ifndef _WIN32
  int lfd = -1;
#endif
};

#endif
