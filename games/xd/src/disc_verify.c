#include "disc_verify.h"

#include <disc_check.h>
#include <port_disc.h>
#include <port_message.h>
#include <port_user_folder.h>

#include <SDL3/SDL_timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define XD_GAME_ID "GXXE01"
#define HASH_TIME_LIMIT_MS (2 * 60 * 1000)

static const char* kTitle = "Pokemon XD";

static bool cache_file(char* out, size_t size) {
  const char* folder = PortUser_FolderUtf8("Pokemon XD");
  return folder[0] != '\0' && snprintf(out, size, "%s/disc_checks.txt", folder) < (int)size;
}

static void problem(const char* text) { PortMessage_Show(PORT_MESSAGE_ERROR, kTitle, text); }

static const char* region_name(char code) {
  switch (code) {
  case 'E': return "USA";
  case 'P': return "European";
  case 'J': return "Japanese";
  default: return NULL;
  }
}

static bool check_game_id(const PortDiscInfo* info, PortXDDiscVerdict* out) {
  const char* id = info->gameId;
  if (strcmp(id, XD_GAME_ID) == 0) {
    return true;
  }
  out->kind = XD_DISC_PROBLEM;
  if (strncmp(id, "GXX", 3) == 0) {
    const char* region = region_name(id[3]);
    snprintf(out->text, sizeof(out->text),
             "This is the %s version of Pokemon XD: Gale of Darkness (game ID %s).\n\n"
             "This version isn't supported yet: for now the game only works with the USA version "
             "(game ID " XD_GAME_ID "). Other versions may be supported later.",
             region != NULL ? region : "another", id);
  } else if (strncmp(id, "GC6", 3) == 0) {
    snprintf(out->text, sizeof(out->text),
             "This is Pokemon Colosseum (game ID %s), not Pokemon XD: Gale of Darkness.\n\n"
             "Colosseum isn't supported yet (it is planned for after XD). "
             "Please choose your Pokemon XD disc image (USA version).",
             id);
  } else {
    snprintf(out->text, sizeof(out->text),
             "This disc isn't Pokemon XD: Gale of Darkness.\n\n"
             "It is \"%s\" (game ID %s).\n\n"
             "Please choose your Pokemon XD disc image (USA version, game ID " XD_GAME_ID ").",
             info->title[0] != '\0' ? info->title : "a game without a name", id[0] != '\0' ? id : "none");
  }
  return false;
}

static void check_content(const char* discPath, const PortDiscInfo* info, PortDiscHashProgress progress, void* user,
                          PortXDDiscVerdict* out) {
  const char* skip = getenv("XD_SKIP_DISC_HASH");
  if (skip != NULL && skip[0] == '1') {
    out->kind = XD_DISC_USABLE;
    snprintf(out->text, sizeof(out->text), "XD_SKIP_DISC_HASH=1: not checking the disc's content.");
    return;
  }
  if (!info->complete) {
    out->kind = XD_DISC_USABLE;
    snprintf(out->text, sizeof(out->text),
             "Your disc image is %s %s file, a format that leaves out unused parts of the disc, so its content "
             "can't be compared with a known-good copy. The game starts without that check.",
             strcmp(info->format, "NKit") == 0 ? "an" : "a", info->format);
    return;
  }

  char cache[2048];
  const bool haveCache = cache_file(cache, sizeof(cache));
  char sha1[41];
  const DiscCheckResult result = DiscCheck_Sha1(discPath, haveCache ? cache : NULL, progress, user, sha1);
  switch (result) {
  case DISC_CHECK_HASHED:
  case DISC_CHECK_REMEMBERED:
    out->remembered = result == DISC_CHECK_REMEMBERED;
    if (strcmp(sha1, XD_DISC_SHA1_GXXE01) == 0) {
      out->kind = XD_DISC_GOOD;
      snprintf(out->text, sizeof(out->text),
               "Disc OK: Pokemon XD, USA (" XD_GAME_ID "), same as a known-good copy%s.",
               out->remembered ? " (checked before)" : "");
      return;
    }
    out->kind = XD_DISC_WARNING;
    snprintf(out->text, sizeof(out->text),
             "Your disc image is Pokemon XD (USA), but its content is different from a known-good copy of the "
             "disc.\n\n"
             "This usually means the file was changed on purpose (for example a ROM hack or a fan "
             "translation), or that it is damaged (a bad or incomplete copy, or a disk problem).\n\n"
             "You can start anyway. A changed disc may play differently from the original, and a damaged one "
             "may crash or freeze later. If you didn't change it on purpose, try making a new copy from your "
             "own GameCube disc.\n\n"
             "(Disc data SHA-1: %s\n expected: " XD_DISC_SHA1_GXXE01 ")",
             sha1);
    return;
  case DISC_CHECK_STOPPED:
    out->kind = XD_DISC_USABLE;
    out->stopped = true;
    snprintf(out->text, sizeof(out->text),
             "The check of the disc's content was stopped before it finished, so the game starts without it. "
             "It will be tried again next time.");
    return;
  case DISC_CHECK_NO_FILE:
  case DISC_CHECK_READ_ERROR:
  default:
    out->kind = XD_DISC_WARNING;
    snprintf(out->text, sizeof(out->text),
             "Part of your disc image couldn't be read:\n%s\n\n"
             "The file may be damaged or incomplete (for example an unfinished download or copy), or the "
             "drive it is on may have a problem or have been disconnected.\n\n"
             "You can start anyway, but the game may crash or freeze when it needs the part that can't be "
             "read. Making a new copy from your own GameCube disc should fix it.",
             discPath);
    return;
  }
}

void PortXD_JudgeDisc(const char* discPath, PortDiscHashProgress progress, void* user, PortXDDiscVerdict* out) {
  memset(out, 0, sizeof(*out));
  out->kind = XD_DISC_PROBLEM;
  if (discPath == NULL || discPath[0] == '\0') {
    snprintf(out->text, sizeof(out->text), "No disc image has been chosen yet.");
    return;
  }

  DiscFileStamp stamp;
  if (!DiscCheck_Stamp(discPath, NULL, 0, &stamp)) {
    snprintf(out->text, sizeof(out->text),
             "The disc image file couldn't be found:\n%s\n\n"
             "Check that the file is still there and that the name and folder are spelled correctly.",
             discPath);
    return;
  }

  PortDiscInfo info;
  char detail[512];
  switch (PortDisc_Probe(discPath, &info, detail, sizeof(detail))) {
  case PORT_DISC_GAMECUBE:
    break;
  case PORT_DISC_WII:
    snprintf(out->text, sizeof(out->text),
             "This is a Wii disc (\"%s\", game ID %s), not a GameCube disc.\n\n"
             "Pokemon XD: Gale of Darkness is a GameCube game: please choose your Pokemon XD disc image "
             "(USA version).",
             info.title, info.gameId);
    return;
  case PORT_DISC_NOT_A_DISC:
  case PORT_DISC_UNREADABLE:
  default:
    snprintf(out->text, sizeof(out->text),
             "This file couldn't be read as a GameCube disc image:\n%s\n\n"
             "It may not be a disc image at all, it may be damaged or incomplete (for example an unfinished "
             "download), or it may be in a format the game can't read. Disc images in ISO, GCM, RVZ, WIA, "
             "GCZ, CISO and WBFS format can be used.\n\n"
             "(Details: %s)",
             discPath, info.kind == PORT_DISC_NOT_A_DISC ? "no GameCube disc header" : detail);
    return;
  }
  snprintf(out->gameId, sizeof(out->gameId), "%s", info.gameId);
  snprintf(out->title, sizeof(out->title), "%s", info.title);
  snprintf(out->format, sizeof(out->format), "%s", info.format != NULL ? info.format : "");

  if (!check_game_id(&info, out)) {
    return;
  }
  check_content(discPath, &info, progress, user, out);
}

typedef struct HashProgress {
  uint64_t started;
  int shown;
  bool timedOut;
} HashProgress;

static bool hash_progress(uint64_t done, uint64_t total, void* user) {
  HashProgress* p = (HashProgress*)user;
  const int percent = total != 0 ? (int)(done * 100 / total) : 0;
  if (percent != p->shown) {
    if (p->shown < 0) {
      fprintf(stderr, "Checking your disc image (this is only done the first time it's used)...\n");
    }
    fprintf(stderr, "\r  %3d%%", percent);
    fflush(stderr);
    p->shown = percent;
  }
  if (SDL_GetTicks() - p->started > HASH_TIME_LIMIT_MS) {
    p->timedOut = true;
    return false;
  }
  return true;
}

bool PortXD_CheckDisc(const char* discPath) {
  if (discPath == NULL || discPath[0] == '\0') {
#ifdef __linux__
    PortMessage_Show(PORT_MESSAGE_INFO, kTitle,
                     "To play, the game needs your own copy of the Pokemon XD: Gale of Darkness disc (USA "
                     "version), saved as a disc image file (for example .iso or .rvz).\n\n"
                     "How to start it:\n"
                     "  - Start \"Pokemon XD.sh\" and choose the file in the window that opens (that window needs "
                     "your desktop's file chooser, or the program \"zenity\"), or\n"
                     "  - From a terminal, in the game's folder:  ./\"Pokemon XD.sh\" \"/home/you/Games/Pokemon XD.iso\"\n\n"
                     "The game remembers a disc image that worked, so this is needed once.");
    return false;
#endif
#ifdef __APPLE__
    PortMessage_Show(PORT_MESSAGE_INFO, kTitle,
                     "To play, the game needs your own copy of the Pokemon XD: Gale of Darkness disc (USA "
                     "version), saved as a disc image file (for example .iso or .rvz).\n\n"
                     "How to start it:\n"
                     "  - Open the app \"Pokemon XD\" and choose the file in the window that opens, or\n"
                     "  - From Terminal:  \"/path/to/Pokemon XD.app/Contents/MacOS/xd\" \"/Users/you/Games/Pokemon XD.iso\"\n\n"
                     "The game remembers a disc image that worked, so this is needed once.");
    return false;
#endif
    PortMessage_Show(PORT_MESSAGE_INFO, kTitle,
                     "To play, the game needs your own copy of the Pokemon XD: Gale of Darkness disc (USA "
                     "version), saved as a disc image file (for example .iso or .rvz).\n\n"
                     "How to start it:\n"
                     "  - Start xd.exe and choose the file in the window that opens, or\n"
                     "  - Start xd_launcher.exe (next to xd.exe) and choose your disc image there, or\n"
                     "  - Drag your disc image file onto xd.exe, or\n"
                     "  - From a command prompt:  xd.exe \"C:\\Games\\Pokemon XD.iso\"");
    return false;
  }
  HashProgress progress = {SDL_GetTicks(), -1, false};
  PortXDDiscVerdict v;
  PortXD_JudgeDisc(discPath, hash_progress, &progress, &v);
  if (progress.shown >= 0) {
    fprintf(stderr, "\n");
  }
  switch (v.kind) {
  case XD_DISC_GOOD:
    fprintf(stderr, "[disc check] %s\n", v.text);
    return true;
  case XD_DISC_USABLE:
    if (v.stopped && progress.timedOut) {
      fprintf(stderr,
              "[disc check] Reading the disc image took longer than %d minutes, so the check was stopped and the "
              "game starts without it. It will be tried again next time.\n",
              HASH_TIME_LIMIT_MS / 60000);
    } else {
      fprintf(stderr, "[disc check] %s\n", v.text);
    }
    return true;
  case XD_DISC_WARNING:
    return PortMessage_Ask(PORT_MESSAGE_WARNING, kTitle, v.text, "Start anyway", "Quit", true);
  case XD_DISC_PROBLEM:
  default:
    problem(v.text);
    return false;
  }
}

void PortXD_DiscUnusable(const char* detail) {
  char text[1536];
  snprintf(text, sizeof(text),
           "The game couldn't start with this disc image after all.\n\n"
           "Most likely the game's main program on it isn't the one from the original USA disc, which this "
           "version of the game needs. That happens with ROM hacks and fan translations that change the "
           "program: please use an unchanged copy of your Pokemon XD disc.\n\n"
           "(Details: %s)",
           detail);
  problem(text);
}
