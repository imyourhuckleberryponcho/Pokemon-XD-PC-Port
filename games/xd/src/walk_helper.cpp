#include <dolphin/types.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
const void* PortXD_FindVariable(const char* path);
void PortInput_SetScriptStick(bool (*fn)(uint32_t field, int8_t* x, int8_t* y));
uint32_t PortVI_GetFlipCount(void);
s32 GSfloorGetCurrent(void);
}

namespace {

struct Point {
  float x, z, radius;
};
struct Walk {
  uint32_t frame;
  std::vector<Point> points;
};

std::vector<Walk> sWalks;
size_t sWalk = 0, sPoint = 0;
bool sWalking = false;
uint32_t sPosEvery = 0, sPosLast = 0;

const float* sLog = nullptr;
const s32* sHead = nullptr;
uint32_t sLookedAt = 0;

bool Position(float& x, float& y, float& z) {
  if (sLog == nullptr || sHead == nullptr) {
    const uint32_t now = PortVI_GetFlipCount();
    if (sLookedAt != 0 && now < sLookedAt + 60) return false;
    sLookedAt = now == 0 ? 1 : now;
    sLog = static_cast<const float*>(PortXD_FindVariable("HeroMove.leaderLog"));
    sHead = static_cast<const s32*>(PortXD_FindVariable("HeroMove.logHead"));
    if (sLog == nullptr || sHead == nullptr) {
      std::fprintf(stderr, "[walk] the hero's position can't be found (no xd.pdb next to xd.exe?)\n");
      return false;
    }
  }
  const s32 head = *sHead;
  if (head < 0 || head >= 60) return false;
  x = sLog[head * 3], y = sLog[head * 3 + 1], z = sLog[head * 3 + 2];
  return true;
}

struct Steering {
  int phase = 0;
  uint32_t since = 0;
  float fromX = 0, fromZ = 0;
  float upX = 0, upZ = -1, rightX = 1, rightZ = 0;
  float aimX = 0, aimZ = 0;
  float lastX = 0, lastZ = 0;
  uint32_t lastMoved = 0, checked = 0;
  float best = 1e9f;
  uint32_t bestAt = 0;
  float checkX = 0, checkZ = 0;
} st;

constexpr uint32_t kTryFields = 16, kCheckFields = 12, kStuckFields = 120;
constexpr int kPush = 100;

bool Normalise(float& x, float& z) {
  const float len = std::sqrt(x * x + z * z);
  if (len < 0.05f) return false;
  x /= len, z /= len;
  return true;
}

void Stop(const char* why, float x, float z) {
  std::fprintf(stderr, "[walk] game frame %u floor 0x%X: %s at %.2f %.2f\n", PortVI_GetFlipCount(), GSfloorGetCurrent(), why, x, z);
  sWalking = false;
  st = Steering{};
}

bool Stick(uint32_t field, int8_t* outX, int8_t* outY) {
  const uint32_t frame = PortVI_GetFlipCount();
  float x = 0, y = 0, z = 0;
  const bool have = Position(x, y, z);
  if (sPosEvery != 0 && have && frame >= sPosLast + sPosEvery) {
    sPosLast = frame;
    std::fprintf(stderr, "[pos] frame %u floor 0x%X %.2f %.2f %.2f\n", frame, GSfloorGetCurrent(), x, y, z);
  }
  if (!sWalking) {
    if (sWalk >= sWalks.size() || frame < sWalks[sWalk].frame || !have) return false;
    sWalking = true;
    sPoint = 0;
    st = Steering{};
    std::fprintf(stderr, "[walk] game frame %u floor 0x%X: walking from %.2f %.2f, %zu point(s)\n", frame, GSfloorGetCurrent(), x,
                 z, sWalks[sWalk].points.size());
  }
  if (!have) return false;
  const Walk& w = sWalks[sWalk];
  while (sPoint < w.points.size()) {
    const Point& p = w.points[sPoint];
    if (std::hypot(p.x - x, p.z - z) > p.radius) break;
    std::fprintf(stderr, "[walk] game frame %u: reached point %zu (%.2f %.2f)\n", frame, sPoint + 1, x, z);
    sPoint++;
    st.best = 1e9f, st.bestAt = field;
  }
  if (sPoint >= w.points.size()) {
    sWalk++;
    Stop("walk done", x, z);
    return false;
  }
  const Point& p = w.points[sPoint];
  if (st.phase == 0) {
    st.phase = 1, st.since = field, st.fromX = x, st.fromZ = z, st.lastX = x, st.lastZ = z, st.lastMoved = field;
  }
  if (std::hypot(x - st.lastX, z - st.lastZ) > 0.02f) st.lastX = x, st.lastZ = z, st.lastMoved = field;
  if (st.phase == 1 || st.phase == 2) {
    if (field >= st.since + kTryFields) {
      float dx = x - st.fromX, dz = z - st.fromZ;
      const bool moved = Normalise(dx, dz);
      if (st.phase == 1) {
        if (moved) st.upX = dx, st.upZ = dz;
        st.phase = 2;
      } else {
        if (moved) st.rightX = dx, st.rightZ = dz;
        else st.rightX = -st.upZ, st.rightZ = st.upX;
        st.phase = 3, st.checked = field, st.checkX = x, st.checkZ = z, st.best = 1e9f, st.bestAt = field;
        std::fprintf(stderr, "[walk] stick up moves the hero %.2f %.2f, stick right %.2f %.2f\n", st.upX, st.upZ, st.rightX, st.rightZ);
      }
      st.since = field, st.fromX = x, st.fromZ = z;
    }
    if (st.phase == 1) *outX = 0, *outY = kPush;
    else if (st.phase == 2) *outX = kPush, *outY = 0;
    if (st.phase != 3) return true;
  }
  const float dist = std::hypot(p.x - x, p.z - z);
  if (dist < st.best - 0.5f) st.best = dist, st.bestAt = field;
  if (field >= st.lastMoved + kStuckFields || field >= st.bestAt + 2 * kStuckFields) {
    const bool still = field >= st.lastMoved + kStuckFields;
    sWalk++;
    Stop(still ? "STUCK (no movement for 2 s), walk given up"
               : "STUCK (no nearer to the point for 4 s: something is in the way), walk given up", x, z);
    return false;
  }
  if (field >= st.checked + kCheckFields) {
    float mx = x - st.checkX, mz = z - st.checkZ;
    if (Normalise(mx, mz) && (st.aimX != 0 || st.aimZ != 0)) {
      const float turn = std::atan2(st.aimX * mz - st.aimZ * mx, st.aimX * mx + st.aimZ * mz) * 0.7f;
      const float c = std::cos(turn), s = std::sin(turn);
      const float ux = st.upX * c - st.upZ * s, uz = st.upX * s + st.upZ * c;
      const float rx = st.rightX * c - st.rightZ * s, rz = st.rightX * s + st.rightZ * c;
      st.upX = ux, st.upZ = uz, st.rightX = rx, st.rightZ = rz;
    }
    st.checked = field, st.checkX = x, st.checkZ = z;
  }
  float dx = p.x - x, dz = p.z - z;
  if (!Normalise(dx, dz)) return false;
  const float det = st.rightX * st.upZ - st.rightZ * st.upX;
  float sx, sy;
  if (std::fabs(det) < 0.2f) {
    sx = dx * -st.upZ + dz * st.upX, sy = dx * st.upX + dz * st.upZ;
  } else {
    sx = (dx * st.upZ - dz * st.upX) / det, sy = (st.rightX * dz - st.rightZ * dx) / det;
  }
  const float len = std::max(std::fabs(sx), std::fabs(sy));
  if (len < 0.01f) return false;
  *outX = static_cast<int8_t>(std::lround(sx / len * kPush)), *outY = static_cast<int8_t>(std::lround(sy / len * kPush));
  st.aimX = dx, st.aimZ = dz;
  return true;
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t end = s.find(sep, start);
    out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) return out;
    start = end + 1;
  }
}

}

extern "C" void PortXD_WalkHelperInit(void) {
  if (const char* v = std::getenv("XD_POS_LOG"); v != nullptr && v[0] != '\0') sPosEvery = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
  if (const char* v = std::getenv("XD_WALK"); v != nullptr && v[0] != '\0') {
    for (const std::string& part : Split(v, ',')) {
      const size_t colon = part.find(':');
      if (colon == std::string::npos) continue;
      Walk w;
      w.frame = static_cast<uint32_t>(std::strtoul(part.c_str(), nullptr, 10));
      for (const std::string& pt : Split(part.substr(colon + 1), ';')) {
        const std::vector<std::string> opts = Split(pt, ':');
        const size_t slash = opts[0].find('/');
        if (slash == std::string::npos) {
          std::fprintf(stderr, "[walk] skipped '%s' (a point is x/z)\n", pt.c_str());
          continue;
        }
        Point p{std::strtof(opts[0].c_str(), nullptr), std::strtof(opts[0].c_str() + slash + 1, nullptr), 2.f};
        for (size_t i = 1; i < opts.size(); i++) {
          if (opts[i].rfind("r=", 0) == 0) p.radius = std::strtof(opts[i].c_str() + 2, nullptr);
        }
        w.points.push_back(p);
      }
      if (!w.points.empty()) sWalks.push_back(w);
    }
    std::fprintf(stderr, "[walk] XD_WALK: %zu walk(s)\n", sWalks.size());
  }
  if (sPosEvery != 0 || !sWalks.empty()) PortInput_SetScriptStick(Stick);
}
