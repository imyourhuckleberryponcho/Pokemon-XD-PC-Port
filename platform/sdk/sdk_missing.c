#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>

#include "include/sdk_port.h"

#include <stdio.h>
#include <stdlib.h>

void PortGX_SetArrayUnsized(int attr, const void* data, u8 stride) {
  GXSetArray((GXAttr)attr, data, GX_ARRAY_SIZE_UNKNOWN, stride, false);
}

#include <math.h>
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, const f32 projmtx[4][4]) {
  f32 xi, iw, rangeVal, nearZ, sideX;
  u32 i;
  if (0.0 == projmtx[3][3]) {
    nearZ = projmtx[2][3] / (projmtx[2][2] - 1.0f);
    sideX = nearZ / projmtx[0][0];
  } else {
    sideX = 1.0f / projmtx[0][0];
    nearZ = 1.73205f * sideX;
  }
  iw = 2.0f / width;
  for (i = 0; i < 10; i++) {
    xi = (f32)((i + 1) << 5);
    xi *= iw;
    xi *= sideX;
    rangeVal = sqrtf(1.0f + ((xi * xi) / (nearZ * nearZ)));
    table->r[i] = (u16)((u32)(256.0f * rangeVal) & 0xFFF);
  }
}

void GXAbortFrame(void) {}

void GXResetWriteGatherPipe(void) {}

void GXSetGPMetric(u32 perf0, u32 perf1) { (void)perf0, (void)perf1; }
void GXClearGPMetric(void) {}

void GXPokeAlphaMode(GXCompare func, u8 threshold) { (void)func, (void)threshold; }
void GXPokeAlphaUpdate(GXBool update_enable) { (void)update_enable; }
void GXPokeBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) { (void)type, (void)src, (void)dst, (void)op; }
void GXPokeColorUpdate(GXBool update_enable) { (void)update_enable; }
void GXPokeDstAlpha(GXBool enable, u8 alpha) { (void)enable, (void)alpha; }
void GXPokeDither(GXBool dither) { (void)dither; }
void GXPokeZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) { (void)compare_enable, (void)func, (void)update_enable; }
