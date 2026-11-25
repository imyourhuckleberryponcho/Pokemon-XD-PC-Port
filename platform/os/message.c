#include "port_message.h"
#include "run_status.h"

#include <SDL3/SDL_messagebox.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#endif

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
    PortRunStatus_MessageWindow(true);
    SDL_ShowSimpleMessageBox(box_flags(kind), title, text, NULL);
    PortRunStatus_MessageWindow(false);
  }
}

#ifdef _WIN32
static const char kComctl6Manifest[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><dependency><dependentAssembly>"
    "<assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
    "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
    "</dependentAssembly></dependency></assembly>\n";

static wchar_t* widen(const char* utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (n <= 0) return NULL;
  wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)n);
  if (w != NULL) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
  return w;
}

typedef HRESULT(WINAPI* TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

static int task_dialog(PortMessageKind kind, const char* title, const char* text, const char* const* labels, int count,
                       int defaultIndex, int* chosen) {
  int shown = 0;
  wchar_t manifestPath[MAX_PATH + 64];
  DWORD n = GetTempPathW(MAX_PATH, manifestPath);
  if (n == 0 || n > MAX_PATH) return 0;
  _snwprintf(manifestPath + n, 63, L"xd_message_%lu.manifest", GetCurrentProcessId());
  HANDLE file = CreateFileW(manifestPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (file == INVALID_HANDLE_VALUE) return 0;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, kComctl6Manifest, (DWORD)strlen(kComctl6Manifest), &written, NULL);
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(manifestPath);
    return 0;
  }
  ACTCTXW ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.cbSize = sizeof ctx;
  ctx.lpSource = manifestPath;
  HANDLE act = CreateActCtxW(&ctx);
  DeleteFileW(manifestPath);
  if (act == INVALID_HANDLE_VALUE) return 0;
  ULONG_PTR cookie = 0;
  if (ActivateActCtx(act, &cookie)) {
    HMODULE comctl = LoadLibraryW(L"comctl32.dll");
    TaskDialogIndirectFn fn = comctl != NULL ? (TaskDialogIndirectFn)GetProcAddress(comctl, "TaskDialogIndirect") : NULL;
    wchar_t* wtitle = widen(title);
    wchar_t* wtext = widen(text);
    TASKDIALOG_BUTTON buttons[4];
    wchar_t* wlabels[4] = {NULL, NULL, NULL, NULL};
    int built = fn != NULL && wtitle != NULL && wtext != NULL && count >= 1 && count <= 4;
    for (int i = 0; built && i < count; i++) {
      wlabels[i] = widen(labels[i]);
      if (wlabels[i] == NULL) built = 0;
      buttons[i].nButtonID = 100 + i;
      buttons[i].pszButtonText = wlabels[i];
    }
    if (built) {
      TASKDIALOGCONFIG cfg;
      memset(&cfg, 0, sizeof cfg);
      cfg.cbSize = sizeof cfg;
      cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
      cfg.pszWindowTitle = wtitle;
      cfg.pszMainIcon = kind == PORT_MESSAGE_ERROR ? TD_ERROR_ICON : kind == PORT_MESSAGE_WARNING ? TD_WARNING_ICON : TD_INFORMATION_ICON;
      cfg.pszContent = wtext;
      cfg.cButtons = (UINT)count;
      cfg.pButtons = buttons;
      cfg.nDefaultButton = 100 + (defaultIndex >= 0 && defaultIndex < count ? defaultIndex : 0);
      int pressed = 0;
      if (SUCCEEDED(fn(&cfg, &pressed, NULL, NULL))) {
        *chosen = pressed >= 100 && pressed < 100 + count ? pressed - 100 : -1;
        shown = 1;
      }
    }
    for (int i = 0; i < 4; i++) free(wlabels[i]);
    free(wtitle);
    free(wtext);
    DeactivateActCtx(0, cookie);
  }
  ReleaseActCtx(act);
  return shown;
}
#endif

int PortMessage_Choose(PortMessageKind kind, const char* title, const char* text, const char* const* labels, int count,
                       int defaultIndex, int escapeIndex, int unattended) {
  print(kind, title, text);
  if (count < 2 || count > 4) return unattended;
  if (!sWindows) {
    fprintf(stderr, "(No message windows: answering \"%s\".)\n", labels[unattended]);
    return unattended;
  }
  int chosen = -1;
  int shown = 0;
  PortRunStatus_MessageWindow(true);
#ifdef _WIN32
  shown = task_dialog(kind, title, text, labels, count, defaultIndex, &chosen);
#endif
  if (!shown) {
    SDL_MessageBoxButtonData buttons[4];
    for (int i = 0; i < count; i++) {
      buttons[i].flags = (i == defaultIndex ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
                         (i == escapeIndex ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
      buttons[i].buttonID = i;
      buttons[i].text = labels[i];
    }
    const SDL_MessageBoxData box = {box_flags(kind) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, title, text, count, buttons,
                                    NULL};
    shown = SDL_ShowMessageBox(&box, &chosen);
  }
  PortRunStatus_MessageWindow(false);
  if (!shown) {
    fprintf(stderr, "(Couldn't show the message window: answering \"%s\".)\n", labels[unattended]);
    return unattended;
  }
  if (chosen < 0 || chosen >= count) chosen = escapeIndex >= 0 && escapeIndex < count ? escapeIndex : unattended;
  fprintf(stderr, "(Chosen: \"%s\".)\n", labels[chosen]);
  return chosen;
}

bool PortMessage_Ask(PortMessageKind kind, const char* title, const char* text, const char* yes, const char* no,
                     bool unattended) {
  const char* labels[2] = {yes, no};
  return PortMessage_Choose(kind, title, text, labels, 2, 0, 1, unattended ? 0 : 1) == 0;
}
