#include "data_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t kind_size(FieldKind kind, bool pc) {
  switch (kind) {
  case FIELD_U8: return 1;
  case FIELD_U16: return 2;
  case FIELD_U32:
  case FIELD_ADDR32: return 4;
  case FIELD_PTR:
  case FIELD_U32_IN_PTR: return pc ? (uint32_t)sizeof(void*) : 4;
  }
  return 0;
}

static uint32_t stride(const FieldDesc* f, bool pc) {
  const uint16_t s = pc ? f->pcStride : f->gcStride;
  return s != 0 ? s : kind_size(f->kind, pc);
}

bool DataTable_Validate(const TableDesc* desc, char* error, size_t errorSize) {
  uint8_t* seen = (uint8_t*)calloc(desc->gcSize, 1);
  bool ok = seen != NULL;
  for (uint32_t i = 0; ok && i < desc->numFields; i++) {
    const FieldDesc* f = &desc->fields[i];
    for (uint32_t e = 0; ok && e < f->count; e++) {
      const uint32_t gc = f->gcOffset + e * stride(f, false);
      const uint32_t pc = f->pcOffset + e * stride(f, true);
      if (gc + kind_size(f->kind, false) > desc->gcSize || pc + kind_size(f->kind, true) > desc->pcSize) {
        snprintf(error, errorSize, "%s: field %s[%u] lies outside the entry", desc->name, f->name, e);
        ok = false;
        break;
      }
      for (uint32_t b = 0; b < kind_size(f->kind, false); b++) {
        if (seen[gc + b]++) {
          snprintf(error, errorSize, "%s: byte 0x%X is described twice (field %s)", desc->name, gc + b, f->name);
          ok = false;
          break;
        }
      }
    }
  }
  for (uint32_t b = 0; ok && b < desc->gcSize; b++) {
    if (!seen[b]) {
      snprintf(error, errorSize, "%s: byte 0x%X of the GameCube entry isn't described", desc->name, b);
      ok = false;
    }
  }
  free(seen);
  return ok;
}

uint32_t DataTable_Convert(const TableDesc* desc, const uint8_t* gc, uint32_t gcBaseOffset, uint32_t count, void* pc,
                           PointerResolver resolve, void* user) {
  uint32_t unresolved = 0;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* src = gc + (size_t)i * desc->gcSize;
    uint8_t* dst = (uint8_t*)pc + (size_t)i * desc->pcSize;
    memset(dst, 0, desc->pcSize);
    for (uint32_t k = 0; k < desc->numFields; k++) {
      const FieldDesc* f = &desc->fields[k];
      for (uint32_t e = 0; e < f->count; e++) {
        const uint32_t go = f->gcOffset + e * stride(f, false);
        const uint8_t* s = src + go;
        uint8_t* d = dst + f->pcOffset + e * stride(f, true);
        const uint32_t raw4 = f->kind == FIELD_U8 || f->kind == FIELD_U16
                                  ? 0
                                  : (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
        void* p = NULL;
        const int isAddress =
            resolve != NULL && f->kind != FIELD_U8 && f->kind != FIELD_U16
                ? resolve(user, gcBaseOffset + i * desc->gcSize, go, &p)
                : 0;
        switch (f->kind) {
        case FIELD_U8:
          *d = *s;
          break;
        case FIELD_U16: {
          const uint16_t v = (uint16_t)(s[0] << 8 | s[1]);
          memcpy(d, &v, 2);
          break;
        }
        case FIELD_U32:
        case FIELD_U32_IN_PTR:
          if (isAddress != 0) {
            unresolved++;
          }
          if (f->kind == FIELD_U32) {
            memcpy(d, &raw4, 4);
          } else {
            const uintptr_t w = raw4;
            memcpy(d, &w, sizeof(w));
          }
          break;
        case FIELD_PTR:
        case FIELD_ADDR32:
          if (isAddress < 0 || (isAddress == 0 && raw4 != 0)) {
            unresolved++;
            p = NULL;
          }
          if (f->kind == FIELD_PTR) {
            memcpy(d, &p, sizeof(p));
          } else {
            if ((uintptr_t)p > 0xFFFFFFFFu) {
              unresolved++;
            }
            const uint32_t v = (uint32_t)(uintptr_t)p;
            memcpy(d, &v, 4);
          }
          break;
        }
      }
    }
  }
  return unresolved;
}

uint32_t DataTable_VerifyRoundTrip(const TableDesc* desc, const uint8_t* gc, uint32_t count, const void* pc) {
  uint32_t differing = 0;
  uint8_t* back = (uint8_t*)malloc(desc->gcSize);
  if (back == NULL) {
    return count;
  }
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* orig = gc + (size_t)i * desc->gcSize;
    const uint8_t* src = (const uint8_t*)pc + (size_t)i * desc->pcSize;
    memcpy(back, orig, desc->gcSize);
    for (uint32_t k = 0; k < desc->numFields; k++) {
      const FieldDesc* f = &desc->fields[k];
      for (uint32_t e = 0; e < f->count; e++) {
        uint8_t* d = back + f->gcOffset + e * stride(f, false);
        const uint8_t* s = src + f->pcOffset + e * stride(f, true);
        uint32_t v = 0;
        switch (f->kind) {
        case FIELD_U8:
          d[0] = s[0];
          break;
        case FIELD_U16: {
          uint16_t h;
          memcpy(&h, s, 2);
          d[0] = (uint8_t)(h >> 8), d[1] = (uint8_t)h;
          break;
        }
        case FIELD_U32:
          memcpy(&v, s, 4);
          d[0] = (uint8_t)(v >> 24), d[1] = (uint8_t)(v >> 16), d[2] = (uint8_t)(v >> 8), d[3] = (uint8_t)v;
          break;
        case FIELD_U32_IN_PTR: {
          uintptr_t w;
          memcpy(&w, s, sizeof(w));
          v = (uint32_t)w;
          d[0] = (uint8_t)(v >> 24), d[1] = (uint8_t)(v >> 16), d[2] = (uint8_t)(v >> 8), d[3] = (uint8_t)v;
          break;
        }
        case FIELD_PTR:
        case FIELD_ADDR32:
          break;
        }
      }
    }
    if (memcmp(back, orig, desc->gcSize) != 0) {
      differing++;
    }
  }
  free(back);
  return differing;
}
