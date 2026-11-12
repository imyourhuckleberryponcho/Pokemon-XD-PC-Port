#include "gs_formats.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

enum { R_DOLPHIN_NOP = 201, R_DOLPHIN_SECTION = 202, R_DOLPHIN_END = 203 };

static int compare_reloc(const void* a, const void* b) {
  const RelReloc* x = (const RelReloc*)a;
  const RelReloc* y = (const RelReloc*)b;
  if (x->section != y->section) {
    return x->section < y->section ? -1 : 1;
  }
  return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}

bool Rel_Parse(const uint8_t* file, size_t size, RelModule* out) {
  memset(out, 0, sizeof(*out));
  if (size < 0x40) {
    return false;
  }
  out->id = be32(file);
  out->numSections = be32(file + 0x0C);
  const uint32_t sectionInfo = be32(file + 0x10);
  const uint32_t impOffset = be32(file + 0x28);
  const uint32_t impSize = be32(file + 0x2C);
  out->prologSection = file[0x30];
  out->epilogSection = file[0x31];
  out->prolog = be32(file + 0x34);
  out->epilog = be32(file + 0x38);
  if (out->numSections == 0 || out->numSections > 64 || sectionInfo + 8ull * out->numSections > size ||
      impOffset + (uint64_t)impSize > size) {
    return false;
  }

  out->sections = (RelSection*)calloc(out->numSections, sizeof(RelSection));
  for (uint32_t i = 0; i < out->numSections; i++) {
    const uint32_t off = be32(file + sectionInfo + 8 * i);
    const uint32_t len = be32(file + sectionInfo + 8 * i + 4);
    const uint32_t start = off & ~3u;
    out->sections[i].size = len;
    out->sections[i].code = (off & 1) != 0;
    out->sections[i].data = (start != 0 && start + (uint64_t)len <= size) ? file + start : NULL;
  }

  uint32_t capacity = 0;
  for (int pass = 0; pass < 2; pass++) {
    uint32_t n = 0;
    for (uint32_t k = 0; k + 8 <= impSize; k += 8) {
      const uint32_t module = be32(file + impOffset + k);
      uint32_t p = be32(file + impOffset + k + 4);
      uint8_t section = 0;
      uint32_t pos = 0;
      for (; p + 8 <= size; p += 8) {
        const uint16_t delta = be16(file + p);
        const uint8_t type = file[p + 2];
        const uint8_t target = file[p + 3];
        const uint32_t addend = be32(file + p + 4);
        if (type == R_DOLPHIN_END) {
          break;
        }
        if (type == R_DOLPHIN_SECTION) {
          section = target;
          pos = 0;
          continue;
        }
        pos += delta;
        if (type == R_DOLPHIN_NOP) {
          continue;
        }
        if (pass == 1) {
          RelReloc* r = &out->relocs[n];
          r->module = module;
          r->section = section;
          r->offset = pos;
          r->type = type;
          r->targetSection = target;
          r->addend = addend;
        }
        n++;
      }
    }
    if (pass == 0) {
      capacity = n;
      out->relocs = (RelReloc*)calloc(capacity ? capacity : 1, sizeof(RelReloc));
    } else {
      out->numRelocs = n;
    }
  }
  qsort(out->relocs, out->numRelocs, sizeof(RelReloc), compare_reloc);
  return true;
}

size_t Rel_FileExtent(const uint8_t* file) {
  const uint32_t numSections = be32(file + 0x0C);
  const uint32_t sectionInfo = be32(file + 0x10);
  const uint32_t impOffset = be32(file + 0x28);
  const uint32_t impSize = be32(file + 0x2C);
  const uint32_t limit = 64u << 20;
  if (numSections == 0 || numSections > 64 || sectionInfo > limit || impOffset > limit || impSize > 0x10000) {
    return 0;
  }
  size_t end = (size_t)impOffset + impSize;
  for (uint32_t i = 0; i < numSections; i++) {
    const uint32_t off = be32(file + sectionInfo + 8 * i) & ~3u;
    const uint32_t len = be32(file + sectionInfo + 8 * i + 4);
    if (off != 0 && (size_t)off + len > end) {
      end = (size_t)off + len;
    }
  }
  for (uint32_t k = 0; k + 8 <= impSize; k += 8) {
    uint32_t p = be32(file + impOffset + k + 4);
    while (p < limit && file[p + 2] != R_DOLPHIN_END) {
      p += 8;
    }
    if (p >= limit) {
      return 0;
    }
    if ((size_t)p + 8 > end) {
      end = (size_t)p + 8;
    }
  }
  return end;
}

void Rel_Free(RelModule* rel) {
  free(rel->sections);
  free(rel->relocs);
  memset(rel, 0, sizeof(*rel));
}

const RelReloc* Rel_FindReloc(const RelModule* rel, uint8_t section, uint32_t offset) {
  RelReloc key;
  memset(&key, 0, sizeof(key));
  key.section = section;
  key.offset = offset;
  return (const RelReloc*)bsearch(&key, rel->relocs, rel->numRelocs, sizeof(RelReloc), compare_reloc);
}

typedef enum { V_UNKNOWN, V_CONST, V_HI, V_ADDR } ValueKind;
typedef struct {
  ValueKind kind;
  uint32_t value;
  RelReloc target;
} Value;

static Value reloc_value(const RelReloc* r, ValueKind kind) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.kind = kind;
  v.target = *r;
  return v;
}

int Rel_ReadPrologStores(const RelModule* rel, RelPrologStore* out, int max) {
  if (rel->prologSection >= rel->numSections || rel->sections[rel->prologSection].data == NULL) {
    return -1;
  }
  const RelSection* code = &rel->sections[rel->prologSection];
  uint32_t end = code->size;
  if (rel->epilogSection == rel->prologSection && rel->epilog > rel->prolog) {
    end = rel->epilog;
  }
  Value regs[32];
  memset(regs, 0, sizeof(regs));
  int count = 0;

  for (uint32_t pc = rel->prolog; pc + 4 <= end; pc += 4) {
    const uint32_t ins = be32(code->data + pc);
    const uint32_t op = ins >> 26;
    const uint32_t rd = (ins >> 21) & 31;
    const uint32_t ra = (ins >> 16) & 31;
    const uint32_t imm = ins & 0xFFFF;
    const int32_t simm = (int16_t)imm;
    const RelReloc* r = Rel_FindReloc(rel, rel->prologSection, pc + 2);
    if (r == NULL) {
      r = Rel_FindReloc(rel, rel->prologSection, pc);
    }

    switch (op) {
    case 15:
      if (ra == 0 && r != NULL && (r->type == REL_R_ADDR16_HA || r->type == REL_R_ADDR16_HI)) {
        regs[rd] = reloc_value(r, V_HI);
      } else if (ra == 0) {
        regs[rd].kind = V_CONST;
        regs[rd].value = (uint32_t)simm << 16;
      } else {
        regs[rd].kind = V_UNKNOWN;
      }
      break;
    case 14:
      if (ra == 0) {
        regs[rd].kind = V_CONST;
        regs[rd].value = (uint32_t)simm;
      } else if (r != NULL && r->type == REL_R_ADDR16_LO && regs[ra].kind == V_HI &&
                 regs[ra].target.module == r->module && regs[ra].target.addend == r->addend &&
                 regs[ra].target.targetSection == r->targetSection) {
        regs[rd] = reloc_value(r, V_ADDR);
      } else if (regs[ra].kind == V_CONST && r == NULL) {
        regs[rd].value = regs[ra].value + (uint32_t)simm;
        regs[rd].kind = V_CONST;
      } else {
        regs[rd].kind = V_UNKNOWN;
      }
      break;
    case 24:
      if (regs[rd].kind == V_CONST && r == NULL) {
        regs[ra].kind = V_CONST;
        regs[ra].value = regs[rd].value | imm;
      } else {
        regs[ra].kind = V_UNKNOWN;
      }
      break;
    case 36:
      if (r != NULL && r->module == 0) {
        if (regs[rd].kind != V_CONST && regs[rd].kind != V_ADDR) {
          return -1;
        }
        if (count < max) {
          RelPrologStore* s = &out[count];
          s->dolAddress = r->addend;
          s->isRelAddress = regs[rd].kind == V_ADDR;
          s->section = regs[rd].target.targetSection;
          s->offset = regs[rd].target.addend;
          s->constant = regs[rd].value;
          if (s->isRelAddress && regs[rd].target.module != rel->id) {
            return -1;
          }
        }
        count++;
      }
      break;
    case 16: case 18: case 19:
    case 37: case 44: case 45: case 47: case 54: case 55:
      break;
    case 31:
      if (((ins >> 1) & 0x3FF) == 444) {
        regs[ra] = regs[rd];
      } else if (rd != 0 && rd != 1) {
        regs[rd].kind = V_UNKNOWN;
      }
      break;
    default:
      regs[rd].kind = V_UNKNOWN;
      break;
    }
  }
  return count;
}
