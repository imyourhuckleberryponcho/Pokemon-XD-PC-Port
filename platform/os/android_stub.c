#ifdef __ANDROID__
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define TAG "XDstub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static void* sGame;
static int sFixedMissing;
#define WIN_LIB "libxdgame_win.so"

#define FIXED_COUNT 3
static const struct { uintptr_t at; size_t size; } kFixed[FIXED_COUNT] = {
    {0x80000000u, 0x02000000u}, {0xC0000000u, 0x02000000u}, {0xE0000000u, 0x00010000u}};

static void maps_lines(uintptr_t lo, uintptr_t hi, char* out, size_t size) {
  static char buf[1 << 16];
  const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  size_t have = 0, used = strlen(out);
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
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) a = a * 16 + (unsigned)v, ++p;
      if (*p == '-') ++p;
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) b = b * 16 + (unsigned)v, ++p;
      if (a < hi && b > lo) {
        const size_t n = (size_t)(end - line);
        if (used + n + 2 < size) {
          memcpy(out + used, line, n);
          used += n;
          out[used++] = '|';
          out[used] = 0;
        }
      }
      line = end + 1;
    }
    have -= (size_t)(line - buf);
    memmove(buf, line, have);
    if (got <= 0) break;
  }
  close(fd);
}

#define STEAL_MARGIN (48u << 20)

static int touched_pages(uintptr_t lo, uintptr_t hi, uintptr_t* highest) {
  static unsigned char vec[4096];
  static uint64_t ent[4096];
  const int pm = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
  int count = 0, usePagemap = pm >= 0;
  uintptr_t a;
  *highest = 0;
  for (a = lo; a < hi;) {
    size_t n = (hi - a) / 4096;
    size_t i;
    if (n > 4096) n = 4096;
    if (usePagemap && pread(pm, ent, n * 8, (off_t)((a / 4096) * 8)) != (ssize_t)(n * 8)) usePagemap = 0;
    if (!usePagemap && mincore((void*)a, n * 4096, vec) != 0) {
      if (pm >= 0) close(pm);
      return -1;
    }
    for (i = 0; i < n; ++i) {
      const int touched = usePagemap ? (int)(((ent[i] >> 63) & 1) | ((ent[i] >> 62) & 1)) : (vec[i] & 1);
      if (touched) {
        ++count;
        *highest = a + (i + 1) * 4096;
      }
    }
    a += n * 4096;
  }
  if (pm >= 0) close(pm);
  return count;
}

static int mapping_of(uintptr_t at, uintptr_t* start, uintptr_t* end, int* isLos) {
  static char buf[1 << 16];
  const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  size_t have = 0;
  int found = 0;
  if (fd < 0) return 0;
  for (;;) {
    const ssize_t got = read(fd, buf + have, sizeof(buf) - 1 - have);
    char* line = buf;
    char* e;
    if (got < 0 && errno == EINTR) continue;
    if (got > 0) have += (size_t)got;
    buf[have] = 0;
    while ((e = memchr(line, '\n', have - (size_t)(line - buf))) != NULL) {
      unsigned long long a = 0, b = 0;
      const char* p = line;
      int v;
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) a = a * 16 + (unsigned)v, ++p;
      if (*p == '-') ++p;
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) b = b * 16 + (unsigned)v, ++p;
      if (!found && a <= at && at < b) {
        char text[512];
        size_t n = (size_t)(e - line);
        if (n >= sizeof(text)) n = sizeof(text) - 1;
        memcpy(text, line, n);
        text[n] = 0;
        *start = (uintptr_t)a;
        *end = (uintptr_t)b;
        *isLos = strstr(text, "large object space") != NULL && strstr(text, " rw-p ") != NULL;
        found = 1;
      }
      line = e + 1;
    }
    have -= (size_t)(line - buf);
    memmove(buf, line, have);
    if (got <= 0) break;
  }
  close(fd);
  return found;
}

static int steal_from_art(uintptr_t at, size_t size, char* note, size_t noteSize) {
  uintptr_t lo = 0, hi = 0, usedEnd = 0, ignore;
  int los = 0, inside, below;
  if (!mapping_of(at, &lo, &hi, &los) || !los || at + size > hi) {
    snprintf(note, noteSize, "not taken over: the owner is not ART's large object space (or the range is not inside one mapping)");
    return 0;
  }
  below = touched_pages(lo, at & ~(uintptr_t)4095, &usedEnd);
  inside = touched_pages(at, at + size, &ignore);
  if (below < 0 || inside < 0) {
    snprintf(note, noteSize, "not taken over: cannot tell which pages ART has used (pagemap and mincore both refused)");
    return 0;
  }
  if (inside != 0 || (usedEnd != 0 && at - usedEnd < STEAL_MARGIN)) {
    snprintf(note, noteSize, "not taken over: ART's large objects reach %lx (used pages in the range: %d, room below it: %lu MB, need %u MB)",
             (unsigned long)usedEnd, inside, usedEnd != 0 && usedEnd < at ? (unsigned long)((at - usedEnd) >> 20) : 0ul, STEAL_MARGIN >> 20);
    return 0;
  }
  if (mmap((void*)at, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) != (void*)at) {
    snprintf(note, noteSize, "not taken over: mmap over it failed (%s)", strerror(errno));
    return 0;
  }
  snprintf(note, noteSize, "taken over from ART's large object space %lx-%lx: ART's objects end at %lx, %lu MB below our range; %lu MB of its tail above",
           (unsigned long)lo, (unsigned long)hi, (unsigned long)usedEnd, (unsigned long)((at - (usedEnd ? usedEnd : lo)) >> 20),
           (unsigned long)((hi - at - size) >> 20));
  return 1;
}

__attribute__((constructor(101))) static void reserve_fixed_ranges(void) {
  static char reserved[64], taken[3000], around[4000], takeover[1400];
  unsigned i;
  reserved[0] = 0;
  taken[0] = 0;
  around[0] = 0;
  takeover[0] = 0;
  if (access("/data/data/net.pokemonxdpc.port/files/xd_test_taken", F_OK) == 0) {
    void* t = mmap((void*)0x80400000u, 0x100000, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (t != MAP_FAILED) prctl(0x53564d41, 0, (unsigned long)t, 0x100000, (unsigned long)"dalvik-test-region-space");
  }
  if (access("/data/data/net.pokemonxdpc.port/files/xd_test_los", F_OK) == 0) {
    void* t = mmap((void*)0x78000000u, 0x20000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (t != MAP_FAILED) {
      memset(t, 1, 0x200000);
      if (access("/data/data/net.pokemonxdpc.port/files/xd_test_los_full", F_OK) == 0) memset((char*)t + 0x07F00000, 1, 0x100000);
      prctl(0x53564d41, 0, (unsigned long)t, 0x20000000, (unsigned long)"dalvik-free list large object space");
    }
  }
  for (i = 0; i < FIXED_COUNT; ++i) {
    void* p = mmap((void*)kFixed[i].at, kFixed[i].size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p == (void*)kFixed[i].at) {
      char one[16];
      snprintf(one, sizeof(one), "%s%lx", reserved[0] ? "," : "", (unsigned long)kFixed[i].at);
      strncat(reserved, one, sizeof(reserved) - strlen(reserved) - 1);
      LOGI("fixed range %lx + %lx reserved", (unsigned long)kFixed[i].at, (unsigned long)kFixed[i].size);
      continue;
    }
    if (p != MAP_FAILED) munmap(p, kFixed[i].size);
    if (p == MAP_FAILED) {
      char note[360], more[420];
      const int ok = steal_from_art(kFixed[i].at, kFixed[i].size, note, sizeof note);
      snprintf(more, sizeof more, "%lx: %s; ", (unsigned long)kFixed[i].at, note);
      strncat(takeover, more, sizeof(takeover) - strlen(takeover) - 1);
      if (ok) {
        char one[16];
        snprintf(one, sizeof(one), "%s%lx", reserved[0] ? "," : "", (unsigned long)kFixed[i].at);
        strncat(reserved, one, sizeof(reserved) - strlen(reserved) - 1);
        LOGI("fixed range %lx + %lx %s", (unsigned long)kFixed[i].at, (unsigned long)kFixed[i].size, note);
        continue;
      }
      LOGE("fixed range %lx: %s", (unsigned long)kFixed[i].at, note);
    }
    LOGE("fixed range %lx + %lx is TAKEN already (%s)", (unsigned long)kFixed[i].at, (unsigned long)kFixed[i].size,
         p == MAP_FAILED ? strerror(errno) : "placed elsewhere");
    sFixedMissing = 1;
    maps_lines(kFixed[i].at, kFixed[i].at + kFixed[i].size, taken, sizeof(taken));
  }
  setenv("XD_ANDROID_FIXED", reserved, 1);
  if (takeover[0]) setenv("XD_ANDROID_TAKEOVER", takeover, 1);
  if (taken[0]) {
    setenv("XD_ANDROID_EARLY_TAKEN", taken, 1);
    LOGE("covered by: %s", taken);
  }
  maps_lines(0x40000000u, 0xA0000000u, around, sizeof(around));
  setenv("XD_ANDROID_EARLY_AROUND", around, 1);
  LOGI("mappings 0x40000000-0xA0000000 at load: %s", around);
}

static size_t library_span(const char* path) {
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  Elf64_Ehdr eh;
  Elf64_Phdr ph[32];
  uint64_t lo = ~(uint64_t)0, hi = 0;
  unsigned i;
  if (fd < 0) return 0;
  if (pread(fd, &eh, sizeof(eh), 0) != (ssize_t)sizeof(eh) || eh.e_phnum == 0 || eh.e_phnum > 32 ||
      pread(fd, ph, eh.e_phnum * sizeof(Elf64_Phdr), (off_t)eh.e_phoff) != (ssize_t)(eh.e_phnum * sizeof(Elf64_Phdr))) {
    close(fd);
    return 0;
  }
  close(fd);
  for (i = 0; i < eh.e_phnum; ++i) {
    if (ph[i].p_type != PT_LOAD) continue;
    if (ph[i].p_vaddr < lo) lo = ph[i].p_vaddr;
    if (ph[i].p_vaddr + ph[i].p_memsz > hi) hi = ph[i].p_vaddr + ph[i].p_memsz;
  }
  if (hi <= lo) return 0;
  return (size_t)(((hi - (lo & ~(uint64_t)0xFFFF)) + 0xFFFF) & ~(uint64_t)0xFFFF);
}

static uintptr_t reserve_low(size_t size, uintptr_t limit) {
  static char buf[1 << 16];
  const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  uintptr_t bestStart = 0, bestEnd = 0, cursor = 0x02000000;
  size_t have = 0;
  if (fd < 0) return 0;
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
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) a = a * 16 + (unsigned)v, ++p;
      if (*p == '-') ++p;
      while ((v = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : -1) >= 0) b = b * 16 + (unsigned)v, ++p;
      if (a < (unsigned long long)limit) {
        uintptr_t gs = (cursor + 0xFFFF) & ~(uintptr_t)0xFFFF, ge = (uintptr_t)a & ~(uintptr_t)0xFFFF;
        if (ge > gs && ge - gs >= size && (bestEnd == 0 || ge - gs < bestEnd - bestStart)) {
          bestStart = gs;
          bestEnd = ge;
        }
      }
      if (b > cursor && b < 0x100000000ull) cursor = (uintptr_t)b;
      line = end + 1;
    }
    have -= (size_t)(line - buf);
    memmove(buf, line, have);
    if (got <= 0) break;
  }
  close(fd);
  {
    uintptr_t gs = (cursor + 0xFFFF) & ~(uintptr_t)0xFFFF, ge = limit;
    if (ge > gs && ge - gs >= size && (bestEnd == 0 || ge - gs < bestEnd - bestStart)) {
      bestStart = gs;
      bestEnd = ge;
    }
  }
  if (bestEnd == 0) return 0;
  {
    const uintptr_t at = bestStart;
    void* p = mmap((void*)at, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == (void*)at) return at;
    if (p != MAP_FAILED) munmap(p, size);
    return 0;
  }
}

static int load_game(void) {
  Dl_info info;
  char path[1024];
  char* slash;
  android_dlextinfo ext;
  size_t span;
  uintptr_t at;
  int useWindow = 0;
  if (sGame != NULL) return 1;
  if (dladdr((void*)&load_game, &info) == 0 || info.dli_fname == NULL) {
    LOGE("dladdr failed");
    return 0;
  }
  snprintf(path, sizeof(path), "%s", info.dli_fname);
  slash = strrchr(path, '/');
  if (slash == NULL) return 0;
  if (sFixedMissing) {
    snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), WIN_LIB);
    if (access(path, R_OK) == 0) {
      useWindow = 1;
      unsetenv("XD_ANDROID_FIXED");
      unsetenv("XD_ANDROID_TAKEOVER");
      unsetenv("XD_ANDROID_EARLY_TAKEN");
      setenv("XD_ANDROID_WINDOW", "1", 1);
      LOGE("LIBRARY CHOICE: the window library " WIN_LIB " (the fixed GameCube address ranges could not all be taken: see the 'fixed range' lines above)");
    } else {
      LOGE("LIBRARY CHOICE: the fixed ranges are not all ours and this app has no " WIN_LIB " (%s): the plain library, as before", strerror(errno));
    }
  } else {
    LOGI("LIBRARY CHOICE: the plain library libxdgame.so (the fixed GameCube address ranges are ours)");
  }
  if (!useWindow) snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "libxdgame.so");
  if (useWindow) {
    span = library_span(path);
    at = span != 0 ? reserve_low(span, 0x50000000u) : 0;
    if (at != 0) {
      memset(&ext, 0, sizeof(ext));
      ext.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
      ext.reserved_addr = (void*)at;
      ext.reserved_size = span;
      sGame = android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &ext);
    } else {
      LOGE("no gap of %zu bytes below 0x50000000 for the window library: it is loaded anywhere", span);
      sGame = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    }
    if (sGame == NULL) {
      LOGE("loading %s failed: %s", path, dlerror());
      return 0;
    }
    LOGI("the game's window library is in place: %s, %s %p, SDL_main at %p", path, at != 0 ? "low at" : "anywhere:", (void*)at, dlsym(sGame, "SDL_main"));
    return 1;
  }
  span = library_span(path);
  if (span == 0) {
    LOGE("cannot read %s", path);
    return 0;
  }
  at = reserve_low(span, 0x80000000u);
  if (at == 0) {
    LOGE("no gap of %zu bytes below 2 GB for the game's library", span);
    return 0;
  }
  memset(&ext, 0, sizeof(ext));
  ext.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
  ext.reserved_addr = (void*)at;
  ext.reserved_size = span;
  sGame = android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &ext);
  if (sGame == NULL) {
    LOGE("loading %s at %p failed: %s", path, (void*)at, dlerror());
    return 0;
  }
  {
    void* where = dlsym(sGame, "SDL_main");
    LOGI("the game's library is in place: %s, reserved %p + %zu bytes, SDL_main at %p", path, (void*)at, span, where);
  }
  return 1;
}

typedef struct NativeEntry {
  const char* className;
  const char* method;
  const char* signature;
  const char* symbol;
} NativeEntry;
static const NativeEntry kNatives[] = {
    {"dev/encounter/aurora/AuroraSurface", "nativeSetSurfaceReady", "(Z)V",
     "Java_dev_encounter_aurora_AuroraSurface_nativeSetSurfaceReady"},
    {"dev/encounter/borealis/BorealisActivity", "nativeFolderDialogResult", "(JLjava/lang/String;Ljava/lang/String;)V",
     "Java_dev_encounter_borealis_BorealisActivity_nativeFolderDialogResult"},
    {"dev/encounter/borealis/BorealisActivity", "nativeExportDialogResult", "(JLjava/lang/String;Ljava/lang/String;)V",
     "Java_dev_encounter_borealis_BorealisActivity_nativeExportDialogResult"},
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  typedef jint (*OnLoad)(JavaVM*, void*);
  OnLoad onLoad;
  JNIEnv* env = NULL;
  jint result = JNI_VERSION_1_4;
  unsigned i;
  if (!load_game()) return JNI_ERR;
  onLoad = (OnLoad)dlsym(sGame, "JNI_OnLoad");
  if (onLoad != NULL) result = onLoad(vm, reserved);
  if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_4) == JNI_OK && env != NULL) {
    for (i = 0; i < sizeof(kNatives) / sizeof(kNatives[0]); ++i) {
      void* fn = dlsym(sGame, kNatives[i].symbol);
      jclass cls = (*env)->FindClass(env, kNatives[i].className);
      if (fn != NULL && cls != NULL) {
        JNINativeMethod m = {kNatives[i].method, kNatives[i].signature, fn};
        if ((*env)->RegisterNatives(env, cls, &m, 1) < 0) LOGE("RegisterNatives failed for %s", kNatives[i].method);
      } else {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        if (fn == NULL && strstr(kNatives[i].method, "Dialog") != NULL) continue;
        LOGE("native %s not registered (symbol %p, class %p)", kNatives[i].method, fn, (void*)cls);
      }
    }
  }
  return result;
}

JNIEXPORT int SDL_main(int argc, char** argv) {
  int (*game)(int, char**) = sGame != NULL ? (int (*)(int, char**))dlsym(sGame, "SDL_main") : NULL;
  if (game == NULL) {
    LOGE("SDL_main: the game's library is not loaded");
    return 1;
  }
  return game(argc, argv);
}
#endif
