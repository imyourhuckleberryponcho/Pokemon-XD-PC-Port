#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
void* PortMem_LowAlloc(size_t size, size_t align);
bool PortMem_LowIsOrdinary(void);
}

static bool ok = true;
static void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

int main(int argc, char** argv) {
  const bool nolow = argc > 1 && std::strcmp(argv[1], "nolow") == 0;
  if (nolow) {
#ifdef _WIN32
    _putenv("PORT_TEST_NO_LOW_MEMORY=1");
#else
    setenv("PORT_TEST_NO_LOW_MEMORY", "1", 1);
#endif
  }
  struct Block { uint8_t* p; size_t n; size_t align; };
  std::vector<Block> blocks;
  const size_t sizes[] = {4, 16, 100, 4096, 70000, 1 << 20, 3 << 20, 0};
  const size_t aligns[] = {4, 16, 32, 64, 0x10000, 0};
  for (size_t s : sizes) {
    for (size_t a : aligns) {
      auto* p = static_cast<uint8_t*>(PortMem_LowAlloc(s, a));
      blocks.push_back({p, s ? s : 1, a ? a : 16});
    }
  }
  bool allThere = true, allAligned = true, allZero = true;
  for (const Block& b : blocks) {
    allThere &= b.p != nullptr;
    if (b.p == nullptr) continue;
    allAligned &= (reinterpret_cast<uintptr_t>(b.p) & (b.align - 1)) == 0;
    for (size_t i = 0; i < b.n; i++) allZero &= b.p[i] == 0;
    std::memset(b.p, 0xA5, b.n);
  }
  check(allThere, "every request is answered (no stop)");
  check(allAligned, "every block is aligned as asked");
  check(allZero, "every block is zeroed");
  bool overlap = false;
  for (size_t i = 0; i < blocks.size() && !overlap; i++) {
    for (size_t j = i + 1; j < blocks.size(); j++) {
      const auto a0 = reinterpret_cast<uintptr_t>(blocks[i].p), a1 = a0 + blocks[i].n;
      const auto b0 = reinterpret_cast<uintptr_t>(blocks[j].p), b1 = b0 + blocks[j].n;
      if (a0 < b1 && b0 < a1) { overlap = true; break; }
    }
  }
  check(!overlap, "no two blocks overlap");
  bool allLow = true;
  for (const Block& b : blocks) allLow &= reinterpret_cast<uintptr_t>(b.p) + b.n <= 0x100000000ull;
  std::printf("info: blocks %s 4 GB, ordinary memory flag: %d\n", allLow ? "below" : "NOT all below", PortMem_LowIsOrdinary() ? 1 : 0);
  if (nolow) {
    check(PortMem_LowIsOrdinary(), "no low memory: the blocks are flagged as ordinary memory");
  } else {
    check(PortMem_LowIsOrdinary() ? true : allLow, "low memory in use: every block is below 4 GB");
  }
  std::printf(ok ? "all passed\n" : "FAILED\n");
  return ok ? 0 : 1;
}
