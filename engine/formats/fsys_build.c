#include "gs_formats.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

enum { kMinMatch = 3, kMaxMatch = 18, kMaxDistance = 4095, kMaxChain = 128, kHashSize = 1 << 15 };

static uint32_t hash3(const uint8_t* p) { return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & (kHashSize - 1); }

uint8_t* Lzss_Pack(const uint8_t* data, uint32_t size, uint32_t* outSize) {
  uint8_t* out = (uint8_t*)malloc(16 + (size_t)size + size / 8 + 1);
  int32_t* head = (int32_t*)malloc(sizeof(int32_t) * kHashSize);
  int32_t* prev = (int32_t*)malloc(sizeof(int32_t) * (size ? size : 1));
  if (out == NULL || head == NULL || prev == NULL) {
    free(out);
    free(head);
    free(prev);
    return NULL;
  }
  for (uint32_t i = 0; i < kHashSize; i++) {
    head[i] = -1;
  }

  size_t o = 16, flagAt = 0;
  unsigned bit = 8;
  uint32_t p = 0, hashed = 0;
  while (p < size) {
    if (bit == 8) {
      flagAt = o++;
      out[flagAt] = 0;
      bit = 0;
    }
    uint32_t bestLen = 0, bestPos = 0;
    if (p + kMinMatch <= size) {
      const uint32_t limit = size - p < kMaxMatch ? size - p : kMaxMatch;
      int chain = 0;
      for (int32_t q = head[hash3(data + p)]; q >= 0 && p - (uint32_t)q <= kMaxDistance && chain < kMaxChain;
           q = prev[q], chain++) {
        uint32_t len = 0;
        while (len < limit && data[(uint32_t)q + len] == data[p + len]) {
          len++;
        }
        if (len > bestLen) {
          bestLen = len;
          bestPos = (uint32_t)q;
          if (len == limit) {
            break;
          }
        }
      }
    }
    const uint32_t step = bestLen >= kMinMatch ? bestLen : 1;
    if (bestLen >= kMinMatch) {
      const uint32_t at = (0xFEE + bestPos) & 0xFFF;
      out[o++] = (uint8_t)(at & 0xFF);
      out[o++] = (uint8_t)(((at >> 4) & 0xF0) | (bestLen - kMinMatch));
    } else {
      out[flagAt] |= (uint8_t)(1u << bit);
      out[o++] = data[p];
    }
    bit++;
    p += step;
    for (; hashed < p && hashed + kMinMatch <= size; hashed++) {
      const uint32_t h = hash3(data + hashed);
      prev[hashed] = head[h];
      head[h] = (int32_t)hashed;
    }
  }
  free(head);
  free(prev);

  memcpy(out, "LZSS", 4);
  put_be32(out + 4, size);
  put_be32(out + 8, (uint32_t)o);
  put_be32(out + 12, 0);
  if (outSize != NULL) {
    *outSize = (uint32_t)o;
  }
  return out;
}

enum { kAlign = 32, kFooterSize = 0x20, kSizeField = 0x20 };

static size_t align_up(size_t v) { return (v + kAlign - 1) & ~(size_t)(kAlign - 1); }

uint8_t* Fsys_Rebuild(const uint8_t* data, size_t size, const FsysReplacement* replacements, size_t count,
                      size_t* outSize) {
  FsysArchive fsys;
  if (!Fsys_Open(data, size, &fsys)) {
    return NULL;
  }
  size_t dataEnd = 0;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e)) {
      return NULL;
    }
    const size_t end = (size_t)e.offset + (e.packed ? e.packedSize : e.size);
    if (end > dataEnd) {
      dataEnd = end;
    }
  }
  const bool footer = size >= kFooterSize && memcmp(data + size - 4, "FSYS", 4) == 0 && dataEnd <= size - kFooterSize;
  const size_t keep = footer ? size - kFooterSize : size;

  uint8_t** stored = (uint8_t**)calloc(count ? count : 1, sizeof(uint8_t*));
  uint32_t* storedSize = (uint32_t*)calloc(count ? count : 1, sizeof(uint32_t));
  size_t* at = (size_t*)calloc(count ? count : 1, sizeof(size_t));
  bool ok = stored != NULL && storedSize != NULL && at != NULL;
  size_t pos = keep;
  for (size_t r = 0; ok && r < count; r++) {
    FsysEntry e;
    ok = Fsys_Entry(&fsys, replacements[r].index, &e) && (replacements[r].data != NULL || replacements[r].size == 0);
    for (size_t s = 0; ok && s < r; s++) {
      ok = replacements[s].index != replacements[r].index;
    }
    if (!ok) {
      break;
    }
    if (e.packed) {
      stored[r] = Lzss_Pack(replacements[r].data, replacements[r].size, &storedSize[r]);
      ok = stored[r] != NULL;
    } else {
      storedSize[r] = replacements[r].size;
    }
    pos = align_up(pos);
    at[r] = pos;
    pos += storedSize[r];
  }
  const size_t total = align_up(pos) + (footer ? kFooterSize : 0);
  uint8_t* out = NULL;
  if (ok && total <= 0xFFFFFFFFu) {
    out = (uint8_t*)calloc(total, 1);
  }
  if (out != NULL) {
    memcpy(out, data, keep);
    for (size_t r = 0; r < count; r++) {
      memcpy(out + at[r], stored[r] != NULL ? stored[r] : replacements[r].data, storedSize[r]);
      uint8_t* h = out + be32(data + fsys.entryTable + 4 * replacements[r].index);
      put_be32(h + 0x04, (uint32_t)at[r]);
      put_be32(h + 0x08, replacements[r].size);
      put_be32(h + 0x14, storedSize[r]);
    }
    if (footer) {
      memcpy(out + total - kFooterSize, data + size - kFooterSize, kFooterSize);
    }
    if (be32(data + kSizeField) == size) {
      put_be32(out + kSizeField, (uint32_t)total);
    }
    if (outSize != NULL) {
      *outSize = total;
    }
  }
  for (size_t r = 0; stored != NULL && r < count; r++) {
    free(stored[r]);
  }
  free(stored);
  free(storedSize);
  free(at);
  return out;
}
