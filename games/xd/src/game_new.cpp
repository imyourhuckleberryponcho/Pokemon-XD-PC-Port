#include <cstdint>
#include <cstdlib>
#ifdef _MSC_VER
#include <intrin.h>
#else
#define _ReturnAddress() __builtin_return_address(0)
#endif
#include <new>

extern "C" {
void GSmemFree(void* ptr);
void* GSmemAlloc(uint32_t size);
bool GSmemIsInitialized(void);
}

#ifdef _MSC_VER
#pragma code_seg(".text$xda")
extern "C" __declspec(noinline) void PortXD_GameCodeStart(void) {}
#pragma code_seg(".text$xdz")
extern "C" __declspec(noinline) void PortXD_GameCodeEnd(void) {}
#pragma code_seg()
#pragma section(".xdgd$a", read, write)
#pragma section(".xdgd$z", read, write)
__declspec(allocate(".xdgd$a")) unsigned char gPortXD_GameDataBegin[16] = {1};
__declspec(allocate(".xdgd$z")) unsigned char gPortXD_GameDataEnd[16] = {1};
extern "C" void PortXD_GameDataRange(unsigned char** begin, size_t* size) {
  *begin = gPortXD_GameDataBegin;
  *size = static_cast<size_t>(gPortXD_GameDataEnd + sizeof(gPortXD_GameDataEnd) - gPortXD_GameDataBegin);
}
extern "C" void PortXD_GameDataRange2(unsigned char** begin, size_t* size) {
  *begin = nullptr;
  *size = 0;
}
#else
#ifdef __APPLE__
extern char __start_xdg_text[] __asm("section$start$__TEXT$xdg_text");
extern char __stop_xdg_text[] __asm("section$end$__TEXT$xdg_text");
extern unsigned char __start_xdg_data[] __asm("section$start$__DATA$xdg_data");
extern unsigned char __stop_xdg_data[] __asm("section$end$__DATA$xdg_data");
extern unsigned char __start_xdg_bss[] __asm("section$start$__DATA$xdg_bss");
extern unsigned char __stop_xdg_bss[] __asm("section$end$__DATA$xdg_bss");
#else
extern "C" {
extern char __start_xdg_text[];
extern char __stop_xdg_text[];
extern unsigned char __start_xdg_data[];
extern unsigned char __stop_xdg_data[];
extern unsigned char __start_xdg_bss[];
extern unsigned char __stop_xdg_bss[];
}
#endif
extern "C" void PortXD_GameDataRange(unsigned char** begin, size_t* size) {
  *begin = __start_xdg_data;
  *size = static_cast<size_t>(__stop_xdg_data - __start_xdg_data);
}
extern "C" void PortXD_GameDataRange2(unsigned char** begin, size_t* size) {
  *begin = __start_xdg_bss;
  *size = static_cast<size_t>(__stop_xdg_bss - __start_xdg_bss);
}
#endif

namespace {

#ifdef _MSC_VER
bool called_from_game(void* returnAddress) {
  const auto a = reinterpret_cast<uintptr_t>(returnAddress);
  return a >= reinterpret_cast<uintptr_t>(&PortXD_GameCodeStart) && a < reinterpret_cast<uintptr_t>(&PortXD_GameCodeEnd);
}
#else
bool called_from_game(void* returnAddress) {
  const auto a = reinterpret_cast<uintptr_t>(returnAddress);
  return a >= reinterpret_cast<uintptr_t>(__start_xdg_text) && a < reinterpret_cast<uintptr_t>(__stop_xdg_text);
}
#endif

bool in_game_heap(void* ptr) {
  const auto a = reinterpret_cast<uintptr_t>(ptr);
  return a >= 0x80000000u && a < 0x81800000u;
}

void* game_new(size_t size) { return GSmemIsInitialized() ? GSmemAlloc(static_cast<uint32_t>(size)) : nullptr; }

void* pc_new(size_t size) {
  void* p = std::malloc(size == 0 ? 1 : size);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}

void either_delete(void* ptr) {
  if (ptr == nullptr) return;
  if (in_game_heap(ptr)) {
    GSmemFree(ptr);
  } else {
    std::free(ptr);
  }
}

}

extern "C" bool PortXD_HeapCheckEnabled(void);
extern "C" bool PortXD_CheckGameHeap(void);

void* operator new(size_t size) {
  if (!called_from_game(_ReturnAddress())) return pc_new(size);
  if (PortXD_HeapCheckEnabled() && GSmemIsInitialized() && !PortXD_CheckGameHeap()) std::abort();
  return game_new(size);
}
void* operator new[](size_t size) { return called_from_game(_ReturnAddress()) ? game_new(size) : pc_new(size); }
void operator delete(void* ptr) noexcept { either_delete(ptr); }
void operator delete[](void* ptr) noexcept { either_delete(ptr); }
void operator delete(void* ptr, size_t) noexcept { either_delete(ptr); }
void operator delete[](void* ptr, size_t) noexcept { either_delete(ptr); }
