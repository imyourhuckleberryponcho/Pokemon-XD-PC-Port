#ifndef PORT_DISC_DIALOG_H
#define PORT_DISC_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortDiscDialogResult {
  PORT_DISC_DIALOG_CHOSEN,
  PORT_DISC_DIALOG_CANCELED,
  PORT_DISC_DIALOG_FAILED,
} PortDiscDialogResult;

PortDiscDialogResult PortDiscDialog_Open(const char* startFolder, char* out, size_t outSize);

#ifdef __cplusplus
}
#endif

#endif
