#include "port_message.h"

#include <SDL3/SDL_messagebox.h>

#include <stdio.h>

static bool sWindows = true;

void PortMessage_SetWindows(bool on) { sWindows = on; }

static SDL_MessageBoxFlags box_flags(PortMessageKind kind) {
  switch (kind) {
  case PORT_MESSAGE_ERROR: return SDL_MESSAGEBOX_ERROR;
  case PORT_MESSAGE_WARNING: return SDL_MESSAGEBOX_WARNING;
  default: return SDL_MESSAGEBOX_INFORMATION;
  }
}

static void print(PortMessageKind kind, const char* title, const char* text) {
  static const char* labels[] = {"PROBLEM", "WARNING", "NOTE"};
  fprintf(stderr, "\n==== %s: %s ====\n%s\n\n", labels[kind], title, text);
  fflush(stderr);
}

void PortMessage_Show(PortMessageKind kind, const char* title, const char* text) {
  print(kind, title, text);
  if (sWindows) {
    SDL_ShowSimpleMessageBox(box_flags(kind), title, text, NULL);
  }
}

bool PortMessage_Ask(PortMessageKind kind, const char* title, const char* text, const char* yes, const char* no,
                     bool unattended) {
  print(kind, title, text);
  if (!sWindows) {
    fprintf(stderr, "(No message windows: answering \"%s\".)\n", unattended ? yes : no);
    return unattended;
  }
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no},
  };
  const SDL_MessageBoxData box = {box_flags(kind) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, title, text, 2, buttons,
                                  NULL};
  int chosen = -1;
  if (!SDL_ShowMessageBox(&box, &chosen) || chosen < 0) {
    fprintf(stderr, "(Couldn't show the message window: answering \"%s\".)\n", unattended ? yes : no);
    return unattended;
  }
  fprintf(stderr, "(Chosen: \"%s\".)\n", chosen == 1 ? yes : no);
  return chosen == 1;
}
