#ifndef PORT_ANDROID_COMPAT_H
#define PORT_ANDROID_COMPAT_H
#ifdef __ANDROID__
#include <sys/mman.h>
#include "port_android_layout.h"

#undef MAP_32BIT
#define MAP_32BIT 0x40000000

static inline void* port_android_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
  if (flags & MAP_32BIT) {
    return PortAndroid_StackTake(len, prot);
  }
  return (mmap)(addr, len, prot, flags, fd, off);
}
static inline int port_android_munmap(void* addr, size_t len) {
  if (PortAndroid_InStackRange(addr)) {
    return PortAndroid_StackGive(addr, len);
  }
  return (munmap)(addr, len);
}
#define mmap(addr, len, prot, flags, fd, off) port_android_mmap((addr), (len), (prot), (flags), (fd), (off))
#define munmap(addr, len) port_android_munmap((addr), (len))
#endif
#endif
