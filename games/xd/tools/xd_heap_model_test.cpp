#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

extern "C" void PortCpuCost_Count(unsigned, uint64_t) {}

extern "C" {
int32_t pokecoloFrameCounter = 0;

static uintptr_t sNext = 0x90000000u;
static int32_t sRealTotal = 0, sRealLargest = 0;
void* PortHeapReal_OSInitAlloc(void*, void*, int) { return reinterpret_cast<void*>(uintptr_t{0x80507EE0}); }
int PortHeapReal_OSCreateHeap(void*, void*) {
  static int n = 0;
  return n++;
}
int32_t PortHeapReal_OSCreateExternalHeap(int32_t, int32_t, int32_t*, int32_t) { return 9; }
void PortHeapReal_OSDestroyHeap(int) {}
void* PortHeapReal_OSAllocFromHeap(int, uint32_t size) {
  void* p = reinterpret_cast<void*>(sNext);
  sNext += (size + 0x3F) & ~0x1Fu;
  return p;
}
void PortHeapReal_OSFreeToHeap(int, void*) {}
int32_t PortHeapReal_OSShrinkHeap(int, void*, uint32_t) { return 1; }
int32_t PortHeapReal_OSSetAllocMethodHeap(int32_t, int32_t) { return 0; }
int32_t PortHeapReal_OSGetHeapFreeSize(int32_t, int32_t* total, int32_t* largest) {
  *total = sRealTotal;
  *largest = sRealLargest;
  return 1;
}
static uint32_t sPoolRequest = 0;
static bool sPoolAllocates = false;
void* OSAllocFromHeap(int heap, uint32_t size);
void* PortHeapReal_GSmemCreatePool(uint32_t num, uint32_t size) {
  sPoolRequest = (num + 1) * ((size + 8 + 7) & ~7u);
  if (sPoolAllocates) OSAllocFromHeap(0, sPoolRequest);
  return reinterpret_cast<void*>(uintptr_t{0x70000000} + size);
}

uint32_t GSmemGetCurrentHeap(void) { return 0; }
uint32_t gc_size_note(uint32_t pc, uint32_t gc);
void* GSmemCreatePool(uint32_t num, uint32_t size);
void* OSAllocFromHeap(int heap, uint32_t size);
void OSFreeToHeap(int heap, void* ptr);
void* HSD_Alloc(uint32_t size);
void PortHeapReal_GSmemInitSysdolAllocator(void) {
  GSmemCreatePool(2, gc_size_note(16, 8));
  GSmemCreatePool(2, gc_size_note(24, 12));
  GSmemCreatePool(2, gc_size_note(40, 20));
  GSmemCreatePool(2, gc_size_note(56, 28));
  GSmemCreatePool(2, gc_size_note(96, 48));
}
static void* pc_pool_for(uint32_t size) {
  for (uint32_t cell : {16u, 24u, 40u, 56u, 96u}) {
    if (size <= cell) return reinterpret_cast<void*>(uintptr_t{0x70000000} + cell);
  }
  return nullptr;
}
void* PortHeapReal_GSmemGetSysdolAllocator(uint32_t size) { return pc_pool_for(size); }
uint32_t sRealPoolFree = 100;
uint32_t PortHeapReal_GSmemGetFreePool(void*) { return sRealPoolFree; }
static uintptr_t sNextCell = 0x60000000u;
void* PortHeapReal_GSmemAllocPool(void*) { return reinterpret_cast<void*>(sNextCell += 0x100); }
void PortHeapReal_GSmemFreePool(void*, void*) {}
void* PortHeapReal_HSD_Alloc(uint32_t size) {
  if (size < 0x40 && pc_pool_for(size) != nullptr) return reinterpret_cast<void*>(sNextCell += 0x100);
  return OSAllocFromHeap(0, size);
}
void PortHeapReal_HSD_Free(void* ptr) {
  if (reinterpret_cast<uintptr_t>(ptr) >= 0x90000000u) OSFreeToHeap(0, ptr);
}
static uint32_t sObjSize = 0;
void PortHeapReal_HSD_ObjAllocInit(void*, uint32_t size, uint32_t align) { sObjSize = (size + align - 1) & ~(align - 1); }
void* PortHeapReal_HSD_ObjAlloc(void*) { return HSD_Alloc(sObjSize); }

void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps);
int OSCreateHeap(void* start, void* end);
void* OSAllocFromHeap(int heap, uint32_t size);
void OSFreeToHeap(int heap, void* ptr);
int32_t OSSetAllocMethodHeap(int32_t heap, int32_t method);
int32_t OSGetHeapFreeSize(int32_t heap, int32_t* total, int32_t* largest);
bool PortXD_HeapModelStats(int heap, int32_t* total, int32_t* largest, int32_t* freeBlocks, int32_t* usedBlocks);
uint32_t gc_size_note(uint32_t pc, uint32_t gc);
void gc_pconly_note(void);
void* GSmemCreatePool(uint32_t num, uint32_t size);
void GSmemInitSysdolAllocator(void);
void* GSmemGetSysdolAllocator(uint32_t size);
uint32_t GSmemGetFreePool(void* pool);
void* GSmemAllocPool(void* pool);
void GSmemFreePool(void* pool, void* cell);
void HSD_Free(void* ptr);
void* HSD_ObjAlloc(void* data);
void HSD_ObjAllocInit(void* data, uint32_t size, uint32_t align);
bool PortXD_HeapModelPoolStats(uint32_t gc, uint32_t* used, uint32_t* num);
}

static int sFailed = 0;
static void expect(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) sFailed++;
}

static void stats(int32_t& total, int32_t& largest, int32_t& freeBlocks, int32_t& used) {
  PortXD_HeapModelStats(0, &total, &largest, &freeBlocks, &used);
}

int main() {
  OSInitAlloc(reinterpret_cast<void*>(uintptr_t{0x80507DA0}), reinterpret_cast<void*>(uintptr_t{0x81800000}), 4);
  OSCreateHeap(reinterpret_cast<void*>(uintptr_t{0x80507EE0}), reinterpret_cast<void*>(uintptr_t{0x80517EE0}));
  int32_t t, l, f, u;
  stats(t, l, f, u);
  expect(t == 0x10000 && l == 0x10000 && f == 1 && u == 0, "new heap: one free block of the whole heap");

  void* a = OSAllocFromHeap(0, 100);
  stats(t, l, f, u);
  expect(t == 0x10000 - 160 && u == 1, "alloc 100: block of 160 bytes (32-byte header, rounded)");

  gc_size_note(200, 40);
  void* b = OSAllocFromHeap(0, 200);
  stats(t, l, f, u);
  expect(t == 0x10000 - 160 - 96, "GC_SIZE note: the block has the GameCube size");

  gc_size_note(999, 8);
  void* c = OSAllocFromHeap(0, 64);
  stats(t, l, f, u);
  expect(t == 0x10000 - 160 - 96 - 96, "a note for another size is ignored");

  gc_pconly_note();
  void* d = OSAllocFromHeap(0, 5000);
  stats(t, l, f, u);
  expect(t == 0x10000 - 352 && u == 3, "GC_PCONLY: left out");
  OSFreeToHeap(0, d);
  stats(t, l, f, u);
  expect(t == 0x10000 - 352 && u == 3, "freeing a PC-only block changes nothing");

  OSFreeToHeap(0, b);
  stats(t, l, f, u);
  expect(f == 2 && l == 0x10000 - 352, "free in the middle: a separate free block");
  OSFreeToHeap(0, a);
  stats(t, l, f, u);
  expect(f == 2 && t == 0x10000 - 96, "free next to a free block: merged (160 + 96)");

  void* e = OSAllocFromHeap(0, 50);
  stats(t, l, f, u);
  expect(f == 2 && t == 0x10000 - 96 - 96, "first fit takes the first block that fits, splits it");
  void* g = OSAllocFromHeap(0, 120);
  stats(t, l, f, u);
  expect(f == 1, "a block that fits exactly takes the whole free block");
  (void)g;
  (void)e;

  OSSetAllocMethodHeap(0, 3);
  void* h = OSAllocFromHeap(0, 1000);
  stats(t, l, f, u);
  expect(f == 1 && l == 0x10000 - 96 - 160 - 96 - 1056, "method 3: cut from the end of the largest block");
  OSSetAllocMethodHeap(0, 0);
  OSFreeToHeap(0, h);
  OSFreeToHeap(0, c);
  stats(t, l, f, u);
  expect(f == 1 && t == 0x10000 - 96 - 160, "frees next to the free rest merge into it");

  sRealTotal = 12345;
  sRealLargest = 678;
  int32_t qt = 0, ql = 0;
  OSGetHeapFreeSize(0, &qt, &ql);
  stats(t, l, f, u);
  expect(qt == t && ql == l, "OSGetHeapFreeSize gives the model's numbers");

  stats(t, l, f, u);
  const int32_t before = t;
  sPoolAllocates = true;
  GSmemCreatePool(8, gc_size_note(16, 8));
  sPoolAllocates = false;
  stats(t, l, f, u);
  expect(sPoolRequest == 216 && before - t == 160, "GSmemCreatePool: the pool's block gets the GameCube size");

  GSmemInitSysdolAllocator();
  uint32_t used = 0, num = 0;
  expect(PortXD_HeapModelPoolStats(28, &used, &num) && used == 0 && num == 2, "sysdol pools known by GameCube size");
  stats(t, l, f, u);
  const int32_t base = t;
  void* o1 = HSD_Alloc(gc_size_note(40, 28));
  void* o2 = HSD_Alloc(gc_size_note(40, 28));
  PortXD_HeapModelPoolStats(28, &used, &num);
  stats(t, l, f, u);
  expect(used == 2 && t == base, "HSD 28-byte GameCube requests: in the 28-byte pool, not the heap");
  void* o3 = HSD_Alloc(gc_size_note(40, 28));
  stats(t, l, f, u);
  expect(t == base - 64, "a full GameCube pool: the request goes to the heap");
  void* o4 = HSD_Alloc(gc_size_note(208, 124));
  stats(t, l, f, u);
  expect(t == base - 64 - 160, "HSD heap request: the GameCube size, the PC's own heap step left out");
  void* o5 = HSD_Alloc(gc_size_note(24, 16));
  stats(t, l, f, u);
  expect(t == base - 64 - 160 - 64, "HSD request of a size without a GameCube pool: heap");
  HSD_Free(o4);
  HSD_Free(o3);
  HSD_Free(o5);
  HSD_Free(o1);
  stats(t, l, f, u);
  PortXD_HeapModelPoolStats(28, &used, &num);
  expect(t == base && used == 1, "HSD_Free: heap blocks and the pool cell given back where the model had them");
  static char objData[64];
  HSD_ObjAllocInit(objData, gc_size_note(24, 12), 4);
  void* o6 = HSD_ObjAlloc(objData);
  PortXD_HeapModelPoolStats(12, &used, &num);
  expect(used == 1, "HSD_ObjAllocInit's note: HSD_ObjAlloc's request has the GameCube size");
  HSD_Free(o6);
  void* p28 = GSmemGetSysdolAllocator(gc_size_note(40, 28));
  expect(GSmemGetFreePool(p28) == 1, "GSmemGetFreePool: the GameCube pool's free cells");
  void* c1 = GSmemAllocPool(p28);
  PortXD_HeapModelPoolStats(28, &used, &num);
  expect(used == 2, "GSmemAllocPool: counted in the GameCube pool");
  p28 = GSmemGetSysdolAllocator(gc_size_note(40, 28));
  expect(GSmemGetFreePool(p28) == 0, "GSmemGetFreePool: 0 once the GameCube pool is full (PC pool still has some)");
  sRealPoolFree = 0;
  GSmemGetSysdolAllocator(gc_size_note(40, 28));
  GSmemFreePool(p28, c1);
  p28 = GSmemGetSysdolAllocator(gc_size_note(40, 28));
  expect(GSmemGetFreePool(p28) == 0, "GSmemGetFreePool: never more than the PC pool really has");
  sRealPoolFree = 100;
  expect(GSmemGetFreePool(p28) == 1, "GSmemFreePool: the cell is free again in the GameCube pool");
  HSD_Free(o2);

  std::printf("%s\n", sFailed == 0 ? "all passed" : "FAILED");
  return sFailed == 0 ? 0 : 1;
}
