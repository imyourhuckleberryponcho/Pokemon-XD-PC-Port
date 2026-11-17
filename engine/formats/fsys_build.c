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

enum { kEntryHeader = 0x70, kHeaderAlign = 16 };

static size_t align_to(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

uint8_t* Fsys_RebuildAdded(const uint8_t* data, size_t size, const FsysAddition* add, size_t addCount, size_t* outSize) {
  FsysArchive fsys;
  if (!Fsys_Open(data, size, &fsys) || addCount == 0 || fsys.count == 0) {
    return NULL;
  }
  const uint32_t info = be32(data + 0x18);
  if ((size_t)info + 12 > size) {
    return NULL;
  }
  const size_t tableOff = be32(data + info), namesOff = be32(data + info + 4), dataStart = be32(data + info + 8);
  const size_t n = fsys.count, total = n + addCount;
  if (tableOff != fsys.entryTable || be32(data + 0x1C) != dataStart || dataStart > size || namesOff < tableOff + 4 * n) {
    return NULL;
  }
  size_t hdrStart = dataStart;
  for (size_t i = 0; i < n; i++) {
    const size_t h = be32(data + tableOff + 4 * i);
    if (h < hdrStart) {
      hdrStart = h;
    }
  }
  if (hdrStart < namesOff || hdrStart + n * kEntryHeader > dataStart || dataStart - (hdrStart + n * kEntryHeader) >= kAlign) {
    return NULL;
  }
  for (size_t i = 0; i < n; i++) {
    if (be32(data + tableOff + 4 * i) != hdrStart + i * kEntryHeader) {
      return NULL;
    }
  }
  const bool footer = size >= kFooterSize && memcmp(data + size - 4, "FSYS", 4) == 0 && be32(data + kSizeField) == size;
  size_t dataEnd = dataStart;
  for (size_t i = 0; i < n; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, (uint32_t)i, &e)) {
      return NULL;
    }
    const size_t end = (size_t)e.offset + (e.packed ? e.packedSize : e.size);
    if (end > dataEnd) {
      dataEnd = end;
    }
  }
  if (dataEnd > size - (footer ? kFooterSize : 0)) {
    return NULL;
  }

  uint8_t** packed = (uint8_t**)calloc(addCount, sizeof(uint8_t*));
  uint32_t* packedSize = (uint32_t*)calloc(addCount, sizeof(uint32_t));
  size_t newNames = 0;
  bool ok = packed != NULL && packedSize != NULL;
  for (size_t k = 0; ok && k < addCount; k++) {
    ok = add[k].templateIndex < n && add[k].name != NULL && (add[k].data != NULL || add[k].size == 0);
    for (size_t j = 0; ok && j < n; j++) {
      ok = (be32(data + hdrStart + j * kEntryHeader) >> 16) != add[k].id;
    }
    for (size_t j = 0; ok && j < k; j++) {
      ok = add[j].id != add[k].id;
    }
    if (!ok) {
      break;
    }
    packed[k] = Lzss_Pack(add[k].data, add[k].size, &packedSize[k]);
    ok = packed[k] != NULL;
    newNames += strlen(add[k].name) + 1;
  }
  const size_t poolOld = hdrStart - namesOff;
  const size_t tableSize = align_to(4 * total, kHeaderAlign);
  const size_t poolSize = align_to(poolOld + newNames, kHeaderAlign);
  const size_t namesNew = tableOff + tableSize, hdrNew = namesNew + poolSize;
  const size_t dataStartNew = align_to(hdrNew + total * kEntryHeader, kAlign);
  const size_t shiftName = namesNew - namesOff, shiftData = dataStartNew - dataStart;
  const size_t keep = dataEnd - dataStart;
  size_t pos = dataStartNew + keep;
  size_t* at = (size_t*)calloc(addCount, sizeof(size_t));
  ok = ok && at != NULL;
  for (size_t k = 0; ok && k < addCount; k++) {
    pos = align_to(pos, kAlign);
    at[k] = pos;
    pos += packedSize[k];
  }
  const size_t totalSize = align_to(pos, kAlign) + (footer ? kFooterSize : 0);
  uint8_t* out = NULL;
  if (ok && totalSize <= 0xFFFFFFFFu) {
    out = (uint8_t*)calloc(totalSize, 1);
  }
  if (out != NULL) {
    memcpy(out, data, tableOff);
    put_be32(out + info + 4, (uint32_t)namesNew);
    put_be32(out + info + 8, (uint32_t)dataStartNew);
    put_be32(out + 0x0C, (uint32_t)total);
    put_be32(out + 0x1C, (uint32_t)dataStartNew);
    put_be32(out + kSizeField, (uint32_t)totalSize);
    for (size_t i = 0; i < total; i++) {
      put_be32(out + tableOff + 4 * i, (uint32_t)(hdrNew + i * kEntryHeader));
    }
    memcpy(out + namesNew, data + namesOff, poolOld);
    size_t namePos = poolOld;
    for (size_t i = 0; i < n; i++) {
      uint8_t* h = out + hdrNew + i * kEntryHeader;
      memcpy(h, data + hdrStart + i * kEntryHeader, kEntryHeader);
      if (be32(h + 0x04) >= dataStart) {
        put_be32(h + 0x04, be32(h + 0x04) + (uint32_t)shiftData);
      }
      if (be32(h + 0x1C) != 0) {
        put_be32(h + 0x1C, be32(h + 0x1C) + (uint32_t)shiftName);
      }
      put_be32(h + 0x24, be32(h + 0x24) + (uint32_t)shiftName);
    }
    for (size_t k = 0; k < addCount; k++) {
      uint8_t* h = out + hdrNew + (n + k) * kEntryHeader;
      memcpy(h, data + hdrStart + add[k].templateIndex * kEntryHeader, kEntryHeader);
      put_be32(h, (uint32_t)add[k].id << 16 | (be32(h) & 0xFFFF));
      put_be32(h + 0x04, (uint32_t)at[k]);
      put_be32(h + 0x08, add[k].size);
      put_be32(h + 0x0C, be32(h + 0x0C) | 0x80000000u);
      put_be32(h + 0x14, packedSize[k]);
      put_be32(h + 0x1C, 0);
      put_be32(h + 0x24, (uint32_t)(namesNew + namePos));
      memcpy(out + namesNew + namePos, add[k].name, strlen(add[k].name) + 1);
      namePos += strlen(add[k].name) + 1;
      memcpy(out + at[k], packed[k], packedSize[k]);
    }
    memcpy(out + dataStartNew, data + dataStart, keep);
    if (footer) {
      memcpy(out + totalSize - kFooterSize, data + size - kFooterSize, kFooterSize);
    }
    if (outSize != NULL) {
      *outSize = totalSize;
    }
  }
  for (size_t k = 0; packed != NULL && k < addCount; k++) {
    free(packed[k]);
  }
  free(packed);
  free(packedSize);
  free(at);
  return out;
}

enum { kEntryHeaderSize = 0x70, kTablePointer = 0x40, kHeaderSizeField = 0x1C, kCountField = 0x0C };

uint8_t* Fsys_AddEntry(const uint8_t* data, size_t size, uint16_t id, uint32_t type, const char* name, const uint8_t* contents,
                       uint32_t contentsSize, bool pack, size_t* outSize) {
  FsysArchive fsys;
  if (!Fsys_Open(data, size, &fsys) || fsys.count == 0 || name == NULL || (contents == NULL && contentsSize != 0)) {
    return NULL;
  }
  const uint32_t hdr = be32(data + kHeaderSizeField);
  if (hdr < 0x60 || hdr > size || be32(data + kTablePointer) != fsys.entryTable) {
    return NULL;
  }
  size_t dataEnd = hdr;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e) || e.id == id || e.offset < hdr) {
      return NULL;
    }
    const size_t end = (size_t)e.offset + (e.packed ? e.packedSize : e.size);
    if (end > dataEnd) {
      dataEnd = end;
    }
  }
  const bool footer = size >= kFooterSize && memcmp(data + size - 4, "FSYS", 4) == 0 && dataEnd <= size - kFooterSize;
  const size_t keep = footer ? size - kFooterSize : size;
  if (dataEnd > keep) {
    return NULL;
  }

  uint8_t* stored = NULL;
  uint32_t storedSize = contentsSize;
  if (pack) {
    stored = Lzss_Pack(contents != NULL ? contents : (const uint8_t*)"", contentsSize, &storedSize);
    if (stored == NULL) {
      return NULL;
    }
  }
  const size_t nameLen = strlen(name) + 1;
  const size_t tableAt = (hdr + 3) & ~(size_t)3;
  const size_t nameAt = tableAt + 4 * (size_t)(fsys.count + 1);
  const size_t headerAt = (nameAt + nameLen + 3) & ~(size_t)3;
  const size_t hdr2 = align_up(headerAt + kEntryHeaderSize);
  const size_t delta = hdr2 - hdr;
  const size_t entryAt = align_up(keep + delta);
  const size_t total = align_up(entryAt + storedSize) + (footer ? kFooterSize : 0);
  uint8_t* out = NULL;
  if (total <= 0xFFFFFFFFu) {
    out = (uint8_t*)calloc(total, 1);
  }
  if (out == NULL) {
    free(stored);
    return NULL;
  }
  memcpy(out, data, hdr);
  memcpy(out + hdr2, data + hdr, keep - hdr);
  for (uint32_t i = 0; i < fsys.count; i++) {
    put_be32(out + tableAt + 4 * i, be32(data + fsys.entryTable + 4 * i));
  }
  put_be32(out + tableAt + 4 * fsys.count, (uint32_t)headerAt);
  memcpy(out + nameAt, name, nameLen);
  const uint32_t lastAt = be32(data + fsys.entryTable + 4 * (fsys.count - 1));
  uint8_t* h = out + headerAt;
  if ((size_t)lastAt + kEntryHeaderSize <= hdr) {
    memcpy(h, data + lastAt, kEntryHeaderSize);
  }
  memset(h + 0x10, 0, kEntryHeaderSize - 0x10);
  put_be32(h, (uint32_t)id << 16 | type << 9);
  put_be32(h + 0x04, (uint32_t)entryAt);
  put_be32(h + 0x08, contentsSize);
  put_be32(h + 0x0C, pack ? 0x80000000u : 0);
  put_be32(h + 0x14, storedSize);
  put_be32(h + 0x1C, 0);
  put_be32(h + 0x20, type);
  put_be32(h + 0x24, (uint32_t)nameAt);
  memcpy(out + entryAt, stored != NULL ? stored : contents, storedSize);
  free(stored);
  for (uint32_t i = 0; i < fsys.count; i++) {
    uint8_t* eh = out + be32(data + fsys.entryTable + 4 * i);
    put_be32(eh + 0x04, be32(eh + 0x04) + (uint32_t)delta);
  }
  put_be32(out + kCountField, fsys.count + 1);
  put_be32(out + kTablePointer, (uint32_t)tableAt);
  put_be32(out + kHeaderSizeField, (uint32_t)hdr2);
  if (be32(data + 0x48) == hdr) {
    put_be32(out + 0x48, (uint32_t)hdr2);
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
  return out;
}
