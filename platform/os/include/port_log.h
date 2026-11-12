#ifndef PORT_LOG_H
#define PORT_LOG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortLog_Start(const char* appFolder, const char* kind, size_t keep);

void PortLog_Flush(void);

const char* PortLog_Path(void);

bool PortLog_CopyTail(const wchar_t* dest, size_t maxBytes);

#ifdef __cplusplus
}
#endif

#endif
