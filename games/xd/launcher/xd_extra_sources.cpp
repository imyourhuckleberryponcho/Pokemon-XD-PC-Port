#include "xd_extra_sources.h"

#include "xd_battle_models.h"
#include "xd_languages.h"

namespace xd {

namespace {
const char* Tr(const char* english) { return english; }
}

const std::vector<launcher::GameInfo::ExtraSource>& ExtraSources() {
  static const std::vector<launcher::GameInfo::ExtraSource> sources = [] {
    std::vector<launcher::GameInfo::ExtraSource> v;
    launcher::GameInfo::ExtraSource pbr;
    pbr.id = "pbr";
    pbr.name = "Pokemon Battle Revolution (USA)";
    pbr.gameIds = {"RPBE01"};
    pbr.provides = Tr("Battle Revolution battle models and Gen 4 Pokemon");
    pbr.makeLabel = Tr("Make models and Gen 4 pack");
    pbr.how = Tr("The program pbr_models, which ships with the game, converts the Pokemon models on that disc into "
                 "extra assets (the folder extra_assets\\pbr in your data folder) made here on your PC; XD keeps its own "
                 "model for any Pokemon without one. The same Make also builds the Gen 4 pack (the folder extra_assets\\pbr_gen4: "
                 "the Diamond/Pearl Pokemon with their models, cries, faces, moves and stats) that the New Game choices "
                 "\"Gen 4 Pokemon\" and \"Gen 4 rules\" use. They are built in, not a mod.");
    pbr.discSetting = "system.extra_disc.pbr";
    pbr.manifestId = xd::battle_models::kManifestId;
    pbr.switchSetting = xd::battle_models::kSetting;
    pbr.converterStem = "pbr_models";
    pbr.converterFolder = "pbr_models";
    pbr.converterOverrideEnv = "XD_PBR_MODELS_EXE";
    pbr.converterArguments = {"--pbr-disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}", "--gen4-pack", "{out2}"};
    pbr.secondFolder = "pbr_gen4";
    pbr.secondManifestFile = "gen4.json";
    pbr.secondManifestId = "battle-revolution-gen4";
    pbr.secondProvides = Tr("Gen 4 Pokemon");
    pbr.secondVersion = 2;
    v.push_back(pbr);

    launcher::GameInfo::ExtraSource colo;
    colo.id = "colosseum";
    colo.name = "Pokemon Colosseum (USA)";
    colo.gameIds = {"GC6E01"};
    colo.provides = Tr("Colosseum characters");
    colo.makeLabel = Tr("Make characters");
    colo.how = Tr("The program colo_models, which ships with the game, converts the trainer models on that disc (Wes, Rui and the other "
               "Colosseum characters) into XD's model format, in the folder extra_assets\\colosseum in your data folder. Only the "
               "Colosseum disc is needed. Nothing in the game uses them on their own yet: they are the characters the online "
               "battle's trainer card can pick from.");
    colo.discSetting = "system.extra_disc.colosseum";
    colo.manifestId = "colosseum-characters";
    colo.modelsFolder = "models";
    colo.contentsFile = "characters.json";
    colo.needsGameDisc = false;
    colo.converterStem = "colo_models";
    colo.converterFolder = "pbr_models";
    colo.converterOverrideEnv = "XD_COLO_MODELS_EXE";
    colo.converterArguments = {"--disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}"};
    v.push_back(colo);
    launcher::GameInfo::ExtraSource languages;
    languages.id = xd::languages::kFolderName;
    languages.name = "Pokemon XD (European, PAL)";
    languages.gameIds = {xd::languages::kPalGameId};
    languages.provides = Tr("Languages (from the European disc)");
    languages.makeLabel = Tr("Make the language pack");
    languages.how = Tr("The program xd_languages, which ships with the game, copies the German, French, Italian and Spanish texts, "
                       "menus and pictures from that disc into the folder extra_assets\\languages in your data folder; "
                       "the game itself stays the USA game and only its text and pictures change. Your USA copy (the Disc page) "
                       "is needed too: the game's code comes from it.");
    languages.discSetting = "system.extra_disc.languages";
    languages.manifestId = xd::languages::kManifestId;
    languages.converterStem = "xd_languages";
    languages.converterFolder = "xd_languages";
    languages.converterOverrideEnv = "XD_LANGUAGES_EXE";
    languages.converterArguments = {"--pal-disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}"};
    languages.doneText = Tr("Choose the language on the Play bar (or in the game's SETTINGS): it applies the next time the game starts.");
    v.push_back(languages);
    launcher::GameInfo::ExtraSource japanese;
    japanese.id = xd::languages::kJpFolderName;
    japanese.name = "Pokemon XD (Japanese)";
    japanese.gameIds = {xd::languages::kJpGameId};
    japanese.provides = Tr("Japanese (from the Japanese disc)");
    japanese.makeLabel = Tr("Make the Japanese pack");
    japanese.how = Tr("The program xd_languages, which ships with the game, copies the Japanese texts, fonts and pictures with words "
                      "from that disc into the folder extra_assets\\languages_jp in your data folder; the game itself stays the USA "
                      "game and only its text and pictures change. Your USA copy (the Disc page) is needed too: the game's code comes "
                      "from it.");
    japanese.discSetting = "system.extra_disc.languages_jp";
    japanese.manifestId = xd::languages::kJpManifestId;
    japanese.versionArgument = "jp";
    japanese.converterStem = "xd_languages";
    japanese.converterFolder = "xd_languages";
    japanese.converterOverrideEnv = "XD_LANGUAGES_EXE";
    japanese.converterArguments = {"--jp-disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}"};
    japanese.doneText = Tr("Choose Japanese on the Play bar (or in the game's SETTINGS): it applies the next time the game starts.");
    v.push_back(japanese);
    return v;
  }();
  return sources;
}

}
