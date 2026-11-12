#ifndef PORT_MAC_COMPAT_H
#define PORT_MAC_COMPAT_H
#if (defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)) && !defined(__OBJC__)

#include <fcntl.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#else
extern int port_test_mac_has_reserve;
#define getsegbyname(name) (port_test_mac_has_reserve ? (const void*)(name) : (const void*)0)
#endif
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_STACK
#define MAP_STACK 0
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef SYS_gettid
#define SYS_gettid SYS_thread_selfid
#endif

#undef MAP_FIXED_NOREPLACE
#undef MAP_32BIT
#define MAP_FIXED_NOREPLACE 0x20000000
#define MAP_32BIT 0x40000000

static inline int port_mac_in_reserve(uintptr_t a, size_t n) {
  static const uintptr_t r[][2] = {{0x70000000u, 0x10000000u}, {0x80000000u, 0x01800000u},
                                   {0xC0000000u, 0x01800000u}, {0xE0000000u, 0x00004000u}};
  if (getsegbyname("__XDLOW") == NULL) return 0;
  for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); ++i) {
    if (a >= r[i][0] && a + n <= r[i][0] + r[i][1]) return 1;
  }
  return 0;
}

static inline void* port_mac_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
  const int real = flags & ~(MAP_FIXED_NOREPLACE | MAP_32BIT);
  if (flags & MAP_FIXED_NOREPLACE) {
    void* p;
    if (port_mac_in_reserve((uintptr_t)addr, len)) {
      return (mmap)(addr, len, prot, real | MAP_FIXED, fd, off);
    }
    p = (mmap)(addr, len, prot, real, fd, off);
    if (p != MAP_FAILED && p != addr) {
      munmap(p, len);
      return MAP_FAILED;
    }
    return p;
  }
  if (flags & MAP_32BIT) {
    static volatile uintptr_t next = 0x10000000u;
    const size_t step = (len + 0xFFFFu) & ~(size_t)0xFFFFu;
    for (unsigned tries = 0; tries < 20000; ++tries) {
      uintptr_t want = __sync_fetch_and_add(&next, step);
      void* p;
      if (want + step > 0x3F000000u) {
        next = 0x10000000u;
        continue;
      }
      p = (mmap)((void*)want, len, prot, real, fd, off);
      if (p == (void*)want) return p;
      if (p != MAP_FAILED) munmap(p, len);
    }
    return MAP_FAILED;
  }
  return (mmap)(addr, len, prot, flags, fd, off);
}
#define mmap(addr, len, prot, flags, fd, off) port_mac_mmap((addr), (len), (prot), (flags), (fd), (off))

static inline int port_mac_memfd(const char* name) {
  char path[32];
  int fd;
  snprintf(path, sizeof(path), "/xd%d_%.12s", (int)getpid(), name);
  shm_unlink(path);
  fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd >= 0) shm_unlink(path);
  return fd;
}
#define memfd_create(name, flags) port_mac_memfd(name)

#endif
#endif
