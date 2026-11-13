#include "xd_extra_sources.h"

#include "xd_battle_models.h"

namespace xd {

const std::vector<launcher::GameInfo::ExtraSource>& ExtraSources() {
  static const std::vector<launcher::GameInfo::ExtraSource> sources = [] {
    std::vector<launcher::GameInfo::ExtraSource> v;
    launcher::GameInfo::ExtraSource pbr;
    pbr.id = "pbr";
    pbr.name = "Pokemon Battle Revolution (USA)";
    pbr.gameIds = {"RPBE01"};
    pbr.provides = "Battle Revolution battle models";
    pbr.makeLabel = "Make models";
    pbr.how = "The program pbr_models, which ships with the game, converts the Pokemon models on that disc into "
              "extra assets (the folder extra_assets\\pbr in your data folder) made here on your PC; XD keeps its own "
              "model for any Pokemon without one. They are built in, not a mod.";
    pbr.discSetting = "system.extra_disc.pbr";
    pbr.manifestId = xd::battle_models::kManifestId;
    pbr.switchSetting = xd::battle_models::kSetting;
    pbr.converterStem = "pbr_models";
    pbr.converterFolder = "pbr_models";
    pbr.converterOverrideEnv = "XD_PBR_MODELS_EXE";
    pbr.converterArguments = {"--pbr-disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}"};
    v.push_back(pbr);
    return v;
  }();
  return sources;
}

}
