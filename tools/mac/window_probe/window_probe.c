#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <sys/sysctl.h>
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define GiB ((uint64_t)1 << 30)
#define MiB ((uint64_t)1 << 20)

#if defined(__APPLE__) && defined(PROBE_SEG_BYTES)
#define XSTR(x) #x
#define STR(x) XSTR(x)
__asm__(".zerofill __XDBIG,__reserve,_xd_probe_big," STR(PROBE_SEG_BYTES) ",14\n"
        ".no_dead_strip _xd_probe_big\n");
extern char xd_probe_big[] __asm__("_xd_probe_big");
#endif

static FILE* sOut;
static int sFact[4];

static void say(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  fputs(buf, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  if (sOut) {
    fputs(buf, sOut);
    fputc('\n', sOut);
    fflush(sOut);
  }
}

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void* reserve(void* hint, uint64_t size, int* err) {
  void* p = mmap(hint, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  *err = (p == MAP_FAILED) ? errno : 0;
  return p;
}

static int touch(volatile uint8_t* p, uint64_t len, uint8_t seed) {
  p[0] = seed;
  p[len - 1] = (uint8_t)(seed + 1);
  p[len / 2] = (uint8_t)(seed + 2);
  return p[0] == seed && p[len - 1] == (uint8_t)(seed + 1) && p[len / 2] == (uint8_t)(seed + 2);
}

static void fact1(void) {
  say("");
  say("== FACT 1: one 4 GB block, pieces of it usable, a second view of the same memory ==");
  int err;
  double t0 = now_ms();
  uint8_t* base = reserve(NULL, 4 * GiB, &err);
  double t1 = now_ms();
  if (base == MAP_FAILED) {
    say("1a  reserve 4 GB (kernel's choice, no access, no swap reserved): FAILED, errno %d (%s)", err, strerror(err));
    sFact[1] = 0;
    return;
  }
  say("1a  reserve 4 GB (kernel's choice): OK at %p in %.2f ms; 4 GB aligned: %s", (void*)base, t1 - t0,
      ((uintptr_t)base & 0xFFFFFFFFu) == 0 ? "yes" : "no");
  say("    (low 32 bits of the base: 0x%x)", (unsigned)((uintptr_t)base & 0xFFFFFFFFu));
  int ok = 1;
  uint64_t mem1 = 0x80000000u, mem1Size = 24 * MiB;
  void* p = mmap(base + mem1, mem1Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  int b = p == (void*)(base + mem1) && touch(base + mem1, mem1Size, 10);
  say("1b  MAP_FIXED a 24 MB read/write piece at base+0x80000000 inside the block, write and read back: %s", b ? "OK" : "FAILED");
  ok = ok && b;
  int c = mprotect(base + 0x70000000u, 64 * MiB, PROT_READ | PROT_WRITE) == 0 && touch(base + 0x70000000u, 64 * MiB, 20);
  say("1c  mprotect 64 MB of the reserve read/write at base+0x70000000, write and read back: %s", c ? "OK" : "FAILED");
  ok = ok && c;
  int d = mprotect(base + 4 * GiB - 16384, 16384, PROT_READ | PROT_WRITE) == 0 && touch(base + 4 * GiB - 16384, 16384, 30);
  say("1d  the last 16 KB of the block (offset 0xFFFFC000) usable: %s", d ? "OK" : "FAILED");
  ok = ok && d;
  char name[40];
  snprintf(name, sizeof name, "/xdprobe%d", (int)getpid());
  int e = 0, fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd >= 0) {
    shm_unlink(name);
    if (ftruncate(fd, (off_t)mem1Size) == 0) {
      uint8_t* v1 = mmap(base + 0x84000000u, mem1Size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
      uint8_t* v2 = mmap(base + 0xC4000000u, mem1Size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
      if (v1 == (void*)(base + 0x84000000u) && v2 == (void*)(base + 0xC4000000u)) {
        v1[100] = 0x5A;
        e = v2[100] == 0x5A;
        v2[200] = 0xA5;
        e = e && v1[200] == 0xA5;
      } else {
        say("    shared views failed: errno %d (%s)", errno, strerror(errno));
      }
    } else {
      say("    ftruncate of the shared object failed: errno %d (%s)", errno, strerror(errno));
    }
    close(fd);
  } else {
    say("    shm_open failed: errno %d (%s)", errno, strerror(errno));
  }
  say("1e  one shared-memory object mapped twice (MAP_SHARED|MAP_FIXED) inside the block; a write through one is seen through the other: %s", e ? "OK" : "FAILED");
  int alias = e;
#ifdef __APPLE__
  {
    mach_vm_address_t target = (mach_vm_address_t)(uintptr_t)(base + 0xC8000000u);
    vm_prot_t cur, max;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &target, mem1Size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                                     (mach_vm_address_t)(uintptr_t)(base + mem1), FALSE, &cur, &max, VM_INHERIT_NONE);
    int f = 0;
    if (kr == KERN_SUCCESS && target == (mach_vm_address_t)(uintptr_t)(base + 0xC8000000u)) {
      uint8_t* src = base + mem1;
      uint8_t* dst = (uint8_t*)(uintptr_t)target;
      src[300] = 0x3C;
      f = dst[300] == 0x3C;
      dst[400] = 0xC3;
      f = f && src[400] == 0xC3;
    }
    say("1f  mach_vm_remap of the 24 MB piece to a second address inside the block, kr=%d: %s", (int)kr, f ? "OK" : "FAILED");
    alias = alias || f;
  }
#endif
  sFact[1] = ok && alias;
  say("FACT 1: %s", sFact[1] ? "YES (reserve, pieces, second view all work)" : "NO (see the lines above)");
  munmap(base, 4 * GiB);
}

static void fact2(void) {
  say("");
  say("== FACT 2: is a high address hint honoured for a 4 GB block? ==");
  static const uint64_t hints[] = {0x2000000000ull, 0x8000000000ull, 0x10000000000ull, 0x100000000000ull, 0x200000000000ull};
  int honoured = 0;
  int first = 0;
  for (unsigned i = 0; i < sizeof hints / sizeof hints[0]; i++) {
    int err;
    void* p = reserve((void*)(uintptr_t)hints[i], 4 * GiB, &err);
    if (p == MAP_FAILED) {
      say("2.%u hint 0x%llx: mmap FAILED errno %d (%s)", i + 1, (unsigned long long)hints[i], err, strerror(err));
      continue;
    }
    int same = (uint64_t)(uintptr_t)p == hints[i];
    say("2.%u hint 0x%llx: answer %p -> %s", i + 1, (unsigned long long)hints[i], p, same ? "honoured exactly" : "moved by the system");
    honoured += same;
    if (i == 0) {
      first = same;
    }
    munmap(p, 4 * GiB);
  }
  {
    int err;
    void* p = reserve(NULL, 8 * GiB, &err);
    if (p == MAP_FAILED) {
      say("2b 8 GB without a hint: mmap FAILED errno %d (%s)", err, strerror(err));
    } else {
      uint64_t a = ((uint64_t)(uintptr_t)p + 4 * GiB - 1) & ~(4 * GiB - 1);
      say("2b 8 GB without a hint at %p: the 4 GB-aligned block inside starts at 0x%llx (%s)", p, (unsigned long long)a,
          a + 4 * GiB <= (uint64_t)(uintptr_t)p + 8 * GiB ? "fits" : "does not fit");
      munmap(p, 8 * GiB);
    }
  }
  sFact[2] = honoured > 0;
  say("FACT 2: %s (%d of %u hints honoured exactly; the plan's own, 0x20_0000_0000: %s)", honoured > 0 ? "YES" : "NO", honoured,
      (unsigned)(sizeof hints / sizeof hints[0]), first ? "honoured" : "not honoured");
}

static void fact3(void) {
  say("");
  say("== FACT 3: a huge zero-fill segment in a position independent arm64 program ==");
#if defined(__APPLE__) && defined(PROBE_SEG_BYTES)
  {
    uint64_t want = (uint64_t)(PROBE_SEG_BYTES);
    uintptr_t a = (uintptr_t)xd_probe_big;
    say("3a  this program was linked with a __XDBIG segment of 0x%llx bytes (%llu MB): it linked and started", (unsigned long long)want,
        (unsigned long long)(want / MiB));
    say("3b  the segment starts at %p (%s 4 GB)", (void*)a, a < 0x100000000ull ? "BELOW" : "above");
    int ok = touch((volatile uint8_t*)xd_probe_big, want, 40);
    say("3c  first, middle and last byte of the segment written and read back: %s", ok ? "OK" : "FAILED");
    say("3d  the program image: header at %p, flags 0x%x, position independent (MH_PIE): %s", (void*)&_mh_execute_header,
        (unsigned)_mh_execute_header.flags, (_mh_execute_header.flags & 0x200000) ? "yes" : "no");
    sFact[3] = ok;
    say("FACT 3 (this variant, %llu MB): %s", (unsigned long long)(want / MiB), ok ? "YES" : "NO");
  }
#else
  sFact[3] = -1;
  say("not asked by this build: it has no zero-fill segment (use window_probe_seg512 / window_probe_seg3g / window_probe_segfix)");
#endif
}

int main(int argc, char** argv) {
  (void)argc;
  sOut = fopen("window_probe_result.txt", "w");
  say("window_probe: %s", argv[0]);
#ifdef __APPLE__
  {
    char buf[256] = "?";
    size_t n = sizeof buf;
    sysctlbyname("kern.osproductversion", buf, &n, NULL, 0);
    char model[128] = "?";
    n = sizeof model;
    sysctlbyname("hw.model", model, &n, NULL, 0);
    char brand[128] = "?";
    n = sizeof brand;
    sysctlbyname("machdep.cpu.brand_string", brand, &n, NULL, 0);
    int translated = 0;
    n = sizeof translated;
    sysctlbyname("sysctl.proc_translated", &translated, &n, NULL, 0);
    say("macOS %s, model %s, cpu %s, running translated by Rosetta: %s", buf, model, brand, translated ? "YES (not a native arm64 run)" : "no");
  }
#else
  say("(not macOS: this is the code's own test; facts 1 and 2 only)");
#endif
#if defined(__aarch64__) || defined(__arm64__)
  say("this program is arm64 (native)");
#else
  say("this program is NOT arm64: answers for arm64 need an arm64 build on an Apple silicon Mac");
#endif
  fact1();
  fact2();
  fact3();
  say("");
  say("== SUMMARY ==");
  say("FACT 1 (4 GB reserve, pieces, second view): %s", sFact[1] ? "YES" : "NO");
  say("FACT 2 (high hint honoured):                %s", sFact[2] ? "YES" : "NO");
  say("FACT 3 (huge zero-fill segment):            %s", sFact[3] < 0 ? "not asked by this build" : sFact[3] ? "YES" : "NO");
  if (sOut) {
    fclose(sOut);
  }
  return 0;
}
