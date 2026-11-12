#ifndef XD_COMMON_REL_H
#define XD_COMMON_REL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PokemonData;

#define XD_COMMON_MAX_GLOBALS 256

typedef struct {
  const char* name;
  void* value;
  bool converted;
} XDRelGlobalValue;

typedef struct {
  XDRelGlobalValue globals[XD_COMMON_MAX_GLOBALS];
  uint32_t numGlobals;
  struct PokemonData* pokemon_data;
  uint32_t pokemon_data_number;
  uint32_t tablesFound;
} XDCommonTables;

typedef struct {
  uint32_t gcAddress;
  uint32_t size;
  void* pc;
} XDMainProgramAddress;
void PortXD_SetMainProgramMap(const XDMainProgramAddress* map, size_t count);

bool PortXD_ConvertCommonRel(const uint8_t* relFile, size_t relSize, XDCommonTables* out, char* error, size_t errorSize);

bool PortXD_LoadCommonRel(XDCommonTables* out, char* error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif
