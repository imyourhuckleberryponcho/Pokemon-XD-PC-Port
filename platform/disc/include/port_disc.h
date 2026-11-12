#ifndef PORT_DISC_H
#define PORT_DISC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortDisc_Open(const char* path, char* error, size_t errorSize);
void PortDisc_Close(void);

const char* PortDisc_GameId(void);

const uint8_t* PortDisc_Dol(size_t* size);

typedef void (*PortDiscFileCallback)(const char* path, uint32_t size, void* user);
void PortDisc_ListFiles(PortDiscFileCallback callback, void* user);

bool PortDisc_FileDiscOffset(const char* path, uint32_t* offset);

uint8_t* PortDisc_ReadFile(const char* path, size_t* size);
uint8_t* PortDisc_ReadOriginalFile(const char* path, size_t* size);
void PortDisc_Free(void* data);

typedef struct PortDiscOverlayFile {
  const char* path;
  const char* localFile;
  const uint8_t* data;
  uint64_t size;
} PortDiscOverlayFile;

bool PortDisc_SetOverlay(const PortDiscOverlayFile* files, size_t count);
size_t PortDisc_OverlayCount(void);

void PortDisc_InstallOverlayInAurora(void);

bool PortDol_Read(const uint8_t* dol, size_t dolSize, uint32_t address, void* dst, uint32_t size);
bool PortDol_Load(const uint8_t* dol, size_t dolSize, uint8_t* memory, uint32_t memoryBase, uint32_t memorySize,
                  uint32_t* imageEnd);

void PortSha1Hex(const void* data, size_t size, char out[41]);

typedef struct PortSha1 {
  uint32_t h[5];
  uint64_t length;
  uint8_t block[64];
  size_t used;
} PortSha1;
void PortSha1_Begin(PortSha1* s);
void PortSha1_Add(PortSha1* s, const void* data, size_t size);
void PortSha1_FinishHex(PortSha1* s, char out[41]);

typedef enum PortDiscKind {
  PORT_DISC_UNREADABLE,
  PORT_DISC_NOT_A_DISC,
  PORT_DISC_WII,
  PORT_DISC_GAMECUBE,
} PortDiscKind;

typedef struct PortDiscInfo {
  PortDiscKind kind;
  char gameId[7];
  char title[65];
  uint8_t discNumber;
  uint8_t version;
  const char* format;
  uint64_t discSize;
  bool complete;
} PortDiscInfo;

PortDiscKind PortDisc_Probe(const char* path, PortDiscInfo* info, char* error, size_t errorSize);

typedef bool (*PortDiscHashProgress)(uint64_t done, uint64_t total, void* user);

typedef enum PortDiscHashResult {
  PORT_DISC_HASH_OK,
  PORT_DISC_HASH_STOPPED,
  PORT_DISC_HASH_READ_ERROR,
} PortDiscHashResult;

PortDiscHashResult PortDisc_HashSha1(const char* path, PortDiscHashProgress progress, void* user, char out[41]);

#ifdef __cplusplus
}
#endif

#endif
