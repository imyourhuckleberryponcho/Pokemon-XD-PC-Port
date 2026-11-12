#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FIELD_U8,
  FIELD_U16,
  FIELD_U32,
  FIELD_PTR,
  FIELD_U32_IN_PTR,

  FIELD_ADDR32,

} FieldKind;

typedef struct {
  const char* name;
  FieldKind kind;
  uint16_t count;
  uint32_t gcOffset;
  uint32_t pcOffset;
  uint16_t gcStride;
  uint16_t pcStride;
} FieldDesc;

typedef struct {
  const char* name;
  uint32_t gcSize;
  uint32_t pcSize;
  const FieldDesc* fields;
  uint32_t numFields;
} TableDesc;

typedef int (*PointerResolver)(void* user, uint32_t gcEntryOffset, uint32_t fieldGcOffset, void** out);

bool DataTable_Validate(const TableDesc* desc, char* error, size_t errorSize);

uint32_t DataTable_Convert(const TableDesc* desc, const uint8_t* gc, uint32_t gcBaseOffset, uint32_t count, void* pc,
                       PointerResolver resolve, void* user);

uint32_t DataTable_VerifyRoundTrip(const TableDesc* desc, const uint8_t* gc, uint32_t count, const void* pc);

#ifdef __cplusplus
}
#endif

#endif
