#include <cstdint>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <vector>

#include <xxhash.h>

#include "gfx/hash.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what, size_t len, size_t off) {
  if (!ok && failures++ < 20) std::printf("FAIL %s len %zu offset %zu\n", what, len, off);
}
}

int main() {
  std::vector<uint8_t> buf(1 << 21);
  uint64_t x = 0x9E3779B97F4A7C15ull;
  for (auto& b : buf) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    b = static_cast<uint8_t>(x >> 24);
  }
  std::vector<size_t> lens;
  for (size_t n = 0; n <= 6000; ++n) lens.push_back(n);
  for (size_t n : {8191u, 8192u, 65535u, 65536u, 100000u, 1u << 20, (1u << 20) + 37}) lens.push_back(n);
  const uint64_t seeds[] = {0, 1, 0xDEADBEEFCAFEF00Dull};
  size_t checked = 0;
  for (size_t len : lens) {
    for (size_t off : {0u, 1u, 7u, 33u}) {
      const uint8_t* p = buf.data() + off;
      for (uint64_t seed : seeds) {
        check(aurora::xxh3_hash_s(p, len, seed) == XXH3_64bits_withSeed(p, len, seed), "64 seeded", len, off);
        ++checked;
      }
      const XXH128_hash_t a = aurora::xxh3_128(p, len), b = XXH3_128bits(p, len);
      check(a.low64 == b.low64 && a.high64 == b.high64, "128", len, off);
      if (len % 17 == 0 || len > 5000) {
        aurora::Hasher h(7);
        XXH3_state_t st;
        XXH3_INITSTATE(&st);
        XXH3_64bits_reset_withSeed(&st, 7);
        size_t done = 0, step = 1 + (len % 251);
        while (done < len) {
          const size_t n = std::min(step, len - done);
          h.update(p + done, n);
          XXH3_64bits_update(&st, p + done, n);
          done += n;
        }
        check(h.digest() == XXH3_64bits_digest(&st), "streaming", len, off);
      }
    }
  }
#ifdef AURORA_XXH_DISPATCH
  std::printf("dispatch on: best implementation = %d (0 scalar, 1 SSE2, 2 AVX2, 3 AVX512)\n", XXH_featureTest());
#else
  std::printf("dispatch off (not x86_64): the plain functions against themselves\n");
#endif
  for (size_t block : {size_t(1024), size_t(4096), size_t(8192), size_t(16384), size_t(65536), size_t(262144)}) {
    const int reps = static_cast<int>((size_t(1) << 30) / block);
    uint64_t sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) sink += XXH3_128bits(buf.data() + (i & 15) * 64, block).low64;
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) sink += aurora::xxh3_128(buf.data() + (i & 15) * 64, block).low64;
    auto t2 = std::chrono::steady_clock::now();
    const double a = std::chrono::duration<double>(t1 - t0).count(), b = std::chrono::duration<double>(t2 - t1).count();
    std::printf("128-bit hash of %zu byte blocks: plain %.1f GB/s, aurora's %.1f GB/s (%.2fx) [%llu]\n", block, 1.0 / a, 1.0 / b,
                a / b, static_cast<unsigned long long>(sink & 1));
  }
  std::printf("%zu hashes compared, %d failures\n", checked, failures);
  return failures == 0 ? 0 : 1;
}
