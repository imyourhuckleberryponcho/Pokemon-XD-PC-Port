#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pbr_batch.h"
#include "pbr_pack.h"
#include "port_disc.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
namespace {
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  static volatile LONG once = 0;
  if (InterlockedExchange(&once, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* er = info->ExceptionRecord;
  const unsigned char* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
  size_t image = 0;
  if (base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    image = nt->OptionalHeader.SizeOfImage;
  }
  char line[2048];
  size_t n = 0;
  const unsigned char* at = static_cast<const unsigned char*>(er->ExceptionAddress);
  const bool inside = base && at >= base && at < base + image;
  n += snprintf(line + n, sizeof line - n, "error The converter crashed (exception 0x%08lX at %s+0x%llX", static_cast<unsigned long>(er->ExceptionCode),
                inside ? "pbr_models.exe" : "another module", static_cast<unsigned long long>(inside ? at - base : reinterpret_cast<size_t>(at)));
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    n += snprintf(line + n, sizeof line - n, ", %s address 0x%llX", er->ExceptionInformation[0] == 1 ? "writing" : er->ExceptionInformation[0] == 8 ? "running" : "reading",
                  static_cast<unsigned long long>(er->ExceptionInformation[1]));
  n += snprintf(line + n, sizeof line - n, "; build %s; calls:", pbr::CONVERTER_VERSION);
  if (base) {
    const ULONG_PTR* sp = reinterpret_cast<const ULONG_PTR*>(info->ContextRecord->Rsp);
    int found = 0;
    __try {
      for (int i = 0; i < 4096 && found < 24; i++) {
        const unsigned char* v = reinterpret_cast<const unsigned char*>(sp[i]);
        if (v > base + 5 && v < base + image) {
          n += snprintf(line + n, sizeof line - n, " 0x%llX", static_cast<unsigned long long>(v - base));
          found++;
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  n += snprintf(line + n, sizeof line - n, ")\n");
  fputs(line, stdout);
  fflush(stdout);
  return EXCEPTION_CONTINUE_SEARCH;
}
}
#endif

namespace {

class PortSource : public pbr::DiscSource {
 public:
  explicit PortSource(std::string path) : path_(std::move(path)) {}
  ~PortSource() override {
    if (current_ == this) {
      PortDisc_Close();
      current_ = nullptr;
    }
  }
  void open_first(const char* what) {
    char err[512];
    if (!PortDisc_Open(path_.c_str(), err, sizeof err)) {
      current_ = nullptr;
      std::string e = err;
      while (!e.empty() && isspace((unsigned char)e.back())) e.pop_back();
      throw pbr::Fail(path_ + " cannot be read as a disc image (" + e + ").", 2);
    }
    current_ = this;
    (void)what;
    id_ = PortDisc_GameId();
    PortDisc_ListFiles(
        [](const char* p, uint32_t size, void* user) { static_cast<PortSource*>(user)->list_.emplace_back(p, size); }, this);
  }
  std::string game_id() const override { return id_; }
  void activate() const override { ensure_open(); }
  std::string main_dol_sha1() const override {
    ensure_open();
    size_t n = 0;
    const uint8_t* dol = PortDisc_Dol(&n);
    if (!dol || !n) return std::string();
    char hex[41];
    PortSha1Hex(dol, n, hex);
    return hex;
  }
  std::vector<std::pair<std::string, uint32_t>> files() const override { return list_; }
  std::vector<uint8_t> read(const std::string& path) const override {
    ensure_open();
    size_t n = 0;
    uint8_t* d = PortDisc_ReadOriginalFile(path.c_str(), &n);
    if (!d) throw std::runtime_error("read " + path);
    std::vector<uint8_t> v(d, d + n);
    PortDisc_Free(d);
    return v;
  }

 private:
  void ensure_open() const {
    if (current_ == this) return;
    char err[512];
    if (!PortDisc_Open(path_.c_str(), err, sizeof err)) throw std::runtime_error(std::string("cannot open the disc again: ") + err);
    current_ = const_cast<PortSource*>(this);
  }
  std::string path_, id_;
  std::vector<std::pair<std::string, uint32_t>> list_;
  static PortSource* current_;
};
PortSource* PortSource::current_ = nullptr;

}

#ifdef PBR_WITH_XD_TABLES
std::unique_ptr<pbr::XdTables> pbr_make_xd_tables(const pbr::DiscSource& xd_disc);
#endif

int run_main(const std::vector<std::string>& args) {
  pbr::DiscOpener opener = [](const std::string& path, const char* what) -> std::unique_ptr<pbr::DiscSource> {
    auto s = std::make_unique<PortSource>(path);
    s->open_first(what);
    return s;
  };
  pbr::XdTablesFactory tables;
#ifdef PBR_WITH_XD_TABLES
  tables = [](const pbr::DiscSource& xd) { return pbr_make_xd_tables(xd); };
#endif
  return pbr::pbr_models_main(args, opener, tables);
}

#ifdef _WIN32
static std::string utf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
int wmain(int argc, wchar_t** argv) {
  SetUnhandledExceptionFilter(CrashFilter);
  if (const char* t = std::getenv("PBR_MODELS_TEST_CRASH"); t && t[0] == '1') *reinterpret_cast<volatile int*>(0x100008f) = 1;
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(utf8(argv[i]));
  return run_main(args);
}
#else
int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(argv[i]);
  return run_main(args);
}
#endif
