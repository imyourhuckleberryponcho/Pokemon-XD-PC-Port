#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define FIFO_SIZE (256 * 1024)
alignas(32) static u8 fifo_buffer[FIFO_SIZE];

constexpr int kEntries = 1003;
alignas(32) static u32 sPositions[kEntries * 3];
alignas(32) static u8 sColors[kEntries * 4];

static u32 be_float(f32 f) {
  u32 v;
  std::memcpy(&v, &f, 4);
  return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static void set_entry(int i, f32 x, f32 y, u8 r, u8 g, u8 b) {
  sPositions[i * 3 + 0] = be_float(x);
  sPositions[i * 3 + 1] = be_float(y);
  sPositions[i * 3 + 2] = be_float(0.0f);
  sColors[i * 4 + 0] = r;
  sColors[i * 4 + 1] = g;
  sColors[i * 4 + 2] = b;
  sColors[i * 4 + 3] = 255;
}

static void setup() {
  for (int i = 0; i < kEntries; ++i) {
    set_entry(i, 5.0f, 5.0f, 255, 255, 255);
  }
  set_entry(0, -0.7f, 0.7f, 255, 60, 60);
  set_entry(1, -1.1f, 0.1f, 255, 60, 60);
  set_entry(2, -0.3f, 0.1f, 255, 60, 60);
  set_entry(3, 0.7f, 0.7f, 60, 255, 60);
  set_entry(4, 0.3f, 0.1f, 60, 255, 60);
  set_entry(5, 1.1f, 0.1f, 60, 255, 60);
  set_entry(1000, 0.0f, -0.1f, 60, 60, 255);
  set_entry(1001, -0.4f, -0.7f, 60, 60, 255);
  set_entry(1002, 0.4f, -0.7f, 60, 60, 255);

  GXInit(fifo_buffer, FIFO_SIZE);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetArray(GX_VA_POS, sPositions, 12);
  GXSetArray(GX_VA_CLR0, sColors, 4);
  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(0);
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  GXSetCullMode(GX_CULL_NONE);
  GXSetCopyClear(GXColor{20, 20, 40, 255}, GX_MAX_Z24);
}

static void draw() {
  const GXRenderModeObj* rmode = &GXNtsc480IntDf;
  const f32 aspect = (f32)rmode->fbWidth / (f32)rmode->efbHeight;
  GXSetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
  GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  Mtx44 proj;
  MTXOrtho(proj, 1.0f, -1.0f, -aspect, aspect, 0.1f, 10.0f);
  GXSetProjection(proj, GX_ORTHOGRAPHIC);
  Mtx model;
  MTXTrans(model, 0.0f, 0.0f, -1.0f);
  GXLoadPosMtxImm(model, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);

  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
  GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
  for (int tri = 0; tri < 2; ++tri) {
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    for (int i = 0; i < 3; ++i) {
      GXWGFifo.u8 = (u8)(tri * 3 + i);
      GXWGFifo.u8 = (u8)(tri * 3 + i);
    }
    GXEnd();
  }

  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
  GXSetVtxDesc(GX_VA_CLR0, GX_INDEX16);
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  for (int i = 0; i < 3; ++i) {
    GXWGFifo.u16 = (u16)(1000 + i);
    GXWGFifo.u16 = (u16)(1000 + i);
  }
  GXEnd();
}

int main(int argc, char* argv[]) {
  int maxFrames = -1;
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--frames") == 0) {
      maxFrames = std::atoi(argv[i + 1]);
    }
  }
  AuroraConfig config{};
  config.appName = "XD port - indexed vertex test";
  config.vsync = true;
  aurora_initialize(argc, argv, &config);
  setup();

  bool exiting = false;
  for (int frame = 0; !exiting && frame != maxFrames;) {
    for (const AuroraEvent* event = aurora_update(); event != nullptr && event->type != AURORA_NONE; ++event) {
      if (event->type == AURORA_EXIT) {
        exiting = true;
      }
    }
    if (exiting || !aurora_begin_frame()) {
      continue;
    }
    draw();
    PortGX_EndOfFrame();
    aurora_end_frame();
    frame++;
  }
  aurora_shutdown();
  return 0;
}
