#include <port_addr_window.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <process.h>
#endif

static bool ok = true;
static void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

int g_testGlobal = 7;
static void testFunction() {}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "truncate") == 0) {
#ifdef _WIN32
    _putenv("XD_WINDOW_STRICT=1");
#else
    setenv("XD_WINDOW_STRICT", "1", 1);
#endif
    PortWin_Init();
    void* p = std::malloc(256u << 20);
    std::fprintf(stderr, "about to store %p\n", p);
    const uint32_t v = PortWin_Off(p);
    std::printf("NOT STOPPED: value 0x%x\n", v);
    return 0;
  }
  check(PortWin_Init() == 1, "the window is reserved");
  check(PortWin_Active() == 1, "it is active");
  check(g_portWinBase > 0xFFFFFFFFull, "the window starts above 4 GB");
  check(PortWin_Ptr(0) == nullptr && PortWin_Off(nullptr) == 0, "0 is NULL both ways");
  check(PortWin_Ptr(0x3ACF) == reinterpret_cast<void*>(0x3ACF) && PortWin_Off(reinterpret_cast<void*>(0x3ACF)) == 0x3ACF,
        "a number kept in a pointer (below 0x1000000) stays a number both ways");
  const uint32_t values[] = {0x70000000u, 0x80000000u, 0x80001800u, 0x817FFFFFu, 0xC0000000u, 0xE0000000u, 0x90000010u};
  bool round = true;
  for (uint32_t v : values) {
    void* p = PortWin_Ptr(v);
    round = round && reinterpret_cast<uintptr_t>(p) == g_portWinBase + v && PortWin_Off(p) == v;
  }
  check(round, "value -> pointer -> value for the game's address ranges (window start + value)");
  const uint32_t g = PortWin_Off(&g_testGlobal);
  check(g - PORT_WIN_IMG_LO < PORT_WIN_IMG_SIZE && PortWin_Ptr(g) == &g_testGlobal, "a global of the program image round trips (image range)");
  const uint32_t f = PortWin_Off(reinterpret_cast<void*>(&testFunction));
  check(f - PORT_WIN_IMG_LO < PORT_WIN_IMG_SIZE && PortWin_Ptr(f) == reinterpret_cast<void*>(&testFunction), "a function round trips too");
  void* h = PortWin_Malloc(100);
  check(h != nullptr && PortWin_Owns(h) && PortWin_Off(h) >= 0x90000000u, "the window heap hands out blocks inside the window");
  std::memset(h, 0xAB, 100);
  void* h2 = PortWin_Realloc(h, 5000);
  check(h2 != nullptr && static_cast<unsigned char*>(h2)[99] == 0xAB, "realloc keeps the contents");
  PortWin_Free(h2);
  void* h3 = PortWin_Calloc(10, 10);
  check(h3 != nullptr && static_cast<unsigned char*>(h3)[99] == 0, "calloc is zeroed");
  PortWin_Free(h3);
  std::string cmd = std::string("\"") + argv[0] + "\" truncate > nul 2>&1";
#ifndef _WIN32
  cmd = std::string("\"") + argv[0] + "\" truncate > /dev/null 2>&1";
#endif
  const int rc = std::system(cmd.c_str());
  check(rc != 0, "an address outside the window and the image stops at once (child exit code non-zero)");
  return ok ? 0 : 1;
}
