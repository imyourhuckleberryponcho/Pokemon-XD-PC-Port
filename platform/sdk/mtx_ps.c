#include <stdint.h>

typedef float f32;
typedef uint32_t u32;
typedef f32 Mtx[3][4];
typedef struct { f32 x, y, z; } Vec;
typedef struct { f32 x, y, z, w; } Quaternion;

void C_MTXIdentity(Mtx m);
void C_MTXCopy(const Mtx src, Mtx dst);
void C_MTXConcat(const Mtx a, const Mtx b, Mtx ab);
void C_MTXTranspose(const Mtx src, Mtx xPose);
u32 C_MTXInverse(const Mtx src, Mtx inv);
u32 C_MTXInvXpose(const Mtx src, Mtx invX);
void C_MTXMultVec(const Mtx m, const Vec* src, Vec* dst);
void C_MTXMultVecSR(const Mtx m, const Vec* src, Vec* dst);
void C_MTXQuat(Mtx m, const Quaternion* q);
void C_MTXTrans(Mtx m, f32 xT, f32 yT, f32 zT);
void C_MTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT);
void C_MTXScale(Mtx m, f32 xS, f32 yS, f32 zS);
void C_MTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS);
void C_MTXRotRad(Mtx m, char axis, f32 rad);
void C_MTXRotAxisRad(Mtx m, const Vec* axis, f32 rad);
void C_VECAdd(const Vec* a, const Vec* b, Vec* ab);
void C_VECSubtract(const Vec* a, const Vec* b, Vec* a_b);
void C_VECScale(const Vec* src, Vec* dst, f32 scale);
f32 C_VECDotProduct(const Vec* a, const Vec* b);
void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb);
void C_QUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq);
void C_QUATInverse(const Quaternion* src, Quaternion* inv);

void PortCpuCost_Count(unsigned counter, uint64_t amount);
#define MTX_OP() PortCpuCost_Count(3u, 1u)

void PSMTXIdentity(Mtx m) { MTX_OP(); C_MTXIdentity(m); }
void PSMTXCopy(const Mtx src, Mtx dst) { MTX_OP(); C_MTXCopy(src, dst); }
void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) { MTX_OP(); C_MTXConcat(a, b, ab); }
void PSMTXTranspose(const Mtx src, Mtx xPose) { MTX_OP(); C_MTXTranspose(src, xPose); }
u32 PSMTXInverse(const Mtx src, Mtx inv) { MTX_OP(); return C_MTXInverse(src, inv); }
u32 PSMTXInvXpose(const Mtx src, Mtx invX) { MTX_OP(); return C_MTXInvXpose(src, invX); }
void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst) { MTX_OP(); C_MTXMultVec(m, src, dst); }
void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) { MTX_OP(); C_MTXMultVecSR(m, src, dst); }
void PSMTXQuat(Mtx m, const Quaternion* q) { MTX_OP(); C_MTXQuat(m, q); }
void PSMTXTrans(Mtx m, f32 x, f32 y, f32 z) { MTX_OP(); C_MTXTrans(m, x, y, z); }
void PSMTXTransApply(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) { MTX_OP(); C_MTXTransApply(src, dst, x, y, z); }
void PSMTXScale(Mtx m, f32 x, f32 y, f32 z) { MTX_OP(); C_MTXScale(m, x, y, z); }
void PSMTXScaleApply(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) { MTX_OP(); C_MTXScaleApply(src, dst, x, y, z); }
void PSMTXRotRad(Mtx m, char axis, f32 rad) { MTX_OP(); C_MTXRotRad(m, axis, rad); }
void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) { MTX_OP(); C_MTXRotAxisRad(m, axis, rad); }
void PSVECAdd(const Vec* a, const Vec* b, Vec* ab) { MTX_OP(); C_VECAdd(a, b, ab); }
void PSVECSubtract(const Vec* a, const Vec* b, Vec* a_b) { MTX_OP(); C_VECSubtract(a, b, a_b); }
void PSVECScale(const Vec* src, Vec* dst, f32 scale) { MTX_OP(); C_VECScale(src, dst, scale); }
#include <math.h>
#include <string.h>
#include <dolphin/ppc_math.h>

static double gk_f25(double c) {
  uint64_t u;
  memcpy(&u, &c, 8);
  u = (u & 0xFFFFFFFFF8000000ULL) + (u & 0x8000000ULL);
  memcpy(&c, &u, 8);
  return c;
}
static f32 gk_muls(double a, double c) { return (f32)(a * gk_f25(c)); }
static f32 gk_adds(f32 a, f32 b) { return (f32)((double)a + (double)b); }
static f32 gk_madds(double a, double c, double b) { return (f32)fma(a, gk_f25(c), b); }
static f32 gk_msubs(double a, double c, double b) { return (f32)fma(a, gk_f25(c), -b); }
static f32 gk_nmsubs(double a, double c, double b) { return -(f32)fma(a, gk_f25(c), -b); }

static f32 gk_rsqrt_step(f32 x) {
  double r = frsqrte((double)x);
  f32 n0 = gk_muls(r, r);
  f32 n1 = gk_muls(r, 0.5);
  n0 = gk_nmsubs(n0, x, 3.0);
  return gk_muls(n0, n1);
}

f32 PortPS_VECSquareMag(const Vec* v) {
  f32 xx = gk_muls(v->x, v->x), yy = gk_muls(v->y, v->y);
  return gk_adds(gk_madds(v->z, v->z, xx), yy);
}
f32 PortPS_VECMag(const Vec* v) {
  f32 sq = PortPS_VECSquareMag(v);
  if (sq == 0.0f) return sq;
  return gk_muls(sq, gk_rsqrt_step(sq));
}
void PortPS_VECNormalize(const Vec* src, Vec* unit) {
  f32 sq = PortPS_VECSquareMag(src);
  f32 r = gk_rsqrt_step(sq);
  unit->x = gk_muls(src->x, r);
  unit->y = gk_muls(src->y, r);
  unit->z = gk_muls(src->z, r);
}
f32 PortPS_VECSquareDistance(const Vec* a, const Vec* b) {
  f32 dx = (f32)((double)a->x - b->x), dy = (f32)((double)a->y - b->y), dz = (f32)((double)a->z - b->z);
  f32 yy = gk_muls(dy, dy), zz = gk_muls(dz, dz);
  return gk_adds(gk_madds(dx, dx, yy), zz);
}
f32 PortPS_VECDistance(const Vec* a, const Vec* b) {
  f32 sq = PortPS_VECSquareDistance(a, b);
  if (sq == 0.0f) return sq;
  return gk_muls(sq, gk_rsqrt_step(sq));
}
f32 PSVECDotProduct(const Vec* a, const Vec* b) {
  MTX_OP();
  f32 yy = gk_muls(a->y, b->y), zz = gk_muls(a->z, b->z);
  return gk_adds(gk_madds(a->x, b->x, yy), zz);
}
void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
  MTX_OP();
  f32 bxaz = gk_muls(b->x, a->z), byaz = gk_muls(b->y, a->z), byax = gk_muls(b->y, a->x);
  f32 x = gk_msubs(a->y, b->z, byaz);
  f32 y = -gk_msubs(a->x, b->z, bxaz);
  f32 z = -gk_msubs(a->y, b->x, byax);
  axb->x = x;
  axb->y = y;
  axb->z = z;
}
void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) { MTX_OP(); C_QUATMultiply(p, q, pq); }
void PSQUATInverse(const Quaternion* src, Quaternion* inv) { MTX_OP(); C_QUATInverse(src, inv); }
