#include "../../../third_party/renderdoc/renderdoc_app.h"

#include <port_user_folder.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <port_win_compat.h>
#endif

static RENDERDOC_API_1_1_2* sApi;
static uint32_t sCaptureFields[16];
static int sNumCaptureFields;

bool PortXD_RenderDocInit(const char* field) {
  if (field == NULL || field[0] == '\0') {
    return false;
  }
  const char* dll = getenv("XD_RENDERDOC_DLL");
#ifdef _WIN32
  HMODULE module = LoadLibraryA(dll != NULL && dll[0] != '\0' ? dll : "C:\\Program Files\\RenderDoc\\renderdoc.dll");
  if (module == NULL) {
    fprintf(stderr, "[renderdoc] renderdoc.dll not found; no capture\n");
    return false;
  }
  pRENDERDOC_GetAPI getApi = (pRENDERDOC_GetAPI)(void*)GetProcAddress(module, "RENDERDOC_GetAPI");
#else
  void* module = dlopen(dll != NULL && dll[0] != '\0' ? dll : "librenderdoc.so", RTLD_NOW);
  if (module == NULL) {
    fprintf(stderr, "[renderdoc] librenderdoc.so not found; no capture\n");
    return false;
  }
  pRENDERDOC_GetAPI getApi = (pRENDERDOC_GetAPI)dlsym(module, "RENDERDOC_GetAPI");
#endif
  if (getApi == NULL || getApi(eRENDERDOC_API_Version_1_1_2, (void**)&sApi) != 1) {
    fprintf(stderr, "[renderdoc] the API isn't available; no capture\n");
    sApi = NULL;
    return false;
  }
  char path[MAX_PATH];
#ifdef _WIN32
  snprintf(path, sizeof(path), "%s\\renderdoc", PortUser_FolderUtf8("Pokemon XD"));
  CreateDirectoryA(path, NULL);
  strncat(path, "\\xd", sizeof(path) - strlen(path) - 1);
#else
  snprintf(path, sizeof(path), "%s/renderdoc", PortUser_FolderUtf8("Pokemon XD"));
  mkdir(path, 0755);
  strncat(path, "/xd", sizeof(path) - strlen(path) - 1);
#endif
  sApi->SetCaptureFilePathTemplate(path);
  for (const char* p = field; *p != '\0' && sNumCaptureFields < 16;) {
    char* end;
    sCaptureFields[sNumCaptureFields++] = (uint32_t)strtoul(p, &end, 10);
    p = *end == ',' ? end + 1 : end + strlen(end);
  }
  fprintf(stderr, "[renderdoc] will capture %d frame(s), the first at field %u, into %s_*.rdc\n", sNumCaptureFields,
          sCaptureFields[0], path);
  return true;
}

void PortXD_RenderDocField(uint32_t field) {
  if (sApi == NULL) {
    return;
  }
  for (int i = 0; i < sNumCaptureFields; i++) {
    if (field == sCaptureFields[i]) {
      sApi->TriggerCapture();
      fprintf(stderr, "[renderdoc] capture triggered at field %u\n", field);
    }
  }
}
