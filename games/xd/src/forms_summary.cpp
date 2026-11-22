#include "xd_forms.h"

#include <imgui.h>
#include <i18n.h>
#include "ui_kit2.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

extern "C" {
void menuModelLoadPokemon(void* model, void* pokemon);
void menuModelTerm(void* model);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
void PortVideo_SetFrameDrawer(void (*draw)());
void PortMenu_PictureRect(float* x, float* y, float* scale);
float PortXD_GameTextWidth(const char* text, float scale);
void PortXD_GameText(float x, float y, const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale);
}

namespace {
std::atomic<bool> sAlolan{false};

void Draw() {
  if (!sAlolan) return;
  float x0, y0, s;
  PortMenu_PictureRect(&x0, &y0, &s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  auto P = [&](float x, float y) { return ImVec2(x0 + x * s, y0 + y * s); };
  const char* text = engine::i18n::Tr("ALOLAN");
  const float scale = 0.62f;
  const float tw = PortXD_GameTextWidth(text, scale);
  const float x = 44.f, y = 282.f, w = tw + 26.f, h = std::max(18.f, w / 4.5f);
  xd::kit2::DrawPiece(dl, P(x, y), P(x + w, y + h), xd::kit2::Piece{0x17BF, 2, 41, 144, 32});
  PortXD_GameText(x + 13.f, y + 1.f, text, 255, 255, 255, 255, scale);
}
struct Register {
  Register() { PortVideo_SetFrameDrawer(&Draw); }
} sRegister;
}

extern "C" void PortXD_SummaryModelLoad(void* model, void* pokemon) {
  bool alolan = false;
  if (xd::forms::Loaded() && pokemon != nullptr) {
    const uint32_t row = pokemonBiosGetPokemonDataId(pokemon);
    for (const auto& f : xd::forms::All()) {
      if (f.kind == xd::forms::Kind::Alola && f.row == row) alolan = true;
    }
  }
  sAlolan = alolan;
  menuModelLoadPokemon(model, pokemon);
}

extern "C" void PortXD_SummaryModelTerm(void* model) {
  sAlolan = false;
  menuModelTerm(model);
}
