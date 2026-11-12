#ifndef ENGINE_DISC_CHECK_H
#define ENGINE_DISC_CHECK_H

#include <port_disc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiscFileStamp {
  uint64_t size;
  int64_t modified;
} DiscFileStamp;

bool DiscCheck_Stamp(const char* path, char* fullPath, size_t fullPathSize, DiscFileStamp* stamp);

bool DiscCheck_Recall(const char* cacheFile, const char* fullPath, const DiscFileStamp* stamp, char sha1[41]);

#define DISC_CHECK_MAX_ENTRIES 32
bool DiscCheck_Remember(const char* cacheFile, const char* fullPath, const DiscFileStamp* stamp, const char* sha1);

typedef enum DiscCheckResult {
  DISC_CHECK_HASHED,
  DISC_CHECK_REMEMBERED,
  DISC_CHECK_NO_FILE,
  DISC_CHECK_STOPPED,
  DISC_CHECK_READ_ERROR,
} DiscCheckResult;

DiscCheckResult DiscCheck_Sha1(const char* discPath, const char* cacheFile, PortDiscHashProgress progress, void* user,
                               char sha1[41]);

#ifdef __cplusplus
}
#endif

#endif
