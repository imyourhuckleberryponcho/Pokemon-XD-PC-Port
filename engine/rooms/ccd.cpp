#include "room_ccd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace engine::rooms {

namespace {

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
float BeF(const uint8_t* p) {
  const uint32_t u = Be32(p);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PutBe32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }
void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
void PutBeF(uint8_t* p, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  PutBe32(p, u);
}

constexpr size_t kPointerAt[kCcdKinds] = {0x24, 0x28, 0x2C, 0x30, 0x34, 0x38};

bool Sane(float f) { return std::isfinite(f) && std::fabs(f) < 1.0e7f; }

}

bool CcdIsGrid(int kind) { return kind == kCcdWalk || kind == kCcdHit || kind == kCcdThru || kind == kCcdHit34; }
size_t CcdStride(int kind) { return kind == kCcdSun ? 0x30 : 0x34; }
const char* CcdKindName(int kind) {
  static const char* const names[kCcdKinds] = {"walk", "hit", "thru", "check", "hit34", "sun"};
  return kind >= 0 && kind < kCcdKinds ? names[kind] : "?";
}

bool CcdValidate(const uint8_t* d, size_t size, std::vector<std::string>& problems) {
  const size_t before = problems.size();
  const auto bad = [&](const std::string& s) { problems.push_back("collision: " + s); };
  if (d == nullptr || size < 8) {
    bad("the file is shorter than its header");
    return false;
  }
  const uint32_t recOff = Be32(d), count = Be32(d + 4);
  if (count < 1 || count > kCcdMaxRecords) {
    bad("it has " + std::to_string(count) + " records; the game keeps state for 1 to 64");
    return false;
  }
  if (recOff % 4 != 0 || recOff < 8 || size_t(recOff) + size_t(count) * kCcdRecordSize > size) {
    bad("the record list does not fit in the file");
    return false;
  }
  for (uint32_t r = 0; r < count; r++) {
    const uint8_t* rec = d + recOff + size_t(r) * kCcdRecordSize;
    const std::string rn = "record " + std::to_string(r) + ": ";
    for (int i = 0; i < 9; i++) {
      if (!Sane(BeF(rec + 4 * i))) bad(rn + "its position, rotation or scale is not a usable number");
    }
    for (int kind = 0; kind < kCcdKinds; kind++) {
      const uint32_t p = Be32(rec + kPointerAt[kind]);
      if (p == 0) continue;
      const std::string bn = rn + CcdKindName(kind) + " block: ";
      const size_t head = CcdIsGrid(kind) ? 0x24 : 8;
      if (p % 4 != 0 || size_t(p) + head > size) {
        bad(bn + "its header is outside the file");
        continue;
      }
      const uint32_t tris = Be32(d + p), n = Be32(d + p + 4);
      const size_t stride = CcdStride(kind);
      if (n > 200000 || (n > 0 && (tris % 4 != 0 || size_t(tris) + size_t(n) * stride > size))) {
        bad(bn + "its triangles are outside the file");
        continue;
      }
      bool floatsOk = true;
      for (uint32_t t = 0; t < n && floatsOk; t++) {
        const uint8_t* tp = d + tris + size_t(t) * stride;
        for (int i = 0; i < 9; i++) floatsOk = floatsOk && Sane(BeF(tp + 4 * i));
      }
      if (!floatsOk) {
        bad(bn + "a triangle holds a number that is not usable (NaN or huge)");
        continue;
      }
      if (!CcdIsGrid(kind)) continue;
      const uint32_t cells = Be32(d + p + 8), idx = Be32(d + p + 0xC);
      if (cells == 0) {
        if (n != 0) bad(bn + "it has triangles but no grid of cells");
        continue;
      }
      const uint32_t cx = Be16(d + p + 0x10), cz = Be16(d + p + 0x12);
      const float cw = BeF(d + p + 0x14), cd = BeF(d + p + 0x18);
      if (cx == 0 || cz == 0 || cx * cz > 400000u) {
        bad(bn + "the grid is " + std::to_string(cx) + " x " + std::to_string(cz) + " cells");
        continue;
      }
      if (!(cw > 0.01f && cd > 0.01f && Sane(cw) && Sane(cd) && Sane(BeF(d + p + 0x1C)) && Sane(BeF(d + p + 0x20)))) {
        bad(bn + "the grid's cell size or corner is not usable");
        continue;
      }
      if (cells % 4 != 0 || size_t(cells) + size_t(cx) * cz * 8 > size) {
        bad(bn + "its grid cells are outside the file");
        continue;
      }
      size_t maxEnd = 0;
      bool cellsOk = true;
      for (uint32_t c = 0; c < cx * cz; c++) {
        const uint32_t first = Be32(d + cells + size_t(c) * 8), num = Be32(d + cells + size_t(c) * 8 + 4);
        if (num > n || first > 4000000u) cellsOk = false;
        maxEnd = std::max<size_t>(maxEnd, size_t(first) + num);
      }
      if (!cellsOk) {
        bad(bn + "a grid cell lists more triangles than the block has");
        continue;
      }
      if (maxEnd == 0) continue;
      if (idx % 4 != 0 || size_t(idx) + maxEnd * 4 > size) {
        bad(bn + "its triangle numbers are outside the file");
        continue;
      }
      for (size_t i = 0; i < maxEnd; i++) {
        if (Be32(d + idx + i * 4) >= n) {
          bad(bn + "a grid cell names a triangle that does not exist");
          break;
        }
      }
    }
  }
  return problems.size() == before;
}

bool CcdRead(const uint8_t* d, size_t size, Ccd& out, std::string& problem) {
  std::vector<std::string> p;
  if (!CcdValidate(d, size, p)) {
    problem = p.empty() ? "not a collision file" : p[0];
    return false;
  }
  out.records.clear();
  const uint32_t recOff = Be32(d), count = Be32(d + 4);
  for (uint32_t r = 0; r < count; r++) {
    const uint8_t* rec = d + recOff + size_t(r) * kCcdRecordSize;
    CcdRecord cr;
    std::memcpy(cr.head, rec, kCcdRecordSize);
    for (int kind = 0; kind < kCcdKinds; kind++) {
      const uint32_t ptr = Be32(rec + kPointerAt[kind]);
      if (ptr == 0) continue;
      CcdBlock& b = cr.block[kind];
      b.present = true;
      const uint32_t tris = Be32(d + ptr), n = Be32(d + ptr + 4);
      const size_t stride = CcdStride(kind);
      for (uint32_t t = 0; t < n; t++) {
        const uint8_t* tp = d + tris + size_t(t) * stride;
        CcdTri tri;
        for (int i = 0; i < 9; i++) tri.v[i] = BeF(tp + 4 * i);
        for (int i = 0; i < 3; i++) tri.n[i] = BeF(tp + 0x24 + 4 * i);
        if (stride >= 0x34) std::memcpy(tri.tail, tp + 0x30, 4);
        b.tris.push_back(tri);
      }
      if (CcdIsGrid(kind) && Be32(d + ptr + 8) != 0) {
        b.cellW = BeF(d + ptr + 0x14);
        b.cellD = BeF(d + ptr + 0x18);
      }
    }
    out.records.push_back(std::move(cr));
  }
  return true;
}

std::vector<uint8_t> CcdWrite(const Ccd& ccd) {
  const size_t n = ccd.records.size();
  const size_t base = 0x10 + n * kCcdRecordSize;
  std::vector<uint8_t> body;
  const auto align = [&] { while (body.size() % 4) body.push_back(0); };
  const auto here = [&] { return uint32_t(base + body.size()); };
  const auto put32 = [&](uint32_t v) { uint8_t b[4]; PutBe32(b, v); body.insert(body.end(), b, b + 4); };
  std::vector<std::array<uint32_t, kCcdKinds>> heads(n);
  for (size_t r = 0; r < n; r++) {
    heads[r].fill(0);
    for (int kind = 0; kind < kCcdKinds; kind++) {
      const CcdBlock& b = ccd.records[r].block[kind];
      if (!b.present) continue;
      align();
      const size_t stride = CcdStride(kind);
      const auto putTris = [&] {
        for (const CcdTri& t : b.tris) {
          uint8_t raw[0x34] = {};
          for (int i = 0; i < 9; i++) PutBeF(raw + 4 * i, t.v[i]);
          for (int i = 0; i < 3; i++) PutBeF(raw + 0x24 + 4 * i, t.n[i]);
          if (stride >= 0x34) std::memcpy(raw + 0x30, t.tail, 4);
          body.insert(body.end(), raw, raw + stride);
        }
      };
      if (CcdIsGrid(kind) && !b.tris.empty()) {
        float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
        for (const CcdTri& t : b.tris) {
          for (int i = 0; i < 3; i++) {
            minx = std::min(minx, t.v[i * 3]), maxx = std::max(maxx, t.v[i * 3]);
            minz = std::min(minz, t.v[i * 3 + 2]), maxz = std::max(maxz, t.v[i * 3 + 2]);
          }
        }
        const float cw = b.cellW, cd = b.cellD;
        const int cx = std::max(1, int(std::floor((maxx - minx) / cw)) + 1), cz = std::max(1, int(std::floor((maxz - minz) / cd)) + 1);
        std::vector<std::vector<uint32_t>> cells(size_t(cx) * cz);
        for (size_t i = 0; i < b.tris.size(); i++) {
          const CcdTri& t = b.tris[i];
          const float tx0 = std::min({t.v[0], t.v[3], t.v[6]}), tx1 = std::max({t.v[0], t.v[3], t.v[6]});
          const float tz0 = std::min({t.v[2], t.v[5], t.v[8]}), tz1 = std::max({t.v[2], t.v[5], t.v[8]});
          const int x0 = int(std::floor((tx0 - minx) / cw)), x1 = int(std::floor((tx1 - minx) / cw));
          const int z0 = int(std::floor((tz0 - minz) / cd)), z1 = int(std::floor((tz1 - minz) / cd));
          for (int z = std::max(0, z0); z <= std::min(cz - 1, z1); z++) {
            for (int x = std::max(0, x0); x <= std::min(cx - 1, x1); x++) cells[size_t(z) * cx + x].push_back(uint32_t(i));
          }
        }
        const uint32_t head = here();
        body.insert(body.end(), 0x24, 0);
        const uint32_t trisOff = here();
        putTris();
        const uint32_t cellOff = here();
        std::vector<uint32_t> idx;
        for (const auto& c : cells) {
          put32(uint32_t(idx.size())), put32(uint32_t(c.size()));
          idx.insert(idx.end(), c.begin(), c.end());
        }
        const uint32_t idxOff = here();
        for (uint32_t v : idx) put32(v);
        uint8_t* h = body.data() + (head - base);
        PutBe32(h, trisOff), PutBe32(h + 4, uint32_t(b.tris.size())), PutBe32(h + 8, cellOff), PutBe32(h + 0xC, idxOff);
        PutBe16(h + 0x10, uint16_t(cx)), PutBe16(h + 0x12, uint16_t(cz));
        PutBeF(h + 0x14, cw), PutBeF(h + 0x18, cd), PutBeF(h + 0x1C, minx), PutBeF(h + 0x20, minz);
        heads[r][kind] = head;
      } else {
        const uint32_t head = here();
        body.insert(body.end(), CcdIsGrid(kind) ? 0x24 : 8, 0);
        const uint32_t trisOff = here();
        putTris();
        uint8_t* h = body.data() + (head - base);
        PutBe32(h, trisOff), PutBe32(h + 4, uint32_t(b.tris.size()));
        heads[r][kind] = head;
      }
    }
  }
  std::vector<uint8_t> out(base, 0);
  PutBe32(out.data(), 0x10), PutBe32(out.data() + 4, uint32_t(n));
  for (size_t r = 0; r < n; r++) {
    uint8_t* rec = out.data() + 0x10 + r * kCcdRecordSize;
    std::memcpy(rec, ccd.records[r].head, kCcdRecordSize);
    for (int kind = 0; kind < kCcdKinds; kind++) PutBe32(rec + kPointerAt[kind], heads[r][kind]);
  }
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

namespace {

void Normal(const float v[9], float n[3]) {
  const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2], bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
  float x = ay * bz - az * by, y = az * bx - ax * bz, z = ax * by - ay * bx;
  float l = std::sqrt(x * x + y * y + z * z);
  if (l < 1e-12f) l = 1.f;
  n[0] = x / l, n[1] = y / l, n[2] = z / l;
}

}

void CcdMakeWall(float x0, float z0, float x1, float z1, float y0, float y1, bool flip, CcdTri out[2]) {
  float ax = x0, az = z0, bx = x1, bz = z1;
  if (flip) std::swap(ax, bx), std::swap(az, bz);
  const float a[9] = {ax, y0, az, ax, y1, az, bx, y1, bz};
  const float b[9] = {ax, y0, az, bx, y1, bz, bx, y0, bz};
  std::memcpy(out[0].v, a, sizeof a), std::memcpy(out[1].v, b, sizeof b);
  Normal(out[0].v, out[0].n), Normal(out[1].v, out[1].n);
  PutBe16(out[0].tail, 3), PutBe16(out[0].tail + 2, 0);
  PutBe16(out[1].tail, 6), PutBe16(out[1].tail + 2, 0);
}

void CcdMakeGround(float x0, float z0, float x1, float z1, float y, CcdTri out[2]) {
  const float a[9] = {x0, y, z0, x0, y, z1, x1, y, z1};
  const float b[9] = {x0, y, z0, x1, y, z1, x1, y, z0};
  std::memcpy(out[0].v, a, sizeof a), std::memcpy(out[1].v, b, sizeof b);
  for (int i = 0; i < 2; i++) {
    out[i].n[0] = 0, out[i].n[1] = 1, out[i].n[2] = 0;
    out[i].tail[0] = 0xFF, out[i].tail[1] = 0, out[i].tail[2] = 0, out[i].tail[3] = 0;
  }
}

CcdRecord CcdNewRecord() {
  CcdRecord r;
  const float one[3] = {1, 1, 1};
  for (int i = 0; i < 3; i++) PutBeF(r.head + 0x18 + 4 * i, one[i]);
  r.block[kCcdWalk].present = r.block[kCcdHit].present = r.block[kCcdHit34].present = true;
  return r;
}

void CcdAddWall(CcdRecord& rec, float x0, float z0, float x1, float z1, float y0, float y1, bool flip) {
  CcdTri t[2];
  CcdMakeWall(x0, z0, x1, z1, y0, y1, flip, t);
  for (int kind : {int(kCcdHit), int(kCcdHit34)}) {
    CcdBlock& b = rec.block[kind];
    b.present = true;
    b.tris.push_back(t[0]), b.tris.push_back(t[1]);
  }
}

void CcdAddBox(CcdRecord& rec, float cx, float cz, float hx, float hz, float y0, float y1) {
  const float x0 = cx - hx, x1 = cx + hx, z0 = cz - hz, z1 = cz + hz;
  CcdAddWall(rec, x0, z0, x1, z0, y0, y1, false);
  CcdAddWall(rec, x1, z1, x0, z1, y0, y1, false);
  CcdAddWall(rec, x1, z0, x1, z1, y0, y1, false);
  CcdAddWall(rec, x0, z1, x0, z0, y0, y1, false);
}

size_t CcdRemoveHitIn(Ccd& ccd, float xa, float za, float xb, float zb) {
  const float x0 = std::min(xa, xb), x1 = std::max(xa, xb), z0 = std::min(za, zb), z1 = std::max(za, zb);
  size_t removed = 0;
  for (CcdRecord& rec : ccd.records) {
    for (int kind : {int(kCcdHit), int(kCcdHit34)}) {
      auto& tris = rec.block[kind].tris;
      const size_t was = tris.size();
      tris.erase(std::remove_if(tris.begin(), tris.end(),
                                [&](const CcdTri& t) {
                                  for (int i = 0; i < 3; i++) {
                                    if (t.v[i * 3] < x0 - 0.01f || t.v[i * 3] > x1 + 0.01f || t.v[i * 3 + 2] < z0 - 0.01f || t.v[i * 3 + 2] > z1 + 0.01f) return false;
                                  }
                                  return true;
                                }),
                 tris.end());
      if (kind == kCcdHit) removed += was - tris.size();
    }
  }
  return removed;
}

void CcdTransform(CcdRecord& rec, float turnDegrees, float dx, float dy, float dz) {
  const float a = turnDegrees * 3.14159265358979f / 180.f, c = std::cos(a), s = std::sin(a);
  for (CcdBlock& b : rec.block) {
    for (CcdTri& t : b.tris) {
      for (int i = 0; i < 3; i++) {
        const float x = t.v[i * 3], z = t.v[i * 3 + 2];
        t.v[i * 3] = x * c + z * s + dx, t.v[i * 3 + 1] += dy, t.v[i * 3 + 2] = -x * s + z * c + dz;
      }
      const float nx = t.n[0], nz = t.n[2];
      t.n[0] = nx * c + nz * s, t.n[2] = -nx * s + nz * c;
    }
  }
}

}
