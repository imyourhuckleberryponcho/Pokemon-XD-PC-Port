#ifndef PORT_ANDROID_INSTALL_H
#define PORT_ANDROID_INSTALL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
bool PortAndroid_InstallApk(const char* apkPath, char* error, size_t errorSize);
bool PortAndroid_DisplayName(const char* uri, char* name, size_t nameSize);
bool PortAndroid_HttpFetch(const char* url, const char* file, unsigned long long maxBytes, int timeoutMs, bool httpsOnly, const char* userAgent,
                           char* error, size_t errorSize);
void PortAndroid_HttpCancel(void);
#ifdef __cplusplus
}
#endif

#endif
