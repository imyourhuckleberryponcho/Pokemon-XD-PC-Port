#ifndef PORT_MAC_COMPAT_H
#define PORT_MAC_COMPAT_H
#if (defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)) && !defined(__OBJC__)

#include <fcntl.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#else
extern int port_test_mac_has_reserve;
#define getsegbyname(name) (port_test_mac_has_reserve ? (const void*)(name) : (const void*)0)
extern int port_test_mac_has_stack_reserve;
#endif
#include <sched.h>
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

#define PORT_MAC_STK_BASE ((uintptr_t)0x50000000u)
#define PORT_MAC_STK_SIZE ((uintptr_t)0x20000000u)
#define PORT_MAC_STK_UNIT ((uintptr_t)0x10000u)
#define PORT_MAC_STK_UNITS ((unsigned)(PORT_MAC_STK_SIZE / PORT_MAC_STK_UNIT))
typedef struct PortMacStackKeeper {
  volatile int lock;
  unsigned char used[PORT_MAC_STK_UNITS];
} PortMacStackKeeper;

static inline int port_mac_has_stack_reserve(void) {
#ifdef __APPLE__
  return getsegbyname("__XDSTK") != NULL;
#else
  return port_test_mac_has_stack_reserve;
#endif
}

static inline int port_mac_in_stack_reserve(const void* p) {
  return (uintptr_t)p >= PORT_MAC_STK_BASE + PORT_MAC_STK_UNIT && (uintptr_t)p < PORT_MAC_STK_BASE + PORT_MAC_STK_SIZE &&
         port_mac_has_stack_reserve();
}

static inline void* port_mac_stack_take(size_t len, int prot) {
  PortMacStackKeeper* k = (PortMacStackKeeper*)PORT_MAC_STK_BASE;
  const unsigned n = (unsigned)((len + PORT_MAC_STK_UNIT - 1) / PORT_MAC_STK_UNIT);
  unsigned i, run = 0, first = 0;
  if (n == 0 || n >= PORT_MAC_STK_UNITS) return MAP_FAILED;
  while (__sync_lock_test_and_set(&k->lock, 1)) sched_yield();
  for (i = 1; i < PORT_MAC_STK_UNITS; ++i) {
    run = k->used[i] ? 0 : run + 1;
    if (run == n) {
      first = i + 1 - n;
      break;
    }
  }
  for (i = 0; first != 0 && i < n; ++i) k->used[first + i] = 1;
  __sync_lock_release(&k->lock);
  if (first == 0) return MAP_FAILED;
  return (mmap)((void*)(PORT_MAC_STK_BASE + first * PORT_MAC_STK_UNIT), n * PORT_MAC_STK_UNIT, prot,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
}

static inline int port_mac_munmap(void* addr, size_t len) {
  if (port_mac_in_stack_reserve(addr)) {
    PortMacStackKeeper* k = (PortMacStackKeeper*)PORT_MAC_STK_BASE;
    const unsigned first = (unsigned)(((uintptr_t)addr - PORT_MAC_STK_BASE) / PORT_MAC_STK_UNIT);
    const unsigned n = (unsigned)((len + PORT_MAC_STK_UNIT - 1) / PORT_MAC_STK_UNIT);
    unsigned i;
    if (((uintptr_t)addr & (PORT_MAC_STK_UNIT - 1)) != 0 || n == 0 || first + n > PORT_MAC_STK_UNITS) return -1;
    (mmap)(addr, n * PORT_MAC_STK_UNIT, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    while (__sync_lock_test_and_set(&k->lock, 1)) sched_yield();
    for (i = 0; i < n; ++i) k->used[first + i] = 0;
    __sync_lock_release(&k->lock);
    return 0;
  }
  return (munmap)(addr, len);
}

static inline void* port_mac_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
  const int real = flags & ~(MAP_FIXED_NOREPLACE | MAP_32BIT);
  if ((flags & MAP_32BIT) && port_mac_has_stack_reserve()) {
    return port_mac_stack_take(len, prot);
  }
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
#define munmap(addr, len) port_mac_munmap((addr), (len))

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
