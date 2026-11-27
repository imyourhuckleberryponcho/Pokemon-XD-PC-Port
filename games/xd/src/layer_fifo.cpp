#include <cstdint>
#include <cstdio>

typedef uint32_t u32;
typedef int32_t s32;

struct _GSgfxLayerData {
  s32 id;
  const char* name;
  u32 unk8;
  bool clearColor;
  bool clearAlpha;
  bool clearDepth;
  bool visible;
  u32 width;
  u32 height;
  u32 format;
};

class GSgfxLayerConfig {
public:
  static void Setup(_GSgfxLayerData* data);
};
class PortXD_FloorLayerConfig {
public:
  static void Setup(_GSgfxLayerData* data);
};

namespace {
bool sBigModels = false;
constexpr u32 kBigOpaque = 0x100000;
constexpr u32 kBigXlu = 0x80000;
}

void xd_layers_set_big_models(bool on) { sBigModels = on; }

void PortXD_FloorLayerConfig::Setup(_GSgfxLayerData* data) {
  if (sBigModels) {
    for (_GSgfxLayerData* row = data; row->id != -1; row++) {
      if (row->id != 2) continue;
      if (row->width < kBigOpaque) row->width = kBigOpaque;
      if (row->height < kBigXlu) row->height = kBigXlu;
      static bool told = false;
      if (!told) {
        told = true;
        std::fprintf(stderr, "[layers] bigger models in use: the main layer's command buffers are %u / %u bytes (the game's: 0x50000 each)\n", row->width, row->height);
      }
    }
  }
  GSgfxLayerConfig::Setup(data);
}
