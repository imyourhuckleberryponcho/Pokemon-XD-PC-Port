#include "port_disc.h"

#include <stdio.h>
#include <string.h>

#define DOL_SECTIONS 18
#define DOL_HEADER_SIZE 0x100

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool PortDol_Read(const uint8_t* dol, size_t dolSize, uint32_t address, void* dst, uint32_t size) {
  if (dol == NULL || dolSize < DOL_HEADER_SIZE) {
    return false;
  }
  for (int i = 0; i < DOL_SECTIONS; i++) {
    const uint32_t offset = be32(dol + 0x00 + i * 4);
    const uint32_t start = be32(dol + 0x48 + i * 4);
    const uint32_t length = be32(dol + 0x90 + i * 4);
    if (length == 0 || address < start || address - start >= length) {
      continue;
    }
    const uint32_t rel = address - start;
    if (size > length - rel || (uint64_t)offset + rel + size > dolSize) {
      return false;
    }
    memcpy(dst, dol + offset + rel, size);
    return true;
  }
  return false;
}

bool PortDol_Load(const uint8_t* dol, size_t dolSize, uint8_t* memory, uint32_t memoryBase, uint32_t memorySize,
                  uint32_t* imageEnd) {
  if (dol == NULL || dolSize < DOL_HEADER_SIZE) {
    return false;
  }
  uint32_t end = 0;
  for (int i = 0; i < DOL_SECTIONS; i++) {
    const uint32_t offset = be32(dol + 0x00 + i * 4);
    const uint32_t start = be32(dol + 0x48 + i * 4);
    const uint32_t length = be32(dol + 0x90 + i * 4);
    if (length == 0) {
      continue;
    }
    if (start < memoryBase || start - memoryBase > memorySize || length > memorySize - (start - memoryBase) ||
        (uint64_t)offset + length > dolSize) {
      return false;
    }
    memcpy(memory + (start - memoryBase), dol + offset, length);
    if (start + length > end) {
      end = start + length;
    }
  }
  const uint32_t bssStart = be32(dol + 0xD8);
  const uint32_t bssSize = be32(dol + 0xDC);
  if (bssSize != 0) {
    if (bssStart < memoryBase || bssStart - memoryBase > memorySize || bssSize > memorySize - (bssStart - memoryBase)) {
      return false;
    }
    for (uint32_t a = bssStart; a < bssStart + bssSize;) {
      uint32_t next = bssStart + bssSize;
      bool inSection = false;
      for (int i = 0; i < DOL_SECTIONS; i++) {
        const uint32_t start = be32(dol + 0x48 + i * 4);
        const uint32_t length = be32(dol + 0x90 + i * 4);
        if (length == 0) {
          continue;
        }
        if (a >= start && a < start + length) {
          a = start + length;
          inSection = true;
          break;
        }
        if (start > a && start < next) {
          next = start;
        }
      }
      if (!inSection) {
        memset(memory + (a - memoryBase), 0, next - a);
        a = next;
      }
    }
    if (bssStart + bssSize > end) {
      end = bssStart + bssSize;
    }
  }
  if (imageEnd != NULL) {
    *imageEnd = end;
  }
  return true;
}

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(PortSha1* s, const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = be32(p + i * 4);
  }
  for (int i = 16; i < 80; i++) {
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d), k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d, k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d, k = 0xCA62C1D6;
    }
    const uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d, d = c, c = rol(b, 30), b = a, a = t;
  }
  s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

void PortSha1_Add(PortSha1* s, const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data;
  s->length += n;
  while (n > 0) {
    size_t take = 64 - s->used;
    if (take > n) {
      take = n;
    }
    memcpy(s->block + s->used, p, take);
    s->used += take, p += take, n -= take;
    if (s->used == 64) {
      sha1_block(s, s->block);
      s->used = 0;
    }
  }
}

void PortSha1_Begin(PortSha1* s) {
  static const uint32_t start[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  memcpy(s->h, start, sizeof(start));
  s->length = 0;
  s->used = 0;
}

void PortSha1_FinishHex(PortSha1* s, char out[41]) {
  const uint64_t bits = s->length * 8;
  const uint8_t pad = 0x80, zero = 0;
  PortSha1_Add(s, &pad, 1);
  while (s->used != 56) {
    PortSha1_Add(s, &zero, 1);
  }
  uint8_t len[8];
  for (int i = 0; i < 8; i++) {
    len[i] = (uint8_t)(bits >> (56 - i * 8));
  }
  PortSha1_Add(s, len, 8);
  for (int i = 0; i < 5; i++) {
    snprintf(out + i * 8, 9, "%08x", s->h[i]);
  }
}

void PortSha1Hex(const void* data, size_t size, char out[41]) {
  PortSha1 s;
  PortSha1_Begin(&s);
  PortSha1_Add(&s, data, size);
  PortSha1_FinishHex(&s, out);
}
