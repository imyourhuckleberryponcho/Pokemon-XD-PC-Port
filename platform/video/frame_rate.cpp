#include <port_frame_rate.h>

#include <port_freeze.h>
#include <port_video.h>

#include <settings.h>

#include <aurora/gfx.h>
#include <aurora/time.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

namespace {

bool sOn = false;
bool sStarted = false;
size_t sSetting = engine::settings::kNoSetting;
int32_t sChoiceOriginal = 0;
void (*sPresentAndBegin)(void) = nullptr;
constexpr double kGameHz = 60000.0 / 1001.0;
double sPerField = 1.0;
double sCarry = 0.0;
int sHighHz = 0;
bool sExactNext = false;
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
uint32_t sBetweenDraws = 1;
constexpr double kSlowMs = 8.0;
double sBudgetMs = 8.0;
bool sBudgetRead = false;
double sPerPictureMs = 0.0;
constexpr uint32_t kPausePictures = 150;
constexpr uint32_t kStrikePictures = 60;
uint32_t sPause = 0;
uint32_t sStrikeAge = kStrikePictures;

bool Strike() {
  const bool again = sStrikeAge < kStrikePictures;
  sStrikeAge = 0;
  if (again) sPause = kPausePictures;
  return again;
}
struct Switch {
  uint32_t field;
  int hz;
};
Switch sSwitches[8];
int sSwitchCount = 0;
bool sFastToo = false;

struct Counts {
  uint32_t fields = 0, pictures = 0, between = 0;
  uint32_t rhythm = 0, partial = 0, unmatched = 0, fast = 0, busy = 0, full = 0, slow = 0;
  uint32_t budgetHooks = 0, budgetDropped = 0;
  uint64_t blended = 0, plain = 0;
  float use = 0.f;
  double ms = 0.0;
  AuroraInterpInfo last{};
} sCounts;
uint32_t sLogField = 0;
int sLogPictures = 0;

void Why(const char* why, uint32_t field, uint32_t fresh, uint32_t gap, const AuroraInterpInfo& i) {
  if (sLogPictures == 0) return;
  std::fprintf(stderr,
               "[frame rate] picture %u field %u: %s%s%s; new pictures %u, fields since the last %u; %u transform "
               "sets, %u 3D, %u found, %u moved, %u jumped, %u new, %u unknown; sprites: %u in steady motion, %u moved "
               "otherwise\n",
               i.pictures, field, why == nullptr ? "in-between" : "none (", why == nullptr ? "" : why,
               why == nullptr ? "" : ")", fresh, gap, i.draws, i.perspective, i.matched, i.moved, i.jumped, i.fresh,
               i.unknown, i.sprites, i.unsteady);
}

double NowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Log(uint32_t field) {
  if (sLogFields == 0 || field - sLogField < sLogFields) return;
  const Counts& c = sCounts;
  std::fprintf(stderr,
               "[frame rate] %u fields: %u game pictures, %u in-between pictures (%.0f%%); none because: rhythm %u, "
               "next picture begun %u, no match %u, fast-forward %u, frozen %u, no room %u, PC too slow %u; left out for the sound budget: %u pictures in %u hooks; per in-between picture %.0f draws "
               "blended, %.0f as they are, %.2f ms of extra work; fullest frame buffer %.0f%%; last comparison: %u transform sets, %u 3D, %u found, %u moved, %u jumped, %u new, %u unknown, %u unsteady; slipped %u\n",
               c.fields, c.pictures, c.between, c.pictures ? 100.0 * c.between / c.pictures : 0.0, c.rhythm, c.partial,
               c.unmatched, c.fast, c.busy, c.full, c.slow, c.budgetDropped, c.budgetHooks, c.between ? static_cast<double>(c.blended) / c.between : 0.0,
               c.between ? static_cast<double>(c.plain) / c.between : 0.0, c.between ? c.ms / c.between : 0.0, c.use * 100.0, c.last.draws, c.last.perspective,
               c.last.matched, c.last.moved, c.last.jumped, c.last.fresh, c.last.unknown, c.last.unsteady, c.last.slipped);
  std::fflush(stderr);
  sCounts = Counts{};
  sLogField = field;
}

}

extern "C" {

static int WantedHz() {
  const engine::settings::Registry& r = engine::settings::Global();
  const engine::settings::Value v = r.ValueOf(sSetting);
  int hz = 0;
  if (v.i != sChoiceOriginal) {
    const std::string& id = r.Get(sSetting).choices[static_cast<size_t>(v.i)].id;
    hz = id == "monitor" ? -1 : std::atoi(id.c_str());
  }
  if (r.Bool("video.performance").Get()) hz = 0;
  for (int i = 0; i < sSwitchCount; i++) {
    if (sField() >= sSwitches[i].field) hz = sSwitches[i].hz;
  }
  return hz;
}

static double PerFieldFor(int hz) {
  if (hz > 0 && hz <= 61) return 1.0;
  const float monitor = PortVideo_MonitorHz();
  double target = hz < 0 ? static_cast<double>(monitor) : static_cast<double>(hz);
  if (monitor > 0.f && target > monitor * 1.01) target = monitor;
  return std::max(1.0, target / kGameHz);
}

static uint32_t TakeSlots() {
  sCarry += sPerField;
  const uint32_t whole = static_cast<uint32_t>(std::floor(sCarry + 1e-6));
  sCarry -= whole;
  return std::max<uint32_t>(whole, 1);
}

static void FollowSetting() {
  if (!sStarted || sSetting == engine::settings::kNoSetting) return;
  const int hz = WantedHz();
  const bool want = hz != 0;
  if (want) {
    const double now = PerFieldFor(hz);
    if (now != sPerField || hz != sHighHz) {
      sPerField = now;
      sHighHz = hz;
      if (sOn) {
        aurora_set_frame_slots(sPerField > 1.0 ? 3 : 2);
        sCarry = 0.0;
        std::fprintf(stderr, "[frame rate] now %.2f pictures per game field (%d: %s)\n", sPerField, hz,
                     hz < 0 ? "the monitor's rate" : "chosen");
      }
    }
  }
  if (want == sOn) return;
  AuroraInterpInfo info{};
  AuroraInterpGetInfo(&info);
  sOn = want;
  sHavePicture = false;
  sLastHadBetween = false;
  sPause = 0;
  sStrikeAge = kStrikePictures;
  if (sOn) {
    PortFreeze_Capture();
    aurora_set_frame_slots(sPerField > 1.0 ? 3 : 2);
    AuroraInterpSetMode(AURORA_INTERP_RECORD, 1.f);
    size_t partial = 0;
    sPictures = PortFreeze_PictureCount(&partial);
    sCarry = 0.0;
    sExactNext = false;
    if (sPerField <= 1.0) {
      std::fprintf(stderr, "[frame rate] 60: an in-between picture between the game's own 30 a second (video.frame_rate; "
                           "the game itself still runs at its own speed)\n");
    } else {
      std::fprintf(stderr,
                   "[frame rate] %s: %.2f pictures per video field, %.1f in-between pictures between the game's own 30 "
                   "a second (video.frame_rate; monitor %.2f Hz; the game itself still runs at its own speed)\n",
                   sHighHz < 0 ? "monitor" : std::to_string(sHighHz).c_str(), sPerField, 2.0 * sPerField - 1.0,
                   static_cast<double>(PortVideo_MonitorHz()));
    }
  } else {
    aurora_set_frame_slots(2);
    AuroraInterpSetMode(AURORA_INTERP_OFF, 1.f);
    std::fprintf(stderr, "[frame rate] original: the game's own pictures only\n");
  }
}

void PortFrameRate_SetPresenter(void (*presentAndBegin)(void)) { sPresentAndBegin = presentAndBegin; }

bool PortFrameRate_Init(uint32_t (*field)(void)) {
  const engine::settings::Registry& r = engine::settings::Global();
  sSetting = r.Find("video.frame_rate");
  sChoiceOriginal = static_cast<int32_t>(r.ChoiceIndex("video.frame_rate", "original"));
  sField = field;
  if (field == nullptr || sSetting == engine::settings::kNoSetting) return false;
  if (const char* blend = std::getenv("XD_FRAME_RATE_BLEND"); blend != nullptr && blend[0] != '\0') {
    sBlend = std::clamp(static_cast<float>(std::atof(blend)), 0.f, 1.f);
  }
  if (const char* log = std::getenv("XD_FRAME_RATE_LOG"); log != nullptr && std::atof(log) > 0.0) {
    sLogFields = static_cast<uint32_t>(std::atof(log) * 60.0);
  }
  if (const char* pics = std::getenv("XD_FRAME_RATE_LOG_PICTURES"); pics != nullptr) sLogPictures = std::atoi(pics);
  if (const char* fast = std::getenv("XD_FRAME_RATE_FAST"); fast != nullptr && fast[0] == '1') sFastToo = true;
  if (const char* at = std::getenv("XD_FRAME_RATE_AT"); at != nullptr) {
    for (const char* q = at; *q != '\0' && sSwitchCount < 8;) {
      char* end = nullptr;
      const unsigned long f = std::strtoul(q, &end, 10);
      if (end == q || *end != ':') break;
      const char* value = end + 1;
      sSwitches[sSwitchCount++] =
          Switch{static_cast<uint32_t>(f), std::strncmp(value, "mon", 3) == 0 ? -1 : std::atoi(value)};
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
  if (fresh == 0) {
    if (sExactNext && sPerField > 1.0) PortVideo_PresentsComing(1, false);
    sExactNext = false;
    return;
  }
  sExactNext = false;
  sCounts.pictures += fresh;
  const uint32_t gap = field - sPictureField;
  sStrikeAge = sStrikeAge > kStrikePictures ? sStrikeAge : sStrikeAge + fresh;
  const bool hadPicture = sHavePicture;
  const bool lastHadBetween = sLastHadBetween;
  sLastHadBetween = false;
  sPictureField = field;
  sHavePicture = true;
  if (sPause > 0) {
    sPause = fresh >= sPause ? 0 : sPause - fresh;
    sCounts.slow++;
    Why("PC too slow: pause", field, fresh, gap, info);
    return;
  }
  if (lastHadBetween && fresh == 1 && gap == 3 && Strike()) {
    sCounts.slow++;
    Why("late picture, twice", field, fresh, gap, info);
    return;
  }
  if (!hadPicture || fresh != 1 || gap < 2 || gap > 4) {
    sCounts.rhythm++;
    Why("rhythm", field, fresh, gap, info);
    return;
  }
  if (info.pending != 0) {
    sCounts.partial++;
    Why("next picture begun", field, fresh, gap, info);
    return;
  }
  if (aurora::time::scale() > 1.5f && !sFastToo) {
    sCounts.fast++;
    return;
  }
  sCounts.last = info;
  if (!info.usable) {
    sCounts.unmatched++;
    Why(info.moved == 0 && info.jumped == 0 ? "nothing moved" : info.moved != 0 && info.moved <= info.unsteady ? "only sprites not in steady motion" : "a cut",
        field, fresh, gap, info);
    return;
  }
  if (use > 0.3f) {
    sCounts.full++;
    Why("no room", field, fresh, gap, info);
    return;
  }
  const bool high = sPerField > 1.0 && sPresentAndBegin != nullptr;
  uint32_t total = high ? TakeSlots() + TakeSlots() : 2;
  if (!sBudgetRead) {
    sBudgetRead = true;
    if (const char* e = std::getenv("XD_FRAME_RATE_BUDGET")) sBudgetMs = std::atof(e);
  }
  if (high && sBudgetMs > 0.0 && sPerPictureMs > 0.0) {
    const uint32_t afford = std::max<uint32_t>(1, static_cast<uint32_t>(sBudgetMs / sPerPictureMs));
    if (total - 1 > afford) {
      sCounts.budgetHooks++;
      sCounts.budgetDropped += total - 1 - afford;
      total = afford + 1;
    }
  }
  const uint32_t count = total - 1;
  if (!PortFreeze_RedrawBegin()) {
    sCounts.busy++;
    return;
  }
  if (sLogPictures >= 2) Why(nullptr, field, fresh, gap, info);
  if (high) PortVideo_PresentsComing(count, true);
  double spent = 0.0;
  float perPicture = 0.f;
  for (uint32_t j = 1; j <= count; j++) {
    const double start = NowMs();
    const float before = high ? AuroraGetFrameBufferUse() : 0.f;
    AuroraInterpSetMode(AURORA_INTERP_REPLAY, high ? static_cast<float>(j) / static_cast<float>(total) : sBlend);
    PortFreeze_DrawPicture();
    sBetweenTotal++;
    sCounts.between++;
    if (j < count) {
      const float after = AuroraGetFrameBufferUse();
      perPicture = std::max(perPicture, after - before);
      if (after + perPicture * 1.2f > 0.85f || !AuroraPresentPicture()) sPresentAndBegin();
    }
    spent += NowMs() - start;
  }
  sBetween = true;
  sBetweenShown = true;
  sLastHadBetween = true;
  if (high && count > 0) sPerPictureMs = sPerPictureMs == 0.0 ? spent / count : sPerPictureMs * 0.7 + (spent / count) * 0.3;
  sBetweenStart = spent;
  sBetweenDraws = count;
  sCounts.ms += spent;
  sExactNext = high;
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
  if (sLogPictures >= 1) std::fprintf(stderr, "[frame rate] cost: %.2f ms for %u in-between drawings, %.2f ms for the exact one\n", sBetweenStart, sBetweenDraws, took);
  if (sBetweenStart + took > kSlowMs * 0.5 * static_cast<double>(sBetweenDraws + 1) && !sFastToo) Strike();
  sCounts.blended += blended;
  sCounts.plain += plain;
}

}
