#include "disc_check.h"

#include <cstring>

extern "C" DiscCheckResult DiscCheck_Sha1(const char* discPath, const char* cacheFile, PortDiscHashProgress progress,
                                          void* user, char sha1[41]) {
  char fullPath[4096];
  DiscFileStamp stamp{};
  sha1[0] = '\0';
  if (!DiscCheck_Stamp(discPath, fullPath, sizeof(fullPath), &stamp)) {
    return DISC_CHECK_NO_FILE;
  }
  if (cacheFile != nullptr && DiscCheck_Recall(cacheFile, fullPath, &stamp, sha1)) {
    return DISC_CHECK_REMEMBERED;
  }
  switch (PortDisc_HashSha1(fullPath, progress, user, sha1)) {
  case PORT_DISC_HASH_OK:
    break;
  case PORT_DISC_HASH_STOPPED:
    return DISC_CHECK_STOPPED;
  default:
    return DISC_CHECK_READ_ERROR;
  }
  DiscFileStamp after{};
  if (cacheFile != nullptr && DiscCheck_Stamp(fullPath, nullptr, 0, &after) && after.size == stamp.size &&
      after.modified == stamp.modified) {
    DiscCheck_Remember(cacheFile, fullPath, &stamp, sha1);
  }
  return DISC_CHECK_HASHED;
}
