#include <xd_randomizer.h>

#include <cstdio>
#include <string>

namespace er = engine::randomizer;
namespace xr = xd::randomizer;

static bool sOk = true;
static void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  if (!cond) sOk = false;
}

int main() {
  const auto& sc = xr::GetSchema();

  er::Settings custom;
  xr::ApplyStyle(custom, xr::Style::Custom);
  check(custom.Get(sc, "trainers.moves") == 1, "a fresh Custom setup starts on natural moves");
  check(custom.Get(sc, "trainers") == 0 && custom.Get(sc, "wild") == 0, "...with everything else off");

  er::Settings light, classic, chaos;
  xr::ApplyStyle(light, xr::Style::Light);
  xr::ApplyStyle(classic, xr::Style::Classic);
  xr::ApplyStyle(chaos, xr::Style::Chaos);
  check(light.Get(sc, "trainers.moves") == 0 && light.Get(sc, "trainers") == 0, "Light: trainers off, moves kept (as always)");
  check(classic.Get(sc, "trainers.moves") == 1 && chaos.Get(sc, "trainers.moves") == 1, "Classic and Chaos: natural moves (as always)");
  check(xr::StyleOf(light) == xr::Style::Light && xr::StyleOf(classic) == xr::Style::Classic && xr::StyleOf(chaos) == xr::Style::Chaos,
        "the three styles are still recognised");
  check(xr::CodeFor(xr::Style::Light, 7) == "XDR1-2622-2222-2222-23Y5-S752-6", "Light seed 7: the same code as before");
  check(xr::CodeFor(xr::Style::Classic, 7) == "XDR2-2A22-2222-2222-23ZZ-VQ24-SQ2", "Classic seed 7: the same code as before");
  check(xr::CodeFor(xr::Style::Chaos, 7) == "XDR8-3222-2222-2222-23XC-QT22-2262-MS22", "Chaos seed 7: the same code as before");

  er::Settings old;
  xr::ApplyStyle(old, xr::Style::Classic);
  old.Set(sc, "trainers.moves", 0);
  old.seed = 4242;
  const std::string code = er::EncodeCode(sc, old);
  er::Settings back;
  bool seedOnly = false;
  std::string error;
  check(er::DecodeCode(sc, code, back, seedOnly, error) && !seedOnly, "an older Custom code with kept moves is read");
  check(back.Get(sc, "trainers") == 1 && back.Get(sc, "trainers.moves") == 0 && back.values == old.values && back.seed == 4242,
        "...and still means kept moves (every value the same)");
  er::Settings onto;
  xr::ApplyStyle(onto, xr::Style::Custom);
  check(er::DecodeCode(sc, code, onto, seedOnly, error) && onto.values == old.values, "a code read onto a fresh Custom base overwrites every value");
  std::printf(sOk ? "ALL OK\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
