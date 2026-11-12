#include <port_frame_rate.h>

#include <port_freeze.h>

#include <settings.h>

#include <aurora/time.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

bool sOn = false;
bool sStarted = false;
size_t sSetting = engine::settings::kNoSetting;
int32_t sChoice60 = -1;
uint32_t (*sField)(void) = nullptr;
float sBlend = 0.5f;
uint32_t sLogFields = 0;

uint32_t sPictures = 0;
uint32_t sPictureField = 0;
bool sHavePicture = false;
bool sBetween = false;
bool sBetweenShown = false;
bool sLastHadBetween = false;
uint32_t sBetweenTotal = 0;
double sBetweenStart = 0.0;
constexpr double kSlowMs = 8.0;
constexpr uint32_t kPausePictures = 150;
uint32_t sPause = 0;
struct Switch {
  uint32_t field;
  bool on;
};
Switch sSwitches[8];
int sSwitchCount = 0;
bool sFastToo = false;

struct Counts {
  uint32_t fields = 0, pictures = 0, between = 0;
  uint32_t rhythm = 0, partial = 0, unmatched = 0, fast = 0, busy = 0, full = 0, slow = 0;
  uint64_t blended = 0, plain = 0;
  float use = 0.f;
  double ms = 0.0;
  AuroraInterpInfo last{};
} sCounts;
uint32_t sLogField = 0;

double NowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Log(uint32_t field) {
  if (sLogFields == 0 || field - sLogField < sLogFields) return;
  const Counts& c = sCounts;
  std::fprintf(stderr,
               "[frame rate] %u fields: %u game pictures, %u in-between pictures (%.0f%%); none because: rhythm %u, "
               "next picture begun %u, no match %u, fast-forward %u, frozen %u, no room %u, PC too slow %u; per in-between picture %.0f draws "
               "blended, %.0f as they are, %.2f ms of extra work; fullest frame buffer %.0f%%; last comparison: %u transform sets, %u 3D, %u found, %u moved, %u jumped\n",
               c.fields, c.pictures, c.between, c.pictures ? 100.0 * c.between / c.pictures : 0.0, c.rhythm, c.partial,
               c.unmatched, c.fast, c.busy, c.full, c.slow, c.between ? static_cast<double>(c.blended) / c.between : 0.0,
               c.between ? static_cast<double>(c.plain) / c.between : 0.0, c.between ? c.ms / c.between : 0.0, c.use * 100.0, c.last.draws, c.last.perspective,
               c.last.matched, c.last.moved, c.last.jumped);
  std::fflush(stderr);
  sCounts = Counts{};
  sLogField = field;
}

}

extern "C" {

static void FollowSetting() {
  if (!sStarted || sSetting == engine::settings::kNoSetting) return;
  bool want = engine::settings::Global().ValueOf(sSetting).i == sChoice60;
  for (int i = 0; i < sSwitchCount; i++) {
    if (sField() >= sSwitches[i].field) want = sSwitches[i].on;
  }
  if (want == sOn) return;
  AuroraInterpInfo info{};
  AuroraInterpGetInfo(&info);
  sOn = want;
  sHavePicture = false;
  sLastHadBetween = false;
  sPause = 0;
  if (sOn) {
    PortFreeze_Capture();
    AuroraInterpSetMode(AURORA_INTERP_RECORD, 1.f);
    size_t partial = 0;
    sPictures = PortFreeze_PictureCount(&partial);
    std::fprintf(stderr, "[frame rate] 60: an in-between picture between the game's own 30 a second (video.frame_rate; "
                         "the game itself still runs at its own speed)\n");
  } else {
    AuroraInterpSetMode(AURORA_INTERP_OFF, 1.f);
    std::fprintf(stderr, "[frame rate] original: the game's own pictures only\n");
  }
}

bool PortFrameRate_Init(uint32_t (*field)(void)) {
  const engine::settings::Registry& r = engine::settings::Global();
  sSetting = r.Find("video.frame_rate");
  sChoice60 = static_cast<int32_t>(r.ChoiceIndex("video.frame_rate", "60"));
  sField = field;
  if (field == nullptr || sSetting == engine::settings::kNoSetting) return false;
  if (const char* blend = std::getenv("XD_FRAME_RATE_BLEND"); blend != nullptr && blend[0] != '\0') {
    sBlend = std::clamp(static_cast<float>(std::atof(blend)), 0.f, 1.f);
  }
  if (const char* log = std::getenv("XD_FRAME_RATE_LOG"); log != nullptr && std::atof(log) > 0.0) {
    sLogFields = static_cast<uint32_t>(std::atof(log) * 60.0);
  }
  if (const char* fast = std::getenv("XD_FRAME_RATE_FAST"); fast != nullptr && fast[0] == '1') sFastToo = true;
  if (const char* at = std::getenv("XD_FRAME_RATE_AT"); at != nullptr) {
    for (const char* q = at; *q != '\0' && sSwitchCount < 8;) {
      char* end = nullptr;
      const unsigned long f = std::strtoul(q, &end, 10);
      if (end == q || *end != ':') break;
      sSwitches[sSwitchCount++] = Switch{static_cast<uint32_t>(f), end[1] == '6'};
      q = end + 1;
      while (*q != '\0' && *q != ',') q++;
      if (*q == ',') q++;
    }
  }
  sStarted = true;
  FollowSetting();
  return sOn;
}

bool PortFrameRate_ShowingBetween(void) { return sBetweenShown; }

uint32_t PortFrameRate_BetweenCount(void) { return sBetweenTotal; }

void PortFrameRate_BeforePresent(void) {
  sBetweenShown = false;
  FollowSetting();
  if (!sOn) return;
  const uint32_t field = sField();
  sCounts.fields++;
  Log(field);
  AuroraInterpInfo info{};
  AuroraInterpGetInfo(&info);
  const float use = AuroraGetFrameBufferUse();
  sCounts.use = std::max(sCounts.use, use);
  if (use > 0.6f && sPause < kPausePictures) {
    sPause = kPausePictures;
    sCounts.full++;
  }
  size_t partial = 0;
  const uint32_t pictures = PortFreeze_PictureCount(&partial);
  const uint32_t fresh = pictures - sPictures;
  sPictures = pictures;
  if (fresh == 0) return;
  sCounts.pictures += fresh;
  const uint32_t gap = field - sPictureField;
  const bool hadPicture = sHavePicture;
  const bool lastHadBetween = sLastHadBetween;
  sLastHadBetween = false;
  sPictureField = field;
  sHavePicture = true;
  if (sPause > 0) {
    sPause = fresh >= sPause ? 0 : sPause - fresh;
    sCounts.slow++;
    return;
  }
  if (lastHadBetween && fresh == 1 && gap == 3) {
    sPause = kPausePictures;
    sCounts.slow++;
    return;
  }
  if (!hadPicture || fresh != 1 || gap < 2 || gap > 4) {
    sCounts.rhythm++;
    return;
  }
  if (info.pending != 0) {
    sCounts.partial++;
    return;
  }
  if (aurora::time::scale() > 1.5f && !sFastToo) {
    sCounts.fast++;
    return;
  }
  sCounts.last = info;
  if (!info.usable) {
    sCounts.unmatched++;
    return;
  }
  if (use > 0.3f) {
    sCounts.full++;
    return;
  }
  const double start = NowMs();
  if (!PortFreeze_RedrawBegin()) {
    sCounts.busy++;
    return;
  }
  AuroraInterpSetMode(AURORA_INTERP_REPLAY, sBlend);
  PortFreeze_DrawPicture();
  sBetween = true;
  sBetweenShown = true;
  sLastHadBetween = true;
  sBetweenTotal++;
  sCounts.between++;
  sBetweenStart = NowMs() - start;
  sCounts.ms += sBetweenStart;
}

void PortFrameRate_AfterBegin(void) {
  if (!sBetween) return;
  sBetween = false;
  const double start = NowMs();
  AuroraInterpSetMode(AURORA_INTERP_REPLAY, 1.f);
  u32 blended = 0, plain = 0;
  AuroraInterpGetReplayCounts(&blended, &plain);
  PortFreeze_DrawPicture();
  AuroraInterpSetMode(AURORA_INTERP_RECORD, 1.f);
  PortFreeze_RedrawEnd();
  const double took = NowMs() - start;
  sCounts.ms += took;
  if (sBetweenStart + took > kSlowMs && !sFastToo) sPause = kPausePictures;
  sCounts.blended += blended;
  sCounts.plain += plain;
}

}
