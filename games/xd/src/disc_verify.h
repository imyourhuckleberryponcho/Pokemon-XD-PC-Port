#ifndef XD_DISC_VERIFY_H
#define XD_DISC_VERIFY_H

#include <port_disc.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortXDDiscVerdictKind {
  XD_DISC_GOOD,
  XD_DISC_USABLE,

  XD_DISC_WARNING,
  XD_DISC_PROBLEM,
} PortXDDiscVerdictKind;

typedef struct PortXDDiscVerdict {
  PortXDDiscVerdictKind kind;
  char text[2048];
  char gameId[7];
  char title[65];
  char format[16];
  bool remembered;
  bool stopped;
} PortXDDiscVerdict;

void PortXD_JudgeDisc(const char* discPath, PortDiscHashProgress progress, void* user, PortXDDiscVerdict* out);

#define XD_DISC_SHA1_GXXE01 "c1b5218f832403d15aa500ac4d6aacc8865c792d"

bool PortXD_CheckDisc(const char* discPath);

void PortXD_DiscUnusable(const char* detail);

const char* PortXD_ChooseDisc(const char* given, bool interactive);
void PortXD_RememberDisc(const char* path);
void PortXD_DiscFailed(const char* path);

#ifdef __cplusplus
}
#endif

#endif
