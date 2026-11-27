#ifdef __ANDROID__

#include "port_low_arena.h"

#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

namespace {

typedef void (*FreeFn)(void*);
typedef size_t (*UsableFn)(const void*);
FreeFn sSystemFree;
UsableFn sSystemUsable;
pthread_once_t sOnce = PTHREAD_ONCE_INIT;

void start() {
  sSystemFree = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
  sSystemUsable = reinterpret_cast<UsableFn>(dlsym(RTLD_NEXT, "malloc_usable_size"));
}
inline void ready() { pthread_once(&sOnce, start); }

void free_stranger(void* ptr) {
  ready();
  if (sSystemFree != nullptr) sSystemFree(ptr);
}

}

extern "C" {

int PortLowMalloc_Mode(void) { return 1; }

void* malloc(size_t size) { return PortLow_Malloc(size); }
void* calloc(size_t count, size_t size) { return PortLow_Calloc(count, size); }

void free(void* ptr) {
  if (ptr == nullptr) return;
  if (PortLow_Owns(ptr)) {
    PortLow_Free(ptr);
  } else {
    free_stranger(ptr);
  }
}

void* realloc(void* ptr, size_t size) {
  if (ptr == nullptr) return PortLow_Malloc(size);
  if (PortLow_Owns(ptr)) {
    if (size == 0) size = 1;
    return PortLow_Realloc(ptr, size);
  }
  ready();
  const size_t had = sSystemUsable != nullptr ? sSystemUsable(ptr) : 0;
  void* fresh = PortLow_Malloc(size);
  if (fresh == nullptr) return nullptr;
  memcpy(fresh, ptr, had < size ? had : size);
  free_stranger(ptr);
  return fresh;
}

void* memalign(size_t alignment, size_t size) { return PortLow_Memalign(alignment, size); }
void* aligned_alloc(size_t alignment, size_t size) { return PortLow_Memalign(alignment, size); }

int posix_memalign(void** out, size_t alignment, size_t size) {
  if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) return EINVAL;
  void* p = PortLow_Memalign(alignment, size);
  if (p == nullptr) return ENOMEM;
  *out = p;
  return 0;
}

void* valloc(size_t size) { return PortLow_Memalign(4096, size); }
void* pvalloc(size_t size) { return PortLow_Memalign(4096, (size + 4095) & ~static_cast<size_t>(4095)); }

size_t malloc_usable_size(const void* ptr) {
  if (ptr == nullptr) return 0;
  if (PortLow_Owns(ptr)) return PortLow_Size(ptr);
  ready();
  return sSystemUsable != nullptr ? sSystemUsable(ptr) : 0;
}

char* strdup(const char* text) {
  const size_t n = strlen(text) + 1;
  char* copy = static_cast<char*>(malloc(n));
  if (copy != nullptr) memcpy(copy, text, n);
  return copy;
}

char* strndup(const char* text, size_t most) {
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
