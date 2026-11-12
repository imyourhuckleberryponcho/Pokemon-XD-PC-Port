#include "gs_formats.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

uint8_t* Lzss_Unpack(const uint8_t* data, size_t size, uint32_t* outSize) {
  if (size < 16 || memcmp(data, "LZSS", 4) != 0) {
    return NULL;
  }
  const uint32_t total = be32(data + 4);
  uint8_t* out = (uint8_t*)malloc(total ? total : 1);
  if (out == NULL) {
    return NULL;
  }
  uint8_t window[0x1000];
  memset(window, 0, sizeof(window));
  uint32_t wpos = 0xFEE, flags = 0, dpos = 0;
  size_t in = 16;
  while (dpos < total) {
    flags >>= 1;
    if ((flags & 0x100) == 0) {
      if (in >= size) {
        break;
      }
      flags = data[in++] | 0xFF00;
    }
    if (flags & 1) {
      if (in >= size) {
        break;
      }
      const uint8_t b = data[in++];
      out[dpos++] = b;
      window[wpos] = b;
      wpos = (wpos + 1) & 0xFFF;
    } else {
      if (in + 1 >= size) {
        break;
      }
      const uint32_t lo = data[in++];
      const uint32_t hi = data[in++];
      const uint32_t off = lo | ((hi & 0xF0) << 4);
      const uint32_t len = (hi & 0xF) + 3;
      for (uint32_t k = 0; k < len && dpos < total; k++) {
        const uint8_t c = window[(off + k) & 0xFFF];
        out[dpos++] = c;
        window[wpos] = c;
        wpos = (wpos + 1) & 0xFFF;
      }
    }
  }
  if (dpos != total) {
    free(out);
    return NULL;
  }
  if (outSize != NULL) {
    *outSize = total;
  }
  return out;
}

bool Fsys_Open(const uint8_t* data, size_t size, FsysArchive* out) {
  if (size < 0x60 || memcmp(data, "FSYS", 4) != 0) {
    return false;
  }
  const uint32_t info = be32(data + 0x18);
  if (info + 4 > size) {
    return false;
  }
  out->data = data;
  out->size = size;
  out->count = be32(data + 0x0C);
  out->entryTable = be32(data + info);
  return (uint64_t)out->entryTable + 4ull * out->count <= size;
}

bool Fsys_Entry(const FsysArchive* fsys, uint32_t index, FsysEntry* out) {
  if (index >= fsys->count) {
    return false;
  }
  const uint32_t e = be32(fsys->data + fsys->entryTable + 4 * index);
  if (e + 0x28 > fsys->size) {
    return false;
  }
  const uint8_t* h = fsys->data + e;
  const uint32_t nameOff = be32(h + 0x24);
  const uint32_t fullNameOff = be32(h + 0x1C);
  out->id = (uint16_t)(be32(h) >> 16);
  out->offset = be32(h + 0x04);
  out->size = be32(h + 0x08);
  out->packed = (be32(h + 0x0C) & 0x80000000u) != 0;
  out->packedSize = be32(h + 0x14);
  out->type = be32(h + 0x20);
  out->name = nameOff < fsys->size ? (const char*)fsys->data + nameOff : "";
  out->fullName = fullNameOff != 0 && fullNameOff < fsys->size ? (const char*)fsys->data + fullNameOff : "";
  return true;
}

int Fsys_Find(const FsysArchive* fsys, const char* name) {
  FsysEntry e;
  for (uint32_t i = 0; i < fsys->count; i++) {
    if (Fsys_Entry(fsys, i, &e) && strcmp(e.name, name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

uint8_t* Fsys_Extract(const FsysArchive* fsys, uint32_t index, uint32_t* outSize) {
  FsysEntry e;
  if (!Fsys_Entry(fsys, index, &e)) {
    return NULL;
  }
  const uint32_t stored = e.packed ? e.packedSize : e.size;
  if ((uint64_t)e.offset + stored > fsys->size) {
    return NULL;
  }
  if (e.packed) {
    return Lzss_Unpack(fsys->data + e.offset, stored, outSize);
  }
  uint8_t* out = (uint8_t*)malloc(e.size ? e.size : 1);
  if (out != NULL) {
    memcpy(out, fsys->data + e.offset, e.size);
    if (outSize != NULL) {
      *outSize = e.size;
    }
  }
  return out;
}
