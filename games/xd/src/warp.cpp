#include <game/pxdvs/app/floor/floorData.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <excpt.h>
#include <port_seh.h>
#else
#include <port_win_compat.h>
#endif

extern "C" {
void (*PortInput_SetFrameHook(void (*hook)(uint32_t frame)))(uint32_t frame);

void floorLink(u32 floorID, u32 posIndex);
s32 GSfloorGetCurrent(void);
s32 floorGetNextChangeID(void);
int floorGetFloorState(void);
FloorData* floorDataBiosGetPtr(u32 key);
u32 floorDataBiosGetFileGroupID(FloorData* floor);
u16* GSmsgGetGSchar(u32 key);
extern u32* floordata_number;
extern FloorData* floordata;
}

namespace {

void (*sPrevious)(uint32_t) = nullptr;
bool sList = false;
uint32_t sTarget = 0, sEntrance = 0, sFrom = 600, sExitAfter = 0;
enum class Phase { Waiting, Changing, Arrived } sPhase = Phase::Waiting;
uint32_t sArrivedFrame = 0, sTries = 0;

void finish(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
}

void list_floors() {
  const uint32_t n = *floordata_number;
  std::printf("[warp] %u floors\n", n);
  for (uint32_t i = 0; i < n; i++) {
    FloorData* f = &floordata[i];
    std::printf("[warp] floor 0x%03X area %u kind %u group 0x%08X place 0x%08X\n", f->floorID, f->area, f->kind,
                floorDataBiosGetFileGroupID(f), f->placeName);
  }
}

void on_frame(uint32_t frame) {
  if (sPrevious != nullptr) {
    sPrevious(frame);
  }
  if (sList) {
    if (floordata_number != nullptr && floordata != nullptr && frame >= 2) {
      list_floors();
      finish(0);
    }
    return;
  }
  if (frame < sFrom) {
    return;
  }
  const s32 current = GSfloorGetCurrent();
  switch (sPhase) {
  case Phase::Waiting:
    if (sTries == 0) {
      if (floorDataBiosGetPtr(sTarget) == nullptr) {
        std::fprintf(stderr, "[warp] floor 0x%X is not in the floor table\n", sTarget);
        finish(4);
      }
      std::fprintf(stderr, "[warp] game frame %u: from floor 0x%X to floor 0x%X (entrance %u)\n", frame,
                   (unsigned)current, sTarget, sEntrance);
    }
    sTries++;
    floorLink(sTarget, sEntrance);
    if (floorGetNextChangeID() == (s32)sTarget) {
      std::fprintf(stderr, "[warp] game frame %u: change started (after %u tries)\n", frame, sTries);
      sPhase = Phase::Changing;
    } else if (sTries % 600 == 0) {
      std::fprintf(stderr, "[warp] game frame %u: still refused after %u tries (floor 0x%X)\n", frame, sTries,
                   (unsigned)current);
    }
    break;
  case Phase::Changing:
    if (current == (s32)sTarget && floorGetFloorState() != 0) {
      std::fprintf(stderr, "[warp] game frame %u: arrived on floor 0x%X (loaded, running)\n", frame, sTarget);
      sPhase = Phase::Arrived;
      sArrivedFrame = frame;
    }
    break;
  case Phase::Arrived:
    if (sExitAfter != 0 && frame >= sArrivedFrame + sExitAfter) {
      std::fprintf(stderr, "[warp] game frame %u: %u frames on floor 0x%X, exiting\n", frame, sExitAfter, sTarget);
      finish(0);
    }
    break;
  }
  if (frame % 60 == 0) {
    std::fprintf(stderr, "[warp] game frame %u: floor 0x%X\n", frame, (unsigned)current);
  }
}

}

extern "C" void PortXD_WarpStart(void) {
  const char* warp = std::getenv("XD_WARP");
  if (warp == nullptr || warp[0] == '\0') {
    return;
  }
  if (std::strcmp(warp, "list") == 0) {
    sList = true;
  } else {
    char* end = nullptr;
    sTarget = (uint32_t)std::strtoul(warp, &end, 0);
    if (end != nullptr && *end == ':') {
      sEntrance = (uint32_t)std::strtoul(end + 1, &end, 0);
    }
    if (end != nullptr && *end == '@') {
      sFrom = (uint32_t)std::strtoul(end + 1, &end, 10);
    }
    const char* exitAfter = std::getenv("XD_WARP_EXIT");
    if (exitAfter != nullptr && exitAfter[0] != '\0') {
      sExitAfter = (uint32_t)std::strtoul(exitAfter, nullptr, 10);
    }
    std::fprintf(stderr, "[warp] XD_WARP: floor 0x%X entrance %u from game frame %u\n", sTarget, sEntrance, sFrom);
  }
  sPrevious = PortInput_SetFrameHook(on_frame);
}

extern "C" int PortXD_ReportMap(char* out, int size) {
  if (out == nullptr || size < 32) {
    return 0;
  }
  out[0] = '\0';
  PORT_SEH_TRY {
    const s32 current = GSfloorGetCurrent();
    if (current <= 0) {
      return 0;
    }
    int n = 0;
    FloorData* floor = floorDataBiosGetPtr(static_cast<u32>(current));
    const u16* name = floor != nullptr && floor->placeName != 0 ? GSmsgGetGSchar(floor->placeName) : nullptr;
    for (int i = 0; name != nullptr && i < 80 && n + 4 < size - 16; i++) {
      const u16 c = name[i];
      if (c == 0 || c == 0xFFFF) {
        break;
      }
      if (c < 0x80) {
        out[n++] = static_cast<char>(c);
      } else if (c < 0x800) {
        out[n++] = static_cast<char>(0xC0 | c >> 6);
        out[n++] = static_cast<char>(0x80 | (c & 0x3F));
      } else {
        out[n++] = static_cast<char>(0xE0 | c >> 12);
        out[n++] = static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out[n++] = static_cast<char>(0x80 | (c & 0x3F));
      }
    }
    std::snprintf(out + n, static_cast<size_t>(size - n), n > 0 ? " (0x%03X)" : "0x%03X", static_cast<unsigned>(current));
    return 1;
  } PORT_SEH_EXCEPT {
    out[0] = '\0';
    return 0;
  }
}
