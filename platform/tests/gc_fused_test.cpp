#include "game/gc_fused.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

static uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static double from_bits(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

static double force25(double d) {
  uint64_t u = dbits(d);
  u = (u & 0xFFFFFFFFF8000000ULL) + (u & 0x8000000ULL);
  return from_bits(u);
}
static float g_fmadds(float a, float c, float b) { return (float)std::fma((double)a, force25(c), (double)b); }
static float g_fmsubs(float a, float c, float b) { return (float)std::fma((double)a, force25(c), -(double)b); }
static float g_fnmsubs(float a, float c, float b) { return -(float)std::fma((double)a, force25(c), -(double)b); }
static double g_fmadd(double a, double c, double b) { return std::fma(a, c, b); }
static double g_fnmsub(double a, double c, double b) { return -std::fma(a, c, -b); }

static int failures = 0;
static void expect_f(const char* what, float got, float want, float a, float b, float c) {
  if (fbits(got) != fbits(want)) {
    if (failures < 20)
      printf("FAIL %s(%a, %a, %a): got %a want %a\n", what, a, b, c, got, want);
    failures++;
  }
}
static void expect_d(const char* what, double got, double want, double a, double b, double c) {
  if (dbits(got) != dbits(want)) {
    if (failures < 20)
      printf("FAIL %s(%a, %a, %a): got %a want %a\n", what, a, b, c, got, want);
    failures++;
  }
}

static volatile float vsink;
static volatile double vdsink;

static void check_float(float a, float b, float c, long* plain_diff, long* fmaf_diff) {
  float add = GC_FUSED(a, b) + c;
  float add2 = c + GC_FUSED(a, b);
  float sub = GC_FUSED(a, b) - c;
  float subfrom = c - GC_FUSED(a, b);
  float acc = c;
  acc += GC_FUSED(a, b);
  float dec = c;
  dec -= GC_FUSED(a, b);
  expect_f("a*b+c", add, g_fmadds(a, b, c), a, b, c);
  expect_f("c+a*b", add2, g_fmadds(a, b, c), a, b, c);
  expect_f("a*b-c", sub, g_fmsubs(a, b, c), a, b, c);
  expect_f("c-a*b", subfrom, g_fnmsubs(a, b, c), a, b, c);
  expect_f("c+=a*b", acc, g_fmadds(a, b, c), a, b, c);
  expect_f("c-=a*b", dec, g_fnmsubs(a, b, c), a, b, c);
  volatile float va = a, vb = b, vc = c;
  float plain = va * vb + vc;
  if (fbits(plain) != fbits(g_fmadds(a, b, c))) (*plain_diff)++;
  if (fbits(std::fmaf(a, b, c)) != fbits(g_fmadds(a, b, c))) (*fmaf_diff)++;
}

extern "C" int gc_fused_c_check(float a, float b, float c, double da, double dc);

static void check_double(double a, double b, double c) {
  int bad = gc_fused_c_check((float)a, (float)b, (float)c, b, c);
  if (bad) {
    if (failures < 20) printf("FAIL C forms (%a, %a, %a): %d mismatches\n", a, b, c, bad);
    failures += bad;
  }
  expect_d("d a*b+c", GC_FUSED(a, b) + c, g_fmadd(a, b, c), a, b, c);
  expect_d("d c-a*b", c - GC_FUSED(a, b), g_fnmsub(a, b, c), a, b, c);
  float fa = (float)a;
  expect_d("f*d+c", GC_FUSED(fa, b) + c, g_fmadd((double)fa, b, c), fa, b, c);
  float fb = (float)b;
  double want = (double)(fa * fb) + c;
  expect_d("f*f+d", GC_FUSED(fa, fb) + c, want, fa, fb, c);
}

static void run(const char* label, long n, uint32_t seed) {
  std::mt19937_64 rng(seed);
  long plain_diff = 0, fmaf_diff = 0;
  std::uniform_real_distribution<float> uf(-1000.0f, 1000.0f);
  std::uniform_real_distribution<double> ud(-1000.0, 1000.0);
  for (long i = 0; i < n; i++) {
    float a, b, c;
    if (i & 1) {
      a = uf(rng); b = uf(rng) * 0.001f; c = uf(rng);
    } else {
      uint32_t x = (uint32_t)rng(), y = (uint32_t)rng(), z = (uint32_t)rng();
      x = (x & 0x8FFFFFFF) | 0x30000000; y = (y & 0x8FFFFFFF) | 0x30000000; z = (z & 0x8FFFFFFF) | 0x30000000;
      memcpy(&a, &x, 4); memcpy(&b, &y, 4); memcpy(&c, &z, 4);
    }
    check_float(a, b, c, &plain_diff, &fmaf_diff);
    if ((i & 7) == 0) check_double(ud(rng), ud(rng), ud(rng));
  }
  printf("%s: %ld random cases; plain a*b+c differs from the GameCube in %ld (%.3f%%), fmaf in %ld\n", label, n,
         plain_diff, 100.0 * plain_diff / n, fmaf_diff);
}

static void hard_cases() {
  long found = 0, tried = 0;
  std::mt19937_64 rng(7);
  for (long i = 0; i < 4000000 && found < 2000; i++) {
    float a = (float)(4097 + 2 * (rng() % 2048));
    float b = (float)(4097 + 2 * (rng() % 2048));
    double p = (double)a * (double)b;
    if ((double)(float)p == p) continue;
    float base = (float)p;
    double lo = (double)std::nextafter(base, 0.0f), hi = (double)std::nextafter(base, 1e30f);
    if (p - lo != (double)base - p && hi - p != p - (double)base) continue;
    tried++;
    float c = std::ldexp(1.0f, -30 + (int)(rng() % 8)) * ((rng() & 1) ? 1.0f : -1.0f);
    float model = g_fmadds(a, b, c);
    float once = std::fmaf(a, b, c);
    float hdr = GC_FUSED(a, b) + c;
    expect_f("hard a*b+c", hdr, model, a, b, c);
    if (fbits(model) != fbits(once)) found++;
  }
  printf("hard cases: %ld halfway products tried, in %ld the GameCube's double-then-float rounding differs from fmaf "
         "(single rounding); the header matched the GameCube model in all\n", tried, found);
  if (!found) {
    printf("FAIL: no hard case found (the test can't tell the two roundings apart)\n");
    failures++;
  }
}

static void timing() {
  const int N = 20000000;
  static float xs[1024], ys[1024], zs[1024];
  for (int i = 0; i < 1024; i++) { xs[i] = 1.0f + i * 0.37f; ys[i] = 0.001f * i; zs[i] = -3.0f + i; }
  auto t0 = std::chrono::steady_clock::now();
  float s = 0;
  for (int i = 0; i < N; i++) { int k = i & 1023; s = xs[k] * ys[k] + s * 0.5f; }
  auto t1 = std::chrono::steady_clock::now();
  float s2 = 0;
  for (int i = 0; i < N; i++) { int k = i & 1023; s2 = GC_FUSED(xs[k], ys[k]) + s2 * 0.5f; }
  auto t2 = std::chrono::steady_clock::now();
  double d = 0;
  for (int i = 0; i < N; i++) { int k = i & 1023; d = GC_FUSED((double)xs[k], (double)ys[k] + 1e-9) + d * 0.5; }
  auto t3 = std::chrono::steady_clock::now();
  vsink = s + s2; vdsink = d;
  auto ns = [](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count() / N; };
  printf("timing per operation: plain float a*b+c %.2f ns, GC_FUSED float %.2f ns, GC_FUSED double (fma) %.2f ns\n",
         ns(t0, t1), ns(t1, t2), ns(t2, t3));
}

int main() {
  volatile float one = 1.0f;
  float e = one + 1.0f / 4096.0f;
  float r = GC_FUSED(e, e) - (one + 1.0f / 2048.0f);
  expect_f("traps example", r, 1.0f / 16777216.0f, e, e, one + 1.0f / 2048.0f);
  float neg0 = 3.0f - GC_FUSED(one, 3.0f);
  if (fbits(neg0) != 0x80000000u) { printf("FAIL c - a*b with a*b == c: %a (want -0)\n", neg0); failures++; }
  float pos0 = GC_FUSED(one, 3.0f) - 3.0f;
  if (fbits(pos0) != 0u) { printf("FAIL a*b - c with a*b == c: %a (want +0)\n", pos0); failures++; }
  float plainprod = GC_FUSED(e, e);
  expect_f("plain use", plainprod, e * e, e, e, 0);
  run("FMA3 path", 3000000, 1);
#if defined(_MSC_VER) && defined(_M_X64)
  _set_FMA3_enable(0);
  run("software fma (no FMA3)", 1000000, 2);
  timing();
  _set_FMA3_enable(1);
  printf("with FMA3: ");
#endif
  timing();
  hard_cases();
  if (failures) {
    printf("gc_fused_test: %d FAILURES\n", failures);
    return 1;
  }
  printf("gc_fused_test: all passed\n");
  return 0;
}
