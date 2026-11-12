#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  static const char* names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
  std::fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s: %s] %.*s\n", names[level], module, (int)len, message);
  if (level == LOG_FATAL) {
    std::fflush(stderr);
    std::abort();
  }
}

#define FIFO_SIZE (256 * 1024)
alignas(32) static u8 fifo_buffer[FIFO_SIZE];

static void setup_gx() {
  GXInit(fifo_buffer, FIFO_SIZE);
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
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

static const f32 kTri[3][2] = {{0.0f, 0.6f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
static const u8 kCol[3][4] = {{255, 60, 60, 255}, {60, 255, 60, 255}, {60, 60, 255, 255}};

static void draw() {
  const GXRenderModeObj* rmode = &GXNtsc480IntDf;
  const f32 aspect = (f32)rmode->fbWidth / (f32)rmode->efbHeight;
  GXSetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
  GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  Mtx44 proj;
  MTXOrtho(proj, 1.0f, -1.0f, -aspect, aspect, 0.1f, 10.0f);
  GXSetProjection(proj, GX_ORTHOGRAPHIC);
  GXSetCurrentMtx(GX_PNMTX0);

  Mtx model;
  MTXTrans(model, -0.6f, 0.0f, -1.0f);
  GXLoadPosMtxImm(model, GX_PNMTX0);
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  for (int i = 0; i < 3; i++) {
    GXPosition3f32(kTri[i][0], kTri[i][1], 0.0f);
    GXColor4u8(kCol[i][0], kCol[i][1], kCol[i][2], kCol[i][3]);
  }
  GXEnd();

  MTXTrans(model, 0.6f, 0.0f, -1.0f);
  GXLoadPosMtxImm(model, GX_PNMTX0);
  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  for (int i = 0; i < 3; i++) {
    GXWGFifo.f32 = kTri[i][0];
    GXWGFifo.f32 = kTri[i][1];
    GXWGFifo.f32 = 0.0f;
    GXWGFifo.u8 = kCol[i][0];
    GXWGFifo.u8 = kCol[i][1];
    GXWGFifo.u8 = kCol[i][2];
    GXWGFifo.u8 = kCol[i][3];
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
  config.appName = "XD port - GX write pipe test";
  config.logCallback = &log_callback;
  config.vsync = true;
  aurora_initialize(argc, argv, &config);
  setup_gx();

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
