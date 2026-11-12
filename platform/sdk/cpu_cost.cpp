#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <settings_c.h>

extern "C" {
int64_t PortTime_Now(void);
int64_t PortOS_BusyFor(int64_t ticks);
int PortOS_InHandler(void);
int PortOS_OnGameThread(void);
void PortCpuCost_Accounted(int64_t ticks);
}

namespace {

constexpr double kTicksPerVsync = 40500000.0 / 60.0;

std::once_flag sInit;
int sEnabled = -1;
bool sEnvForced = false;
double sScale = 1.0;
std::unordered_map<uint32_t, double> sTable;
bool sHaveTable = false;
FILE* sLog = nullptr;

constexpr const char* kCounterNames[] = {"gx_vertices", "gx_dl_bytes", "gx_tex_loads", "mtx_ops",
                                         "cache_bytes", "disc_bytes", "aram_bytes", "gx_begins",
                                         "heap_ops", "heap_walk", "pool_allocs",
                                         "bound_instr", "bound_verts",
                                         "fc_walk", "fc_anim", "fc_mtx", "fc_load", "fc_exact", "fc_min", "fc_span",
                                         "fc_loop_out", "fc_loop_body", "fc_unknown",
                                         "sdk_instr", "sdk_span", "sdk_dirty", "mem_instr", "sdk_calls", "heap_instr",
                                         "cyc_est"};
constexpr unsigned kCounters = sizeof(kCounterNames) / sizeof(kCounterNames[0]);
std::atomic<uint64_t> sCount[kCounters];
uint64_t sCountAtStart[kCounters];
double sCoef[kCounters];
double sCoefBase = 0.0;
bool sHaveCounterModel = false;
bool sCounterModelFromFile = false;
FILE* sCountLog = nullptr;
bool sInPlace = false;
double sInPlaceChunk = 4000.0 / 8100000.0;
thread_local double tDebt = 0.0;
thread_local bool tPaying = false;

void load_counter_model(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    std::fprintf(stderr, "[cpu cost] XD_CPU_COUNTER_MODEL: can't read %s\n", path);
    return;
  }
  char line[512];
  char name[128];
  double v = 0.0;
  unsigned n = 0;
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (line[0] == '#' || std::sscanf(line, "%127s %lf", name, &v) != 2) {
      continue;
    }
    if (std::strcmp(name, "base") == 0) {
      sCoefBase = v;
      ++n;
      continue;
    }
    for (unsigned i = 0; i < kCounters; ++i) {
      if (std::strcmp(name, kCounterNames[i]) == 0) {
        sCoef[i] = v;
        ++n;
      }
    }
  }
  std::fclose(f);
  sHaveCounterModel = n > 0;
  sCounterModelFromFile = sHaveCounterModel;
  std::fprintf(stderr, "[cpu cost] XD_CPU_COUNTER_MODEL: %u values from %s\n", n, path);
}

bool sInFrame = false;
int64_t sFrameStart = 0;
int64_t sWaitStart = 0;
int sWaitDepth = 0;
int64_t sWaited = 0;
int64_t sAccounted = 0;
double sModel = 0.0;
double sBase = 0.0;

void load_table(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    std::fprintf(stderr, "[cpu cost] XD_CPU_COST_TABLE: can't read %s\n", path);
    return;
  }
  char line[8192];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (std::strncmp(line, "pf it ", 6) == 0) {
      const unsigned long id = std::strtoul(line + 6, nullptr, 10);
      const char* c = std::strstr(line, " cpu ");
      if (c == nullptr) {
        continue;
      }
      double v = std::strtod(c + 5, nullptr);
      if (const char* w = std::strstr(line, "ffff00:")) {
        v -= std::strtod(w + 7, nullptr);
      }
      sTable.emplace(static_cast<uint32_t>(id), v);
    } else if (line[0] >= '0' && line[0] <= '9') {
      char* end = nullptr;
      const unsigned long id = std::strtoul(line, &end, 10);
      sTable[static_cast<uint32_t>(id)] = std::strtod(end, nullptr);
    }
  }
  std::fclose(f);
  sHaveTable = !sTable.empty();
  std::fprintf(stderr, "[cpu cost] XD_CPU_COST_TABLE: %zu frames from %s\n", sTable.size(), path);
}

void init() {
  std::call_once(sInit, [] {
    const int32_t* slot = EngineSettings_IntSlot("system.cpu_timing");
    if (slot != nullptr) {
      sEnabled = *slot == EngineSettings_ChoiceIndex("system.cpu_timing", "gamecube") ? 1 : 0;
    }
    if (const char* e = std::getenv("XD_CPU_TIMING"); e != nullptr && e[0] != '\0') {
      sEnvForced = true;
      sEnabled = std::strcmp(e, "gamecube") == 0 ? 1 : 0;
    }
    if (const char* e = std::getenv("XD_CPU_COST_SCALE"); e != nullptr && e[0] != '\0') {
      sScale = std::strtod(e, nullptr);
    }
    if (const char* e = std::getenv("XD_CPU_COST_TABLE"); e != nullptr && e[0] != '\0') {
      load_table(e);
    }
    if (const char* e = std::getenv("XD_CPU_COST_LOG"); e != nullptr && e[0] != '\0') {
      sLog = std::fopen(e, "w");
    }
    if (const char* e = std::getenv("XD_CPU_COUNTER_MODEL"); e != nullptr && e[0] != '\0') {
      load_counter_model(e);
    }
    if (const char* e = std::getenv("XD_CPU_COUNTER_SPEND"); e != nullptr && std::strncmp(e, "inplace", 7) == 0) {
      sInPlace = true;
      if (e[7] == ':' && std::strtod(e + 8, nullptr) >= 12.0) {
        sInPlaceChunk = std::strtod(e + 8, nullptr) / 8100000.0;
      }
      std::fprintf(stderr, "[cpu cost] XD_CPU_COUNTER_SPEND: the counters' time is spent in place, in pieces of %.0f "
                           "instructions\n", sInPlaceChunk * 8100000.0);
    }
    if (const char* e = std::getenv("XD_CPU_COUNTERS_LOG"); e != nullptr && e[0] != '\0') {
      sCountLog = std::fopen(e, "w");
      if (sCountLog != nullptr) {
        std::fprintf(sCountLog, "# cnt <frame id> <worked vsyncs>");
        for (const char* n : kCounterNames) {
          std::fprintf(sCountLog, " %s", n);
        }
        std::fprintf(sCountLog, "\n");
      }
    }
  });
}

}

extern "C" {

void PortCpuCost_SetEnabled(bool on) {
  init();
  if (!sEnvForced) {
    sEnabled = on ? 1 : 0;
  }
}

bool PortCpuCost_Enabled(void) {
  init();
  return sEnabled == 1;
}

void PortCpuCost_SetBase(double vsyncs) { sBase = vsyncs; }

void PortCpuCost_Add(double vsyncs) {
  if (sInFrame) {
    sModel += vsyncs;
  }
}

void PortCpuCost_Count(unsigned counter, uint64_t amount) {
  if (counter < kCounters) {
    sCount[counter].fetch_add(amount, std::memory_order_relaxed);
    if (sInPlace) {
      tDebt += sCoef[counter] * static_cast<double>(amount);
    }
  }
}

double PortCpuCost_Debt(void) { return tDebt; }

static void pay(double least) {
  if (tPaying || tDebt < least || tDebt <= 0.0) {
    return;
  }
  tPaying = true;
  if (sEnabled == 1 && PortOS_InHandler() == 0 && PortOS_OnGameThread() != 0) {
    const int64_t ticks = static_cast<int64_t>(tDebt * sScale * kTicksPerVsync);
    const int64_t spent = PortOS_BusyFor(ticks);
    if (spent > 0) {
      PortCpuCost_Accounted(spent);
      tDebt -= static_cast<double>(spent < ticks ? spent : ticks) / (sScale * kTicksPerVsync);
    }
  }
  tPaying = false;
}

void PortCpuCost_PayDebt(void) {
  if (sInPlace) {
    pay(sInPlaceChunk);
  }
}

unsigned PortCpuCost_ReadCounters(uint64_t* out, unsigned max) {
  unsigned n = 0;
  for (; n < kCounters && n < max; ++n) {
    out[n] = sCount[n].load(std::memory_order_relaxed);
  }
  return n;
}

const char* PortCpuCost_CounterName(unsigned i) { return i < kCounters ? kCounterNames[i] : nullptr; }

void PortCpuCost_SetCounterModel(const double* coef, unsigned n, double base) {
  init();
  if (sCounterModelFromFile) {
    return;
  }
  for (unsigned i = 0; i < kCounters; ++i) {
    sCoef[i] = i < n ? coef[i] : 0.0;
  }
  sCoefBase = base;
  sHaveCounterModel = true;
}

void PortCpuCost_FrameStart(void) {
  init();
  for (unsigned i = 0; i < kCounters; ++i) {
    sCountAtStart[i] = sCount[i].load(std::memory_order_relaxed);
  }
  sInFrame = true;
  sFrameStart = PortTime_Now();
  sWaited = 0;
  sAccounted = 0;
  sWaitDepth = 0;
  sModel = 0.0;
}

void PortCpuCost_Accounted(int64_t ticks) {
  if (sInFrame && ticks > 0) {
    sAccounted += ticks;
  }
}

void PortCpuCost_WaitBegin(void) {
  if (sInFrame && sWaitDepth++ == 0) {
    sWaitStart = PortTime_Now();
  }
}

void PortCpuCost_WaitEnd(void) {
  if (sInFrame && sWaitDepth > 0 && --sWaitDepth == 0) {
    sWaited += PortTime_Now() - sWaitStart;
  }
}

void PortCpuCost_FrameEnd(uint32_t id) {
  init();
  if (!sInFrame) {
    return;
  }
  if (sInPlace) {
    pay(0.0);
  }
  sInFrame = false;
  uint64_t counts[kCounters];
  for (unsigned i = 0; i < kCounters; ++i) {
    counts[i] = sCount[i].load(std::memory_order_relaxed) - sCountAtStart[i];
  }
  if (sCountLog != nullptr) {
    const int64_t worked = (PortTime_Now() - sFrameStart) - sWaited;
    std::fprintf(sCountLog, "cnt %u %.6f", id, static_cast<double>(worked) / kTicksPerVsync);
    for (uint64_t c : counts) {
      std::fprintf(sCountLog, " %llu", static_cast<unsigned long long>(c));
    }
    std::fprintf(sCountLog, "\n");
    if ((id & 63) == 0) {
      std::fflush(sCountLog);
    }
  }
  if (sEnabled != 1) {
    return;
  }
  double model = sBase + sModel;
  if (sHaveCounterModel) {
    model = sCoefBase;
    for (unsigned i = 0; i < kCounters && !sInPlace; ++i) {
      model += sCoef[i] * static_cast<double>(counts[i]);
    }
  }
  if (sHaveTable) {
    auto it = sTable.find(id);
    model = it != sTable.end() ? it->second : 0.0;
  }
  model *= sScale;
  const int64_t now = PortTime_Now();
  const int64_t worked = (now - sFrameStart) - sWaited - sAccounted;
  const int64_t want = static_cast<int64_t>(model * kTicksPerVsync + 0.5);
  int64_t spent = 0;
  if (want > worked) {
    spent = PortOS_BusyFor(want - worked);
  }
  if (sLog != nullptr) {
    std::fprintf(sLog, "cpu %u model %.6f worked %.6f waited %.6f spent %.6f other %.6f start %lld end %lld\n", id,
                 model, static_cast<double>(worked) / kTicksPerVsync, static_cast<double>(sWaited) / kTicksPerVsync,
                 static_cast<double>(spent) / kTicksPerVsync, static_cast<double>(sAccounted) / kTicksPerVsync,
                 static_cast<long long>(sFrameStart), static_cast<long long>(PortTime_Now()));
    if ((id & 63) == 0) {
      std::fflush(sLog);
    }
  }
}

}
