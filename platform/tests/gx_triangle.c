#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <stdio.h>
#include <stdlib.h>

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  static const char* names[] = {"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
  FILE* out = level >= LOG_ERROR ? stderr : stdout;
  fprintf(out, "[%s: %s] %.*s\n", names[level], module, (int)len, message);
  if (level == LOG_FATAL) {
    fflush(out);
    abort();
  }
}

#define FIFO_SIZE (256 * 1024)
static _Alignas(32) u8 fifo_buffer[FIFO_SIZE];

static void setup_gx(void) {
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
  GXSetCopyClear((GXColor){20, 20, 40, 255}, GX_MAX_Z24);
}

static void draw(float angle) {
  const GXRenderModeObj* rmode = &GXNtsc480IntDf;
  const float w = (float)rmode->fbWidth;
  const float h = (float)rmode->efbHeight;
  const float aspect = w / h;
  GXSetViewport(0.0f, 0.0f, w, h, 0.0f, 1.0f);
  GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);

  Mtx44 proj;
  MTXOrtho(proj, 1.0f, -1.0f, -aspect, aspect, 0.1f, 10.0f);
  GXSetProjection(proj, GX_ORTHOGRAPHIC);

  Mtx model;
  MTXRotRad(model, 'z', angle);
  model[2][3] = -1.0f;
  GXLoadPosMtxImm(model, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);

  GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
  GXPosition3f32(0.0f, 0.8f, 0.0f);
  GXColor4u8(255, 60, 60, 255);
  GXPosition3f32(-0.7f, -0.6f, 0.0f);
  GXColor4u8(60, 255, 60, 255);
  GXPosition3f32(0.7f, -0.6f, 0.0f);
  GXColor4u8(60, 60, 255, 255);
  GXEnd();
}

int main(int argc, char* argv[]) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const AuroraConfig config = {
      .appName = "XD port - GX triangle test",
      .logCallback = &log_callback,
      .vsync = true,
  };
  aurora_initialize(argc, argv, &config);
  setup_gx();

  float angle = 0.0f;
  bool exiting = false;
  bool paused = false;
  while (!exiting) {
    const AuroraEvent* event = aurora_update();
    while (event != NULL && event->type != AURORA_NONE) {
      switch (event->type) {
      case AURORA_EXIT:
        exiting = true;
        break;
      case AURORA_PAUSED:
        paused = true;
        break;
      case AURORA_UNPAUSED:
        paused = false;
        break;
      default:
        break;
      }
      ++event;
    }
    if (exiting || paused || !aurora_begin_frame()) {
      continue;
    }
    draw(angle);
    aurora_end_frame();
    angle += 0.02f;
  }

  aurora_shutdown();
  return 0;
}
