#ifndef XD_DOL_TABLES_H
#define XD_DOL_TABLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XD_DOL_SHA1_GXXE01 "ff9e752ead9914af0b363ae6c831a34ccce189d2"

bool PortXD_LoadDolTables(const uint8_t* dol, size_t dolSize, char* error, size_t errorSize);
bool PortXD_LoadDspProgram(void);

uint32_t PortXD_ArenaLo(void);

void PortXD_DolImage(uint32_t* bssStart, uint32_t* bssEnd, uint32_t* imageEnd);

bool PortXD_DolIsLoaded(uint32_t address, uint32_t size);

uint8_t* PortXD_GetDolTable(const char* name);

int PortXD_DolTableCount(void);
const char* PortXD_DolTableName(int index);
uint32_t PortXD_DolTableSize(int index);

#ifdef __cplusplus
}
#endif

#endif
