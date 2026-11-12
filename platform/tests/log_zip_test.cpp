





#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <port_log.h>
#include <port_zip.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static std::string ReadAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

static int Child(const char* how) {
  if (!PortLog_Start("LogZipTest", "test", 2)) {
    return 10;
  }
  std::printf("out line 1\n");
  std::fprintf(stderr, "err line 1\n");
  DWORD done = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "handle line\n", 12, &done, nullptr);


  PortLog_Flush();
  std::printf("out line 2\n");
  if (std::strcmp(how, "tail") == 0) {
    const fs::path tail = fs::path(_wgetenv(L"APPDATA")) / L"tail.txt";
    std::printf("%s\n", PortLog_CopyTail(tail.c_str(), 16) ? "tail ok" : "tail failed");
  }
  if (std::strcmp(how, "exit") == 0) {
    PortLog_Flush();
    std::_Exit(0);
  }
  return 0;
}


static bool RunChild(const fs::path& dir, const char* how, std::string* out, std::string* err, bool same = false) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  const fs::path outPath = dir / L"out.txt", errPath = dir / L"err.txt";
  HANDLE hOut = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
  HANDLE hErr = same ? hOut : CreateFileW(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(exe) + L"\" child " + std::wstring(how, how + std::strlen(how));
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = hOut;
  si.hStdError = hErr;
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  CloseHandle(hOut);
  if (!same) CloseHandle(hErr);
  if (!started) {
    return false;
  }
  WaitForSingleObject(pi.hProcess, 20000);
  DWORD code = 99;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  *out = ReadAll(outPath);
  *err = same ? std::string() : ReadAll(errPath);
  return code == 0;
}

static std::vector<fs::path> Logs(const fs::path& dir) {
  std::vector<fs::path> logs;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir / L"LogZipTest" / L"logs", ec)) {
    logs.push_back(e.path());
  }
  std::sort(logs.begin(), logs.end());
  return logs;
}

static void TestLog() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path dir = fs::path(temp) / (L"log_zip_test_" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  SetEnvironmentVariableW(L"APPDATA", dir.c_str());

  const std::string expectOut = "out line 1\r\nhandle line\nout line 2\r\n";
  const std::string expectErr = "err line 1\r\n";
  for (const char* how : {"return", "exit"}) {
    std::string out, err;
    CHECK(RunChild(dir, how, &out, &err));
    CHECK(out == expectOut);
    CHECK(err == expectErr);
    if (out != expectOut || err != expectErr) {
      std::fprintf(stderr, "  (%s) stdout [%s] stderr [%s]\n", how, out.c_str(), err.c_str());
    }
    const std::vector<fs::path> logs = Logs(dir);
    CHECK(!logs.empty());
    if (!logs.empty()) {
      const std::string log = ReadAll(logs.back());
      for (const char* line : {"out line 1", "handle line", "out line 2", "err line 1"}) {
        CHECK(log.find(line) != std::string::npos);
      }
    }
    Sleep(1100);
  }
  std::string out, err;
  CHECK(RunChild(dir, "return", &out, &err, true));
  CHECK(out == "out line 1\r\nerr line 1\r\nhandle line\nout line 2\r\n");
  if (out != "out line 1\r\nerr line 1\r\nhandle line\nout line 2\r\n") {
    std::fprintf(stderr, "  (one file) [%s]\n", out.c_str());
  }
  CHECK(RunChild(dir, "tail", &out, &err));
  CHECK(out.find("tail ok") != std::string::npos);
  const std::string tail = ReadAll(dir / L"tail.txt");
  CHECK(tail.rfind("(the start of this log is left out", 0) == 0);
  CHECK(tail.find("out line 2") != std::string::npos);
  CHECK(tail.find("out line 1") == std::string::npos);
  CHECK(Logs(dir).size() == 2);
  fs::remove_all(dir, ec);
}

static uint32_t Get16(const std::string& s, size_t at) {
  return static_cast<uint8_t>(s[at]) | (static_cast<uint8_t>(s[at + 1]) << 8);
}
static uint32_t Get32(const std::string& s, size_t at) { return Get16(s, at) | (Get16(s, at + 2) << 16); }

static uint32_t Crc(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (char ch : data) {
    crc ^= static_cast<uint8_t>(ch);
    for (int k = 0; k < 8; k++) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

static void TestZip() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path dir = fs::path(temp) / (L"zip_test_" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir / L"folder" / L"sub");

  const std::string a = "hello\r\n", b(70000, 'x'), c(5000, 'p');
  std::ofstream(dir / L"folder" / L"a.txt", std::ios::binary) << a;
  std::ofstream(dir / L"folder" / L"b.bin", std::ios::binary) << b;
  std::ofstream(dir / L"folder" / L"c.png", std::ios::binary) << c;
  std::ofstream(dir / L"folder" / L"sub" / L"skipped.txt", std::ios::binary) << "not zipped";
  const fs::path zip = dir / L"folder.zip";
  CHECK(PortZip_StoreFolder((dir / L"folder").c_str(), zip.c_str()));
  const std::string z = ReadAll(zip);
  CHECK(z.size() > 22);
  if (z.size() <= 22) return;
  const size_t end = z.size() - 22;
  CHECK(Get32(z, end) == 0x06054b50);
  CHECK(Get16(z, end + 10) == 3);
  size_t at = Get32(z, end + 16);
  const std::string names[] = {"a.txt", "b.bin", "c.png"}, contents[] = {a, b, c};
  const uint32_t methods[] = {0, 8, 0};
  for (int i = 0; i < 3; i++) {
    CHECK(Get32(z, at) == 0x02014b50);
    const uint32_t method = Get16(z, at + 10), crc = Get32(z, at + 16), packed = Get32(z, at + 20);
    const uint32_t size = Get32(z, at + 24), nameLen = Get16(z, at + 28);
    const uint32_t local = Get32(z, at + 42);
    CHECK(z.substr(at + 46, nameLen) == names[i]);
    CHECK(method == methods[i]);
    CHECK(size == contents[i].size());
    CHECK(crc == Crc(contents[i]));
    CHECK(Get32(z, local) == 0x04034b50);
    CHECK(Get16(z, local + 8) == method && Get32(z, local + 18) == packed && Get32(z, local + 22) == size);
    const size_t data = local + 30 + Get16(z, local + 26) + Get16(z, local + 28);
    std::string body = z.substr(data, packed);
    if (method == 8) {
      CHECK(packed < size / 10);
      std::string plain(size, '\0');
      z_stream s{};
      inflateInit2(&s, -MAX_WBITS);
      s.next_in = reinterpret_cast<Bytef*>(body.data());
      s.avail_in = static_cast<uInt>(body.size());
      s.next_out = reinterpret_cast<Bytef*>(plain.data());
      s.avail_out = static_cast<uInt>(plain.size());
      CHECK(inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == size);
      inflateEnd(&s);
      body = plain;
    } else {
      CHECK(packed == size);
    }
    CHECK(body == contents[i]);
    at += 46 + nameLen;
  }
  CHECK(at == end);
  fs::remove_all(dir, ec);
}

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "child") == 0) {
    return Child(argv[2]);
  }
  TestLog();
  TestZip();
  std::printf(sFailures == 0 ? "log_zip_test: all passed\n" : "log_zip_test: %d FAILED\n", sFailures);
  return sFailures == 0 ? 0 : 1;
}
