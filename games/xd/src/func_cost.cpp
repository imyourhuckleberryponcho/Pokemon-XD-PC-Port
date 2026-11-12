#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct HSD_AObj {
  uint32_t flags;
  float curr_frame, rewind_frame, end_frame, framerate;
  struct HSD_FObj* fobj;
  void* hsd_obj;
};
struct HSD_FObj {
  HSD_FObj* next;
  uint8_t* ad;
  uint8_t* ad_head;
  uint32_t length;
  uint8_t flags, op, op_intrp, obj_type, frac_value, frac_slope;
  uint16_t nb_pack;
  int16_t startframe;
  uint16_t fterm;
  float time, p0, p1, d0, d1;
};
struct HSD_JObj {
  void* class_info;
  uint16_t ref_count, ref_count_individual;
  HSD_JObj* next;
  HSD_JObj* parent;
  HSD_JObj* child;
  uint32_t flags;
  void* u;
  float rotate[4], scale[3], translate[3], mtx[3][4];
  float* scl;
  void* envelopemtx;
  HSD_AObj* aobj;
  void* robj;
};
struct HSD_DObj {
  void* class_info;
  HSD_DObj* next;
  struct HSD_MObj* mobj;
  struct HSD_PObj* pobj;
  HSD_AObj* aobj;
};
struct HSD_TObj {
  void* class_info;
  uint16_t ref_count, ref_count_individual;
  HSD_TObj* next;
};
struct HSD_PObj {
  void* class_info;
  HSD_PObj* next;
};
struct HSD_RObj {
  HSD_RObj* next;
};
constexpr uint32_t JOBJ_CLASSICAL_SCALE = 1u << 3, JOBJ_MTX_DIRTY = 1u << 6, JOBJ_INSTANCE = 1u << 12,
                   JOBJ_USE_QUATERNION = 1u << 17, JOBJ_JOINT1 = 1u << 21, JOBJ_JOINT2 = 2u << 21,
                   JOBJ_JOINT = 3u << 21, JOBJ_EFFECTOR = 3u << 21, JOBJ_USER_DEF_MTX = 1u << 23,
                   JOBJ_MTX_INDEP_PARENT = 1u << 24, JOBJ_MTX_INDEP_SRT = 1u << 25;

extern "C" {
void PortCpuCost_Count(unsigned counter, uint64_t amount);
int PortFuncCost_Init(void);
int PortFuncCost_Resolve(const char* name);
void PortFuncCost_Call(int entry, const uintptr_t* regs);
void PortFuncCost_Statics(const uint8_t* partFound);
void PortFuncCost_TevGlobals(const void* const* toon, const void* const* shadows);
int PortFuncCost_Row(const char* name, char* kind, uint32_t* a, uint32_t* b);
void PortSdkCost_SkipConcat(void);
}

namespace {

enum : unsigned { kWalk = 13, kAnim, kMtx, kLoad, kExact, kMin, kSpan, kLoopOut, kLoopBody, kUnknown, kCycEst = 29 };

thread_local unsigned tFobjCyc = 0;

enum Handler : uint8_t {
  hTable, hWalkRoot, hWalkVisit, hPartFind, hFObj, hFObjAll, hAObj, hJObjUpdate, hDirtySub, hSetupMtx, hMakeMtx,
  hMtxSRT, hJObjLoad,
  hForeach, hJForeach, hDForeach, hMForeach, hTForeach, hPForeach, hRForeach, hForeachCB,
  hJAnimAllTop, hJAnimAll, hJAnim, hCheckDepend, hDAnimAll, hRAnimAll, hPAnimAll, hTAnimAll, hMAnim,
  hJReqAll, hJReq, hDReqAll, hRReqAll, hPReqAll, hTReqAll, hMReq, hAReq, hFReqAll,
  hModelAddAll, hJAddAnim, hJAddAll, hSortAnim, hJSetFlags, hALoadDesc, hFLoadDesc, hFAlloc, hASetFlags,
  hCompileTev,
  hPeConvert, hShapeLoad
};
struct Entry {
  Handler handler = hTable;
  char kind = '?';
  uint32_t a = 0, b = 0;
  uint32_t x = 0;
};
std::vector<Entry> sEntries;
std::unordered_map<std::string, int> sByName;
bool sLogUnknown = false;
HSD_JObj* sWalkRoot = nullptr;
const uint8_t* sPartFound = nullptr;

const struct {
  const char* name;
  Handler handler;
} kHandlers[] = {
    {"HSD_JObjWalkTree", hWalkRoot},       {"HSD_JObjWalkTree0", hWalkVisit},     {"_partFindJObjCB", hPartFind},
    {"HSD_FObjInterpretAnim", hFObj},      {"HSD_FObjInterpretAnimAll", hFObjAll}, {"HSD_AObjInterpretAnim", hAObj},
    {"JObjUpdateFunc", hJObjUpdate},       {"HSD_JObjSetMtxDirtySub", hDirtySub}, {"HSD_JObjSetupMatrixSub", hSetupMtx},
    {"HSD_JObjMakeMatrix", hMakeMtx},      {"HSD_MtxSRT", hMtxSRT},               {"JObjLoad", hJObjLoad},
    {"HSD_ForeachAnim", hForeach},         {"JObjForeachAnim", hJForeach},        {"DObjForeachAnim", hDForeach},
    {"MObjForeachAnim", hMForeach},        {"TObjForeachAnim", hTForeach},        {"PObjForeachAnim", hPForeach},
    {"RObjForeachAnim", hRForeach},        {"callbackForeachFunc", hForeachCB},
    {"HSD_JObjAnimAll", hJAnimAllTop},     {"JObjAnimAll", hJAnimAll},            {"HSD_JObjAnim", hJAnim},
    {"HSD_JObjCheckDepend", hCheckDepend}, {"HSD_DObjAnimAll", hDAnimAll},        {"HSD_RObjAnimAll", hRAnimAll},
    {"HSD_PObjAnimAll", hPAnimAll},        {"HSD_TObjAnimAll", hTAnimAll},        {"HSD_MObjAnim", hMAnim},
    {"HSD_JObjReqAnimAllByFlags", hJReqAll}, {"HSD_JObjReqAnimByFlags", hJReq},
    {"HSD_DObjReqAnimAllByFlags", hDReqAll}, {"HSD_RObjReqAnimAllByFlags", hRReqAll},
    {"HSD_PObjReqAnimAllByFlags", hPReqAll}, {"HSD_TObjReqAnimAllByFlags", hTReqAll},
    {"HSD_MObjReqAnimByFlags", hMReq},     {"HSD_AObjReqAnim", hAReq},            {"HSD_FObjReqAnimAll", hFReqAll},
    {"_jobjAddAnimAll", hModelAddAll},     {"HSD_JObjAddAnim", hJAddAnim},        {"HSD_JObjAddAnimAll", hJAddAll},
    {"JObjSortAnim", hSortAnim},           {"HSD_JObjSetFlags", hJSetFlags},      {"HSD_JObjClearFlags", hJSetFlags},
    {"HSD_AObjLoadDesc", hALoadDesc},      {"HSD_FObjLoadDesc", hFLoadDesc},      {"HSD_FObjAlloc", hFAlloc},
    {"HSD_AObjSetFlags", hASetFlags},      {"HSD_MObjCompileTev", hCompileTev},
    {"_peConvertFileData", hPeConvert},    {"GsShapeRender::LoadShapeRender", hShapeLoad},
};

struct PeCost {
  uint32_t convert = 0;
  std::vector<uint32_t> shapes;
};
std::unordered_map<uint32_t, PeCost> sPe;
thread_local const PeCost* tPeFile = nullptr;
thread_local size_t tPeShape = 0;

uint32_t crc32_bytes(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
  }
  return ~c;
}

bool mtx_dirty(const HSD_JObj* j) { return (j->flags & JOBJ_MTX_DIRTY) != 0 && (j->flags & JOBJ_USER_DEF_MTX) == 0; }

constexpr uint32_t JOBJ_NO_DOBJ = 0x4020;
constexpr int64_t kTableRow = -1;

struct WalkPath {
  const HSD_JObj* at[64];
  int depth = -1;
  int enter(const HSD_JObj* j) {
    for (int l = depth < 62 ? depth + 1 : 62; l >= 1; l--) {
      if (j->parent == at[l - 1]) {
        at[l] = j;
        depth = l;
        return l;
      }
    }
    at[0] = j;
    depth = 0;
    return 0;
  }
};
thread_local WalkPath tAnimPath, tReqPath, tModelAddPath, tAddPath;
thread_local const HSD_JObj* tAnimJoint = nullptr;
thread_local int tAnimClass = 0;
thread_local const HSD_JObj* tCheckInlined = nullptr;
thread_local const HSD_JObj* tReqInlined = nullptr;
thread_local const HSD_JObj* tAddInlined = nullptr;
thread_local const HSD_JObj* tSetFlagsInlined = nullptr;
thread_local unsigned tForeachF = 1;
thread_local bool tForeachPending = false;
thread_local unsigned tFObjTrack = 0;
thread_local bool tFObjInList = false;
thread_local unsigned tFAllocInlined = 0, tASetFlagsInlined = 0;

unsigned foreach_form(uintptr_t form, unsigned* top) {
  static const unsigned kF[12] = {0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 2};
  const unsigned f = (form & 0xFFFFFFFFu) < 12 ? static_cast<unsigned>(form & 0xFFFFFFFFu) : 2;
  *top = f % 4 == 0 ? 54 : f % 4 == 1 ? 102 : 87;
  return kF[f];
}

unsigned depend_path(const HSD_JObj* j) {
  if ((j->flags & JOBJ_MTX_DIRTY) != 0 && (j->flags & JOBJ_USER_DEF_MTX) == 0) {
    return 0;
  }
  const HSD_JObj* p = j->parent;
  if ((j->flags & JOBJ_USER_DEF_MTX) != 0) {
    if ((j->flags & JOBJ_MTX_INDEP_PARENT) != 0) {
      return 1;
    }
    if (p == nullptr) {
      return 2;
    }
    if ((p->flags & JOBJ_USER_DEF_MTX) != 0) {
      return 3;
    }
    return (p->flags & JOBJ_MTX_DIRTY) != 0 ? 5 : 4;
  }
  if (p != nullptr && (p->flags & JOBJ_MTX_DIRTY) != 0) {
    return 6;
  }
  const uint32_t ik = j->flags & JOBJ_JOINT;
  const unsigned k = ik == JOBJ_JOINT1 ? 0 : ik == JOBJ_JOINT2 ? 1 : ik == JOBJ_EFFECTOR ? 2 : j->robj != nullptr ? 3 : 4;
  return (p == nullptr ? 7 : 12) + k;
}
const uint8_t kDependFunc[17] = {24, 24, 27, 34, 36, 41, 33, 34, 37, 40, 43, 40, 37, 40, 43, 46, 43};
const uint8_t kDependAnim[17] = {38, 39, 42, 49, 51, 56, 48, 49, 52, 55, 58, 55, 52, 55, 58, 61, 58};
const uint8_t kDependA[17] = {0, 1, 4, 11, 13, 18, 10, 11, 14, 17, 20, 17, 14, 17, 20, 23, 20};
const uint8_t kDependB[17] = {0, 5, 4, 11, 11, 15, 12, 11, 14, 17, 20, 17, 16, 19, 22, 25, 22};

template <class T> unsigned list_len(const T* p) {
  unsigned n = 0;
  for (; p != nullptr && n < 4096; p = p->next) {
    n++;
  }
  return n;
}

struct TevCost {
  uint32_t first = 0, again = 0;
  uint64_t calls = 0;
  bool known = false;
};
std::unordered_map<std::string, TevCost> sTev;
FILE* sTevLog = nullptr;
bool sTevGlobalsSeen = false;
const void* const* sTevToon = nullptr;
const void* const* sTevShadows = nullptr;

uint32_t tev_extensions(const uint8_t* mobj) {
  __try {
    const uint8_t* user = *reinterpret_cast<const uint8_t* const*>(mobj + 64);
    if (user == nullptr || *reinterpret_cast<const uint8_t* const*>(user + 8) != mobj) {
      return 0;
    }
    return *reinterpret_cast<const uint16_t*>(user + 2);
  } __except (1) {
    return 0;
  }
}

int64_t tev_compile_cost(const uint8_t* mobj) {
  const uint32_t rendermode = *reinterpret_cast<const uint32_t*>(mobj + 8);
  const bool again = *reinterpret_cast<const void* const*>(mobj + 48) != nullptr ||
                     *reinterpret_cast<const void* const*>(mobj + 56) != nullptr;
  char key[1024];
  int at = std::snprintf(key, sizeof(key), "%08x\t", rendermode);
  const uint8_t* t = *reinterpret_cast<const uint8_t* const*>(mobj + 16);
  if (t == nullptr) {
    at += std::snprintf(key + at, sizeof(key) - at, "-");
  }
  for (unsigned n = 0; t != nullptr && n < 8 && at < 800; t = *reinterpret_cast<const uint8_t* const*>(t + 16), n++) {
    at += std::snprintf(key + at, sizeof(key) - at, "%s%08x", n != 0 ? "," : "", *reinterpret_cast<const uint32_t*>(t + 88));
    if (const uint8_t* tev = *reinterpret_cast<const uint8_t* const*>(t + 208); tev != nullptr) {
      at += std::snprintf(key + at, sizeof(key) - at, ":");
      for (unsigned i = 0; i < 28; i++) {
        at += std::snprintf(key + at, sizeof(key) - at, "%02x", tev[i]);
      }
      for (unsigned i = 0; i < 4; i++) {
        at += std::snprintf(key + at, sizeof(key) - at, "%02x", tev[31 - i]);
      }
    }
  }
  at += std::snprintf(key + at, sizeof(key) - at, "\t%x", tev_extensions(mobj));
  const bool toon = (rendermode & 0x1000u) != 0 && sTevToon != nullptr && *sTevToon != nullptr &&
                    *reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(*sTevToon) + 104) != nullptr;
  const bool shadows = (rendermode & 0x04000000u) != 0 && sTevShadows != nullptr && *sTevShadows != nullptr;
  if (toon || shadows) {
    std::snprintf(key + at, sizeof(key) - at, "\t%s%s", toon ? "toon" : "", shadows ? "shadows" : "");
    if (!sTevGlobalsSeen) {
      sTevGlobalsSeen = true;
      std::fprintf(stderr, "[func cost] a material is compiled with a global texture (rendermode %08x%s%s)\n", rendermode,
                   toon ? ", toon" : "", shadows ? ", shadows" : "");
    }
  }
  TevCost& c = sTev[key];
  if (c.calls == 0 && !c.known && std::strchr(key, ':') != nullptr) {
    std::string alt;
    for (const char* p = key; *p != 0;) {
      if (*p == ':') {
        alt += ":tev";
        p += 1 + 64;
      } else {
        alt += *p++;
      }
    }
    if (auto it = sTev.find(alt); it != sTev.end() && it->second.known) {
      const TevCost found = it->second;
      c.first = found.first;
      c.again = found.again;
      c.known = true;
    }
  }
  if (c.calls++ == 0 && !c.known && sTevLog != nullptr) {
    std::fprintf(sTevLog, "%s\n", key);
    std::fflush(sTevLog);
  }
  if (!c.known) {
    PortCpuCost_Count(kUnknown, 1000000);
    return again ? 12386 : 12286;
  }
  return again ? c.again : c.first;
}

int64_t run_anim(const Entry& e, const uintptr_t* regs, unsigned* counter) {
  const uintptr_t a0 = regs[1], a1 = regs[2], a2 = regs[3], a3 = regs[4];
  const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
  *counter = kAnim;
  switch (e.handler) {
    case hForeach:
      if (a0 == 0) {
        return 25;
      }
      tForeachPending = true;
      return 54;
    case hJForeach: {
      unsigned top = 54;
      tForeachF = foreach_form(a3, &top);
      int64_t n = tForeachPending ? top - 54 : 0;
      tForeachPending = false;
      if (j == nullptr) {
        return n;
      }
      n += (a1 & 0x20) != 0 ? 37 : 34;
      if ((j->flags & JOBJ_NO_DOBJ) == 0) {
        n += 6;
      }
      if ((j->flags & JOBJ_INSTANCE) != 0) {
        n -= 4;
      } else {
        n += 9 * list_len(j->child);
      }
      return n;
    }
    case hDForeach: {
      unsigned top = 54;
      tForeachF = foreach_form(a3, &top);
      int64_t n = tForeachPending ? top - 54 : 0;
      tForeachPending = false;
      n += 20;
      unsigned guard = 0;
      for (auto* d = reinterpret_cast<const HSD_DObj*>(a0); d != nullptr && guard < 4096; d = d->next, guard++) {
        n += 12 + ((a1 & 0x4) != 0 ? 3 : 0);
      }
      return n;
    }
    case hMForeach:
      tForeachPending = false;
      return a0 == 0 ? 0 : (a1 & 0x80) != 0 ? 9 : 6;
    case hTForeach:
      tForeachPending = false;
      return list_len(reinterpret_cast<const HSD_TObj*>(a0)) * ((a1 & 0x400) != 0 ? 8u : 5u);
    case hPForeach:
      tForeachPending = false;
      return list_len(reinterpret_cast<const HSD_PObj*>(a0)) * ((a1 & 0x100) != 0 ? 8u : 5u);
    case hRForeach:
      tForeachPending = false;
      return list_len(reinterpret_cast<const HSD_RObj*>(a0)) * ((a1 & 0x200) != 0 ? 8u : 5u);
    case hForeachCB:
      return 12 + tForeachF;

    case hJAnimAllTop:
      tAnimPath.depth = -1;
      return a0 == 0 ? 11 : 15;
    case hJAnimAll: {
      if (j == nullptr) {
        return 0;
      }
      const int d = tAnimPath.enter(j);
      const int c = d == 0 ? 0 : (d - 1) % 4 + 1;
      const unsigned path = depend_path(j);
      int64_t n = c == 0 ? 50 + kDependA[path] : c == 4 ? 55 + kDependA[path] : c == 1 ? 32 + kDependB[path] : c == 2 ? 29 : 14;
      if ((j->flags & JOBJ_INSTANCE) != 0) {
        n -= 4;
      }
      if (c != 3 && (j->flags & JOBJ_NO_DOBJ) != 0) {
        n -= 2;
      }
      tAnimJoint = j;
      tAnimClass = c;
      return n;
    }
    case hJAnim: {
      if (j == nullptr) {
        return 13;
      }
      const bool fromWalk = j == tAnimJoint;
      tAnimJoint = nullptr;
      if (fromWalk && tAnimClass != 3) {
        tCheckInlined = tAnimClass == 2 ? nullptr : j;
        return 0;
      }
      tCheckInlined = j;
      return kDependAnim[depend_path(j)] - ((j->flags & JOBJ_NO_DOBJ) != 0 ? 2 : 0);
    }
    case hCheckDepend: {
      if (j == nullptr) {
        return 13;
      }
      if (j == tCheckInlined) {
        tCheckInlined = nullptr;
        return 0;
      }
      return kDependFunc[depend_path(j)];
    }
    case hDAnimAll:
      return a0 == 0 ? 11 : 15 + 14 * list_len(reinterpret_cast<const HSD_DObj*>(a0));
    case hRAnimAll:
      return a0 == 0 ? 13 : 19 + 9 * list_len(reinterpret_cast<const HSD_RObj*>(a0));
    case hPAnimAll:
      return a0 == 0 ? 11 : 15 + 10 * list_len(reinterpret_cast<const HSD_PObj*>(a0));
    case hTAnimAll:
      return a0 == 0 ? 11 : 15 + 10 * list_len(reinterpret_cast<const HSD_TObj*>(a0));
    case hMAnim:
      return a0 == 0 ? 11 : 18;

    case hJReqAll: {
      if (j == nullptr) {
        return 0;
      }
      const int d = tReqPath.enter(j);
      const bool top = d % 6 == 0, real = d % 6 == 5;
      int64_t n = d == 0 ? 41 : top ? 48 : real ? 16 : 31;
      if (!real) {
        if ((j->flags & JOBJ_NO_DOBJ) != 0) {
          n -= 4;
        }
        if ((a1 & 1) == 0) {
          n -= top ? 2 : 3;
        }
      }
      if ((j->flags & JOBJ_INSTANCE) != 0) {
        n -= top ? 5 : 4;
      }
      tReqInlined = real ? nullptr : j;
      return n;
    }
    case hJReq: {
      if (j == nullptr) {
        return 17;
      }
      if (j == tReqInlined) {
        tReqInlined = nullptr;
        return 0;
      }
      return ((j->flags & JOBJ_NO_DOBJ) != 0 ? 30 : 34) - ((a1 & 1) != 0 ? 0 : 2);
    }
    case hDReqAll:
      return a0 == 0 ? 17 : 21 + 13 * list_len(reinterpret_cast<const HSD_DObj*>(a0));
    case hRReqAll:
      return a0 == 0 ? 17 : 21 + 10 * list_len(reinterpret_cast<const HSD_RObj*>(a0));
    case hPReqAll:
      return a0 == 0 ? 16 : 21 + ((a2 & 0x8) != 0 ? 10u : 7u) * list_len(reinterpret_cast<const HSD_PObj*>(a0));
    case hTReqAll:
      return a0 == 0 ? 16 : 21 + ((a2 & 0x10) != 0 ? 10u : 7u) * list_len(reinterpret_cast<const HSD_TObj*>(a0));
    case hMReq:
      return a0 == 0 ? 17 : (a2 & 0x4) != 0 ? 25 : 23;
    case hAReq:
      return a0 == 0 ? 9 : 16;
    case hFReqAll:
      return a0 == 0 ? 5 : 8 + 35 * list_len(reinterpret_cast<const HSD_FObj*>(a0));

    case hModelAddAll: {
      *counter = kLoad;
      if (j == nullptr) {
        return 0;
      }
      const int d = tModelAddPath.enter(j);
      const bool instance = (j->flags & JOBJ_INSTANCE) != 0;
      int64_t n = d != 0 && d % 4 == 0 ? 58 : 41;
      if (instance) {
        n -= 13;
      }
      if (j->aobj == nullptr) {
        n -= 3;
      } else if ((j->aobj->flags & 0x10000u) != 0) {
        n -= 5;
      }
      for (const uintptr_t node : {a1, a2, a3}) {
        if (node != 0) {
          n += (instance ? 0 : 1) + (d != 0 ? 1 : 0);
        }
      }
      return n;
    }
    case hJAddAnim: {
      *counter = kLoad;
      if (j == nullptr) {
        return 20;
      }
      if (j == tAddInlined) {
        tAddInlined = nullptr;
        return 0;
      }
      int64_t n;
      if (a1 == 0) {
        n = 35;
      } else {
        n = 88 - 17 + (j->aobj != nullptr ? 1 : 0);
        const bool set = (reinterpret_cast<const uint8_t*>(a1)[19] & 1) != 0;
        if ((j->flags & JOBJ_CLASSICAL_SCALE) != 0) {
          n -= set ? 11 : 12;
        } else if (mtx_dirty(j)) {
          n += set ? 1 : 0;
        } else if ((j->flags & JOBJ_USER_DEF_MTX) != 0) {
          n += set ? 0 : -1;
        } else {
          n += set ? 2 : 1;
        }
        tSetFlagsInlined = j;
      }
      if ((j->flags & JOBJ_NO_DOBJ) != 0) {
        n -= 8;
      } else {
        n += (a2 != 0 ? 1 : 0) + (a3 != 0 ? 1 : 0);
      }
      return n;
    }
    case hSortAnim: {
      *counter = kLoad;
      const auto* a = reinterpret_cast<const HSD_AObj*>(a0);
      if (a == nullptr) {
        return 0;
      }
      if (a->fobj == nullptr) {
        return 3;
      }
      unsigned k = 0;
      for (const HSD_FObj* f = a->fobj; f != nullptr && k < 4096; f = f->next, k++) {
        if (f->obj_type == 12) {
          return 17 + 7 * k;
        }
      }
      return 8 + 7 * k;
    }
    case hJSetFlags:
      if (j != nullptr && j == tSetFlagsInlined) {
        tSetFlagsInlined = nullptr;
        return 0;
      }
      return kTableRow;
    case hJAddAll: {
      *counter = kLoad;
      if (j == nullptr) {
        return 0;
      }
      if (a1 != 0) {
        return kTableRow;
      }
      const int d = tAddPath.enter(j);
      const bool real = d % 3 == 2, instance = (j->flags & JOBJ_INSTANCE) != 0;
      int64_t n = d == 0 ? 46 : real ? 35 : d % 3 == 0 ? 63 : 46;
      if (instance) {
        n -= 13;
      }
      if (a2 != 0) {
        n += (instance ? 0 : 1) + (d != 0 ? 1 : 0) + (!real && (j->flags & JOBJ_NO_DOBJ) == 0 ? 1 : 0);
      }
      if (!real && (j->flags & JOBJ_NO_DOBJ) != 0) {
        n -= 8;
      }
      tAddInlined = real ? nullptr : j;
      return n;
    }
    case hALoadDesc: {
      *counter = kLoad;
      if (a0 == 0) {
        return 16;
      }
      tASetFlagsInlined = 1;
      const auto* desc = reinterpret_cast<const uint8_t*>(a0);
      const bool id = (desc[12] | desc[13] | desc[14] | desc[15]) != 0;
      return 55 + (id ? 18 : 0);
    }
    case hASetFlags:
      if (tASetFlagsInlined != 0) {
        tASetFlagsInlined = 0;
        return 0;
      }
      return kTableRow;
    case hFLoadDesc: {
      *counter = kLoad;
      if (a0 == 0) {
        const bool empty = !tFObjInList;
        tFObjInList = false;
        return empty ? 12 : 0;
      }
      int64_t n = 0;
      if (!tFObjInList) {
        tFObjInList = true;
        tFObjTrack = 0;
        n = 12;
      }
      const unsigned k = tFObjTrack++ % 4;
      tFAllocInlined = k == 3 ? 0 : 1;
      return n + (k == 0 || k == 3 ? 34 : 33);
    }
    case hFAlloc:
      if (tFAllocInlined != 0) {
        tFAllocInlined = 0;
        return 0;
      }
      return kTableRow;
    case hCompileTev:
      *counter = kExact;
      return a0 == 0 ? 10 : tev_compile_cost(reinterpret_cast<const uint8_t*>(a0));
    default:
      break;
  }
  return 0;
}

unsigned frac_extra(uint8_t frac, unsigned* bytes) {
  if (frac == 0) {
    *bytes = 4;
    return 0;
  }
  switch (frac & 0xE0) {
    case 0x60: *bytes = 1; tFobjCyc += 16; return 1;
    case 0x80: *bytes = 1; tFobjCyc += 16; return 2;
    case 0x40: *bytes = 2; tFobjCyc += 16; return 5;
    case 0x20: *bytes = 2; tFobjCyc += 16; return 9;
    default: *bytes = 0; return 0;
  }
}

unsigned update_in_wait(HSD_FObj& f) {
  tFobjCyc += 1;
  switch (f.op_intrp) {
    case 6: f.flags &= 0x7F; return 21;
    case 1: return 29;
    case 2: f.flags &= ~0x20; return 19;
    case 3: case 4: case 5: tFobjCyc += f.fterm != 0 ? 30 : 0; return f.fterm != 0 ? 60 : 21;
    default: return 0;
  }
}

uint64_t fobj_cost(const HSD_FObj* real, float rate) {
  tFobjCyc = 1;
  if (real == nullptr || (real->flags & 0xF) == 0) {
    return 29;
  }
  HSD_FObj f = *real;
  unsigned state = f.flags & 0xF;
  f.time += rate;
  if (f.time < 0.0f) {
    return 36;
  }
  static const unsigned kLoad[7] = {0, 112, 109, 107, 131, 97, 106};
  uint64_t n = 27;
  for (unsigned turns = 0; turns < 100000; turns++) {
    switch (state) {
      case 6: {
        unsigned c;
        if ((f.flags & 0x40) != 0) {
          f.op_intrp = f.op;
          f.flags = static_cast<uint8_t>((f.flags & ~0x40) | 0x80);
          n += 10;
        }
        switch (f.op_intrp) {
          case 6: c = (f.flags & 0x80) != 0 ? 43 : 31; break;
          case 1: c = f.time >= f.fterm ? 50 : 49; break;
          case 2: c = (f.flags & 0x20) != 0 ? (f.fterm != 0 ? 58 : 50) : 41; break;
          case 3: case 4: case 5: c = f.fterm != 0 ? 81 : 42; break;
          default: c = 29; break;
        }
        if (c != 29 && c != 31) tFobjCyc += 1;
        if (c == 58) tFobjCyc += 16;
        if (c == 81) tFobjCyc += 30;
        return n + c;
      }
      case 1:
      case 2: {
        if (static_cast<unsigned>(f.ad - f.ad_head) >= f.length) {
          n += 32;
          tFobjCyc += 1;
          state = 6;
          break;
        }
        f.op_intrp = f.op;
        if (f.nb_pack == 0) {
          uint8_t d = *f.ad++;
          f.op = d & 0xF;
          unsigned pack = ((d >> 4) & 7) + 1, shift = 3;
          n += 15;
          if ((d & 0x80) != 0) {
            n -= 1;
            do {
              d = *f.ad++;
              pack += static_cast<unsigned>(d & 0x7F) << shift;
              shift += 7;
              n += 10;
            } while ((d & 0x80) != 0 && shift < 32);
          }
          f.nb_pack = static_cast<uint16_t>(pack);
        }
        f.nb_pack -= 1;
        if (f.op < 1 || f.op > 6) {
          tFobjCyc += 2;
          return n + (f.op == 0 ? 49 : 46) + 14;
        }
        unsigned bytes = 0;
        n += kLoad[f.op];
        tFobjCyc += 1;
        if (f.op == 5) {
          n += frac_extra(f.frac_slope, &bytes);
          f.ad += bytes;
          break;
        }
        n += frac_extra(f.frac_value, &bytes);
        f.ad += bytes;
        if (f.op == 4) {
          n += frac_extra(f.frac_slope, &bytes);
          f.ad += bytes;
        }
        if ((f.op == 1 || f.op == 2) && f.op_intrp == 5) {
          n -= 4;
        }
        if (f.op == 6) {
          if ((f.flags & 0x40) != 0) {
            f.op_intrp = f.op;
            f.flags = static_cast<uint8_t>(f.flags | 0x80);
            n += 10;
          }
          f.flags |= 0x40;
        }
        state = state == 1 ? 3 : 4;
        break;
      }
      case 3: {
        if ((f.flags & 0x80) != 0) {
          n += update_in_wait(f);
        }
        if (static_cast<unsigned>(f.ad - f.ad_head) >= f.length) {
          n += 25;
          state = 6;
          break;
        }
        unsigned wait = 0, shift = 0;
        uint8_t d;
        n += 47 - 10;
        do {
          d = *f.ad++;
          wait |= static_cast<unsigned>(d & 0x7F) << shift;
          shift += 7;
          n += 10;
        } while ((d & 0x80) != 0 && shift < 32);
        f.fterm = static_cast<uint16_t>(wait);
        f.flags |= 0x20;
        state = 2;
        break;
      }
      case 4: {
        if (static_cast<float>(f.fterm) <= f.time) {
          f.time -= static_cast<float>(f.fterm);
          n += 31;
          state = 3;
          break;
        }
        switch (f.op_intrp) {
          case 3: case 4: case 5: tFobjCyc += 31; return n + 85;
          case 2: tFobjCyc += (f.flags & 0x20) != 0 ? 17 : 1; return n + ((f.flags & 0x20) != 0 ? 64 : 49);
          case 1: tFobjCyc += 1; return n + 53;
          case 6: tFobjCyc += (f.flags & 0x80) != 0 ? 1 : 0; return n + ((f.flags & 0x80) != 0 ? 51 : 40);
          default: return n + 38;
        }
      }
      case 5:
        n += 15;
        state = 4;
        break;
      default:
        return n;
    }
  }
  return n;
}

unsigned trig_cost(float x) {
  const double a = std::fabs(static_cast<double>(x));
  const unsigned neg = x < 0 ? 1u : 0u;
  if (a < 7.450580596923828e-9) {
    return 33 + 33;
  }
  if (a <= 0.7853981633974483) {
    return 45 + (a < 0.3 ? 51 : 61);
  }
  if (a < 2.356194490192345) {
    return (neg ? 110 : 107) + (neg ? 100 : 99);
  }
  return 127 + 128 + 16 * neg;
}

int64_t run(const Entry& e, const uintptr_t* regs, unsigned* counter) {
  const uintptr_t a0 = regs[1], a1 = regs[2], a2 = regs[3];
  if (e.handler >= hForeach && e.handler < hPeConvert) {
    return run_anim(e, regs, counter);
  }
  switch (e.handler) {
    case hWalkRoot: {
      *counter = kWalk;
      sWalkRoot = reinterpret_cast<HSD_JObj*>(a0);
      if (sWalkRoot == nullptr) {
        return 17 + 1;
      }
      return 31u - ((sWalkRoot->flags & JOBJ_INSTANCE) != 0 ? 4u : 0u) - (a1 == 0 ? 5u : 0u) + 1u + (a1 != 0 ? 1u : 0u);
    }
    case hWalkVisit: {
      *counter = kWalk;
      const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
      if (j == nullptr) {
        return 0;
      }
      unsigned depth = 0;
      for (const HSD_JObj* p = j; p != nullptr && p != sWalkRoot && depth < 64; p = p->parent) {
        depth++;
      }
      return 29u + ((depth - 1) % 6 == 0 ? 15u + 21u : 0u) - ((j->flags & JOBJ_INSTANCE) != 0 ? 4u : 0u) -
             (a1 == 0 ? 5u : 0u) + (a1 != 0 ? 1u : 0u);
    }
    case hPartFind:
      *counter = kWalk;
      return sPartFound != nullptr && *sPartFound == 1 ? 3 : 11;
    case hFObj: {
      *counter = kAnim;
      float rate;
      std::memcpy(&rate, reinterpret_cast<const char*>(regs) + 112, 4);
      return fobj_cost(reinterpret_cast<const HSD_FObj*>(a0), rate);
    }
    case hFObjAll: {
      *counter = kAnim;
      uint64_t n = 22;
      unsigned guard = 0;
      for (auto* f = reinterpret_cast<const HSD_FObj*>(a0); f != nullptr && guard < 4096; f = f->next, guard++) {
        n += 8;
      }
      return n;
    }
    case hAObj: {
      *counter = kAnim;
      const auto* a = reinterpret_cast<const HSD_AObj*>(a0);
      if (a == nullptr) {
        return 17;
      }
      if ((a->flags & 0x40000000u) != 0) {
        return 21;
      }
      return ((a->flags & 0x20000000u) != 0 ? 52u : 47u) + ((a->flags & 0x10000000u) != 0 ? 1u : 0u);
    }
    case hJObjUpdate: {
      *counter = kAnim;
      const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
      const auto type = static_cast<uint32_t>(a1);
      if (j == nullptr) {
        return 27;
      }
      const unsigned c = (j->flags & JOBJ_MTX_INDEP_SRT) != 0 ? 0 : mtx_dirty(j) ? 1 : (j->flags & JOBJ_USER_DEF_MTX) != 0 ? 2 : 3;
      static const unsigned kRotYZ[4] = {37, 49, 49, 51}, kRotX[4] = {40, 52, 52, 54}, kTraSca[4] = {34, 46, 46, 48};
      if (type == 2 || type == 3) {
        return kRotYZ[c];
      }
      if (type == 1) {
        return kRotX[c] + ((j->flags & JOBJ_JOINT1) != 0 && (j->flags & JOBJ_JOINT2) == 0 ? 6u : 0u);
      }
      if (type >= 5 && type <= 10) {
        return kTraSca[c];
      }
      if (type == 11) {
        return (j->flags & JOBJ_CLASSICAL_SCALE) == 0 ? 41u : c == 1 ? 53u : c == 2 ? 52u : 54u;
      }
      return type == 0 ? 27u : type >= 58 ? 21u : 34u;
    }
    case hDirtySub: {
      *counter = kMtx;
      const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
      if (j == nullptr) {
        return 0;
      }
      if ((j->flags & JOBJ_INSTANCE) != 0) {
        return 15;
      }
      uint64_t n = 19;
      unsigned guard = 0;
      for (const HSD_JObj* c = j->child; c != nullptr && guard < 4096; c = c->next, guard++) {
        if ((c->flags & JOBJ_MTX_INDEP_PARENT) != 0) {
          n += 6;
        } else if (mtx_dirty(c)) {
          n += 17;
        } else {
          n += (c->flags & JOBJ_USER_DEF_MTX) != 0 ? 5 : 7;
        }
      }
      return n;
    }
    case hSetupMtx: {
      *counter = kMtx;
      const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
      if (j == nullptr) {
        return 0;
      }
      if ((j->flags & JOBJ_USER_DEF_MTX) != 0) {
        return 28;
      }
      const uint32_t ik = j->flags & JOBJ_JOINT;
      if (ik == JOBJ_JOINT1) {
        return 42;
      }
      if (ik == JOBJ_JOINT2) {
        return 38;
      }
      if (ik == JOBJ_EFFECTOR) {
        return 46;
      }
      return j->robj != nullptr ? 61 : 43;
    }
    case hMakeMtx: {
      *counter = kMtx;
      const auto* j = reinterpret_cast<const HSD_JObj*>(a0);
      if (j == nullptr) {
        return 0;
      }
      const bool classical = (j->flags & JOBJ_CLASSICAL_SCALE) != 0;
      const HSD_JObj* p = j->parent;
      uint64_t n;
      if (p == nullptr) {
        n = classical ? 45 : 52;
      } else {
        n = p->scl == nullptr ? (classical ? 64 : 71) : (classical ? 76 : 86);
        n += 51;
        PortSdkCost_SkipConcat();
        n += p->scl != nullptr ? 45 : 0;
        if ((p->flags & JOBJ_MTX_DIRTY) != 0) {
          n += (p->flags & JOBJ_USER_DEF_MTX) != 0 ? -2 : 3;
        }
      }
      if ((j->flags & JOBJ_USE_QUATERNION) != 0) {
        n += 1;
      }
      if (j->aobj != nullptr) {
        n += 3;
      }
      return n;
    }
    case hMtxSRT: {
      *counter = kMtx;
      const auto* rot = reinterpret_cast<const float*>(a2);
      uint64_t n = 126;
      if (rot != nullptr) {
        n += trig_cost(rot[0]) + trig_cost(rot[1]) + trig_cost(rot[2]);
      }
      return n;
    }
    case hJObjLoad: {
      *counter = kLoad;
      const auto* parent = reinterpret_cast<const HSD_JObj*>(a2);
      uint64_t n = 70 + 36;
      if (parent != nullptr) {
        unsigned brothers = 0;
        const auto* self = reinterpret_cast<const HSD_JObj*>(a0);
        for (const HSD_JObj* c = parent->child; c != nullptr && c != self && brothers < 4096; c = c->next) {
          brothers++;
        }
        n += brothers == 0 ? 52 : 59 + 6 * (brothers - 1);
      }
      return n;
    }
    case hPeConvert: {
      *counter = kExact;
      tPeFile = nullptr;
      tPeShape = 0;
      const uintptr_t a3 = regs[4];
      if (a3 == 0) {
        return kTableRow;
      }
      const uint32_t crc = crc32_bytes(reinterpret_cast<const uint8_t*>(a3), 64);
      if (auto it = sPe.find(crc); it != sPe.end()) {
        tPeFile = &it->second;
        return it->second.convert;
      }
      std::fprintf(stderr, "[func cost] particle file without a row: CRC32 of its first 64 bytes %08X\n", crc);
      PortCpuCost_Count(kUnknown, 1000000);
      return 4565;
    }
    case hShapeLoad: {
      *counter = kExact;
      if (tPeFile != nullptr && tPeShape < tPeFile->shapes.size()) {
        return tPeFile->shapes[tPeShape++];
      }
      return 870;
    }
    default:
      break;
  }
  return 0;
}

}

extern "C" {

int PortFuncCost_Init(void) {
  const char* path = std::getenv("XD_FUNC_COST");
  if (path == nullptr || path[0] == '\0' || path[0] == '0') {
    return 0;
  }
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) {
    std::fprintf(stderr, "[func cost] can't read %s: off\n", path);
    return 0;
  }
  if (const char* e = std::getenv("XD_FUNC_COST_LOG"); e != nullptr && e[0] == '1') {
    sLogUnknown = true;
  }
  char line[1024];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (line[0] == '#') {
      continue;
    }
    if (std::strncmp(line, "@pe\t", 4) == 0) {
      unsigned crc = 0, convert = 0;
      char shapes[512] = "";
      if (std::sscanf(line + 4, "%x %u %511s", &crc, &convert, shapes) >= 2) {
        PeCost c;
        c.convert = convert;
        for (const char* q = shapes; *q >= '0' && *q <= '9';) {
          c.shapes.push_back(static_cast<uint32_t>(std::strtoul(q, const_cast<char**>(&q), 10)));
          if (*q == ',') {
            q++;
          }
        }
        sPe[crc] = c;
      }
      continue;
    }
    if (std::strncmp(line, "@tev\t", 5) == 0) {
      char* p = line + 5;
      char* end = p;
      for (int tabs = 0; *end != '\0' && tabs < 3; end++) {
        tabs += *end == '\t' ? 1 : 0;
      }
      if (end > p + 1) {
        TevCost c;
        c.known = std::sscanf(end, "%u %u", &c.first, &c.again) == 2;
        sTev[std::string(p, static_cast<size_t>(end - p - 1))] = c;
      }
      continue;
    }
    char* t1 = std::strchr(line, '\t');
    if (t1 == nullptr || t1[1] == '\0') {
      continue;
    }
    *t1 = '\0';
    Entry e;
    e.kind = t1[1];
    unsigned a = 0, b = 0, x = 0;
    std::sscanf(t1 + 2, "%u %u %u", &a, &b, &x);
    e.a = a;
    e.b = b;
    e.x = x;
    sByName[line] = static_cast<int>(sEntries.size());
    sEntries.push_back(e);
  }
  std::fclose(f);
  for (const auto& h : kHandlers) {
    Entry e;
    if (auto it = sByName.find(h.name); it != sByName.end()) {
      e = sEntries[static_cast<size_t>(it->second)];
    }
    e.handler = h.handler;
    sByName[h.name] = static_cast<int>(sEntries.size());
    sEntries.push_back(e);
  }
  if (const char* e = std::getenv("XD_FUNC_COST_TEV_LOG"); e != nullptr && e[0] != '\0') {
    sTevLog = std::fopen(e, "w");
  }
  std::fprintf(stderr, "[func cost] XD_FUNC_COST: %zu functions with a cost, %zu material configurations (%s)\n",
               sEntries.size(), sTev.size(), path);
  return 1;
}

void PortFuncCost_Statics(const uint8_t* partFound) { sPartFound = partFound; }

void PortFuncCost_TevGlobals(const void* const* toon, const void* const* shadows) {
  if (toon != nullptr) {
    sTevToon = toon;
  }
  if (shadows != nullptr) {
    sTevShadows = shadows;
  }
}

int PortFuncCost_Resolve(const char* name) {
  auto it = sByName.find(name);
  if (it != sByName.end()) {
    return it->second;
  }
  const char* colon = std::strrchr(name, ':');
  if (colon != nullptr && std::strchr(name, '<') == nullptr && std::strstr(name, "::") == colon - 1) {
    return -1;
  }
  if (colon != nullptr) {
    it = sByName.find(colon + 1);
    if (it != sByName.end()) {
      return it->second;
    }
  }
  if (sLogUnknown) {
    std::fprintf(stderr, "[func cost] no row: %s\n", name);
  }
  return -2;
}

int PortFuncCost_Row(const char* name, char* kind, uint32_t* a, uint32_t* b) {
  auto it = sByName.find(name);
  if (it == sByName.end()) {
    return 0;
  }
  const Entry& e = sEntries[static_cast<size_t>(it->second)];
  *kind = e.kind;
  *a = e.a;
  *b = e.b;
  return 1;
}

void PortFuncCost_Call(int entry, const uintptr_t* regs) {
  if (entry < 0) {
    if (entry == -2) {
      PortCpuCost_Count(kUnknown, 1);
    }
    return;
  }
  const Entry& e = sEntries[static_cast<size_t>(entry)];
  if (e.handler != hTable) {
    unsigned counter = kExact;
    const int64_t n = run(e, regs, &counter);
    if (n >= 0) {
      PortCpuCost_Count(counter, static_cast<uint64_t>(n));
      if (e.handler == hFObj) {
        PortCpuCost_Count(kAnim, tFobjCyc);
      } else if (e.handler != hWalkRoot && e.handler != hWalkVisit && e.x != 0) {
        PortCpuCost_Count(kCycEst, e.x);
      }
      return;
    }
  }
  if (e.x != 0 && e.handler == hTable) {
    PortCpuCost_Count(kCycEst, e.x);
  }
  switch (e.kind) {
    case 'E':
      PortCpuCost_Count(kExact, e.a);
      break;
    case 'M':
      PortCpuCost_Count(kMin, e.a);
      PortCpuCost_Count(kSpan, e.b - e.a);
      break;
    case 'L':
      PortCpuCost_Count(kLoopOut, e.a);
      PortCpuCost_Count(kLoopBody, e.b);
      break;
    default:
      break;
  }
}

}
