#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
bool PortTime_IsDeterministic(void);
int64_t PortOS_BusyFor(int64_t ticks);
}

namespace {

constexpr double kFieldTicks = 40500000.0 * 1001.0 / 60000.0;

std::once_flag sInit;
std::map<uint32_t, double> sCosts;
bool sActive = false;
uint32_t sLastFrame = UINT32_MAX;
FILE* sPacingLog = nullptr;

bool (*sPacer)(uint32_t frame, int64_t* field) = nullptr;
bool sPaceBased = false;
int64_t sPaceBase = 0;
uint64_t sPaced = 0, sPacedFields = 0, sBehind = 0, sBehindFields = 0;
bool sPaceAtFrame = [] {
  const char* e = std::getenv("XD_REPLAY_PACING_AT");
  return e != nullptr && std::strcmp(e, "frame") == 0;
}();

void init() {
  std::call_once(sInit, [] {
    const char* log = std::getenv("XD_PACING_LOG");
    if (log != nullptr && log[0] != '\0') {
      sPacingLog = std::fopen(log, "w");
      if (sPacingLog == nullptr) {
        std::fprintf(stderr, "[frame cost] XD_PACING_LOG: can't write %s\n", log);
      }
    }
    const char* spec = std::getenv("XD_FRAME_COST");
    if (spec == nullptr || spec[0] == '\0') {
      return;
    }
    if (!PortTime_IsDeterministic()) {
      std::fprintf(stderr, "[frame cost] XD_FRAME_COST needs XD_DETERMINISTIC=1; ignored\n");
      return;
    }
    const char* p = spec;
    while (*p != '\0') {
      char* end = nullptr;
      const unsigned long frame = std::strtoul(p, &end, 10);
      if (end == p || *end != ':') {
        std::fprintf(stderr, "[frame cost] XD_FRAME_COST: can't read '%s' (want frame:fields,...)\n", p);
        sCosts.clear();
        return;
      }
      p = end + 1;
      const double fields = std::strtod(p, &end);
      if (end == p || fields < 0.0) {
        std::fprintf(stderr, "[frame cost] XD_FRAME_COST: can't read the fields at '%s'\n", p);
        sCosts.clear();
        return;
      }
      sCosts[static_cast<uint32_t>(frame)] += fields;
      p = end;
      while (*p == ',' || *p == ' ' || *p == ';') {
        p++;
      }
    }
    sActive = !sCosts.empty();
    double total = 0;
    for (auto& [f, n] : sCosts) {
      total += n;
    }
    std::fprintf(stderr, "[frame cost] XD_FRAME_COST: %zu frames cost %.2f extra fields in all (first at frame %u)\n",
                 sCosts.size(), total, sCosts.empty() ? 0u : sCosts.begin()->first);
  });
}

}

extern "C" {

bool PortFrameCost_Active(void) {
  init();
  return sActive;
}

void PortFrameCost_FrameWork(void) {
  init();
  if (!sActive && sPacingLog == nullptr && sPacer == nullptr) {
    return;
  }
  const uint32_t frame = PortVI_GetFlipCount();
  if (frame == sLastFrame) {
    return;
  }
  sLastFrame = frame;
  if (sPacingLog != nullptr) {
    std::fprintf(sPacingLog, "work %u field %u\n", frame, VIGetRetraceCount());
  }
  int64_t target = 0;
  if (sPacer != nullptr && sPaceAtFrame && sPacer(frame, &target)) {
    const int64_t now = VIGetRetraceCount();
    if (!sPaceBased) {
      sPaceBased = true;
      sPaceBase = now - target;
    }
    const int64_t need = sPaceBase + target - now;
    if (need > 0) {
      PortOS_BusyFor(need * static_cast<int64_t>(kFieldTicks) + 64);
      sPaced++;
      sPacedFields += static_cast<uint64_t>(need);
    } else if (need < 0) {
      sBehind++;
      sBehindFields += static_cast<uint64_t>(-need);
    }
    if (sPacingLog != nullptr) {
      std::fprintf(sPacingLog, "pace %u target %lld field %u\n", frame, static_cast<long long>(sPaceBase + target),
                   VIGetRetraceCount());
    }
  }
  auto it = sCosts.find(frame);
  if (it == sCosts.end()) {
    return;
  }
  const double fields = it->second;
  sCosts.erase(it);
  const uint32_t before = VIGetRetraceCount();
  const int64_t spent = PortOS_BusyFor(static_cast<int64_t>(fields * kFieldTicks + 0.5));
  if (sPacingLog != nullptr) {
    std::fprintf(sPacingLog, "busy %u until field %u\n", frame, VIGetRetraceCount());
  }
  std::fprintf(stderr, "[frame cost] game frame %u: busy %.2f fields (field %u -> %u)%s\n", frame,
               static_cast<double>(spent) / kFieldTicks, before, VIGetRetraceCount(),
               spent == 0 ? " -- NOT applied (interrupts off or not the running thread)" : "");
}

void PortFrameCost_SetPacer(bool (*pacer)(uint32_t frame, int64_t* field)) {
  sPacer = PortTime_IsDeterministic() ? pacer : nullptr;
  sPaceBased = false;
}

bool PortFrameCost_GpuDueField(int64_t* field) {
  init();
  if (sPacer == nullptr || sPaceAtFrame) return false;
  int64_t target = 0;
  if (!sPacer(PortVI_GetFlipCount(), &target)) return false;
  const int64_t now = VIGetRetraceCount();
  if (!sPaceBased) {
    sPaceBased = true;
    sPaceBase = now - target;
  }
  *field = sPaceBase + target;
  if (*field > now) {
    sPaced++;
    sPacedFields += static_cast<uint64_t>(*field - now);
  } else if (*field < now) {
    sBehind++;
    sBehindFields += static_cast<uint64_t>(now - *field);
  }
  if (sPacingLog != nullptr) {
    std::fprintf(sPacingLog, "gpu %u target %lld field %lld\n", PortVI_GetFlipCount(), static_cast<long long>(*field),
                 static_cast<long long>(now));
  }
  return *field > now;
}

bool PortFrameCost_PaceBase(int64_t* base) {
  *base = sPaceBase;
  return sPaceBased;
}

void PortFrameCost_Rebase(void) { sPaceBased = false; }

void PortFrameCost_PacingStats(uint64_t* paced, uint64_t* pacedFields, uint64_t* behind, uint64_t* behindFields) {
  *paced = sPaced, *pacedFields = sPacedFields, *behind = sBehind, *behindFields = sBehindFields;
}

void PortFrameCost_OnFlip(uint32_t flip) {
  init();
  if (sPacingLog != nullptr) {
    std::fprintf(sPacingLog, "flip %u field %u\n", flip, VIGetRetraceCount());
    std::fflush(sPacingLog);
  }
}

}
