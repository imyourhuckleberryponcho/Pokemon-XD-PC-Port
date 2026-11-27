#ifdef __ANDROID__
#include "port_android_layout.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/system_properties.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

typedef struct Range { uintptr_t s, e; } Range;
#define MAX_OCC 6000
#define MAX_GAPS 64
#define UNIT ((uintptr_t)0x10000)
#define PAGE_ALIGN ((uintptr_t)0x10000)

static Range sOcc[MAX_OCC];
static unsigned sOccCount;
static char sReport[1024];
static size_t sReportLen;

static pthread_once_t sOnce = PTHREAD_ONCE_INIT;
static uintptr_t sStackBase, sStackSize;
static uintptr_t sLowBase, sLowSize;
static Range sArena[16];
static unsigned sArenaCount;
static pthread_mutex_t sStackLock = PTHREAD_MUTEX_INITIALIZER;
#define MAX_UNITS 16384
static unsigned char sUsed[MAX_UNITS];

static void report(const char* text) {
  size_t n = strlen(text);
  if (sReportLen + n + 2 >= sizeof(sReport)) return;
  memcpy(sReport + sReportLen, text, n);
  sReportLen += n;
  sReport[sReportLen++] = ';';
  sReport[sReportLen] = 0;
}
static void report_range(const char* what, uintptr_t s, uintptr_t e) {
  char b[96];
  snprintf(b, sizeof(b), "%s %08lx-%08lx", what, (unsigned long)s, (unsigned long)e);
  report(b);
}

static void occ_add(uintptr_t s, uintptr_t e) {
  unsigned i;
  if (e <= s) return;
  for (i = 0; i < sOccCount; ++i) {
    if (s <= sOcc[i].e && e >= sOcc[i].s) {
      if (s < sOcc[i].s) sOcc[i].s = s;
      if (e > sOcc[i].e) sOcc[i].e = e;
      while (i + 1 < sOccCount && sOcc[i + 1].s <= sOcc[i].e) {
        if (sOcc[i + 1].e > sOcc[i].e) sOcc[i].e = sOcc[i + 1].e;
        memmove(&sOcc[i + 1], &sOcc[i + 2], (sOccCount - i - 2) * sizeof(Range));
        --sOccCount;
      }
      return;
    }
    if (e < sOcc[i].s) break;
  }
  if (sOccCount >= MAX_OCC) return;
  memmove(&sOcc[i + 1], &sOcc[i], (sOccCount - i) * sizeof(Range));
  sOcc[i].s = s;
  sOcc[i].e = e;
  ++sOccCount;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static void read_maps(void) {
  static char buf[1 << 16];
  const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  size_t have = 0;
  if (fd < 0) return;
  for (;;) {
    const ssize_t got = read(fd, buf + have, sizeof(buf) - 1 - have);
    char* line = buf;
    char* end;
    if (got < 0 && errno == EINTR) continue;
    if (got > 0) have += (size_t)got;
    buf[have] = 0;
    while ((end = memchr(line, '\n', have - (size_t)(line - buf))) != NULL) {
      unsigned long long a = 0, b = 0;
      const char* p = line;
      int v;
      while ((v = hexval(*p)) >= 0) a = a * 16 + (unsigned)v, ++p;
      if (*p == '-') ++p;
      while ((v = hexval(*p)) >= 0) b = b * 16 + (unsigned)v, ++p;
      if (a < 0x100000000ull) occ_add((uintptr_t)a, (uintptr_t)(b > 0x100000000ull ? 0x100000000ull : b));
      line = end + 1;
    }
    have -= (size_t)(line - buf);
    memmove(buf, line, have);
    if (got <= 0) break;
  }
  close(fd);
}

static unsigned gaps(Range* out, unsigned max, uintptr_t lo, uintptr_t hi, uintptr_t minSize) {
  unsigned n = 0, i;
  uintptr_t cursor = lo;
  for (i = 0; i <= sOccCount && n < max; ++i) {
    uintptr_t gs = cursor;
    uintptr_t ge = i < sOccCount ? sOcc[i].s : (uintptr_t)0xFFFFFFFFu;
    if (i < sOccCount && sOcc[i].e > cursor) cursor = sOcc[i].e;
    if (i == sOccCount) ge = hi;
    if (gs < lo) gs = lo;
    if (ge > hi) ge = hi;
    gs = (gs + PAGE_ALIGN - 1) & ~(PAGE_ALIGN - 1);
    ge &= ~(PAGE_ALIGN - 1);
    if (ge > gs && ge - gs >= minSize) {
      out[n].s = gs;
      out[n].e = ge;
      ++n;
    }
  }
  return n;
}

static int reserve(uintptr_t at, uintptr_t size) {
  void* p = mmap((void*)at, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == (void*)at) return 1;
  if (p != MAP_FAILED) munmap(p, size);
  return 0;
}

static uintptr_t take_best_in(uintptr_t size, const char* what, uintptr_t lo, uintptr_t hi) {
  Range g[MAX_GAPS];
  const unsigned n = gaps(g, MAX_GAPS, lo, hi, size);
  for (;;) {
    unsigned i, best = n;
    for (i = 0; i < n; ++i) {
      if (g[i].e > g[i].s && g[i].e - g[i].s >= size && (best == n || g[i].e - g[i].s < g[best].e - g[best].s)) best = i;
    }
    if (best == n) return 0;
    if (reserve(g[best].s, size)) {
      report_range(what, g[best].s, g[best].s + size);
      occ_add(g[best].s, g[best].s + size);
      return g[best].s;
    }
    g[best].e = g[best].s;
  }
}
static uintptr_t take_best(uintptr_t size, const char* what) { return take_best_in(size, what, 0x02000000, 0x80000000); }

static void layout_once(void) {
  Range g[MAX_GAPS];
  unsigned i, n;
  uintptr_t size;
  read_maps();
  occ_add(0, 0x02000000);
  occ_add(0x80000000u, 0x80000000u + 0x02000000u);
  occ_add(0xC0000000u, 0xC0000000u + 0x02000000u);
  occ_add(0xE0000000u, 0xE0000000u + 0x00010000u);
  for (size = (uintptr_t)384 << 20; size >= ((uintptr_t)128 << 20) && sStackBase == 0; size -= (uintptr_t)128 << 20) {
    sStackBase = take_best(size, "stacks");
    if (sStackBase != 0) sStackSize = size;
  }
  sLowBase = take_best((uintptr_t)256 << 20, "low block");
  sLowSize = (uintptr_t)256 << 20;
  if (sLowBase == 0) sLowBase = take_best_in((uintptr_t)256 << 20, "low block (above 2 GB)", 0x82000000u, 0xC0000000u);
  for (size = (uintptr_t)192 << 20; sLowBase == 0 && size >= ((uintptr_t)96 << 20); size -= (uintptr_t)32 << 20) {
    sLowBase = take_best(size, "low block (smaller)");
    if (sLowBase != 0) sLowSize = size;
  }
  if (sLowBase == 0) sLowSize = 0;
  if (sStackBase == 0) report("NO STACK RANGE");
  if (sLowBase == 0) report("NO LOW BLOCK");
  n = gaps(g, MAX_GAPS, 0x02000000, 0xE0000000u, (uintptr_t)16 << 20);
  for (i = 0; i < n && sArenaCount < 16; ++i) {
    sArena[sArenaCount++] = g[i];
    report_range("arena", g[i].s, g[i].e);
  }
}

void PortAndroid_LayoutInit(void) { pthread_once(&sOnce, layout_once); }

uintptr_t PortAndroid_LowBlockBase(void) {
  PortAndroid_LayoutInit();
  return sLowBase;
}

uintptr_t PortAndroid_LowBlockSize(void) {
  PortAndroid_LayoutInit();
  return sLowSize;
}

const char* PortAndroid_LargestGaps(void) {
  static char out[240];
  Range g[MAX_GAPS];
  unsigned n, i, k;
  size_t used = 0;
  PortAndroid_LayoutInit();
  n = gaps(g, MAX_GAPS, 0x02000000, 0x80000000, (uintptr_t)8 << 20);
  out[0] = 0;
  for (k = 0; k < 5; ++k) {
    unsigned best = n;
    for (i = 0; i < n; ++i) {
      if (g[i].e > g[i].s && (best == n || g[i].e - g[i].s > g[best].e - g[best].s)) best = i;
    }
    if (best == n) break;
    used += (size_t)snprintf(out + used, sizeof(out) - used, "%s%lu MB at %lx", k ? ", " : "", (unsigned long)((g[best].e - g[best].s) >> 20),
                             (unsigned long)g[best].s);
    g[best].e = g[best].s;
  }
  return out;
}

int PortAndroid_InStackRange(const void* p) {
  return sStackBase != 0 && (uintptr_t)p >= sStackBase && (uintptr_t)p < sStackBase + sStackSize;
}

void* PortAndroid_StackTake(size_t len, int prot) {
  const unsigned n = (unsigned)((len + UNIT - 1) / UNIT);
  unsigned i, run = 0, first = 0, total;
  PortAndroid_LayoutInit();
  total = (unsigned)(sStackSize / UNIT);
  if (sStackBase == 0 || n == 0 || n > total || total > MAX_UNITS) return MAP_FAILED;
  pthread_mutex_lock(&sStackLock);
  for (i = 0; i < total; ++i) {
    run = sUsed[i] ? 0 : run + 1;
    if (run == n) {
      first = i + 2 - n;
      break;
    }
  }
  for (i = 0; first != 0 && i < n; ++i) sUsed[first - 1 + i] = 1;
  pthread_mutex_unlock(&sStackLock);
  if (first == 0) return MAP_FAILED;
  return mmap((void*)(sStackBase + (uintptr_t)(first - 1) * UNIT), (size_t)n * UNIT, prot,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
}

int PortAndroid_StackGive(void* addr, size_t len) {
  const unsigned n = (unsigned)((len + UNIT - 1) / UNIT);
  unsigned first, i;
  if (!PortAndroid_InStackRange(addr) || ((uintptr_t)addr & (UNIT - 1)) != 0) return -1;
  first = (unsigned)(((uintptr_t)addr - sStackBase) / UNIT);
  if (first + n > sStackSize / UNIT) return -1;
  mmap(addr, (size_t)n * UNIT, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
  pthread_mutex_lock(&sStackLock);
  for (i = 0; i < n; ++i) sUsed[first + i] = 0;
  pthread_mutex_unlock(&sStackLock);
  return 0;
}

unsigned PortAndroid_ArenaWindows(uintptr_t (*out)[2], unsigned max) {
  unsigned i;
  PortAndroid_LayoutInit();
  for (i = 0; i < sArenaCount && i < max; ++i) {
    out[i][0] = sArena[i].s;
    out[i][1] = sArena[i].e;
  }
  return i;
}

const char* PortAndroid_LayoutReport(void) {
  PortAndroid_LayoutInit();
  return sReport;
}

int PortAndroid_FixedRangeReserved(uintptr_t at) {
  const char* list = getenv("XD_ANDROID_FIXED");
  char want[16];
  const char* hit;
  if (list == NULL) return 0;
  snprintf(want, sizeof(want), "%lx", (unsigned long)at);
  for (hit = strstr(list, want); hit != NULL; hit = strstr(hit + 1, want)) {
    const char after = hit[strlen(want)];
    if ((hit == list || hit[-1] == ',') && (after == 0 || after == ',')) return 1;
  }
  return 0;
}

static void owners_of(uintptr_t lo, uintptr_t hi, char* out, size_t size) {
  FILE* f = fopen("/proc/self/maps", "r");
  char line[512];
  size_t used = strlen(out);
  if (f == NULL) return;
  while (fgets(line, sizeof(line), f) != NULL) {
    unsigned long long a = 0, b = 0;
    if (sscanf(line, "%llx-%llx", &a, &b) == 2 && a < hi && b > lo) {
      size_t n = strlen(line);
      while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
      {
        size_t r, w = 0;
        for (r = 0; r < n; ++r) {
          if (line[r] == ' ' && w > 0 && line[w - 1] == ' ') continue;
          line[w++] = line[r];
        }
        line[w] = 0;
        n = w;
      }
      if (used + n + 4 < size) {
        memcpy(out + used, line, n);
        used += n;
        out[used++] = '|';
        out[used] = 0;
      }
    }
  }
  fclose(f);
}

const char* PortAndroid_RangeOwners(uintptr_t at, uintptr_t size) {
  static char out[700];
  out[0] = 0;
  owners_of(at, at + size, out, sizeof(out));
  return out;
}

const char* PortAndroid_FixedRangeOwners(void) {
  static char out[1200];
  out[0] = 0;
  owners_of(0x80000000u, 0x80000000u + 0x01800000u, out, sizeof(out));
  owners_of(0xC0000000u, 0xC0000000u + 0x01800000u, out, sizeof(out));
  owners_of(0xE0000000u, 0xE0000000u + 0x4000u, out, sizeof(out));
  return out;
}

void PortAndroid_WriteStartFailure(const char* folder, const char* message) {
  char model[96] = "", maker[96] = "", release[96] = "", sdk[96] = "", abi[96] = "", fp[200] = "", hw[96] = "", kernel[200] = "";
  const char* early = getenv("XD_ANDROID_EARLY_TAKEN");
  const char* around = getenv("XD_ANDROID_EARLY_AROUND");
  char dir[1300], path[1400];
  time_t now = time(NULL);
  struct tm tmv;
  FILE* f;
  FILE* maps;
  char line[600];
  __system_property_get("ro.product.model", model);
  __system_property_get("ro.product.manufacturer", maker);
  __system_property_get("ro.build.version.release", release);
  __system_property_get("ro.build.version.sdk", sdk);
  __system_property_get("ro.product.cpu.abi", abi);
  __system_property_get("ro.build.fingerprint", fp);
  __system_property_get("ro.hardware", hw);
  {
    struct utsname un;
    if (uname(&un) == 0) snprintf(kernel, sizeof(kernel), "%s %s %s\n", un.sysname, un.release, un.machine);
  }
  localtime_r(&now, &tmv);
  fprintf(stderr, "[android] START FAILURE: %s\n", message);
  fprintf(stderr, "[android] device: %s %s, Android %s (API %s), %s, hardware %s, page size %ld, build %s\n", maker, model, release, sdk, abi,
          hw, (long)sysconf(_SC_PAGESIZE), fp);
  fprintf(stderr, "[android] kernel: %s", kernel);
  fprintf(stderr, "[android] on the game's fixed ranges now: %s\n", PortAndroid_FixedRangeOwners());
  fprintf(stderr, "[android] on them when the library loaded: %s\n", early != NULL && early[0] ? early : "(they were free; taken since)");
  fprintf(stderr, "[android] fixed ranges reserved at load: %s\n", getenv("XD_ANDROID_FIXED") != NULL ? getenv("XD_ANDROID_FIXED") : "(none)");
  fprintf(stderr, "[android] around them at load: %s\n", around != NULL ? around : "");
  maps = fopen("/proc/self/maps", "r");
  fprintf(stderr, "[android] ---- /proc/self/maps ----\n");
  while (maps != NULL && fgets(line, sizeof(line), maps) != NULL) fputs(line, stderr);
  fprintf(stderr, "[android] ---- end of maps ----\n");
  if (maps != NULL) fclose(maps);
  fflush(stderr);
  if (folder == NULL || folder[0] == 0) return;
  snprintf(dir, sizeof(dir), "%s/reports", folder);
  mkdir(dir, 0777);
  snprintf(dir, sizeof(dir), "%s/reports/report_%04d-%02d-%02d_%02d-%02d-%02d_stop", folder, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  if (mkdir(dir, 0777) != 0 && errno != EEXIST) return;
  snprintf(path, sizeof(path), "%s/info.txt", dir);
  if ((f = fopen(path, "w")) != NULL) {
    fprintf(f, "what happened: the game could not start: %s\n", message);
    fprintf(f, "device: %s %s\nandroid: %s (API %s)\nabi: %s\nhardware: %s\npage size: %ld\nbuild: %s\nkernel: %s", maker, model, release, sdk, abi, hw,
            (long)sysconf(_SC_PAGESIZE), fp, kernel);
    fprintf(f, "\non the game's fixed ranges now:\n%s\n", PortAndroid_FixedRangeOwners());
    fprintf(f, "\non them when the library loaded:\n%s\n", early != NULL && early[0] ? early : "(they were free; taken since)");
    fprintf(f, "\nfixed ranges reserved at load: %s\n", getenv("XD_ANDROID_FIXED") != NULL ? getenv("XD_ANDROID_FIXED") : "(none)");
    fprintf(f, "\nmappings 0x40000000-0xA0000000 at load:\n%s\n", around != NULL ? around : "");
    fclose(f);
  }
  snprintf(path, sizeof(path), "%s/maps.txt", dir);
  maps = fopen("/proc/self/maps", "r");
  if (maps != NULL && (f = fopen(path, "w")) != NULL) {
    while (fgets(line, sizeof(line), maps) != NULL) fputs(line, f);
    fclose(f);
  }
  if (maps != NULL) fclose(maps);
}
#endif
