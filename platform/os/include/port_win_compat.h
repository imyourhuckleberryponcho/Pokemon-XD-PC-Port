#ifndef PORT_WIN_COMPAT_H
#define PORT_WIN_COMPAT_H
#ifndef _WIN32
#include "port_win_time.h"
#include "port_seh.h"

#include <execinfo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

typedef uint64_t DWORD64;
typedef unsigned short USHORT;
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

static inline unsigned short RtlCaptureStackBackTrace(unsigned skip, unsigned count, void** frames, void* hash) {
  void* all[64];
  int n, i, out = 0;
  (void)hash;
  n = backtrace(all, 64);
  for (i = (int)skip + 1; i < n && out < (int)count; i++) {
    frames[out++] = all[i];
  }
  return (unsigned short)out;
}

static inline int localtime_s(struct tm* out, const time_t* when) { return localtime_r(when, out) != NULL ? 0 : 1; }
static inline int _putenv(const char* assignment) {
  return putenv(strdup(assignment));
}
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

#endif
#endif
