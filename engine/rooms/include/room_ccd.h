#ifndef ENGINE_ROOM_CCD_H
#define ENGINE_ROOM_CCD_H

#include <cstdint>
#include <string>
#include <vector>

namespace engine::rooms {

enum CcdKind { kCcdWalk = 0, kCcdHit = 1, kCcdThru = 2, kCcdCheck = 3, kCcdHit34 = 4, kCcdSun = 5, kCcdKinds = 6 };

constexpr uint32_t kCcdMaxRecords = 64;
constexpr uint32_t kCcdRecordSize = 0x40;

struct CcdTri {
  float v[9] = {};
  float n[3] = {};
  uint8_t tail[4] = {};
};

struct CcdBlock {
  bool present = false;
  std::vector<CcdTri> tris;
  float cellW = 40.f, cellD = 40.f;
};

struct CcdRecord {
  uint8_t head[kCcdRecordSize] = {};
  CcdBlock block[kCcdKinds];
};

struct Ccd {
  std::vector<CcdRecord> records;
};

bool CcdIsGrid(int kind);
size_t CcdStride(int kind);
const char* CcdKindName(int kind);

bool CcdValidate(const uint8_t* data, size_t size, std::vector<std::string>& problems);
bool CcdRead(const uint8_t* data, size_t size, Ccd& out, std::string& problem);
std::vector<uint8_t> CcdWrite(const Ccd& ccd);

void CcdMakeWall(float x0, float z0, float x1, float z1, float y0, float y1, bool flip, CcdTri out[2]);
void CcdMakeGround(float x0, float z0, float x1, float z1, float y, CcdTri out[2]);
void CcdAddWall(CcdRecord& rec, float x0, float z0, float x1, float z1, float y0, float y1, bool flip);
void CcdAddBox(CcdRecord& rec, float cx, float cz, float hx, float hz, float y0, float y1);
size_t CcdRemoveHitIn(Ccd& ccd, float x0, float z0, float x1, float z1);
CcdRecord CcdNewRecord();
void CcdTransform(CcdRecord& rec, float turnDegrees, float dx, float dy, float dz);

}

#endif
