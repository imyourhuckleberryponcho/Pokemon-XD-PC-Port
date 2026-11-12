#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int64_t PortOS_BusyFor(int64_t ticks);
void PortCpuCost_Accounted(int64_t ticks);
void PortCpuCost_Count(unsigned counter, uint64_t amount);
int64_t PortTime_Now(void);
extern int32_t pokecoloFrameCounter;

void GSgfxParseDisplayList(void* desc, void* list, uint32_t size, void* callbacks, void* user);
struct GSbound;
void GSboundAddPoint(GSbound* bound, float x, float y, float z);
}

namespace {

constexpr unsigned kBoundInstr = 11;
constexpr unsigned kBoundVerts = 12;

struct Switches {
  bool on = false;
  bool log = false;
};
const Switches& switches() {
  static const Switches s = [] {
    Switches v;
    if (const char* e = std::getenv("XD_BOUND_WALK_COST"); e != nullptr && e[0] != '\0') {
      v.on = std::strcmp(e, "exact") == 0;
      std::fprintf(stderr, "[bound walk cost] XD_BOUND_WALK_COST=%s: measuring a model %s\n", e,
                   v.on ? "takes the GameCube's time" : "takes no time (unknown value)");
    }
    if (const char* e = std::getenv("XD_BOUND_WALK_LOG"); e != nullptr && e[0] != '\0' && e[0] != '0') {
      v.log = true;
    }
    return v;
  }();
  return s;
}

int32_t be32(const uint8_t* p) {
  return static_cast<int32_t>((uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]);
}

struct Attr {
  int32_t attr, how, count, type;
};
constexpr int kMaxAttrs = 32;

int attr_cost(const Attr& a, bool enveloped, int* bytes, bool* odd) {
  if (a.attr >= 0 && a.attr <= 8) {
    *bytes = 1;
    const int pv = a.attr == 8 ? 39 : 40;
    const int cb = a.attr == 0 ? 24 : 17;
    return 10 + pv + cb + 4 + (a.attr == 0 ? 4 : 0);
  }
  const bool colour = a.attr == 11 || a.attr == 12;
  int s = 0;
  int unit = 0;
  int mult = 1;
  bool multSet = true;
  if (colour) {
    static const int c[6] = {13, 10, 11, 7, 12, 10};
    static const int size[6] = {2, 3, 4, 2, 3, 4};
    const int t = a.type >= 0 && a.type <= 5 ? a.type : (*odd = true, 5);
    s = (a.attr == 11 ? 6 : 7) + c[t];
    unit = size[t];
  } else {
    static const int tt[5] = {8, 9, 5, 10, 7};
    static const int size[5] = {1, 1, 2, 2, 4};
    const int t = a.type >= 0 && a.type <= 4 ? a.type : (*odd = true, 4);
    int k = 0;
    switch (a.attr) {
    case 9:
      k = 14;
      mult = a.count == 0 ? 2 : 3;
      break;
    case 10:
      k = a.count == 0 ? 11 : 9;
      mult = 3;
      multSet = a.count == 0;
      break;
    case 25:
      k = a.count == 1 ? 11 : 9;
      mult = 3;
      multSet = a.count == 1;
      break;
    case 20:
      k = a.count == 0 ? 24 : 23;
      mult = a.count == 0 ? 1 : 2;
      break;
    default:
      k = a.count == 0 ? 25 : 24;
      mult = a.count == 0 ? 1 : 2;
      if (a.attr < 13 || a.attr > 19) *odd = true;
      break;
    }
    s = 4 + tt[t] + k;
    unit = size[t];
  }
  int how = 0;
  if (a.how == 2) {
    how = 10;
    *bytes = 1;
  } else if (a.how == 3) {
    how = 9;
    *bytes = 2;
  } else {
    static const int e[5] = {18, 14, 10, 18, 13};
    static const int ec[6] = {6, 18, 13, 10, 18, 13};
    int f = 7;
    if (!colour) {
      switch (a.attr) {
      case 9: f = a.count == 0 ? 5 : 9; break;
      case 10: f = 9; break;
      case 25: f = 9; break;
      default: f = a.count == 0 ? 7 : 5; break;
      }
    }
    const int t = colour ? (a.type >= 0 && a.type <= 5 ? a.type : 5) : (a.type >= 0 && a.type <= 4 ? a.type : 4);
    how = (colour ? ec[t] : e[t]) + f + 2;
    *bytes = unit * mult;
    if (!multSet) *odd = true;
  }
  const int pv = 27 + s + how;
  const int cb = a.attr == 9 ? (enveloped ? 63 : 59) : 17;
  int cyc = 3;
  if (a.attr >= 13 && a.attr <= 20) cyc += 1;
  if (a.how == 2 || a.how == 3) {
    cyc += 4;
  } else if ((a.attr == 9 && a.count != 0) || (a.attr == 10 && a.count == 0) || (a.attr == 25 && a.count == 1)) {
    cyc += 2;
  }
  if (a.attr == 9 && enveloped) cyc += 2;
  return 10 + pv + cb + cyc;
}

bool sWalking = false;
int64_t sPointInstr = 0;
uint64_t sPoints = 0;
bool sFirstCallback = true;

struct Tally {
  int64_t instr = 0;
  int64_t commands = 0;
  int64_t vertices = 0;
  int64_t padding = 0;
  int attrs = 0;
  bool odd = false;
};

Tally tally(const uint8_t* desc, const uint8_t* list, uint32_t size) {
  Tally t;
  t.instr = 37 + 21;
  Attr attrs[kMaxAttrs];
  int n = 0;
  bool enveloped = false;
  for (const uint8_t* d = desc; n < kMaxAttrs; d += 0x18) {
    const int32_t attr = be32(d);
    if (attr == 0xff) break;
    attrs[n++] = Attr{attr, be32(d + 4), be32(d + 8), be32(d + 12)};
    if (attr == 0) enveloped = true;
  }
  t.attrs = n;
  int perVertex = 14;
  int vertexBytes = 0;
  int perSurface = 37 + 22;
  for (int i = 0; i < n; ++i) {
    int bytes = 0;
    perVertex += attr_cost(attrs[i], enveloped, &bytes, &t.odd);
    vertexBytes += bytes;
    perSurface += attrs[i].attr == 20 ? 13 : (attrs[i].attr >= 2 && attrs[i].attr <= 8 ? 12 : 14);
    perSurface += 1;
  }
  const uint8_t* p = list;
  const uint8_t* end = list + size;
  while (p < end) {
    const int cmd = *p++ & 0xF8;
    int c = 0;
    switch (cmd) {
    case 0x00:
      t.instr += 16;
      ++t.padding;
      continue;
    case 0x80: c = 29; break;
    case 0x90: c = 30; break;
    case 0x98: c = 27; break;
    case 0xA0: c = 30; break;
    case 0xA8: c = 33; break;
    case 0xB0: c = 27; break;
    case 0xB8: c = 33; break;
    default: return t;
    }
    const int count = (p[0] << 8) | p[1];
    p += 2;
    t.instr += c + perSurface + static_cast<int64_t>(count) * perVertex;
    t.vertices += count;
    ++t.commands;
    p += static_cast<int64_t>(count) * vertexBytes;
  }
  return t;
}

}

extern "C" void PortXD_BoundAddPoint(GSbound* bound, float x, float y, float z) {
  if (!sWalking) {
    GSboundAddPoint(bound, x, y, z);
    return;
  }
  struct View {
    uint16_t flags, type;
    void* data;
    float box[6];
  };
  const View* v = reinterpret_cast<const View*>(bound);
  const bool first = (v->flags & 1) != 0;
  float before[6];
  std::memcpy(before, v->box, sizeof(before));
  GSboundAddPoint(bound, x, y, z);
  ++sPoints;
  if (first) {
    sPointInstr += 15;
    return;
  }
  int g = 0;
  for (int i = 0; i < 6; ++i) {
    if (std::memcmp(&before[i], &v->box[i], sizeof(float)) != 0) ++g;
  }
  sPointInstr += 23 + g;
  if (std::memcmp(&before[5], &v->box[5], sizeof(float)) != 0) ++sPointInstr;
}

extern "C" void PortXD_BoundParseDisplayList(void* desc, void* list, uint32_t size, void* callbacks, void* user) {
  const Switches& s = switches();
  const Tally t = tally(static_cast<const uint8_t*>(desc), static_cast<const uint8_t*>(list), size);
  sWalking = true;
  sPointInstr = 0;
  sPoints = 0;
  GSgfxParseDisplayList(desc, list, size, callbacks, user);
  sWalking = false;
  int64_t instr = t.instr + sPointInstr;
  if (sFirstCallback && t.vertices > 0) {
    instr += 4;
    sFirstCallback = false;
  }
  PortCpuCost_Count(kBoundInstr, static_cast<uint64_t>(instr));
  PortCpuCost_Count(kBoundVerts, static_cast<uint64_t>(t.vertices));
  int64_t spent = 0;
  if (s.on) {
    static int64_t carry = 0;
    carry += instr;
    const int64_t ticks = carry / 12;
    carry -= ticks * 12;
    if (ticks > 0) {
      spent = PortOS_BusyFor(ticks);
      PortCpuCost_Accounted(spent);
    }
  }
  if (s.log) {
    std::printf("[bound walk] pass %d t %lld commands %lld vertices %lld points %llu attrs %d padding %lld "
                "instructions %lld spent %lld%s\n",
                pokecoloFrameCounter, static_cast<long long>(PortTime_Now()), static_cast<long long>(t.commands),
                static_cast<long long>(t.vertices), static_cast<unsigned long long>(sPoints), t.attrs,
                static_cast<long long>(t.padding), static_cast<long long>(instr), static_cast<long long>(spent),
                t.odd ? " ODD-FORMAT" : "");
  }
}
