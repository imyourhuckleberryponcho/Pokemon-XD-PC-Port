#ifndef XD_DISC_TABLES_H
#define XD_DISC_TABLES_H

#include <stdint.h>

#ifdef __cplusplus
#define XDDT_EXTERN_C extern "C"
#define XDDT_STATIC_ASSERT(cond, name) static_assert(cond, #name)
extern "C" {
#else
#define XDDT_EXTERN_C
#define XDDT_STATIC_ASSERT(cond, name) _Static_assert(cond, #name)
#endif

enum {
  XDDT_NUM = 0,
  XDDT_RAW = 1,
  XDDT_FUNC = 2,
  XDDT_DATA = 3,
  XDDT_STR = 4,
};

typedef struct XDDiscLeaf {
  uint8_t kind;
  uint8_t size;
  uint32_t gcOffset;
  uint32_t pcOffset;
  uint32_t count;
} XDDiscLeaf;

typedef struct XDDiscTable {
  const char* name;
  void* pc;
  uint32_t pcSize;
  uint32_t gcAddress;
  uint32_t gcSize;
  uint32_t count;
  uint32_t gcStride;
  uint32_t pcStride;
  const XDDiscLeaf* leaves;
  uint32_t leafCount;
} XDDiscTable;

typedef struct XDDiscRef {
  uint32_t gcAddress;
  uint32_t gcSize;
  void* pc;
  uint32_t pcSize;
} XDDiscRef;

typedef struct XDDiscFixup {
  uint32_t table;
  uint32_t pcOffset;
  uint32_t size;
  const void* source;
} XDDiscFixup;

typedef struct XDDiscTableFile {
  const char* source;
  XDDiscTable* tables;
  uint32_t tableCount;
  const XDDiscRef* refs;
  uint32_t refCount;
  const XDDiscFixup* fixups;
  uint32_t fixupCount;
} XDDiscTableFile;

int PortXD_FillDiscTables(char* error, unsigned errorSize);

int PortXD_DiscTableCount(void);
const XDDiscTable* PortXD_DiscTable(int index);
const XDDiscTable* PortXD_DiscTableAt(const void* pc);

#ifdef __cplusplus
}
#endif

#endif
