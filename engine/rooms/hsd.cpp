#include "room_hsd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace engine::rooms {

namespace {

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void PutBe32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }
uint32_t Align(uint32_t n, uint32_t a = 32) { return (n + a - 1) / a * a; }

constexpr uint32_t kSharedChunk = 4096;
constexpr uint32_t kJObjSize = 0x40, kDObjSize = 0x10, kMObjSize = 0x18, kPObjSize = 0x18, kTObjSize = 0x54, kImageSize = 0x10;
constexpr uint32_t kJObjNoMeshFlags = 0x4020;
constexpr uint32_t kJObjInstance = 0x1000;

uint32_t ImageBytes(uint32_t format, uint32_t w, uint32_t h) {
  uint32_t bw = 4, bh = 4, unit = 32;
  switch (format) {
    case 0: case 8: case 14: bw = 8, bh = 8; break;
    case 1: case 2: case 9: bw = 8, bh = 4; break;
    case 6: unit = 64; break;
    default: break;
  }
  return (w + bw - 1) / bw * ((h + bh - 1) / bh) * unit;
}

}

uint32_t HsdArchive::U32(uint32_t off) const { return off + 4 <= data.size() ? Be32(data.data() + off) : 0; }
float HsdArchive::F32(uint32_t off) const {
  const uint32_t u = U32(off);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void HsdArchive::SetU32(uint32_t off, uint32_t v) {
  if (off + 4 <= data.size()) PutBe32(data.data() + off, v);
}
void HsdArchive::SetF32(uint32_t off, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  SetU32(off, u);
}
bool HsdArchive::IsSlot(uint32_t off) const { return std::binary_search(slots.begin(), slots.end(), off); }
void HsdArchive::AddSlot(uint32_t off) {
  auto it = std::lower_bound(slots.begin(), slots.end(), off);
  if (it == slots.end() || *it != off) slots.insert(it, off);
}
void HsdArchive::RemoveSlot(uint32_t off) {
  auto it = std::lower_bound(slots.begin(), slots.end(), off);
  if (it != slots.end() && *it == off) slots.erase(it);
}
int HsdArchive::FindPublic(const std::string& name) const {
  for (size_t i = 0; i < publics.size(); i++) {
    if (publics[i].first == name) return int(i);
  }
  return -1;
}

bool HsdParse(const uint8_t* b, size_t size, HsdArchive& out, std::vector<std::string>& problems) {
  const size_t before = problems.size();
  const auto bad = [&](const std::string& s) { problems.push_back("map: " + s); };
  if (b == nullptr || size < 0x20) {
    bad("the file is shorter than an archive header");
    return false;
  }
  const uint32_t fsize = Be32(b), dsize = Be32(b + 4), nreloc = Be32(b + 8), npub = Be32(b + 12), next = Be32(b + 16);
  if (fsize > size || fsize < 0x20) {
    bad("the size in its header (" + std::to_string(fsize) + ") does not fit the file (" + std::to_string(size) + " bytes)");
    return false;
  }
  if (dsize == 0 || dsize % 4 != 0 || dsize > fsize) {
    bad("its data block size is not usable");
    return false;
  }
  const uint64_t tables = uint64_t(0x20) + dsize + uint64_t(nreloc) * 4 + uint64_t(npub) * 8 + uint64_t(next) * 8;
  if (nreloc > 4000000 || npub > 100000 || next > 100000 || tables > fsize) {
    bad("its pointer list and name lists do not fit in the file");
    return false;
  }
  const size_t relocOff = 0x20 + dsize, pubOff = relocOff + size_t(nreloc) * 4, extOff = pubOff + size_t(npub) * 8, strOff = extOff + size_t(next) * 8;
  out.data.assign(b + 0x20, b + 0x20 + dsize);
  out.slots.clear();
  uint32_t last = 0;
  for (uint32_t i = 0; i < nreloc; i++) {
    const uint32_t s = Be32(b + relocOff + size_t(i) * 4);
    if (s % 4 != 0 || size_t(s) + 4 > dsize) {
      bad("a pointer place (" + std::to_string(s) + ") is outside the data");
      return false;
    }
    if (i > 0 && s <= last) {
    }
    last = s;
    out.slots.push_back(s);
  }
  std::sort(out.slots.begin(), out.slots.end());
  out.slots.erase(std::unique(out.slots.begin(), out.slots.end()), out.slots.end());
  const auto str = [&](uint32_t off, std::string& s) {
    const size_t at = strOff + off;
    if (at >= fsize) return false;
    const void* z = std::memchr(b + at, 0, fsize - at);
    if (z == nullptr) return false;
    s.assign(reinterpret_cast<const char*>(b + at), static_cast<const char*>(z));
    return true;
  };
  out.publics.clear();
  out.externs.clear();
  for (uint32_t i = 0; i < npub; i++) {
    std::string name;
    const uint32_t off = Be32(b + pubOff + size_t(i) * 8);
    if (off >= dsize || !str(Be32(b + pubOff + size_t(i) * 8 + 4), name)) {
      bad("a public name is damaged");
      return false;
    }
    out.publics.emplace_back(name, off);
  }
  for (uint32_t i = 0; i < next; i++) {
    std::string name;
    const uint32_t off = Be32(b + extOff + size_t(i) * 8);
    if (off >= dsize || !str(Be32(b + extOff + size_t(i) * 8 + 4), name)) {
      bad("an outside reference is damaged");
      return false;
    }
    out.externs.emplace_back(name, off);
  }
  for (uint32_t s : out.slots) {
    const uint32_t v = Be32(out.data.data() + s);
    if (v >= dsize && v != 0) {
      bool ext = false;
      for (const auto& e : out.externs) ext = ext || e.second == s;
      if (!ext) {
        bad("the pointer at " + std::to_string(s) + " leads outside the data");
        return false;
      }
    }
  }
  return problems.size() == before;
}

std::vector<uint8_t> HsdWrite(const HsdArchive& a) {
  std::vector<uint8_t> strtab, pubs, exts;
  const auto addName = [&](const std::string& n) {
    const uint32_t at = uint32_t(strtab.size());
    strtab.insert(strtab.end(), n.begin(), n.end());
    strtab.push_back(0);
    return at;
  };
  const auto put = [](std::vector<uint8_t>& v, uint32_t x) { uint8_t t[4]; PutBe32(t, x); v.insert(v.end(), t, t + 4); };
  for (const auto& p : a.publics) put(pubs, p.second), put(pubs, addName(p.first));
  for (const auto& p : a.externs) put(exts, p.second), put(exts, addName(p.first));
  std::vector<uint8_t> out(0x20, 0);
  out.insert(out.end(), a.data.begin(), a.data.end());
  for (uint32_t s : a.slots) put(out, s);
  out.insert(out.end(), pubs.begin(), pubs.end());
  out.insert(out.end(), exts.begin(), exts.end());
  out.insert(out.end(), strtab.begin(), strtab.end());
  PutBe32(out.data(), uint32_t(out.size())), PutBe32(out.data() + 4, uint32_t(a.data.size())), PutBe32(out.data() + 8, uint32_t(a.slots.size()));
  PutBe32(out.data() + 12, uint32_t(a.publics.size())), PutBe32(out.data() + 16, uint32_t(a.externs.size()));
  return out;
}

namespace {

struct Walk {
  const HsdArchive& a;
  std::vector<std::string>& problems;
  bool check;
  std::set<uint32_t> images;
  uint32_t textureBytes = 0;
  bool failed = false;

  bool Bad(const std::string& s) {
    failed = true;
    if (check && problems.size() < 40) problems.push_back("map: " + s);
    return false;
  }
  bool Listed(uint32_t slotAt, const char* what) {
    if (a.U32(slotAt) == 0 || a.IsSlot(slotAt)) return true;
    return Bad(std::string(what) + " (pointer at " + std::to_string(slotAt) + ") is not in the archive's pointer list");
  }
  bool Ptr(uint32_t off, uint32_t slotAt, size_t bytes, const char* what, bool mayBeNull = true) {
    const uint32_t p = a.U32(slotAt);
    (void)off;
    if (p == 0) return mayBeNull ? true : Bad(std::string(what) + " is missing");
    if (!a.InData(p, bytes)) return Bad(std::string(what) + " at " + std::to_string(slotAt) + " leads outside the data");
    return true;
  }
};

bool WalkDObjs(Walk& w, uint32_t first, ModelInfo& m, JointInfo& j) {
  uint32_t d = first;
  int guard = 0;
  std::set<uint32_t> seen;
  while (d != 0) {
    if (!w.a.InData(d, kDObjSize) || !seen.insert(d).second || ++guard > 2000) return w.Bad("a joint's mesh list is damaged or loops");
    m.dobjs++, j.dobjs++;
    if (!w.Listed(d + 4, "a mesh's next link") || !w.Listed(d + 8, "a mesh's material") || !w.Listed(d + 0xC, "a mesh's piece list")) return false;
    const uint32_t mobj = w.a.U32(d + 8), pobj = w.a.U32(d + 0xC);
    if (mobj != 0) {
      if (!w.a.InData(mobj, kMObjSize)) return w.Bad("a material leads outside the data");
      if (!w.Listed(mobj + 8, "a material's texture list")) return false;
      uint32_t t = w.a.U32(mobj + 8);
      int tg = 0;
      std::set<uint32_t> tseen;
      while (t != 0) {
        if (!w.a.InData(t, kTObjSize) || !tseen.insert(t).second || ++tg > 64) return w.Bad("a material's texture list is damaged or loops");
        if (!w.Listed(t + 4, "a texture's next link") || !w.Listed(t + 0x4C, "a texture's picture")) return false;
        const uint32_t img = w.a.U32(t + 0x4C);
        if (img != 0) {
          if (!w.a.InData(img, kImageSize)) return w.Bad("a texture's picture header is outside the data");
          if (w.images.insert(img).second) {
            if (!w.Listed(img, "a picture's pixels")) return false;
            const uint32_t dataAt = w.a.U32(img), width = (uint32_t(w.a.data[img + 4]) << 8) | w.a.data[img + 5],
                           height = (uint32_t(w.a.data[img + 6]) << 8) | w.a.data[img + 7], format = w.a.U32(img + 8);
            const uint32_t bytes = ImageBytes(format, width, height);
            if (dataAt == 0 || width == 0 || height == 0 || width > 1024 || height > 1024 || !w.a.InData(dataAt, bytes)) {
              return w.Bad("a texture's pixels are outside the data or its size is not usable (" + std::to_string(width) + "x" + std::to_string(height) + ")");
            }
            w.textureBytes += bytes;
          }
          m.textures++, j.textures++;
        }
        t = w.a.U32(t + 4);
      }
    }
    uint32_t p = pobj;
    int pg = 0;
    std::set<uint32_t> pseen;
    while (p != 0) {
      if (!w.a.InData(p, kPObjSize) || !pseen.insert(p).second || ++pg > 2000) return w.Bad("a mesh's piece list is damaged or loops");
      m.pobjs++;
      if (!w.Listed(p + 4, "a mesh piece's next link") || !w.Listed(p + 8, "a mesh piece's vertex description") || !w.Listed(p + 0x10, "a mesh piece's display list")) return false;
      const uint32_t blocks = (uint32_t(w.a.data[p + 0xE]) << 8) | w.a.data[p + 0xF], dl = w.a.U32(p + 0x10), desc = w.a.U32(p + 8);
      if (dl == 0 || !w.a.InData(dl, size_t(blocks) * 32)) return w.Bad("a mesh's display list is outside the data (mesh at " + std::to_string(p) + ", list at " + std::to_string(dl) + ", " + std::to_string(blocks * 32) + " bytes, data " + std::to_string(w.a.data.size()) + " bytes)");
      if (desc != 0) {
        uint32_t at = desc;
        int n = 0;
        while (true) {
          if (!w.a.InData(at, 0x18) || ++n > 40) return w.Bad("a mesh's vertex description is damaged");
          if (w.a.U32(at) == 0xFF) break;
          const uint32_t base = w.a.U32(at + 0x14);
          const uint32_t attrType = w.a.U32(at + 4);
          if ((attrType == 2 || attrType == 3) && !w.a.IsSlot(at + 0x14)) return w.Bad("a mesh's vertex array (pointer at " + std::to_string(at + 0x14) + ") is not in the archive's pointer list");
          if (base != 0 && !w.a.InData(base, 4)) return w.Bad("a mesh's vertex array is outside the data (mesh at " + std::to_string(p) + ", array at " + std::to_string(base) + ", attribute " + std::to_string(w.a.U32(at)) + ")");
          at += 0x18;
        }
      }
      m.dlBytes += blocks * 32, j.dlBytes += blocks * 32;
      p = w.a.U32(p + 4);
    }
    d = w.a.U32(d + 4);
  }
  return true;
}

bool WalkJoints(Walk& w, uint32_t first, int parent, int depth, ModelInfo& m, std::set<uint32_t>& seen, std::set<uint32_t>& onPath) {
  if (depth > 200) return w.Bad("a joint tree is deeper than 200 levels");
  std::vector<uint32_t> chain;
  bool ok = true;
  for (uint32_t off = first; off != 0 && ok;) {
    if (!w.a.InData(off, kJObjSize)) {
      ok = w.Bad("a joint (at " + std::to_string(off) + ") leads outside the data");
      break;
    }
    if (onPath.count(off)) {
      ok = w.Bad("a joint tree loops back on itself (at " + std::to_string(off) + ")");
      break;
    }
    if (seen.count(off)) break;
    seen.insert(off), onPath.insert(off), chain.push_back(off);
    if (m.joints.size() >= 8192) {
      ok = w.Bad("a model has more than 8192 joints");
      break;
    }
    JointInfo j;
    j.number = int(m.joints.size()), j.parent = parent, j.depth = depth, j.offset = off, j.flags = w.a.U32(off + 4);
    for (int i = 0; i < 3; i++) {
      j.rot[i] = w.a.F32(off + 0x14 + 4 * i), j.scale[i] = w.a.F32(off + 0x20 + 4 * i), j.trans[i] = w.a.F32(off + 0x2C + 4 * i);
      if (!std::isfinite(j.rot[i]) || !std::isfinite(j.scale[i]) || !std::isfinite(j.trans[i])) ok = w.Bad("a joint (at " + std::to_string(off) + ") holds a number that is not usable");
    }
    if (!w.Listed(off + 8, "a joint's child") || !w.Listed(off + 0xC, "a joint's next link")) ok = false;
    const uint32_t child = w.a.U32(off + 8), next = w.a.U32(off + 0xC), dobj = w.a.U32(off + 0x10);
    if (ok && dobj != 0 && !(j.flags & kJObjNoMeshFlags) && !w.Listed(off + 0x10, "a joint's mesh list")) ok = false;
    if (ok && dobj != 0 && !(j.flags & kJObjNoMeshFlags)) ok = WalkDObjs(w, dobj, m, j);
    m.joints.push_back(j);
    if (ok && child != 0 && !(j.flags & kJObjInstance)) ok = WalkJoints(w, child, j.number, depth + 1, m, seen, onPath);
    off = next;
  }
  for (uint32_t c : chain) onPath.erase(c);
  return ok;
}

bool WalkModel(Walk& w, uint32_t set, ModelInfo& m) {
  m.set = set;
  if (!w.a.InData(set, 16)) return w.Bad("a model set leads outside the data");
  m.root = w.a.U32(set);
  m.animated = w.a.U32(set + 4) != 0 || w.a.U32(set + 8) != 0 || w.a.U32(set + 12) != 0;
  if (m.root == 0) return true;
  if (!w.a.InData(m.root, kJObjSize)) return w.Bad("a model set (at " + std::to_string(set) + ") leads outside the data");
  std::set<uint32_t> seen, onPath;
  return WalkJoints(w, m.root, -1, 0, m, seen, onPath);
}

}

bool HsdListScene(const HsdArchive& a, SceneInfo& out, std::vector<std::string>& problems) {
  const size_t before = problems.size();
  Walk w{a, problems, true};
  out = SceneInfo{};
  const int sd = a.FindPublic("scene_data");
  if (sd < 0) {
    problems.push_back("map: it has no \"scene_data\" (it is not a map)");
    return false;
  }
  const uint32_t scene = a.publics[size_t(sd)].second;
  if (!a.InData(scene, 16)) {
    problems.push_back("map: its scene_data is outside the data");
    return false;
  }
  const uint32_t models = a.U32(scene), cameras = a.U32(scene + 4), lights = a.U32(scene + 8), fog = a.U32(scene + 12);
  if (models == 0 || !a.InData(models, 8)) {
    problems.push_back("map: it has no list of model sets");
    return false;
  }
  for (uint32_t i = 0;; i++) {
    if (i > 64 || !a.InData(models + 4 * i, 4)) {
      problems.push_back("map: its list of model sets does not end");
      return false;
    }
    const uint32_t set = a.U32(models + 4 * i);
    if (set == 0) break;
    ModelInfo m;
    if (!WalkModel(w, set, m)) return false;
    out.models.push_back(std::move(m));
  }
  if (out.models.empty()) problems.push_back("map: it has no model set");
  if (lights != 0) {
    for (uint32_t i = 0;; i++) {
      if (i > 32 || !a.InData(lights + 4 * i, 4)) {
        problems.push_back("map: its light list does not end");
        break;
      }
      const uint32_t l = a.U32(lights + 4 * i);
      if (l == 0) break;
      if (!a.InData(l, 0x10)) {
        problems.push_back("map: a light leads outside the data");
        break;
      }
      out.lights++;
    }
  }
  out.camera = cameras != 0 && a.InData(cameras, 0x10);
  if (cameras != 0 && !out.camera) problems.push_back("map: its camera leads outside the data");
  out.fog = fog != 0;
  out.fog = out.fog && a.InData(fog, 4);
  out.textureBytes = w.textureBytes;
  return problems.size() == before;
}

bool HsdValidateMap(const uint8_t* bytes, size_t size, std::vector<std::string>& problems, SceneInfo* info) {
  const size_t before = problems.size();
  HsdArchive a;
  if (!HsdParse(bytes, size, a, problems)) return false;
  SceneInfo local;
  SceneInfo& s = info != nullptr ? *info : local;
  if (!HsdListScene(a, s, problems)) return false;
  if (!s.camera) problems.push_back("map: it has no scene camera (the game stops without one)");
  if (s.lights == 0) problems.push_back("map: it has no light");
  return problems.size() == before;
}

void HsdSetTranslation(HsdArchive& a, uint32_t j, float x, float y, float z) {
  a.SetF32(j + 0x2C, x), a.SetF32(j + 0x30, y), a.SetF32(j + 0x34, z);
}
void HsdAddTurnY(HsdArchive& a, uint32_t j, float degrees) { a.SetF32(j + 0x18, a.F32(j + 0x18) + degrees * 3.14159265358979f / 180.f); }
void HsdSetScale(HsdArchive& a, uint32_t j, float sx, float sy, float sz) { a.SetF32(j + 0x20, sx), a.SetF32(j + 0x24, sy), a.SetF32(j + 0x28, sz); }

int HsdHideJoint(HsdArchive& a, uint32_t joint, bool children) {
  int changed = 0;
  std::vector<uint32_t> stack{joint};
  std::set<uint32_t> seen;
  bool first = true;
  while (!stack.empty()) {
    const uint32_t j = stack.back();
    stack.pop_back();
    if (!a.InData(j, kJObjSize) || !seen.insert(j).second) continue;
    if (!(a.U32(j + 4) & kJObjNoMeshFlags) && a.U32(j + 0x10) != 0) {
      a.SetU32(j + 0x10, 0);
      a.RemoveSlot(j + 0x10);
      changed++;
    }
    if (children && a.U32(j + 8) != 0 && !(a.U32(j + 4) & kJObjInstance)) stack.push_back(a.U32(j + 8));
    if (!first && children && a.U32(j + 0xC) != 0) stack.push_back(a.U32(j + 0xC));
    first = false;
  }
  return changed;
}

bool HsdAddModelSet(HsdArchive& into, uint32_t root, std::string& problem) {
  const int sd = into.FindPublic("scene_data");
  if (sd < 0) {
    problem = "the room's map has no scene_data";
    return false;
  }
  const uint32_t r = root;
  uint32_t setAt = Align(uint32_t(into.data.size()));
  into.data.resize(setAt + 16, 0);
  into.SetU32(setAt, r);
  into.AddSlot(setAt);
  const uint32_t scene = into.publics[size_t(sd)].second;
  const uint32_t oldList = into.U32(scene);
  std::vector<uint32_t> sets;
  for (uint32_t i = 0; oldList != 0 && into.InData(oldList + 4 * i, 4) && into.U32(oldList + 4 * i) != 0 && i < 64; i++) sets.push_back(into.U32(oldList + 4 * i));
  sets.push_back(setAt);
  const uint32_t listAt = Align(uint32_t(into.data.size()), 4);
  into.data.resize(listAt + 4 * (sets.size() + 1), 0);
  for (size_t i = 0; i < sets.size(); i++) {
    into.SetU32(listAt + 4 * uint32_t(i), sets[i]);
    into.AddSlot(listAt + 4 * uint32_t(i));
  }
  into.SetU32(scene, listAt);
  into.AddSlot(scene);
  into.data.resize(Align(uint32_t(into.data.size()), 32), 0);
  return true;
}

namespace {

struct Source {
  const HsdArchive& a;
  std::vector<uint32_t> targets;
  explicit Source(const HsdArchive& arc) : a(arc) {
    for (uint32_t s : a.slots) targets.push_back(a.U32(s));
    targets.push_back(0);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  }
  uint32_t ChunkEnd(uint32_t t) const {
    auto it = std::upper_bound(targets.begin(), targets.end(), t);
    return it == targets.end() ? uint32_t(a.data.size()) : *it;
  }
  std::vector<uint32_t> SlotsIn(uint32_t lo, uint32_t hi) const {
    auto i = std::lower_bound(a.slots.begin(), a.slots.end(), lo), j = std::lower_bound(a.slots.begin(), a.slots.end(), hi);
    return std::vector<uint32_t>(i, j);
  }
};

}

bool HsdAddPiece(HsdArchive& into, const HsdArchive& from, uint32_t root, float x, float y, float z, float turn, std::string& problem, uint32_t* newRoot,
                 uint32_t* bytesAdded, HsdPieceCache* cache) {
  if (!from.InData(root, kJObjSize)) {
    problem = "the piece's joint is outside its archive";
    return false;
  }
  const int sd = into.FindPublic("scene_data");
  if (sd < 0) {
    problem = "the room's map has no scene_data";
    return false;
  }
  Source src(from);
  const uint32_t nextSlot = root + 0xC;
  std::set<uint32_t> seen;
  std::vector<uint32_t> stack{root};
  while (!stack.empty()) {
    const uint32_t t = stack.back();
    stack.pop_back();
    if (seen.count(t)) continue;
    seen.insert(t);
    if (seen.size() > 200000) {
      problem = "the piece is too large";
      return false;
    }
    for (uint32_t s : src.SlotsIn(t, src.ChunkEnd(t))) {
      if (s == nextSlot) continue;
      stack.push_back(from.U32(s));
    }
  }
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  std::vector<bool> bigRun;
  {
    std::vector<std::pair<uint32_t, uint32_t>> chunks;
    for (uint32_t t : seen) chunks.emplace_back(t, src.ChunkEnd(t));
    std::sort(chunks.begin(), chunks.end());
    for (const auto& c : chunks) {
      const bool big = c.second - c.first >= kSharedChunk, lastBig = !runs.empty() && runs.back().second - runs.back().first >= kSharedChunk && runs.size() > 0 && bigRun.back();
      if (!runs.empty() && runs.back().second == c.first && !big && !lastBig) runs.back().second = c.second;
      else runs.push_back(c), bigRun.push_back(big);
    }
  }
  const uint32_t startSize = uint32_t(into.data.size());
  std::vector<uint32_t> base(runs.size());
  std::vector<bool> reused(runs.size(), false);
  for (size_t i = 0; i < runs.size(); i++) {
    if (cache != nullptr && bigRun[i]) {
      const auto it = cache->shared.find({&from, runs[i].first});
      if (it != cache->shared.end()) {
        base[i] = it->second, reused[i] = true;
        continue;
      }
    }
    const uint32_t pos = Align(uint32_t(into.data.size())) + (runs[i].first % 32);
    into.data.resize(pos, 0);
    base[i] = pos;
    into.data.insert(into.data.end(), from.data.begin() + runs[i].first, from.data.begin() + runs[i].second);
    if (cache != nullptr && bigRun[i]) cache->shared[{&from, runs[i].first}] = pos;
  }
  const auto newOff = [&](uint32_t v, bool& ok) {
    for (size_t i = 0; i < runs.size(); i++) {
      if (v >= runs[i].first && v < runs[i].second) return base[i] + v - runs[i].first;
    }
    ok = false;
    return 0u;
  };
  for (size_t i = 0; i < runs.size(); i++) {
    if (reused[i]) continue;
    for (uint32_t s : src.SlotsIn(runs[i].first, runs[i].second)) {
      const uint32_t ns = base[i] + s - runs[i].first;
      if (s == nextSlot) {
        into.SetU32(ns, 0);
        continue;
      }
      const uint32_t v = from.U32(s);
      bool ok = true;
      const uint32_t nv = newOff(v, ok);
      if (!ok) {
        problem = "the piece points at a place the copier did not take (" + std::to_string(v) + ")";
        into.data.resize(startSize);
        return false;
      }
      into.SetU32(ns, nv);
      into.AddSlot(ns);
    }
  }
  bool ok = true;
  const uint32_t r = newOff(root, ok);
  into.SetF32(r + 0x2C, x), into.SetF32(r + 0x30, y), into.SetF32(r + 0x34, z);
  if (turn != 0.f) HsdAddTurnY(into, r, turn);
  if (!HsdAddModelSet(into, r, problem)) {
    into.data.resize(startSize);
    return false;
  }
  if (newRoot != nullptr) *newRoot = r;
  if (bytesAdded != nullptr) *bytesAdded = uint32_t(into.data.size()) - startSize;
  return true;
}

namespace {

struct Mat { float m[3][4]; };
Mat Identity() { Mat r{}; r.m[0][0] = r.m[1][1] = r.m[2][2] = 1; return r; }
Mat Mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + (j == 3 ? a.m[i][3] : 0.f);
  }
  return r;
}
Mat Local(const HsdArchive& a, uint32_t j, bool withTranslation) {
  const float rx = a.F32(j + 0x14), ry = a.F32(j + 0x18), rz = a.F32(j + 0x1C), sx = a.F32(j + 0x20), sy = a.F32(j + 0x24), sz = a.F32(j + 0x28);
  const float cx = std::cos(rx), sxn = std::sin(rx), cy = std::cos(ry), syn = std::sin(ry), cz = std::cos(rz), szn = std::sin(rz);
  Mat r{};
  r.m[0][0] = cz * cy, r.m[0][1] = cz * syn * sxn - szn * cx, r.m[0][2] = cz * syn * cx + szn * sxn;
  r.m[1][0] = szn * cy, r.m[1][1] = szn * syn * sxn + cz * cx, r.m[1][2] = szn * syn * cx - cz * sxn;
  r.m[2][0] = -syn, r.m[2][1] = cy * sxn, r.m[2][2] = cy * cx;
  for (int i = 0; i < 3; i++) r.m[i][0] *= sx, r.m[i][1] *= sy, r.m[i][2] *= sz;
  if (withTranslation) r.m[0][3] = a.F32(j + 0x2C), r.m[1][3] = a.F32(j + 0x30), r.m[2][3] = a.F32(j + 0x34);
  return r;
}

bool PieceVertices(const HsdArchive& a, uint32_t pobj, const Mat& m, float lo[3], float hi[3], size_t& count) {
  const uint32_t blocks = (uint32_t(a.data[pobj + 0xE]) << 8) | a.data[pobj + 0xF], dl = a.U32(pobj + 0x10), desc = a.U32(pobj + 8);
  if (dl == 0 || desc == 0) return false;
  struct Attr { uint32_t attr, type, cnt, comp; uint32_t frac, stride, base; };
  std::vector<Attr> attrs;
  for (uint32_t at = desc; a.InData(at, 0x18) && a.U32(at) != 0xFF && attrs.size() < 40; at += 0x18) {
    attrs.push_back({a.U32(at), a.U32(at + 4), a.U32(at + 8), a.U32(at + 0xC), a.data[at + 0x10], (uint32_t(a.data[at + 0x12]) << 8) | a.data[at + 0x13], a.U32(at + 0x14)});
  }
  const auto compBytes = [](uint32_t t) { return t == 4 ? 4u : (t == 2 || t == 3) ? 2u : 1u; };
  const auto directSize = [&](const Attr& at) -> uint32_t {
    if (at.attr <= 8) return 1;
    if (at.attr == 9) return (at.cnt == 0 ? 2u : 3u) * compBytes(at.comp);
    if (at.attr == 10) return (at.cnt == 0 ? 3u : 9u) * compBytes(at.comp);
    if (at.attr == 11 || at.attr == 12) return at.comp == 0 || at.comp == 3 ? 2u : (at.comp == 1 || at.comp == 4) ? 3u : 4u;
    return (at.cnt == 0 ? 1u : 2u) * compBytes(at.comp);
  };
  uint32_t pos = dl, end = dl + blocks * 32;
  if (!a.InData(dl, size_t(blocks) * 32)) return false;
  while (pos < end) {
    const uint8_t cmd = a.data[pos++];
    if (cmd == 0) continue;
    if (cmd == 0x08) { pos += 5 - 1; continue; }
    if (cmd == 0x61) { pos += 4; continue; }
    if (cmd == 0x10) {
      if (pos + 4 > end) return false;
      const uint32_t n = (((uint32_t(a.data[pos]) << 8) | a.data[pos + 1]) + 1);
      pos += 4 + n * 4;
      continue;
    }
    if ((cmd & 0x80) == 0 || (cmd & 0xF8) > 0xB8) return false;
    if (pos + 2 > end) return false;
    const uint32_t n = (uint32_t(a.data[pos]) << 8) | a.data[pos + 1];
    pos += 2;
    for (uint32_t v = 0; v < n; v++) {
      float p3[3] = {};
      bool have = false;
      for (const Attr& at : attrs) {
        uint32_t size = 0;
        if (at.type == 1) size = directSize(at);
        else if (at.type == 2) size = 1;
        else if (at.type == 3) size = 2;
        else continue;
        if (pos + size > end) return false;
        if (at.attr == 9) {
          uint32_t idx = 0;
          const uint8_t* src = nullptr;
          if (at.type == 1) src = &a.data[pos];
          else {
            idx = at.type == 2 ? a.data[pos] : (uint32_t(a.data[pos]) << 8) | a.data[pos + 1];
            const uint32_t off = at.base + idx * at.stride;
            if (!a.InData(off, (at.cnt == 0 ? 2u : 3u) * compBytes(at.comp))) return false;
            src = &a.data[off];
          }
          const float scale = std::ldexp(1.f, -int(at.frac));
          for (uint32_t c = 0; c < (at.cnt == 0 ? 2u : 3u); c++) {
            const uint8_t* q = src + c * compBytes(at.comp);
            float x = 0;
            switch (at.comp) {
              case 0: x = q[0] * scale; break;
              case 1: x = int8_t(q[0]) * scale; break;
              case 2: x = ((q[0] << 8) | q[1]) * scale; break;
              case 3: x = int16_t((q[0] << 8) | q[1]) * scale; break;
              default: {
                const uint32_t u = uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | q[3];
                std::memcpy(&x, &u, 4);
              }
            }
            p3[c] = x;
          }
          have = true;
        }
        pos += size;
      }
      if (have) {
        for (int i = 0; i < 3; i++) {
          const float w = m.m[i][0] * p3[0] + m.m[i][1] * p3[1] + m.m[i][2] * p3[2] + m.m[i][3];
          if (!std::isfinite(w)) return false;
          lo[i] = std::min(lo[i], w), hi[i] = std::max(hi[i], w);
        }
        count++;
      }
    }
  }
  return true;
}

}

bool HsdSubtreeBounds(const HsdArchive& a, uint32_t root, float outMin[3], float outMax[3]) {
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  size_t count = 0;
  struct Item { uint32_t off; Mat m; bool top; };
  std::vector<Item> stack{{root, Identity(), true}};
  std::set<uint32_t> seen;
  while (!stack.empty()) {
    const Item it = stack.back();
    stack.pop_back();
    if (!a.InData(it.off, kJObjSize) || !seen.insert(it.off).second) continue;
    const Mat m = Mul(it.m, Local(a, it.off, !it.top));
    const uint32_t dobj = a.U32(it.off + 0x10);
    if (dobj != 0 && !(a.U32(it.off + 4) & kJObjNoMeshFlags)) {
      std::set<uint32_t> ds;
      for (uint32_t d = dobj; d != 0 && a.InData(d, kDObjSize) && ds.insert(d).second; d = a.U32(d + 4)) {
        std::set<uint32_t> ps;
        for (uint32_t p = a.U32(d + 0xC); p != 0 && a.InData(p, kPObjSize) && ps.insert(p).second; p = a.U32(p + 4)) PieceVertices(a, p, m, lo, hi, count);
      }
    }
    if (a.U32(it.off + 0xC) != 0 && !it.top) stack.push_back({a.U32(it.off + 0xC), it.m, false});
    if (a.U32(it.off + 8) != 0 && !(a.U32(it.off + 4) & kJObjInstance)) stack.push_back({a.U32(it.off + 8), m, false});
  }
  if (count == 0) return false;
  for (int i = 0; i < 3; i++) outMin[i] = lo[i], outMax[i] = hi[i];
  return true;
}

}
