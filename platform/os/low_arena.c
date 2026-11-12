#include "port_low_arena.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

typedef struct Window { uintptr_t start, end; } Window;
static const Window kWindows[] = {
  {0x42000000u, 0x70000000u},
  {0x81800000u, 0xC0000000u},
  {0xC1800000u, 0xE0000000u},
  {0x01000000u, 0x10000000u},
};
#define WINDOW_COUNT (sizeof(kWindows) / sizeof(kWindows[0]))

#define HDR ((size_t)16)
#define MIN_BLOCK ((size_t)32)
#define GROW ((size_t)32 << 20)
#define KEEP_FREE ((uint64_t)48 << 20)
#define MAX_REQUEST ((size_t)0x70000000u)
#define IN_USE ((uint64_t)1)
#define MARK_USED 0x4C6F7755u
#define MARK_FREE 0x4C6F7746u

typedef struct Block {
  uint64_t prev;
  uint64_t head;
} Block;
typedef struct FreeBlock {
  Block b;
  struct FreeBlock* next;
  struct FreeBlock* before;
} FreeBlock;

#define BINS 240
#define MAX_SEGMENTS 2048
typedef struct Segment { volatile uintptr_t start, end; } Segment;

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static FreeBlock* sBins[BINS];
static uint64_t sBinMap[4];
static Segment sSegments[MAX_SEGMENTS];
static volatile unsigned sSegmentTop;
static uintptr_t sCursor[WINDOW_COUNT];
static PortLowStats sStats;
static uint64_t sFreeBytes;

int port_low_fail_returns_null = 0;
uint64_t port_low_test_limit = 0;

static inline size_t block_size(const Block* b) { return (size_t)(b->head & 0xFFFFFFF0u); }
static inline int block_used(const Block* b) { return (int)(b->head & IN_USE); }
static inline uint32_t block_mark(const Block* b) { return (uint32_t)(b->head >> 32); }
static inline uint32_t mark_for(const Block* b, uint32_t mark) { return mark ^ (uint32_t)(uintptr_t)b; }
static inline void set_head(Block* b, size_t size, int used) {
  b->head = (uint64_t)size | (used ? IN_USE : 0) | ((uint64_t)mark_for(b, used ? MARK_USED : MARK_FREE) << 32);
}
static inline Block* next_block(Block* b) { return (Block*)((char*)b + block_size(b)); }
static inline Block* prev_block(Block* b) { return (Block*)((char*)b - (size_t)b->prev); }

static void say(const char* text) {
  size_t n = strlen(text);
  while (n > 0) {
    ssize_t w = write(2, text, n);
    if (w <= 0) break;
    text += w;
    n -= (size_t)w;
  }
}

static void stop_damaged(const char* what, const void* ptr) {
  char line[256];
  snprintf(line, sizeof(line), "[low memory] STOP: %s (address %p). The program's memory below 4 GB is damaged "
           "or a block was freed twice.\n", what, ptr);
  say(line);
  abort();
}

static unsigned bin_of(size_t size) {
  if (size < 1024) return (unsigned)(size >> 4);
  {
    unsigned msb = 63u - (unsigned)__builtin_clzll((unsigned long long)size);
    unsigned sub = (unsigned)(size >> (msb - 3)) & 7u;
    return 64u + (msb - 10u) * 8u + sub;
  }
}

static void bin_insert(Block* b) {
  FreeBlock* f = (FreeBlock*)b;
  const unsigned bin = bin_of(block_size(b));
  f->before = NULL;
  f->next = sBins[bin];
  if (f->next != NULL) f->next->before = f;
  sBins[bin] = f;
  sBinMap[bin >> 6] |= (uint64_t)1 << (bin & 63);
  sFreeBytes += block_size(b);
}

static void bin_remove(Block* b) {
  FreeBlock* f = (FreeBlock*)b;
  const unsigned bin = bin_of(block_size(b));
  if (f->before != NULL) {
    if (f->before->next != f) stop_damaged("a free block's list links do not agree", b);
    f->before->next = f->next;
  } else {
    if (sBins[bin] != f) stop_damaged("a free block is not in the list of its size", b);
    sBins[bin] = f->next;
  }
  if (f->next != NULL) f->next->before = f->before;
  if (sBins[bin] == NULL) sBinMap[bin >> 6] &= ~((uint64_t)1 << (bin & 63));
  sFreeBytes -= block_size(b);
}

static unsigned next_bin(unsigned bin) {
  while (bin < BINS) {
    const uint64_t bits = sBinMap[bin >> 6] >> (bin & 63);
    if (bits != 0) return bin + (unsigned)__builtin_ctzll((unsigned long long)bits);
    bin = (bin | 63u) + 1u;
  }
  return BINS;
}

static void* map_in_window(unsigned w, size_t len) {
  const Window win = kWindows[w];
  const size_t step = (size_t)4 << 20;
  uintptr_t want;
  unsigned tries = 0;
  if (len > win.end - win.start) return NULL;
  if (sCursor[w] < win.start || sCursor[w] + len > win.end) sCursor[w] = win.start;
  want = sCursor[w];
  while (tries < 2u * (unsigned)((win.end - win.start) / step) + 4u) {
    void* p;
    ++tries;
    if (want + len > win.end) {
      want = win.start;
      continue;
    }
    p = mmap((void*)want, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) {
      const uintptr_t a = (uintptr_t)p;
      if (a >= win.start && a + len <= win.end && (a & 15u) == 0) {
        sCursor[w] = a + len;
        return p;
      }
      munmap(p, len);
    }
    want += step;
  }
  return NULL;
}

static int segment_add(uintptr_t start, uintptr_t end) {
  unsigned i;
  for (i = 0; i < MAX_SEGMENTS; ++i) {
    if (sSegments[i].end == 0) {
      sSegments[i].start = start;
      __sync_synchronize();
      sSegments[i].end = end;
      if (i >= sSegmentTop) sSegmentTop = i + 1;
      __sync_synchronize();
      return 1;
    }
  }
  return 0;
}

static void segment_drop(uintptr_t start) {
  unsigned i;
  for (i = 0; i < sSegmentTop; ++i) {
    if (sSegments[i].start == start && sSegments[i].end != 0) {
      sSegments[i].end = 0;
      __sync_synchronize();
      sSegments[i].start = 0;
      return;
    }
  }
  stop_damaged("a piece of address space is missing from the list", (void*)start);
}

int PortLow_Owns(const void* ptr) {
  const uintptr_t a = (uintptr_t)ptr;
  unsigned i, top;
  if (a >= 0xE0000000u || a < 0x01000000u) return 0;
  top = sSegmentTop;
  for (i = 0; i < top; ++i) {
    const uintptr_t start = sSegments[i].start, end = sSegments[i].end;
    if (a >= start && a < end) return 1;
  }
  return 0;
}

static Block* grow(size_t need) {
  size_t len = need + HDR;
  unsigned w;
  void* p = NULL;
  if (len < GROW) len = GROW;
  len = (len + 0xFFFFu) & ~(size_t)0xFFFFu;
  if (port_low_test_limit != 0 && sStats.reserved + len > port_low_test_limit) return NULL;
  for (w = 0; w < WINDOW_COUNT && p == NULL; ++w) p = map_in_window(w, len);
  if (p == NULL) return NULL;
  if (!segment_add((uintptr_t)p, (uintptr_t)p + len)) {
    munmap(p, len);
    return NULL;
  }
  {
    Block* first = (Block*)p;
    Block* endMark = (Block*)((char*)p + len - HDR);
    first->prev = 0;
    set_head(first, len - HDR, 0);
    endMark->prev = len - HDR;
    set_head(endMark, HDR, 1);
    sStats.reserved += len;
    if (sStats.reserved > sStats.reservedPeak) sStats.reservedPeak = sStats.reserved;
    sStats.segments += 1;
    if (sStats.lowest == 0 || (uintptr_t)p < sStats.lowest) sStats.lowest = (uintptr_t)p;
    if ((uintptr_t)p + len > sStats.highest) sStats.highest = (uintptr_t)p + len;
    return first;
  }
}

static void release_segment(Block* b) {
  const size_t len = block_size(b) + HDR;
  segment_drop((uintptr_t)b);
  sStats.reserved -= len;
  sStats.segments -= 1;
  munmap((void*)b, len);
}

static void no_room(size_t size) {
  char line[400];
  snprintf(line, sizeof(line),
           "[low memory] OUT OF MEMORY BELOW 4 GB: a request of %llu bytes (%llu MB) cannot be answered. "
           "In use: %llu MB in %llu blocks; address space held: %llu MB of %llu MB. "
           "The game keeps addresses in 4 bytes, so memory above 4 GB is never handed out instead.\n",
           (unsigned long long)size, (unsigned long long)(size >> 20), (unsigned long long)(sStats.used >> 20),
           (unsigned long long)sStats.blocks, (unsigned long long)(sStats.reserved >> 20),
           (unsigned long long)(sStats.windowBytes >> 20));
  say(line);
}

static void split_tail(Block* b, size_t need) {
  const size_t size = block_size(b);
  if (size - need >= MIN_BLOCK) {
    Block* rest = (Block*)((char*)b + need);
    Block* after = (Block*)((char*)b + size);
    size_t restSize = size - need;
    set_head(b, need, block_used(b));
    if (!block_used(after)) {
      bin_remove(after);
      restSize += block_size(after);
      after = (Block*)((char*)rest + restSize);
    }
    rest->prev = need;
    set_head(rest, restSize, 0);
    after->prev = restSize;
    bin_insert(rest);
  }
}

static Block* take(size_t need) {
  unsigned bin = bin_of(need);
  Block* b = NULL;
  if (need >= 1024) {
    FreeBlock* f = sBins[bin];
    int looked = 0;
    for (; f != NULL && looked < 32; f = f->next, ++looked) {
      if (block_size(&f->b) >= need) {
        b = &f->b;
        break;
      }
    }
    if (b == NULL) ++bin;
  }
  if (b == NULL) {
    bin = next_bin(bin);
    if (bin < BINS) b = &sBins[bin]->b;
  }
  if (b != NULL) {
    if (block_used(b) || block_mark(b) != mark_for(b, MARK_FREE) || block_size(b) < need) {
      stop_damaged("a free block's header is damaged", b);
    }
    bin_remove(b);
  } else {
    b = grow(need);
    if (b == NULL) return NULL;
  }
  set_head(b, block_size(b), 1);
  split_tail(b, need);
  sStats.used += block_size(b);
  if (sStats.used > sStats.usedPeak) sStats.usedPeak = sStats.used;
  sStats.blocks += 1;
  sStats.allocations += 1;
  return b;
}

static size_t need_for(size_t size) {
  size_t need = (size + HDR + 15u) & ~(size_t)15u;
  return need < MIN_BLOCK ? MIN_BLOCK : need;
}

static Block* block_of(const void* ptr, const char* doing) {
  Block* b = (Block*)((char*)ptr - HDR);
  if (((uintptr_t)ptr & 15u) != 0 || !PortLow_Owns(ptr) || !PortLow_Owns(b)) stop_damaged(doing, ptr);
  if (block_mark(b) == mark_for(b, MARK_FREE) && !block_used(b)) {
    stop_damaged("a block that is already free was freed or used again", ptr);
  }
  if (block_mark(b) != mark_for(b, MARK_USED) || !block_used(b) || block_size(b) < MIN_BLOCK) stop_damaged(doing, ptr);
  return b;
}

static void give_back(Block* b) {
  Block* after = next_block(b);
  size_t size = block_size(b);
  if (after->prev != size) stop_damaged("the block after a freed block does not agree about its size (written past its end?)", b + 1);
  sStats.used -= size;
  sStats.blocks -= 1;
  if (!block_used(after)) {
    bin_remove(after);
    size += block_size(after);
  }
  if (b->prev != 0) {
    Block* before = prev_block(b);
    if (block_size(before) != b->prev) stop_damaged("the block before a freed block does not agree about its size", b + 1);
    if (!block_used(before)) {
      bin_remove(before);
      size += block_size(before);
      b = before;
    }
  }
  set_head(b, size, 0);
  after = (Block*)((char*)b + size);
  after->prev = size;
  if (b->prev == 0 && block_size(after) == HDR && (size + HDR > GROW || sFreeBytes >= KEEP_FREE)) {
    release_segment(b);
    return;
  }
  bin_insert(b);
}

void* PortLow_Malloc(size_t size) {
  Block* b;
  if (size > MAX_REQUEST) {
    no_room(size);
    errno = ENOMEM;
    return NULL;
  }
  pthread_mutex_lock(&sLock);
  if (sStats.windowBytes == 0) {
    unsigned w;
    for (w = 0; w < WINDOW_COUNT; ++w) sStats.windowBytes += kWindows[w].end - kWindows[w].start;
  }
  b = take(need_for(size));
  if (b == NULL) {
    no_room(size);
    pthread_mutex_unlock(&sLock);
    if (!port_low_fail_returns_null) abort();
    errno = ENOMEM;
    return NULL;
  }
  pthread_mutex_unlock(&sLock);
  return (char*)b + HDR;
}

void* PortLow_Calloc(size_t count, size_t size) {
  void* p;
  if (size != 0 && count > (size_t)-1 / size) {
    errno = ENOMEM;
    return NULL;
  }
  p = PortLow_Malloc(count * size);
  if (p != NULL) memset(p, 0, count * size);
  return p;
}

void PortLow_Free(void* ptr) {
  Block* b;
  if (ptr == NULL) return;
  pthread_mutex_lock(&sLock);
  b = block_of(ptr, "free of an address that is not a block in use");
  give_back(b);
  pthread_mutex_unlock(&sLock);
}

size_t PortLow_Size(const void* ptr) {
  const Block* b = (const Block*)((const char*)ptr - HDR);
  if (ptr == NULL || ((uintptr_t)ptr & 15u) != 0 || !PortLow_Owns(ptr) || !PortLow_Owns(b)) return 0;
  if (block_mark(b) != mark_for(b, MARK_USED) || !block_used(b)) return 0;
  return block_size(b) - HDR;
}

void* PortLow_Realloc(void* ptr, size_t size) {
  Block* b;
  size_t need, have;
  void* fresh;
  if (ptr == NULL) return PortLow_Malloc(size);
  if (size > MAX_REQUEST) {
    no_room(size);
    errno = ENOMEM;
    return NULL;
  }
  need = need_for(size);
  pthread_mutex_lock(&sLock);
  b = block_of(ptr, "realloc of an address that is not a block in use");
  have = block_size(b);
  if (need <= have) {
    if (have - need >= 64 && have - need >= have / 8) {
      sStats.used -= have;
      split_tail(b, need);
      sStats.used += block_size(b);
    }
    pthread_mutex_unlock(&sLock);
    return ptr;
  }
  {
    Block* after = next_block(b);
    if (!block_used(after) && have + block_size(after) >= need) {
      Block* then;
      bin_remove(after);
      sStats.used -= have;
      set_head(b, have + block_size(after), 1);
      then = next_block(b);
      then->prev = block_size(b);
      split_tail(b, need);
      sStats.used += block_size(b);
      if (sStats.used > sStats.usedPeak) sStats.usedPeak = sStats.used;
      pthread_mutex_unlock(&sLock);
      return ptr;
    }
  }
  pthread_mutex_unlock(&sLock);
  fresh = PortLow_Malloc(size);
  if (fresh == NULL) return NULL;
  memcpy(fresh, ptr, have - HDR);
  PortLow_Free(ptr);
  return fresh;
}

void* PortLow_Memalign(size_t alignment, size_t size) {
  Block* b;
  size_t need;
  uintptr_t user;
  if (alignment <= 16) return PortLow_Malloc(size);
  if ((alignment & (alignment - 1)) != 0 || alignment > ((size_t)256 << 20) || size > MAX_REQUEST) {
    errno = EINVAL;
    return NULL;
  }
  need = need_for(size);
  pthread_mutex_lock(&sLock);
  if (sStats.windowBytes == 0) {
    unsigned w;
    for (w = 0; w < WINDOW_COUNT; ++w) sStats.windowBytes += kWindows[w].end - kWindows[w].start;
  }
  b = take(need + alignment + MIN_BLOCK);
  if (b == NULL) {
    no_room(size + alignment);
    pthread_mutex_unlock(&sLock);
    if (!port_low_fail_returns_null) abort();
    errno = ENOMEM;
    return NULL;
  }
  user = ((uintptr_t)b + HDR + alignment - 1) & ~(uintptr_t)(alignment - 1);
  if (user != (uintptr_t)b + HDR) {
    Block* nb;
    size_t front, whole = block_size(b);
    if (user - ((uintptr_t)b + HDR) < MIN_BLOCK) user += alignment;
    nb = (Block*)(user - HDR);
    front = (size_t)((uintptr_t)nb - (uintptr_t)b);
    sStats.used -= whole;
    nb->prev = front;
    set_head(nb, whole - front, 1);
    next_block(nb)->prev = whole - front;
    set_head(b, front, 0);
    bin_insert(b);
    b = nb;
    sStats.used += block_size(b);
  }
  sStats.used -= block_size(b);
  split_tail(b, need);
  sStats.used += block_size(b);
  pthread_mutex_unlock(&sLock);
  return (char*)b + HDR;
}

void PortLow_GetStats(PortLowStats* out) {
  pthread_mutex_lock(&sLock);
  if (sStats.windowBytes == 0) {
    unsigned w;
    for (w = 0; w < WINDOW_COUNT; ++w) sStats.windowBytes += kWindows[w].end - kWindows[w].start;
  }
  *out = sStats;
  pthread_mutex_unlock(&sLock);
}

int PortLow_Check(void) {
  unsigned i, bin;
  uint64_t used = 0, blocks = 0, freeWalk = 0, freeLists = 0, reserved = 0, freeCount = 0, listCount = 0;
  int bad = 0;
  pthread_mutex_lock(&sLock);
  for (i = 0; i < sSegmentTop && !bad; ++i) {
    const uintptr_t start = sSegments[i].start, end = sSegments[i].end;
    Block* b = (Block*)start;
    size_t before = 0;
    int lastFree = 0;
    if (end == 0) continue;
    reserved += end - start;
    for (;;) {
      const size_t size = block_size(b);
      if ((uintptr_t)b + HDR > end) { bad = 1; break; }
      if (b->prev != before) { bad = 2; break; }
      if (size < HDR || (uintptr_t)b + size > end) { bad = 3; break; }
      if (block_mark(b) != mark_for(b, block_used(b) ? MARK_USED : MARK_FREE)) { bad = 4; break; }
      if ((uintptr_t)b + size == end) {
        if (size != HDR || !block_used(b)) bad = 5;
        break;
      }
      if (size < MIN_BLOCK) { bad = 6; break; }
      if (block_used(b)) {
        used += size;
        blocks += 1;
        lastFree = 0;
      } else {
        if (lastFree) { bad = 7; break; }
        freeWalk += size;
        freeCount += 1;
        lastFree = 1;
      }
      before = size;
      b = next_block(b);
    }
  }
  for (bin = 0; bin < BINS && !bad; ++bin) {
    FreeBlock* f = sBins[bin];
    FreeBlock* last = NULL;
    const int marked = (int)((sBinMap[bin >> 6] >> (bin & 63)) & 1);
    if ((f != NULL) != marked) { bad = 8; break; }
    for (; f != NULL; last = f, f = f->next) {
      if (!PortLow_Owns(f) || block_used(&f->b) || bin_of(block_size(&f->b)) != bin || f->before != last) { bad = 9; break; }
      freeLists += block_size(&f->b);
      listCount += 1;
    }
  }
  if (!bad && (freeWalk != freeLists || freeCount != listCount || freeLists != sFreeBytes)) bad = 10;
  if (!bad && (used != sStats.used || blocks != sStats.blocks)) bad = 11;
  if (!bad && reserved != sStats.reserved) bad = 12;
  pthread_mutex_unlock(&sLock);
  return bad;
}

void PortLow_Lock(void) { pthread_mutex_lock(&sLock); }
void PortLow_Unlock(void) { pthread_mutex_unlock(&sLock); }
void PortLow_ReinitLock(void) {
  static const pthread_mutex_t fresh = PTHREAD_MUTEX_INITIALIZER;
  memcpy(&sLock, &fresh, sizeof(sLock));
}
