#ifndef PORT_ADDR_WINDOW_H
#define PORT_ADDR_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XD_ADDR_WINDOW

#define PORT_WIN_SIZE      ((uintptr_t)0x100000000ull)
#define PORT_WIN_NUMBER    ((uint32_t)0x10000000u)
#define PORT_WIN_IMG_LO    ((uint32_t)0x40000000u)
#define PORT_WIN_IMG_SIZE  ((uint32_t)0x10000000u)
#define PORT_WIN_STACK_OFF  ((uint32_t)0x50000000u)
#define PORT_WIN_STACK_SIZE ((uint32_t)0x10000000u)
#define PORT_WIN_STANDIN_LOW ((uint32_t)0x70000000u)

extern uintptr_t g_portWinBase;
extern uintptr_t g_portWinImage;
extern uint32_t g_portWinImgLo;
extern uint32_t g_portWinImgSize;
extern uint32_t g_portWinStandinLow;

extern int g_portWinStrict;

#if defined(_MSC_VER)
__declspec(noreturn) void PortWin_Fail(const char* what, uint64_t value);
#else
void PortWin_Fail(const char* what, uint64_t value) __attribute__((noreturn));
#endif

extern int g_portWinStackLog;
void PortWin_StackLogHit(uintptr_t address);

void PortWin_Note(const char* what, uint64_t value);
void PortWin_NoteFrames(const char* what, uint64_t value, void* const* frames, int count);
extern void (*g_portWinDescribe)(const void* address, char* out, size_t outSize);

#ifdef XD_WINDOW_STANDIN
#ifdef _WIN32
static inline int PortWin_InStackRange(uint32_t v) { (void)v; return 0; }
#else
static inline int PortWin_InStackRange(uint32_t v) { return v - PORT_WIN_STACK_OFF <= PORT_WIN_STACK_SIZE; }
#endif
static inline int PortWin_InMapped(uint32_t v) {
  return v - g_portWinStandinLow <= 0x10000000u || PortWin_InStackRange(v) || v - 0x80000000u <= 0x01800000u || v - 0xC0000000u <= 0x01800000u ||
         v - 0xE0000000u <= 0x00004000u || v - 0x90000000u <= 0x30000000u;
}
#endif

static inline void* PortWin_PtrRaw(uint32_t v) {
  if (v < PORT_WIN_NUMBER) {
    return (void*)(uintptr_t)v;
  }
  if (v - g_portWinImgLo < g_portWinImgSize) {
    return (void*)(g_portWinImage + (v - g_portWinImgLo));
  }
#ifdef XD_WINDOW_STANDIN
  if (!PortWin_InMapped(v)) {
    if (g_portWinStrict == 2 && v != 0xFFFFFFFFu) {
      PortWin_Note("a 4-byte value that is not an address of the window was used as a pointer (a native low pointer kept in 4 bytes)", v);
    }
    return (void*)(uintptr_t)v;
  }
#endif
  return (void*)(g_portWinBase + v);
}

static inline uint32_t PortWin_OffRaw(const void* p) {
  const uintptr_t a = (uintptr_t)p;
  if (a < PORT_WIN_NUMBER) {
    return (uint32_t)a;
  }
  if (a - g_portWinBase < PORT_WIN_SIZE) {
    if (g_portWinStackLog && (uint32_t)(a - g_portWinBase) - PORT_WIN_STACK_OFF < PORT_WIN_STACK_SIZE) {
      PortWin_StackLogHit(a);
    }
    return (uint32_t)(a - g_portWinBase);
  }
  if (a - g_portWinImage < g_portWinImgSize) {
    return g_portWinImgLo + (uint32_t)(a - g_portWinImage);
  }
#ifdef XD_WINDOW_STANDIN
  if (a <= 0xFFFFFFFFu && g_portWinStrict != 1) {
    if (g_portWinStrict == 2) {
      PortWin_Note("a native low pointer (not in the window) was stored in a 4-byte slot", a);
    }
    return (uint32_t)a;
  }
#endif
  PortWin_Fail("an address outside the 4 GB window and the program image was stored in a 4-byte slot", a);
}

extern int g_portWinTrace;
void PortWin_TraceHit(int kind, uintptr_t in, uintptr_t out);
static inline void* PortWin_Ptr(uint32_t v) {
  void* r = PortWin_PtrRaw(v);
  if (g_portWinTrace) PortWin_TraceHit(0, v, (uintptr_t)r);
  return r;
}
static inline uint32_t PortWin_Off(const void* p) {
  const uint32_t r = PortWin_OffRaw(p);
  if (g_portWinTrace) PortWin_TraceHit(1, (uintptr_t)p, r);
  return r;
}

static inline int PortWin_TryOff(const void* p, uint32_t* out) {
  const uintptr_t a = (uintptr_t)p;
  if (a < PORT_WIN_NUMBER) { *out = (uint32_t)a; return 1; }
  if (a - g_portWinBase < PORT_WIN_SIZE) { *out = (uint32_t)(a - g_portWinBase); return 1; }
  if (a - g_portWinImage < g_portWinImgSize) { *out = g_portWinImgLo + (uint32_t)(a - g_portWinImage); return 1; }
#ifdef XD_WINDOW_STANDIN
  if (a <= 0xFFFFFFFFu && !g_portWinStrict) { *out = (uint32_t)a; return 1; }
  if (a <= 0xFFFFFFFFu && g_portWinStrict == 2) { PortWin_Note("a native low pointer (not in the window) was stored in a 4-byte slot", a); *out = (uint32_t)a; return 1; }
#endif
  return 0;
}

int PortWin_Init(void);
int PortWin_Active(void);
void* PortWin_At(uint32_t offset);

void* PortWin_StackMapKind(size_t bytes, int kind);
int PortWin_StackKindOn(int kind);
void* PortWin_StackMap(size_t bytes);
void PortWin_StackUnmap(void* p, size_t bytes);
void* PortWin_TaskStackMap(size_t bytes);
void PortWin_TaskStackUnmap(void* p, size_t bytes);

void* PortWin_Malloc(size_t size);
void* PortWin_Calloc(size_t count, size_t size);
void* PortWin_Realloc(void* p, size_t size);
void PortWin_Free(void* p);
int PortWin_Owns(const void* p);

#else

static inline void* PortWin_Ptr(uint32_t v) { return (void*)(uintptr_t)v; }
static inline uint32_t PortWin_Off(const void* p) { return (uint32_t)(uintptr_t)p; }
static inline int PortWin_Active(void) { return 0; }
static inline int PortWin_TryOff(const void* p, uint32_t* out) {
  *out = (uint32_t)(uintptr_t)p;
  return (uintptr_t)p <= 0xFFFFFFFFu;
}

#endif

#ifdef __cplusplus
}
#endif
#endif
