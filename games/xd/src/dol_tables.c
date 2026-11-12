#include "dol_tables.h"

#include <port_disc.h>

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char* name;
  uint32_t address;
  uint32_t size;
  uint8_t* data;
} DolTable;

static DolTable sTables[] = {
    {"dspSlave", 0x80409040, 0x1EC0, NULL},
    {"resident_fnt", 0x8032EE80, 0x48A10, NULL},
    {"common_fnt", 0x80377890, 0x730, NULL},
    {"common_tbl", 0x80377FC0, 0x178BC, NULL},
    {"system_error_fnt", 0x804E7F9C, 0x8, NULL},
    {"system_error_tbl", 0x8038F87C, 0x364, NULL},
    {"debug_menu_fnt", 0x803B1A94, 0x10984, NULL},
    {"debug_menu_tbl", 0x8038FBE0, 0x21EB4, NULL},
    {"uv_cmn_win_00", 0x803D5280, 0x6E0, NULL},
    {"tex_warning", 0x803D5960, 0x21480, NULL},
    {"tex_warning_pstar", 0x803F6DE0, 0x3980, NULL},
    {"uv_cmn_csr_00", 0x803FA760, 0x1680, NULL},
    {"uv_common_icon_00", 0x803FBDE0, 0x1C0, NULL},
    {"_memcardBannerImage", 0x802F4D08, 0x1800, NULL},
    {"_memcardIconImage", 0x802F6508, 0x2200, NULL},
};
#define TABLE_COUNT ((int)(sizeof(sTables) / sizeof(sTables[0])))

#define MEM1_BASE 0x80000000u
#define MEM1_SIZE 0x01800000u
static uint32_t sArenaLo = 0;
static uint32_t sBssStart = 0, sBssEnd = 0, sImageEnd = 0;
static uint32_t sSections[18][2];

static uint32_t be32_at(const uint8_t* p, size_t at) {
  return (uint32_t)p[at] << 24 | (uint32_t)p[at + 1] << 16 | (uint32_t)p[at + 2] << 8 | p[at + 3];
}

static uint32_t xd_arena_lo(uint32_t imageEnd) {
  const uint32_t stackAddr = (imageEnd + 0x10004 + 0x7) & ~0x7u;
  const uint32_t dbStackAddr = stackAddr + 0x8000;
  return (dbStackAddr + 0x1F) & ~0x1Fu;
}

bool PortXD_LoadDolTables(const uint8_t* dol, size_t dolSize, char* error, size_t errorSize) {
  char sha1[41];
  PortSha1Hex(dol, dolSize, sha1);
  if (strcmp(sha1, XD_DOL_SHA1_GXXE01) != 0) {
    snprintf(error, errorSize,
             "This isn't the USA version of Pokemon XD this port supports (main.dol SHA-1 %s, expected %s)",
             sha1, XD_DOL_SHA1_GXXE01);
    return false;
  }
  uint8_t* mem1 = PortMem_MEM1();
  if (mem1 != NULL) {
    uint32_t imageEnd = 0;
    if (!PortDol_Load(dol, dolSize, mem1, MEM1_BASE, MEM1_SIZE, &imageEnd)) {
      snprintf(error, errorSize, "main.dol doesn't fit in GameCube memory");
      return false;
    }
    sArenaLo = xd_arena_lo(imageEnd);
    sBssStart = (uint32_t)dol[0xD8] << 24 | (uint32_t)dol[0xD9] << 16 | (uint32_t)dol[0xDA] << 8 | dol[0xDB];
    sBssEnd = sBssStart + ((uint32_t)dol[0xDC] << 24 | (uint32_t)dol[0xDD] << 16 | (uint32_t)dol[0xDE] << 8 | dol[0xDF]);
    sImageEnd = imageEnd;
    for (int i = 0; i < 18; i++) {
      sSections[i][0] = be32_at(dol, 0x48 + 4 * i);
      sSections[i][1] = be32_at(dol, 0x90 + 4 * i);
    }
    for (int i = 0; i < TABLE_COUNT; i++) {
      sTables[i].data = mem1 + (sTables[i].address - MEM1_BASE);
    }
    return true;
  }
  for (int i = 0; i < TABLE_COUNT; i++) {
    DolTable* t = &sTables[i];
    if (t->data == NULL) {
#ifdef _WIN32
      t->data = (uint8_t*)_aligned_malloc(t->size, 32);
#else
      t->data = (uint8_t*)aligned_alloc(32, (t->size + 31) & ~(size_t)31);
#endif
    }
    if (t->data == NULL || !PortDol_Read(dol, dolSize, t->address, t->data, t->size)) {
      snprintf(error, errorSize, "Could not read %s from main.dol", t->name);
      return false;
    }
  }
  return true;
}

uint8_t* PortXD_GetDolTable(const char* name) {
  for (int i = 0; i < TABLE_COUNT; i++) {
    if (strcmp(sTables[i].name, name) == 0) {
      return sTables[i].data;
    }
  }
  return NULL;
}

uint32_t PortXD_ArenaLo(void) { return sArenaLo; }

void PortXD_DolImage(uint32_t* bssStart, uint32_t* bssEnd, uint32_t* imageEnd) {
  *bssStart = sBssStart;
  *bssEnd = sBssEnd;
  *imageEnd = sImageEnd;
}

bool PortXD_DolIsLoaded(uint32_t address, uint32_t size) {
  for (int i = 0; i < 18; i++) {
    if (sSections[i][1] != 0 && address >= sSections[i][0] && address - sSections[i][0] <= sSections[i][1] &&
        size <= sSections[i][1] - (address - sSections[i][0])) {
      return true;
    }
  }
  return false;
}

int PortXD_DolTableCount(void) { return TABLE_COUNT; }
const char* PortXD_DolTableName(int index) { return sTables[index].name; }
uint32_t PortXD_DolTableSize(int index) { return sTables[index].size; }
