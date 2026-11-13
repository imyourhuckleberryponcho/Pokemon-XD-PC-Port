#include "disc_verify.h"
#include "dol_tables.h"
#include "disc_tables.h"

#include <crash_report.h>
#include <port_log.h>
#include <run_status.h>
#include <port_message.h>

#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/event.h>
#include <aurora/gpu_timing.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <port_disc.h>
#include <port_user_folder.h>
#include <port_video.h>
#include <port_photo.h>
#include <port_frame_rate.h>
#include <port_capture.h>
#include <port_speedups.h>
#include <port_menu.h>
#include <port_focus_pause.h>
#include <port_fps.h>
#include <port_window_icon.h>
#include <port_folder_claim.h>
#include <settings_c.h>

#ifdef _WIN32
#include <intrin.h>
#else
#include <port_win_compat.h>
#include <malloc.h>
#include <sys/mman.h>
#include <ucontext.h>
#define _InterlockedExchange(target, value) __atomic_exchange_n((target), (value), __ATOMIC_SEQ_CST)
#endif
#ifdef _WIN32
#define XD_SEP "\\"
#else
#define XD_SEP "/"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void XDGame_Main(int argc, char** argv);

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  static const char* names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
  fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s: %s] %.*s\n", names[level], module, (int)len, message);
  if (level == LOG_FATAL) {
    fflush(stderr);
    static volatile long sFatal = 0;
    if (_InterlockedExchange(&sFatal, 1) != 0) {
      for (;;) {
        SDL_Delay(1000);
      }
    }
    if (len >= 11 && strncmp(message, "Device lost", 11) == 0) {
      PortMessage_Show(PORT_MESSAGE_ERROR, "Pokemon XD",
                       "The graphics card stopped working for the game (its driver was reset or updated, the "
                       "computer switched graphics cards, or it woke from sleep), so the game has to close.\n\n"
                       "Start it again to carry on from your last save. A report of this was saved in the "
                       "game's reports folder.");
    }
    abort();
  }
}

void* OSGetCurrentThread(void);
void PortVI_SetFrameHook(void (*hook)(void));
void PortXD_RegisterModuleHandlers(void);
void PortXD_RegisterMainProgramMap(void);
bool PortMem_ReserveMEM1(void);
void PortTime_SetSpeed(float speed);
void PortDVD_SetFileLocator(bool (*locator)(const char* path, uint32_t* offset));
void PortDVD_SetFirstCommandTime(int64_t microseconds);
void PortInput_InstallKeyboardDefaults(void);
void PortInput_UpdateControls(void);
void PortInput_ControlsHandleEvent(const SDL_Event* event);
void PortXD_ButtonIconsUpdate(void);
void PortXD_SettingsSwitchesUpdate(void);
void PortXD_LoadSettings(void);
bool PortXD_LoadMods(void);
void PortXD_ReadTrainerLevels(void);
void PortXD_PlaythroughBoot(void);
void PortXD_PlaythroughFrame(void);
const char* PortXD_PlaythroughHeader(void);
const char* PortXD_ActiveMods(void);
const char* PortXD_ExtraAssetsText(void);
bool PortInput_LoadScript(const char* script);
void PortInput_ScriptField(uint32_t field);
void PortVI_SetFieldHook(void (*hook)(uint32_t field));
bool PortXD_RenderDocInit(const char* field);
void PortXD_RenderDocField(uint32_t field);

uint32_t VIGetRetraceCount(void);
uint32_t PortVI_GetFlipCount(void);
void PortXD_Screenshot(void* hwnd, uint32_t field, uint32_t gameFrame);
void PortXD_BugReport(void* hwnd, uint32_t field, uint32_t gameFrame);
void PortXD_BugReportPoll(void);
#ifdef __linux__
void PortXD_GamePictureFrame(void);
#endif
void PortXD_BugReportSetInfo(const char* lines);
void PortXD_BugReportSetWindow(void* hwnd);
void PortXD_BugReportInstallAutomatic(int test);
void PortXD_TestCrashPoll(void);
bool PortInput_StartRecording(const char* appFolder, const char* kind, const char* header);
bool PortInput_StartReplay(const char* path, uint32_t untilFrame);
void PortInput_SetCardRoot(const char* folder);
void PortCardBackup_Start(const char* folder);
void PortCARD_SetUnplugged(bool unplugged);
bool PortCARD_SetRoot(const char* utf8Folder);
const char* PortXD_CardRootUtf8(void);
const char* PortXD_CardSaveName(void);
void PortInput_Mark(void);
void PortInput_SetCheckValue(const char* name, uint32_t (*value)(void), uint32_t everyFrames);
bool PortXD_DevMenuInit(void);
void PortXD_DevMenuToggle(void);
void PortXD_DevMenuFrame(void);
void PortXD_HeapCheckFrame(void);
void PortXD_ShadersFrame(void);
void PortXD_TaskStacksInit(void);
void PortXD_ParticleCheckInit(void);
void PortXD_ParticleCheckFrame(void);
void PortXD_SpeedupsFrame(void);
void PortXD_ReplayPacingInit(void);
void PortXD_SettingsMenuInit(void);
void PortXD_SettingsMenuOverlay(void);

extern uint32_t* __hsd_rand_seed_hold;
void PortXD_StateDumpStart(void);
void PortXD_WalkHelperInit(void);
void PortXD_WarpStart(void);
void PortXD_TimingLogStart(void);
static uint32_t rng_state(void) { return __hsd_rand_seed_hold != NULL ? *__hsd_rand_seed_hold : 0; }

static uint32_t sBugReportAt;
static void bug_report(void) {
  void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                      NULL);
  PortXD_BugReport(hwnd, VIGetRetraceCount(), PortVI_GetFlipCount());
}
void PortOS_SetResetShutdown(void (*fn)(void));
static void reset_shutdown(void) {
  aurora_dvd_close();
  aurora_shutdown();
}
static void handle_events(void) {
  if (sBugReportAt != 0 && VIGetRetraceCount() >= sBugReportAt) {
    sBugReportAt = 0;
    bug_report();
  }
  for (const AuroraEvent* event = aurora_update(); event != NULL && event->type != AURORA_NONE; ++event) {
    if (event->type == AURORA_EXIT) {
      aurora_dvd_close();
      aurora_shutdown();
      exit(0);
    }
    if (event->type == AURORA_SDL_EVENT) {
      PortVideo_HandleEvent(&event->sdl);
      PortPhoto_HandleEvent(&event->sdl);
      PortSpeedups_HandleEvent(&event->sdl);
      PortInput_ControlsHandleEvent(&event->sdl);
    }
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN && !event->sdl.key.repeat &&
        event->sdl.key.scancode == SDL_SCANCODE_F12) {
      void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()),
                                          SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
      PortXD_Screenshot(hwnd, VIGetRetraceCount(), PortVI_GetFlipCount());
    }
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN && !event->sdl.key.repeat &&
        event->sdl.key.scancode == SDL_SCANCODE_F11) {
      PortInput_Mark();
    }
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN && !event->sdl.key.repeat &&
        event->sdl.key.scancode == SDL_SCANCODE_F10) {
      bug_report();
    }
    if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_KEY_DOWN && !event->sdl.key.repeat &&
        event->sdl.key.scancode == SDL_SCANCODE_F9) {
      PortXD_DevMenuToggle();
    }
  }
  PortXD_BugReportPoll();
  PortXD_TestCrashPoll();
  PortInput_UpdateControls();
  PortXD_ButtonIconsUpdate();
  PortXD_SettingsSwitchesUpdate();
}

static void begin_frame(void) {
  while (!aurora_begin_frame()) {
    handle_events();
    SDL_Delay(10);
  }
}

static bool sInputScript, sRenderDoc;
static bool sScriptByFrames;
static void field_hook(uint32_t field) {
  PortSpeedups_Field(field);
  if (sRenderDoc) {
    PortXD_RenderDocField(field);
  }
  if (sInputScript) {
    if (sScriptByFrames && field % 300 == 0) {
      fprintf(stderr, "[input script] field %u = game frame %u\n", field, PortVI_GetFlipCount());
    }
    PortInput_ScriptField(sScriptByFrames ? PortVI_GetFlipCount() : field);
  }
}

#define MAX_SHOT_FRAMES 512
static uint32_t sShotFrames[MAX_SHOT_FRAMES];
static int sShotCount, sShotNext;
static const char* sShotDir;
static bool sShotExit;
void PortXD_ScreenshotTo(void* hwnd, const char* folder, uint32_t field, uint32_t gameFrame);
void PortInput_StopRecording(void);
static int compare_u32(const void* a, const void* b) {
  const uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
  return x < y ? -1 : x > y;
}
static void load_shot_frames(void) {
  const char* list = getenv("XD_SHOT_FRAMES");
  sShotDir = getenv("XD_SHOT_DIR");
  if (list == NULL || list[0] == '\0' || sShotDir == NULL || sShotDir[0] == '\0') {
    return;
  }
  for (const char* p = list; *p != '\0' && sShotCount < MAX_SHOT_FRAMES;) {
    char* end = NULL;
    const unsigned long frame = strtoul(p, &end, 10);
    if (end == p) {
      break;
    }
    sShotFrames[sShotCount++] = (uint32_t)frame;
    p = *end == ',' ? end + 1 : end;
  }
  qsort(sShotFrames, (size_t)sShotCount, sizeof sShotFrames[0], compare_u32);
  const char* exitAfter = getenv("XD_SHOT_EXIT");
  sShotExit = exitAfter != NULL && exitAfter[0] == '1';
  fprintf(stderr, "[games/xd] XD_SHOT_FRAMES: %d frames into %s\n", sShotCount, sShotDir);
}
static void take_due_shots(void) {
  const uint32_t frame = PortVI_GetFlipCount();
  if (sShotNext >= sShotCount || frame < sShotFrames[sShotNext]) {
    return;
  }
  if (PortFrameRate_ShowingBetween()) {
    return;
  }
  void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                      NULL);
  PortXD_ScreenshotTo(hwnd, sShotDir, VIGetRetraceCount(), frame);
  while (sShotNext < sShotCount && sShotFrames[sShotNext] <= frame) {
    sShotNext++;
  }
  if (sShotNext >= sShotCount && sShotExit) {
    fprintf(stderr, "[games/xd] XD_SHOT_EXIT: last picture taken\n");
    PortInput_StopRecording();
    fflush(stdout);
    fflush(stderr);
    _Exit(0);
  }
}

static void frame_hook(void) {
  PortGX_EndOfFrame();
  PortXD_SettingsMenuOverlay();
  PortFps_Frame(PortVI_GetFlipCount(), VIGetRetraceCount());
  PortFrameRate_BeforePresent();
  aurora_end_frame();
  PortCapture_Field();
  if (sShotCount > 0) {
    take_due_shots();
  }
  handle_events();
  begin_frame();
  PortFrameRate_AfterBegin();
#ifdef __linux__
  PortXD_GamePictureFrame();
#endif
  PortXD_HeapCheckFrame();
  PortXD_ShadersFrame();
  PortXD_DevMenuFrame();
  PortPhoto_Frame(handle_events);
  PortFocusPause_Frame();
  PortMenu_Frame(handle_events);
  PortXD_PlaythroughFrame();
  PortXD_SpeedupsFrame();
  PortXD_ParticleCheckFrame();
}

#ifndef XD_PORT_COMMIT
#define XD_PORT_COMMIT "unknown"
#endif
#ifndef XD_SNAPSHOT_ID
#define XD_SNAPSHOT_ID "unknown"
#endif
static void start_input_recording(const char* discPath) {
  const char* replay = getenv("XD_REPLAY");
  const bool replaying = replay != NULL && replay[0] != '\0';
  if (replaying) {
    const char* until = getenv("XD_REPLAY_UNTIL");
    PortInput_StartReplay(replay, until != NULL && until[0] != '\0' ? (uint32_t)strtoul(until, NULL, 10) : UINT32_MAX);
  }
  static const char* const kTimingEnv[] = {"XD_TURBO",     "XD_INPUT_SCRIPT", "XD_INPUT_CLOCK", "XD_REPLAY",
                                           "XD_REPLAY_UNTIL", "XD_GX_INLINE", "XD_RENDERDOC", "XD_SAMPLE",
                                           "XD_WATCH",     "XD_DETERMINISTIC",
                                           "XD_DISC_TIMING", "XD_RNG_SEED", "XD_WARP", "XD_DEV_MENU", "XD_DEV_ACTIONS",
                                           "XD_PHOTO_SCRIPT", "XD_MENU_SCRIPT", "XD_SETTINGS_ENTRY", "XD_FOCUS_TEST",
                                           "XD_PAUSE_ON_FOCUS"};
  char header[2048];
#ifdef _DEBUG
  const char* build = "debug";
#else
  const char* build = "release";
#endif
  int n = snprintf(header, sizeof header,
                   "port commit %s (when the build was configured), %s build\ndecomp snapshot %s\ndisc %s\n",
                   XD_PORT_COMMIT, build, XD_SNAPSHOT_ID, discPath);
  const char* mods = PortXD_ActiveMods();
  if (mods[0] != '\0' && n > 0 && n < (int)sizeof header) {
    n += snprintf(header + n, sizeof header - n, "mods %s\n", mods);
  }
  if (PortXD_ExtraAssetsText()[0] != '\0' && n > 0 && n < (int)sizeof header) {
    n += snprintf(header + n, sizeof header - n, "extra assets %s\n", PortXD_ExtraAssetsText());
  }
  if (n > 0 && n < (int)sizeof header) {
    n += snprintf(header + n, sizeof header - n, "%s\n", PortXD_PlaythroughHeader());
  }
  {
    extern const char* PortXD_SettingsSummary(void);
    char replayText[1024];
    EngineSettings_ReplayText(replayText, sizeof replayText);
    if (n > 0 && n < (int)sizeof header) {
      n += snprintf(header + n, sizeof header - n, "settings %s\nsettings_replay %s\n", PortXD_SettingsSummary(),
                    replayText);
    }
  }
  for (size_t i = 0; i < sizeof kTimingEnv / sizeof kTimingEnv[0]; i++) {
    const char* value = getenv(kTimingEnv[i]);
    if (value != NULL && value[0] != '\0' && n > 0 && n < (int)sizeof header) {
      n += snprintf(header + n, sizeof header - n, "%s=%s\n", kTimingEnv[i], value);
    }
  }
  PortXD_BugReportSetInfo(header);
  const char* record = getenv("XD_RECORD");
  if (record != NULL && strcmp(record, "0") == 0) {
    return;
  }
  const char* script = getenv("XD_INPUT_SCRIPT");
  const bool scripted = script != NULL && script[0] != '\0';
  PortInput_StartRecording("Pokemon XD", replaying || scripted ? "test" : "play", header);
}

static int fail(const char* message) {
  extern bool PortXD_TestRun(void);
  const char* noBoxes = getenv("XD_NO_MESSAGE_BOXES");
  const char* script = getenv("XD_INPUT_SCRIPT");
  const char* folder = PortUser_FolderUtf8("Pokemon XD");
  fprintf(stderr, "XD: %s\n", message);
  fflush(stderr);
  if (folder != NULL && folder[0] != '\0') {
    char path[1100];
    FILE* note;
    snprintf(path, sizeof path, "%s/start_failure.txt", folder);
    note = fopen(path, "a");
    if (note != NULL) {
      fprintf(note, "The game could not start: %s\n", message);
      fclose(note);
    }
  }
  if ((noBoxes != NULL && noBoxes[0] == '1') || (script != NULL && script[0] != '\0') || PortXD_TestRun()) {
    PortMessage_SetWindows(false);
  }
  PortMessage_Show(PORT_MESSAGE_ERROR, "Pok\xC3\xA9mon XD", message);
  return 1;
}

bool PortXD_RenderIconPrepare(void);
void PortXD_RenderIconStart(void);
bool PortXD_ShadersPrepareMode(void);
void PortXD_ShadersStart(void);

#ifndef _WIN32
static int xd_main(int argc, char** argv);
static ucontext_t sLowContext, sStartContext;
static int sLowArgc, sLowResult;
static char** sLowArgv;
static void low_stack_entry(void) { sLowResult = xd_main(sLowArgc, sLowArgv); }

int aurora_main(int argc, char** argv) {
#ifdef __APPLE__
  const size_t size = 8u << 20, guard = 0x4000;
#else
  const size_t size = 8u << 20, guard = 4096;
#endif
  mallopt(M_MMAP_THRESHOLD, 32 << 20);
  mallopt(M_ARENA_MAX, 1);
  mallopt(M_TOP_PAD, 512 << 20);
  {
    void* volatile grow = malloc(4 << 20);
    free(grow);
  }
#ifdef __APPLE__
#define XD_MALLOC_WHERE_ALWAYS 1
#define XD_MALLOC_WHERE_TAG "mac"
#else
#define XD_MALLOC_WHERE_ALWAYS 0
#define XD_MALLOC_WHERE_TAG "linux"
#endif
  if (XD_MALLOC_WHERE_ALWAYS || getenv("XD_MALLOC_WHERE") != NULL) {
    void* volatile small = malloc(64);
    void* volatile middle = malloc(1 << 20);
    void* volatile big = malloc(40 << 20);
    const int low = (uintptr_t)small < 0x100000000ull && (uintptr_t)middle < 0x100000000ull &&
                    (uintptr_t)big < 0x100000000ull;
    fprintf(stderr, "[games/xd] " XD_MALLOC_WHERE_TAG ": C library blocks at %p (64 bytes), %p (1 MB), %p (40 MB); "
            "a local variable at %p; all three blocks below 4 GB: %s\n",
            small, middle, big, (void*)&small, low ? "yes" : "NO");
    free(big);
    free(middle);
    free(small);
  }
#ifdef __APPLE__
  {
    extern int PortLowMalloc_SelfTest(const char* tag, unsigned stressMb);
    extern bool PortXD_TestRun(void);
    const char* stress = getenv("XD_LOW_MALLOC_STRESS");
    PortLowMalloc_SelfTest("[games/xd] mac", stress != NULL ? (unsigned)atoi(stress) : PortXD_TestRun() ? 200u : 0u);
    SDL_SetMemoryFunctions(malloc, calloc, realloc, free);
  }
#endif
  char* stack = (char*)mmap(NULL, size + guard, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  if (stack == MAP_FAILED) {
    return fail("Could not place the program's main stack below 4 GB (the game keeps addresses in 4 bytes). "
                "On a Mac this means the system did not give the program its low memory: please send "
                "the report made by first_run_report.sh.");
  }
  mprotect(stack, guard, PROT_NONE);
  getcontext(&sLowContext);
  sLowContext.uc_stack.ss_sp = stack + guard;
  sLowContext.uc_stack.ss_size = size;
  sLowContext.uc_link = &sStartContext;
  sLowArgc = argc;
  sLowArgv = argv;
  makecontext(&sLowContext, low_stack_entry, 0);
  swapcontext(&sStartContext, &sLowContext);
  return sLowResult;
}
#define aurora_main xd_main
static
#endif
int aurora_main(int argc, char** argv) {
  PortCrash_Install();
  const bool renderIcon = PortXD_RenderIconPrepare();
  const bool prepareShaders = PortXD_ShadersPrepareMode();
  {
    const char* replay = getenv("XD_REPLAY");
    const char* pacing = getenv("XD_REPLAY_PACING");
    const char* det = getenv("XD_DETERMINISTIC");
    if (replay != NULL && replay[0] != '\0' && pacing != NULL && strcmp(pacing, "recorded") == 0 && det == NULL) {
      _putenv("XD_DETERMINISTIC=1");
    }
  }
  bool testRun;
  {
    const char* log = getenv("XD_LOG");
    const char* script = getenv("XD_INPUT_SCRIPT");
    const char* replay = getenv("XD_REPLAY");
    const bool test = (script != NULL && script[0] != '\0') || (replay != NULL && replay[0] != '\0') || renderIcon ||
                      prepareShaders;
    testRun = test;
    if (log == NULL || strcmp(log, "0") != 0) {
      PortLog_Start("Pokemon XD", test ? "test" : "play", 10);
    }
    const char* label = getenv("XD_RUN_LABEL");
    const char* stuck = getenv("XD_STUCK_SECS");
    PortRunStatus_Start("Pokemon XD", label != NULL && label[0] != '\0' ? label : (test ? "test" : "play"),
                        PortVI_GetFlipCount, stuck != NULL ? (unsigned)atoi(stuck) : 60u, test);
    PortXD_BugReportInstallAutomatic(test);
  }
  fprintf(stderr, "[user folder] %s (%s)\n", PortUser_FolderUtf8("Pokemon XD"), PortUser_FolderNote("Pokemon XD"));
  PortXD_LoadSettings();
  {
    extern void PortXD_LoadDspCoef(void);
    PortXD_LoadDspCoef();
  }
  {
    extern int PortXD_SaveCardRead();
    extern int PortXD_SaveCardWrite();
    extern void PortCARD_SetFilters(void* read, void* write);
    const char* filter = getenv("XD_CARD_FILTER");
    if (filter == NULL || strcmp(filter, "0") != 0) {
      PortCARD_SetFilters((void*)PortXD_SaveCardRead, (void*)PortXD_SaveCardWrite);
    }
  }
  if (!PortMem_ReserveMEM1()) {
    return fail("Could not place the game's memory at its GameCube addresses (0x80000000, 0xE0000000)");
  }
#ifdef __APPLE__
  {
    void* PortMem_LowAlloc(size_t size, size_t align);
    volatile unsigned char* cached = (volatile unsigned char*)(uintptr_t)0x80003000u;
    volatile unsigned char* uncached = (volatile unsigned char*)(uintptr_t)0xC0003000u;
    const unsigned char before = *cached;
    int mirrored;
    *cached = 0x5A;
    mirrored = *uncached == 0x5A;
    *cached = before;
    fprintf(stderr, "[games/xd] mac: game memory in place at 0x80000000 (second view at 0xC0000000: %s), "
            "main stack at %p, low data block at %p\n", mirrored ? "same memory" : "NOT the same memory",
            (void*)&before, PortMem_LowAlloc(16, 16));
  }
#endif

  const char* watch = getenv("XD_WATCH");
  if (watch != NULL && watch[0] != '\0') {
    PortCrash_WatchWrites((uintptr_t)strtoull(watch, NULL, 16));
  }
  const char* sample = getenv("XD_SAMPLE");
  if (sample != NULL && sample[0] != '\0') {
    unsigned every = 1000, count = 5;
    const char* comma = strchr(sample, ',');
    if (comma != NULL) {
      every = (unsigned)atoi(comma + 1);
      comma = strchr(comma + 1, ',');
      if (comma != NULL) count = (unsigned)atoi(comma + 1);
    }
    PortCrash_SampleThread((unsigned)(atof(sample) * 1000), every, count);
  }

  extern bool PortXD_TestRun(void);

  bool interactive = !testRun && !PortXD_TestRun();
  {
    const char* noBoxes = getenv("XD_NO_MESSAGE_BOXES");
    const char* script = getenv("XD_INPUT_SCRIPT");
    if ((noBoxes != NULL && noBoxes[0] == '1') || (script != NULL && script[0] != '\0')) {
      PortMessage_SetWindows(false);
      interactive = false;
    }
  }

  const char* discPath = PortXD_ChooseDisc(argc >= 2 ? argv[1] : getenv("XD_DISC"), interactive);
  if (discPath != NULL && discPath[0] == '\0') {
    return 0;
  }
  if (!PortXD_CheckDisc(discPath)) {
    PortXD_DiscFailed(discPath);
    return 1;
  }

  char error[512];
  size_t dolSize = 0;
  if (!PortDisc_Open(discPath, error, sizeof(error)) ||
      !PortXD_LoadDolTables(PortDisc_Dol(&dolSize), dolSize, error, sizeof(error))) {
    PortXD_DiscUnusable(error);
    PortXD_DiscFailed(discPath);
    return 1;
  }
  if (interactive) {
    PortXD_RememberDisc(discPath);
  }
  PortXD_PlaythroughBoot();
  PortXD_LoadMods();
  PortXD_ReadTrainerLevels();
  if (!PortXD_LoadDspProgram()) {
    return fail("MusyX's DSP program isn't in main.dol");
  }
  if (!PortXD_FillDiscTables(error, sizeof(error))) {
    PortXD_DiscUnusable(error);
    PortXD_DiscFailed(discPath);
    return 1;
  }
  char gameId[8];
  snprintf(gameId, sizeof gameId, "%s", PortDisc_GameId());
  PortDisc_Close();
  PortDVD_SetFileLocator(PortDisc_FileDiscOffset);
  PortDVD_SetFirstCommandTime(2094341);

  AuroraConfig config;
  memset(&config, 0, sizeof(config));
  config.appName = "Pokemon XD";
  const char* cardDir = getenv("XD_CARD_DIR");
  config.userPath = cardDir != NULL && cardDir[0] != '\0' ? cardDir : PortUser_FolderUtf8("Pokemon XD");
  const char* cardRoot = PortXD_CardRootUtf8();
  if (strcmp(cardRoot, config.userPath) != 0 && !PortCARD_SetRoot(cardRoot)) {
    config.userPath = cardRoot;
  }
  fprintf(stderr, "[card] save: %s (%s)\n", PortXD_CardSaveName(), cardRoot);
  if (!renderIcon && !prepareShaders) {
    const char* claimOff = getenv("XD_CARD_CLAIM");
    if ((claimOff == NULL || strcmp(claimOff, "0") != 0) && !PortFolderClaim_Take(cardRoot)) {
      fprintf(stderr, "[card] another running copy of the game uses this memory card folder: %s\n", cardRoot);
      if (testRun || !interactive) {
        fprintf(stderr, "[card] test run: stopping (exit code 5; each run needs its own XD_CARD_DIR)\n");
        return 5;
      }
      if (PortMessage_Ask(PORT_MESSAGE_WARNING, "Pok\xC3\xA9mon XD",
                          "Another copy of the game is already running with the same memory card.\n\n"
                          "Two copies would overwrite each other's saves. You can quit, or play this copy "
                          "without the memory card (it can't load or save).",
                          "Quit", "Play without the card", true)) {
        return 5;
      }
      PortCARD_SetUnplugged(true);
      fprintf(stderr, "[card] this copy plays without the memory card\n");
    }
  }
  {
    const char* unplugged = getenv("XD_CARD_UNPLUGGED");
    if (unplugged != NULL && strcmp(unplugged, "1") == 0) {
      PortCARD_SetUnplugged(true);
      fprintf(stderr, "[card] XD_CARD_UNPLUGGED=1: no memory card inserted\n");
    }
  }
  const char* cacheDir = getenv("XD_CACHE_DIR");
  if (cacheDir != NULL && cacheDir[0] != '\0') {
    config.cachePath = cacheDir;
  }
  const char* pipelineList = getenv("XD_PIPELINE_LIST");
  if (pipelineList != NULL && pipelineList[0] != '\0') {
    config.resourcesPath = pipelineList;
  }
  config.logCallback = &log_callback;
  config.windowPosX = 16;
  config.windowPosY = 64;
  config.vsync = true;
  const float* gameSpeed = EngineSettings_FloatSlot("system.game_speed");
  const float turboSpeed = gameSpeed != NULL ? *gameSpeed : 1.0f;
  if (turboSpeed > 1.0f) {
    config.vsync = false;
  }
  config.emulateXfb = true;
  config.mem1Size = MEM1_DEFAULT_SIZE;
  config.mem2Size = 0x01000000;
  {
    const char* backend = getenv("XD_GPU_BACKEND");
    if (backend != NULL && backend[0] != '\0') {
      if (strcmp(backend, "d3d12") == 0) {
        config.desiredBackend = BACKEND_D3D12;
      } else if (strcmp(backend, "d3d11") == 0) {
        config.desiredBackend = BACKEND_D3D11;
      } else if (strcmp(backend, "vulkan") == 0) {
        config.desiredBackend = BACKEND_VULKAN;
      } else if (strcmp(backend, "warp") == 0 || strcmp(backend, "warp11") == 0) {
        config.desiredBackend = strcmp(backend, "warp") == 0 ? BACKEND_D3D12 : BACKEND_D3D11;
        config.allowCpuAdapter = true;
        aurora_set_gpu_fallback_adapter(true);
      } else if (strcmp(backend, "vulkan_cpu") == 0) {
        config.desiredBackend = BACKEND_VULKAN;
        config.allowCpuAdapter = true;
#ifdef __linux__
      } else if (strcmp(backend, "opengles") == 0) {
        config.desiredBackend = BACKEND_OPENGLES;
#endif
      } else {
        fprintf(stderr, "[video] XD_GPU_BACKEND=%s: unknown (d3d12, vulkan, d3d11, warp or warp11); using the default order\n",
                backend);
      }
    }
    const char* timing = getenv("XD_GPU_TIMING");
    const double timingSeconds = timing != NULL && timing[0] != '\0' ? atof(timing) : (testRun ? 0.0 : 60.0);
    if (timingSeconds > 0.0) {
      aurora_set_gpu_timing((float)timingSeconds);
      fprintf(stderr, "[video] GPU/CPU time per frame logged every %g s (\"[gpu timing]\" lines; XD_GPU_TIMING)\n",
              timingSeconds);
    }
    {
      extern void PortXD_PcLoadStart(double seconds);
      PortXD_PcLoadStart(timingSeconds);
    }
  }
  {
    const char* inl = getenv("XD_GX_INLINE");
    const char* det = getenv("XD_DETERMINISTIC");
    if ((inl != NULL && inl[0] == '1') || (det != NULL && det[0] == '1')) {
      config.gxProcessingMode = GX_PROCESSING_INLINE;
    }
    const char* origin = getenv("XD_GX_ORIGIN");
    if (origin != NULL && origin[0] == '1') {
      PortGX_TraceOrigins(PortCrash_DescribeAddress);
    }
    const char* dump = getenv("XD_DUMP_TEXTURES");
    if (dump != NULL && dump[0] == '1') {
      config.allowTextureDumps = true;
    }
  }
  sRenderDoc = PortXD_RenderDocInit(getenv("XD_RENDERDOC"));
  PortXD_DevMenuInit();
  PortVideo_Configure(&config);
  if (renderIcon) {
    config.windowWidth = 2560;
    config.windowHeight = 1920;
    PortVideo_UseWindowResolution();
  }
  {
    int captureW = 0, captureH = 0;
    if (PortCapture_WindowSize(&captureW, &captureH)) {
      config.windowWidth = (uint32_t)captureW;
      config.windowHeight = (uint32_t)captureH;
    }
  }
  {
    const AuroraInfo started = aurora_initialize(argc, argv, &config);
    const char* nullOk = getenv("XD_GPU_NULL_OK");
    if (started.backend == BACKEND_NULL && (nullOk == NULL || nullOk[0] != '1')) {
      return fail("No usable graphics device was found, so the game could not draw anything. "
                  "Please update the graphics driver. (Windows: Direct3D 12, Direct3D 11 or Vulkan is needed; "
                  "Linux: Vulkan; Mac: Metal.)");
    }
  }
#ifdef XD_PORT_VERSION
  SDL_SetWindowTitle(aurora_get_window(), "Pok\xC3\xA9mon XD " XD_PORT_VERSION);
#endif
  PortVideo_Start();
  PortPhoto_Init("Pokemon XD", PortVI_GetFlipCount);
  PortMenu_Init("Pokemon XD", PortVI_GetFlipCount);
  PortFrameRate_Init(VIGetRetraceCount);
  PortCapture_Init(PortVI_GetFlipCount, VIGetRetraceCount);
  PortXD_SettingsMenuInit();
  {
    const char* requireOriginal = getenv("XD_REQUIRE_ORIGINAL");
    PortFocusPause_Init(PortVI_GetFlipCount, testRun || (requireOriginal != NULL && strcmp(requireOriginal, "1") == 0));
  }
  {
    extern void PortXD_SaveSettings(void);
    PortVideo_SetSettingsSaver(PortXD_SaveSettings);
  }
  {
    char icon[1024];
    snprintf(icon, sizeof icon, "%s" XD_SEP "icons" XD_SEP "%s.ico", PortUser_FolderUtf8("Pokemon XD"), gameId);
    PortWindowIcon_SetAppId(aurora_get_window(), "PokemonXD.PCPort");
    fprintf(stderr, "[window] icon: %s\n", PortWindowIcon_Apply(aurora_get_window(), icon) ? icon : "the program's own");
  }
  const char* offscreen = getenv("XD_WINDOW_OFFSCREEN");
  if (offscreen != NULL && offscreen[0] == '1') {
    SDL_SetWindowPosition(aurora_get_window(), -20000, -20000);
  }
  PortXD_ShadersStart();
  PortXD_BugReportSetWindow(SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()),
                                                   SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL));
  if (turboSpeed > 1.0f) {
    PortTime_SetSpeed(turboSpeed);
    fprintf(stderr, "[games/xd] XD_TURBO: game clock at %gx real time\n", (double)turboSpeed);
  }
  PortInput_InstallKeyboardDefaults();
  PortInput_SetCardRoot(cardRoot);
  {
    const char* off = getenv("XD_SAVE_BACKUPS");
    if (off == NULL || strcmp(off, "0") != 0) {
      char backups[1024];
      snprintf(backups, sizeof backups, "%s" XD_SEP "save_backups", cardRoot);
      PortCardBackup_Start(backups);
    }
  }
  start_input_recording(discPath);
  PortXD_StateDumpStart();
  PortXD_WarpStart();
  PortXD_TimingLogStart();
  PortXD_RenderIconStart();
  {
    const char* logRng = getenv("XD_LOG_RNG");
    if (logRng != NULL && logRng[0] != '\0') {
      PortInput_SetCheckValue("rng", rng_state, (uint32_t)strtoul(logRng, NULL, 10));
    }
  }
  load_shot_frames();
  PortXD_ReplayPacingInit();
  PortXD_TaskStacksInit();
  PortXD_ParticleCheckInit();
  sInputScript = PortInput_LoadScript(getenv("XD_INPUT_SCRIPT"));
  PortXD_WalkHelperInit();
  {
    const char* reportAt = getenv("XD_BUG_REPORT_AT");
    if (reportAt != NULL && reportAt[0] != '\0') {
      sBugReportAt = (uint32_t)strtoul(reportAt, NULL, 10);
    }
  }
  {
    const char* clock = getenv("XD_INPUT_CLOCK");
    sScriptByFrames = clock != NULL && strcmp(clock, "frames") == 0;
  }
  PortVI_SetFieldHook(field_hook);
  PortMem_InitOS(PortXD_ArenaLo());
  if (!aurora_dvd_open(discPath)) {
    aurora_shutdown();
    return fail("Aurora could not open the disc for reading game files");
  }
  PortDisc_InstallOverlayInAurora();
  PortOS_SetResetShutdown(reset_shutdown);

  PortXD_RegisterMainProgramMap();
  PortXD_RegisterModuleHandlers();

  OSGetCurrentThread();
  PortVI_SetFrameHook(frame_hook);
  begin_frame();

  static char* gameArgv[] = {"xd", NULL};
  XDGame_Main(1, gameArgv);

  aurora_dvd_close();
  aurora_shutdown();
  return 0;
}
