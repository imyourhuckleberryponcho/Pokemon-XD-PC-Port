#ifndef GS_FORMATS_H
#define GS_FORMATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* Lzss_Unpack(const uint8_t* data, size_t size, uint32_t* outSize);

typedef struct {
  const uint8_t* data;
  size_t size;
  uint32_t count;
  uint32_t entryTable;
} FsysArchive;

typedef struct {
  uint16_t id;
  uint32_t type;
  const char* name;
  const char* fullName;
  uint32_t offset;
  uint32_t size;
  uint32_t packedSize;
  bool packed;
} FsysEntry;

bool Fsys_Open(const uint8_t* data, size_t size, FsysArchive* out);
bool Fsys_Entry(const FsysArchive* fsys, uint32_t index, FsysEntry* out);
int Fsys_Find(const FsysArchive* fsys, const char* name);
uint8_t* Fsys_Extract(const FsysArchive* fsys, uint32_t index, uint32_t* outSize);

uint8_t* Lzss_Pack(const uint8_t* data, uint32_t size, uint32_t* outSize);

typedef struct {
  uint32_t index;
  const uint8_t* data;
  uint32_t size;
} FsysReplacement;

uint8_t* Fsys_Rebuild(const uint8_t* data, size_t size, const FsysReplacement* replacements, size_t count,
                      size_t* outSize);

typedef struct {
  uint16_t id;
  const char* name;
  const uint8_t* data;
  uint32_t size;
  uint32_t templateIndex;
} FsysAddition;

uint8_t* Fsys_RebuildAdded(const uint8_t* data, size_t size, const FsysAddition* add, size_t addCount, size_t* outSize);
uint8_t* Fsys_AddEntry(const uint8_t* data, size_t size, uint16_t id, uint32_t type, const char* name, const uint8_t* contents,
                       uint32_t contentsSize, bool pack, size_t* outSize);

int Tiga_Validate(const uint8_t* data, size_t size, char* why, size_t whySize);

uint32_t Tiga_FlagPlaceholder(const char* name);
uint32_t Tiga_FloorPlaceholder(const char* name);
uint32_t Tiga_BattlePlaceholder(const char* name);
uint32_t Tiga_ItemPlaceholder(const char* name);
uint32_t Tiga_SpeciesPlaceholder(const char* name);
uint32_t Tiga_SongPlaceholder(const char* name);
size_t Tiga_ReplaceInts(uint8_t* data, size_t size, const uint32_t* from, const uint32_t* to, size_t n);

#define TIGA_THIS_ROOM 0x7C000000u
uint32_t Tiga_PersonPlaceholder(const char* name);
int32_t Tiga_FindFunction(const uint8_t* data, size_t size, const char* name);
size_t Tiga_RemapPeople(uint8_t* data, size_t size, int field, const uint32_t* from, const uint32_t* to, size_t n);

enum {
  REL_R_ADDR32 = 1,
  REL_R_ADDR16_LO = 4,
  REL_R_ADDR16_HI = 5,
  REL_R_ADDR16_HA = 6,
  REL_R_REL24 = 10,
  REL_R_EMB_SDA21 = 109,
};

typedef struct {
  const uint8_t* data;
  uint32_t size;
  bool code;
} RelSection;

typedef struct {
  uint32_t module;
  uint8_t section;
  uint8_t type;
  uint8_t targetSection;
  uint32_t offset;
  uint32_t addend;
} RelReloc;

typedef struct {
  uint32_t id;
  uint32_t numSections;
  RelSection* sections;
  RelReloc* relocs;
  uint32_t numRelocs;
  uint8_t prologSection, epilogSection;
  uint32_t prolog, epilog;
} RelModule;

bool Rel_Parse(const uint8_t* file, size_t size, RelModule* out);
size_t Rel_FileExtent(const uint8_t* file);
void Rel_Free(RelModule* rel);
const RelReloc* Rel_FindReloc(const RelModule* rel, uint8_t section, uint32_t offset);

typedef struct {
  uint32_t dolAddress;
  bool isRelAddress;
  uint8_t section;
  uint32_t offset;
  uint32_t constant;
} RelPrologStore;

int Rel_ReadPrologStores(const RelModule* rel, RelPrologStore* out, int max);

#ifdef __cplusplus
}
#endif

#endif
