#ifndef PORT_CRASH_REPORT_H
#define PORT_CRASH_REPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortCrash_Install(void);
void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize);
void PortCrash_WatchWrites(uintptr_t address);
void PortCrash_SampleThread(unsigned afterMs, unsigned everyMs, unsigned count);

void PortCrash_PrintThreadStack(void* thread);

typedef struct PortCrashReport {
  const char* kind;
  const char* summary;
  void* exceptionPointers;
  unsigned long threadId;
} PortCrashReport;
typedef void (*PortCrashReportHook)(const PortCrashReport* report);
void PortCrash_SetReportHook(PortCrashReportHook hook);
int PortCrash_RunReportHook(const char* kind, const char* summary, void* exceptionPointers, unsigned timeoutMs);
int PortCrash_WriteMinidump(const wchar_t* path, const PortCrashReport* report);

#ifdef __cplusplus
}
#endif

#endif
