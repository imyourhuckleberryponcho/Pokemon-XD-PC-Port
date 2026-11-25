#ifndef PORT_FOLDER_CLAIM_H
#define PORT_FOLDER_CLAIM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortFolderClaim_Take(const char* folder);

bool PortFolderClaim_IsFree(const char* folder);

typedef struct PortFolderHolder {
  unsigned long pid;
  long long started;
  char program[520];
  bool alive;
  bool ours;
} PortFolderHolder;
bool PortFolderClaim_Holder(const char* folder, PortFolderHolder* out);

bool PortFolderClaim_EndHolder(const PortFolderHolder* holder);

#ifdef __cplusplus
}
#endif

#endif
