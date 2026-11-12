#ifndef PORT_MESSAGE_H
#define PORT_MESSAGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortMessageKind {
  PORT_MESSAGE_ERROR,
  PORT_MESSAGE_WARNING,
  PORT_MESSAGE_INFO,
} PortMessageKind;

void PortMessage_Show(PortMessageKind kind, const char* title, const char* text);

bool PortMessage_Ask(PortMessageKind kind, const char* title, const char* text, const char* yes, const char* no,
                     bool unattended);

void PortMessage_SetWindows(bool on);

#ifdef __cplusplus
}
#endif

#endif
