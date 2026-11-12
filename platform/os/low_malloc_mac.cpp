#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)

#include "port_low_arena.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
extern "C" {
void __libc_free(void*);
size_t malloc_usable_size(void*) noexcept;
}
struct malloc_zone_t;
struct malloc_statistics_t {
  unsigned blocks_in_use;
  size_t size_in_use, max_size_in_use, size_allocated;
};
struct malloc_introspection_t {
  int (*enumerator)(unsigned, void*, unsigned, uintptr_t, void*, void*);
  size_t (*good_size)(malloc_zone_t*, size_t);
  int (*check)(malloc_zone_t*);
  void (*print)(malloc_zone_t*, int);
  void (*log)(malloc_zone_t*, void*);
  void (*force_lock)(malloc_zone_t*);
  void (*force_unlock)(malloc_zone_t*);
  void (*statistics)(malloc_zone_t*, malloc_statistics_t*);
  int (*zone_locked)(malloc_zone_t*);
  void (*reinit_lock)(malloc_zone_t*);
};
struct malloc_zone_t {
  size_t (*size)(malloc_zone_t*, const void*);
  void* (*malloc)(malloc_zone_t*, size_t);
  void* (*calloc)(malloc_zone_t*, size_t, size_t);
  void* (*valloc)(malloc_zone_t*, size_t);
  void (*free)(malloc_zone_t*, void*);
  void* (*realloc)(malloc_zone_t*, void*, size_t);
  void (*destroy)(malloc_zone_t*);
  const char* zone_name;
  unsigned (*batch_malloc)(malloc_zone_t*, size_t, void**, unsigned);
  void (*batch_free)(malloc_zone_t*, void**, unsigned);
  malloc_introspection_t* introspect;
  unsigned version;
  void* (*memalign)(malloc_zone_t*, size_t, size_t);
  void (*free_definite_size)(malloc_zone_t*, void*, size_t);
  size_t (*pressure_relief)(malloc_zone_t*, size_t);
  int (*claimed_address)(malloc_zone_t*, void*);
};
static malloc_zone_t sStandInSystemZone;
static void malloc_zone_register(malloc_zone_t*) {}
static void malloc_zone_unregister(malloc_zone_t*) {}
static malloc_zone_t* malloc_default_zone() { return &sStandInSystemZone; }
static malloc_zone_t* malloc_default_purgeable_zone() { return nullptr; }
static malloc_zone_t* malloc_zone_from_ptr(const void*) { return &sStandInSystemZone; }
static void malloc_zone_free(malloc_zone_t*, void* p) { __libc_free(p); }
static size_t malloc_size(const void* p) { return malloc_usable_size(const_cast<void*>(p)); }
static int malloc_get_all_zones(unsigned, void*, uintptr_t** zones, unsigned* count) {
  *zones = nullptr;
  *count = 0;
  return 0;
}
typedef uintptr_t vm_address_t;
#endif

#ifdef __APPLE__
#define LOW_NOEXCEPT
#else
#define LOW_NOEXCEPT noexcept
#endif

namespace {

malloc_zone_t sZone;
malloc_introspection_t sIntrospect;
pthread_once_t sOnce = PTHREAD_ONCE_INIT;
int sEverything;
int sFirstChoice;

size_t zone_size(malloc_zone_t*, const void* ptr) { return PortLow_Owns(ptr) ? PortLow_Size(ptr) : 0; }
void* zone_malloc(malloc_zone_t*, size_t size) { return PortLow_Malloc(size); }
void* zone_calloc(malloc_zone_t*, size_t count, size_t size) { return PortLow_Calloc(count, size); }
void* zone_valloc(malloc_zone_t*, size_t size) { return PortLow_Memalign(16384, size); }
void zone_free(malloc_zone_t*, void* ptr) { PortLow_Free(ptr); }
void* zone_realloc(malloc_zone_t*, void* ptr, size_t size) { return PortLow_Realloc(ptr, size); }
void zone_destroy(malloc_zone_t*) {}
unsigned zone_batch_malloc(malloc_zone_t*, size_t size, void** results, unsigned count) {
  unsigned i;
  for (i = 0; i < count; ++i) {
    results[i] = PortLow_Malloc(size);
    if (results[i] == nullptr) break;
  }
  return i;
}
void zone_batch_free(malloc_zone_t*, void** blocks, unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    PortLow_Free(blocks[i]);
    blocks[i] = nullptr;
  }
}
void* zone_memalign(malloc_zone_t*, size_t alignment, size_t size) { return PortLow_Memalign(alignment, size); }
void zone_free_definite_size(malloc_zone_t*, void* ptr, size_t) { PortLow_Free(ptr); }
size_t zone_pressure_relief(malloc_zone_t*, size_t) { return 0; }
int zone_claimed_address(malloc_zone_t*, void* ptr) { return PortLow_Owns(ptr); }

int intro_enumerator(unsigned, void*, unsigned, uintptr_t, void*, void*) { return 0; }
size_t intro_good_size(malloc_zone_t*, size_t size) { return size < 16 ? 16 : (size + 15) & ~static_cast<size_t>(15); }
int intro_check(malloc_zone_t*) { return PortLow_Check() == 0; }
void intro_print(malloc_zone_t*, int) {}
void intro_log(malloc_zone_t*, void*) {}
void intro_force_lock(malloc_zone_t*) { PortLow_Lock(); }
void intro_force_unlock(malloc_zone_t*) { PortLow_Unlock(); }
void intro_statistics(malloc_zone_t*, malloc_statistics_t* out) {
  PortLowStats s;
  PortLow_GetStats(&s);
  out->blocks_in_use = static_cast<unsigned>(s.blocks);
  out->size_in_use = static_cast<size_t>(s.used);
  out->max_size_in_use = static_cast<size_t>(s.usedPeak);
  out->size_allocated = static_cast<size_t>(s.reserved);
}
int intro_zone_locked(malloc_zone_t*) { return 0; }
void intro_reinit_lock(malloc_zone_t*) { PortLow_ReinitLock(); }

malloc_zone_t* first_zone() {
  vm_address_t* zones = nullptr;
  unsigned count = 0;
  if (malloc_get_all_zones(0, nullptr, &zones, &count) == 0 && count > 0 && zones != nullptr) {
    return reinterpret_cast<malloc_zone_t*>(zones[0]);
  }
  return malloc_default_zone();
}

void start_zone() {
  sIntrospect.enumerator = reinterpret_cast<__typeof__(sIntrospect.enumerator)>(intro_enumerator);
  sIntrospect.good_size = reinterpret_cast<__typeof__(sIntrospect.good_size)>(intro_good_size);
  sIntrospect.check = reinterpret_cast<__typeof__(sIntrospect.check)>(intro_check);
  sIntrospect.print = reinterpret_cast<__typeof__(sIntrospect.print)>(intro_print);
  sIntrospect.log = reinterpret_cast<__typeof__(sIntrospect.log)>(intro_log);
  sIntrospect.force_lock = reinterpret_cast<__typeof__(sIntrospect.force_lock)>(intro_force_lock);
  sIntrospect.force_unlock = reinterpret_cast<__typeof__(sIntrospect.force_unlock)>(intro_force_unlock);
  sIntrospect.statistics = reinterpret_cast<__typeof__(sIntrospect.statistics)>(intro_statistics);
  sIntrospect.zone_locked = reinterpret_cast<__typeof__(sIntrospect.zone_locked)>(intro_zone_locked);
  sIntrospect.reinit_lock = reinterpret_cast<__typeof__(sIntrospect.reinit_lock)>(intro_reinit_lock);
  sZone.size = reinterpret_cast<__typeof__(sZone.size)>(zone_size);
  sZone.malloc = reinterpret_cast<__typeof__(sZone.malloc)>(zone_malloc);
  sZone.calloc = reinterpret_cast<__typeof__(sZone.calloc)>(zone_calloc);
  sZone.valloc = reinterpret_cast<__typeof__(sZone.valloc)>(zone_valloc);
  sZone.free = reinterpret_cast<__typeof__(sZone.free)>(zone_free);
  sZone.realloc = reinterpret_cast<__typeof__(sZone.realloc)>(zone_realloc);
  sZone.destroy = reinterpret_cast<__typeof__(sZone.destroy)>(zone_destroy);
  sZone.zone_name = "xd_low_memory";
  sZone.batch_malloc = reinterpret_cast<__typeof__(sZone.batch_malloc)>(zone_batch_malloc);
  sZone.batch_free = reinterpret_cast<__typeof__(sZone.batch_free)>(zone_batch_free);
  sZone.introspect = &sIntrospect;
  sZone.version = 10;
  sZone.memalign = reinterpret_cast<__typeof__(sZone.memalign)>(zone_memalign);
  sZone.free_definite_size = reinterpret_cast<__typeof__(sZone.free_definite_size)>(zone_free_definite_size);
  sZone.pressure_relief = reinterpret_cast<__typeof__(sZone.pressure_relief)>(zone_pressure_relief);
  sZone.claimed_address = reinterpret_cast<__typeof__(sZone.claimed_address)>(zone_claimed_address);
  malloc_zone_register(&sZone);

  const char* mode = getenv("XD_LOW_MALLOC");
  if (mode != nullptr && strcmp(mode, "all") == 0) {
    sEverything = 1;
    malloc_zone_t* purgeable = malloc_default_purgeable_zone();
    for (int tries = 0; tries < 8 && first_zone() != &sZone; ++tries) {
      malloc_zone_t* first = first_zone();
      malloc_zone_unregister(first);
      malloc_zone_register(first);
      if (purgeable != nullptr) {
        malloc_zone_unregister(purgeable);
        malloc_zone_register(purgeable);
      }
    }
    sFirstChoice = first_zone() == &sZone;
  }
}

inline void ready() { pthread_once(&sOnce, start_zone); }

void free_stranger(void* ptr) {
  malloc_zone_t* owner = malloc_zone_from_ptr(ptr);
  malloc_zone_free(owner != nullptr ? owner : malloc_default_zone(), ptr);
}

}

extern "C" {

int PortLowMalloc_Mode(void) {
  ready();
  return sEverything ? (sFirstChoice ? 2 : -2) : 1;
}

void* malloc(size_t size) LOW_NOEXCEPT {
  ready();
  return PortLow_Malloc(size);
}

void* calloc(size_t count, size_t size) LOW_NOEXCEPT {
  ready();
  return PortLow_Calloc(count, size);
}

void free(void* ptr) LOW_NOEXCEPT {
  if (ptr == nullptr) return;
  if (PortLow_Owns(ptr)) {
    PortLow_Free(ptr);
  } else {
    free_stranger(ptr);
  }
}

void* realloc(void* ptr, size_t size) LOW_NOEXCEPT {
  ready();
  if (ptr == nullptr) return PortLow_Malloc(size);
  if (PortLow_Owns(ptr)) {
    if (size == 0) size = 1;
    return PortLow_Realloc(ptr, size);
  }
  const size_t had = malloc_size(ptr);
  void* fresh = PortLow_Malloc(size);
  if (fresh == nullptr) return nullptr;
  memcpy(fresh, ptr, had < size ? had : size);
  free_stranger(ptr);
  return fresh;
}

void* reallocf(void* ptr, size_t size) {
  void* fresh = realloc(ptr, size);
  if (fresh == nullptr && ptr != nullptr && size != 0) free(ptr);
  return fresh;
}

void* valloc(size_t size) LOW_NOEXCEPT {
  ready();
  return PortLow_Memalign(16384, size);
}

void* aligned_alloc(size_t alignment, size_t size) LOW_NOEXCEPT {
  ready();
  return PortLow_Memalign(alignment, size);
}

int posix_memalign(void** out, size_t alignment, size_t size) LOW_NOEXCEPT {
  ready();
  if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) return EINVAL;
  void* p = PortLow_Memalign(alignment, size);
  if (p == nullptr) return ENOMEM;
  *out = p;
  return 0;
}

char* strdup(const char* text) LOW_NOEXCEPT {
  const size_t n = strlen(text) + 1;
  char* copy = static_cast<char*>(malloc(n));
  if (copy != nullptr) memcpy(copy, text, n);
  return copy;
}

char* strndup(const char* text, size_t most) LOW_NOEXCEPT {
  const size_t n = strnlen(text, most);
  char* copy = static_cast<char*>(malloc(n + 1));
  if (copy != nullptr) {
    memcpy(copy, text, n);
    copy[n] = '\0';
  }
  return copy;
}

}

void* operator new(size_t size, std::align_val_t alignment) {
  void* p = aligned_alloc(static_cast<size_t>(alignment), size == 0 ? 1 : size);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](size_t size, std::align_val_t alignment) { return operator new(size, alignment); }
void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return aligned_alloc(static_cast<size_t>(alignment), size == 0 ? 1 : size);
}
void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return aligned_alloc(static_cast<size_t>(alignment), size == 0 ? 1 : size);
}
void operator delete(void* ptr, std::align_val_t) noexcept { free(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { free(ptr); }
void operator delete(void* ptr, size_t, std::align_val_t) noexcept { free(ptr); }
void operator delete[](void* ptr, size_t, std::align_val_t) noexcept { free(ptr); }

#endif
