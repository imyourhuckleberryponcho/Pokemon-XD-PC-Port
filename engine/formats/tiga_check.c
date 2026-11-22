#include "gs_formats.h"

#include <stdio.h>
#include <string.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int fail(char* why, size_t whySize, const char* text, unsigned a) {
  if (why != NULL && whySize > 0) snprintf(why, whySize, text, a);
  return 0;
}

int Tiga_Validate(const uint8_t* data, size_t size, char* why, size_t whySize) {
  static const char* const kTags[8] = {"FTBL", "HEAD", "CODE", "GVAR", "STRG", "VECT", "GIRI", "ARRY"};
  const uint8_t* at[8] = {0};
  uint32_t len[8] = {0}, count[8] = {0}, u14[8] = {0};
  if (size < 16 || memcmp(data, "TCOD", 4) != 0) return fail(why, whySize, "it does not start with a TCOD chunk%.0u", 0);
  if (be32(data + 4) != size) {
    return fail(why, whySize, "the TCOD size field says %u bytes, the file has a different length", be32(data + 4));
  }
  size_t p = 16;
  while (p < size) {
    if (size - p < 0x20) return fail(why, whySize, "a sub-chunk header at byte %u runs past the end", (unsigned)p);
    const uint32_t sz = be32(data + p + 4);
    if (sz < 0x20 || sz > size - p) return fail(why, whySize, "the sub-chunk at byte %u has a size that does not fit", (unsigned)p);
    for (int i = 0; i < 8; i++) {
      if (memcmp(data + p, kTags[i], 4) != 0) continue;
      if (at[i] != NULL) return fail(why, whySize, "the sub-chunk at byte %u is a second one with the same name", (unsigned)p);
      at[i] = data + p;
      len[i] = sz;
      count[i] = be32(data + p + 0x10);
      u14[i] = be32(data + p + 0x14);
    }
    p += sz;
  }
  for (int i = 0; i < 8; i++) {
    if (at[i] == NULL) {
      if (why != NULL && whySize > 0) snprintf(why, whySize, "the %s sub-chunk is missing", kTags[i]);
      return 0;
    }
  }
  enum { FTBL, HEAD, CODE, GVAR, STRG, VECT, GIRI, ARRY };
  const uint32_t room[8] = {len[0] - 0x20, len[1] - 0x20, len[2] - 0x20, len[3] - 0x20,
                            len[4] - 0x20, len[5] - 0x20, len[6] - 0x20, len[7] - 0x20};
  if (u14[CODE] > room[CODE] / 4 || count[CODE] > 0x10000) return fail(why, whySize, "CODE: %u instructions do not fit its size", u14[CODE]);
  if (count[HEAD] > room[HEAD] / 4) return fail(why, whySize, "HEAD: %u entries do not fit its size", count[HEAD]);
  if (count[GVAR] > room[GVAR] / 8) return fail(why, whySize, "GVAR: %u variables do not fit its size", count[GVAR]);
  if (count[VECT] > room[VECT] / 12) return fail(why, whySize, "VECT: %u vectors do not fit its size", count[VECT]);
  if (count[GIRI] > room[GIRI] / 8) return fail(why, whySize, "GIRI: %u people do not fit its size", count[GIRI]);
  if (count[FTBL] > room[FTBL] / 8) return fail(why, whySize, "FTBL: %u functions do not fit its size", count[FTBL]);
  {
    const uint8_t* s = at[STRG] + 0x20;
    uint32_t used = 0;
    for (uint32_t i = 0; i < count[STRG]; i++) {
      while (used < room[STRG] && s[used] != 0) used++;
      if (used >= room[STRG]) return fail(why, whySize, "STRG: string %u has no end inside the chunk", i);
      used++;
    }
  }
  for (uint32_t i = 0; i < count[FTBL]; i++) {
    const uint8_t* e = at[FTBL] + 0x20 + (size_t)i * 8;
    const uint32_t code = be32(e), name = be32(e + 4);
    if (code >= u14[CODE]) return fail(why, whySize, "FTBL: function %u starts outside the code", i);
    if (name < 0x20 || name >= len[FTBL]) return fail(why, whySize, "FTBL: the name of function %u is outside the chunk", i);
    const uint8_t* n = at[FTBL] + name;
    uint32_t k = name;
    while (k < len[FTBL] && n[k - name] != 0) k++;
    if (k >= len[FTBL]) return fail(why, whySize, "FTBL: the name of function %u has no end", i);
  }
  if (count[ARRY] > 0) {
    if ((uint64_t)count[ARRY] * 4 > room[ARRY]) return fail(why, whySize, "ARRY: %u arrays do not fit its size", count[ARRY]);
    for (uint32_t i = 0; i < count[ARRY]; i++) {
      const uint32_t o = be32(at[ARRY] + 0x20 + (size_t)i * 4);
      if (o < 0x10 || o - 0x10 > room[ARRY] || room[ARRY] - (o - 0x10) < 16) return fail(why, whySize, "ARRY: array %u starts outside the chunk", i);
      const uint32_t n = be32(at[ARRY] + 0x20 + (o - 0x10));
      if (n > (room[ARRY] - (o - 0x10) - 16) / 8) return fail(why, whySize, "ARRY: array %u has more elements than fit", i);
    }
  }
  return 1;
}

static uint32_t placeholder_hash(const char* name) {
  uint32_t h = 2166136261u;
  for (const unsigned char* c = (const unsigned char*)name; *c != 0; c++) {
    h ^= *c;
    h *= 16777619u;
  }
  return (h ^ (h >> 24)) & 0x00FFFFFFu;
}

uint32_t Tiga_FlagPlaceholder(const char* name) { return 0x7E000000u | placeholder_hash(name); }

uint32_t Tiga_FloorPlaceholder(const char* name) { return 0x7D000000u | placeholder_hash(name); }
uint32_t Tiga_BattlePlaceholder(const char* name) { return 0x7A000000u | placeholder_hash(name); }
uint32_t Tiga_ItemPlaceholder(const char* name) { return 0x79000000u | placeholder_hash(name); }
uint32_t Tiga_SpeciesPlaceholder(const char* name) { return 0x78000000u | placeholder_hash(name); }
uint32_t Tiga_SongPlaceholder(const char* name) { return 0x77000000u | placeholder_hash(name); }

size_t Tiga_ReplaceInts(uint8_t* data, size_t size, const uint32_t* from, const uint32_t* to, size_t n) {
  size_t code = 0, words = 0, found = 0;
  if (!Tiga_Validate(data, size, NULL, 0)) return 0;
  for (size_t p = 16; p < size;) {
    const uint32_t sz = be32(data + p + 4);
    if (memcmp(data + p, "CODE", 4) == 0) {
      code = p + 0x20;
      words = be32(data + p + 0x14);
      break;
    }
    p += sz;
  }
  for (size_t i = 0; code != 0 && i < words; i++) {
    const uint8_t* w = data + code + i * 4;
    if (w[0] == 2 && (w[1] == 0 || w[1] == 1 || w[1] == 2 || w[1] == 0x35)) {
      if (w[1] == 1 && w[2] == 0 && w[3] == 0 && i + 1 < words) {
        uint8_t* imm = data + code + (i + 1) * 4;
        const uint32_t v = be32(imm);
        for (size_t k = 0; k < n; k++) {
          if (v == from[k]) {
            imm[0] = (uint8_t)(to[k] >> 24), imm[1] = (uint8_t)(to[k] >> 16), imm[2] = (uint8_t)(to[k] >> 8), imm[3] = (uint8_t)to[k];
            found++;
            break;
          }
        }
      }
      i++;
    }
  }
  return found;
}

uint32_t Tiga_PersonPlaceholder(const char* name) { return 0x7B000000u | placeholder_hash(name); }

static const uint8_t* find_chunk(const uint8_t* data, size_t size, const char* tag, uint32_t* count, uint32_t* length) {
  for (size_t p = 16; p + 0x20 <= size;) {
    const uint32_t sz = be32(data + p + 4);
    if (sz < 0x20 || sz > size - p) return NULL;
    if (memcmp(data + p, tag, 4) == 0) {
      if (count != NULL) *count = be32(data + p + 0x10);
      if (length != NULL) *length = sz;
      return data + p;
    }
    p += sz;
  }
  return NULL;
}

int32_t Tiga_FindFunction(const uint8_t* data, size_t size, const char* name) {
  uint32_t count = 0, len = 0;
  if (!Tiga_Validate(data, size, NULL, 0)) return -1;
  const uint8_t* ftbl = find_chunk(data, size, "FTBL", &count, &len);
  if (ftbl == NULL) return -1;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* e = ftbl + 0x20 + (size_t)i * 8;
    if (strcmp((const char*)(ftbl + be32(e + 4)), name) == 0) return (int32_t)i;
  }
  return -1;
}

size_t Tiga_RemapPeople(uint8_t* data, size_t size, int field, const uint32_t* from, const uint32_t* to, size_t n) {
  uint32_t count = 0, len = 0;
  size_t changed = 0;
  if (field < 0 || field > 1 || !Tiga_Validate(data, size, NULL, 0)) return 0;
  const uint8_t* giri = find_chunk(data, size, "GIRI", &count, &len);
  if (giri == NULL) return 0;
  for (uint32_t i = 0; i < count; i++) {
    uint8_t* w = (uint8_t*)giri + 0x20 + (size_t)i * 8 + (size_t)field * 4;
    const uint32_t v = be32(w);
    for (size_t k = 0; k < n; k++) {
      if (v == from[k]) {
        w[0] = (uint8_t)(to[k] >> 24), w[1] = (uint8_t)(to[k] >> 16), w[2] = (uint8_t)(to[k] >> 8), w[3] = (uint8_t)to[k];
        changed++;
        break;
      }
    }
  }
  return changed;
}
