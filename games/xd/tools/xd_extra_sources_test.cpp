#include "../launcher/xd_extra_sources.h"
#include "../src/xd_battle_models.h"
#include "../src/xd_settings.h"

#include <settings.h>

#include <cstdio>
#include <set>
#include <string>

namespace es = engine::settings;

namespace {

bool sOk = true;

void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

}

int main() {
  es::Registry r;
  r.SetWarningSink([](const std::string& m) { std::printf("     warning: %s\n", m.c_str()); });
  xd_settings::Register(r);

  const auto& sources = xd::ExtraSources();
  check(sources.size() == 1, "one source today: Pokemon Battle Revolution");
  std::set<std::string> ids;
  for (const auto& s : sources) {
    const std::string who = "source " + s.id + ": ";
    check(!s.id.empty() && ids.insert(s.id).second, who + "a unique id");
    check(!s.name.empty() && !s.provides.empty() && !s.how.empty(), who + "name, what it provides and how it is made are said");
    check(!s.gameIds.empty(), who + "accepts at least one game ID");
    for (const std::string& g : s.gameIds) check(g.size() == 6, who + "game ID " + g + " has six characters");
    check(!s.converterStem.empty() && !s.converterArguments.empty(), who + "has a converter and its arguments");
    bool disc = false, game = false, out = false;
    for (const std::string& a : s.converterArguments) {
      disc |= a.find("{disc}") != std::string::npos;
      game |= a.find("{game_disc}") != std::string::npos;
      out |= a.find("{out}") != std::string::npos;
    }
    check(disc && game && out, who + "the arguments name the extra disc, the player's disc and the folder to make");
    const size_t i = r.Find(s.discSetting);
    check(i != es::kNoSetting && r.Get(i).type == es::Type::Text && r.Get(i).preference,
          who + "its disc path is a text preference setting (" + s.discSetting + ")");
    check(s.discSetting.rfind("system.extra_disc.", 0) == 0, who + "the disc setting is under system.extra_disc.");
    const size_t sw = r.Find(s.switchSetting);
    check(sw != es::kNoSetting && r.Get(sw).type == es::Type::Bool, who + "its switch is a Bool setting");
    check(!s.manifestId.empty() && engine::mods::ValidModId(s.manifestId), who + "the description file's id is valid");
  }
  const auto& pbr = sources[0];
  check(pbr.id == "pbr" && pbr.gameIds.size() == 1 && pbr.gameIds[0] == "RPBE01", "Battle Revolution: id pbr, game ID RPBE01 (USA)");
  check(pbr.manifestId == "battle-revolution-models" && pbr.manifestId == xd::battle_models::kManifestId &&
            std::string(xd::battle_models::kLayerId) == pbr.id,
        "its folder is extra_assets\pbr (the layer id is the source id), description id battle-revolution-models");
  check(pbr.converterFolder == "pbr_models" && pbr.converterStem == "pbr_models", "converter: <game folder>\\pbr_models\\pbr_models");

  const size_t i = r.Find(xd::battle_models::kSetting);
  check(i != es::kNoSetting && r.Get(i).implemented && !r.Get(i).preference, "the setting exists, works and is not a preference");
  check(r.DefaultOf(i, es::Preset::Original).i == 0 && r.DefaultOf(i, es::Preset::Enhanced).i == 0, "off in Original and in Enhanced");
  check(r.Get(i).description.find("next time the game starts") != std::string::npos,
        "its text says it acts from the next start");
  check(!r.SetBool(xd::battle_models::kSetting, true) || r.CurrentPreset() != es::Preset::Original,
        "turning it on leaves Original (it is a change from the GameCube game)");

  std::printf(sOk ? "xd_extra_sources_test: all passed\n" : "xd_extra_sources_test: FAILED\n");
  return sOk ? 0 : 1;
}
