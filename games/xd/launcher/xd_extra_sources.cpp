#include "xd_extra_sources.h"

#include "xd_battle_models.h"
#include "xd_n3ds_dump.h"
#include "xd_languages.h"
#include "pack_match.h"

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
    pbr.needsPlainGameDisc = true;
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
    pbr.tab = "models";
    pbr.tabOrder = 1;
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
    colo.tab = "models";
    colo.tabOrder = 2;
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
    languages.tab = "languages";
    languages.tabOrder = 0;
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
    japanese.tab = "languages";
    japanese.tabOrder = 1;
    v.push_back(japanese);
    launcher::GameInfo::ExtraSource usum;
    usum.id = xd::battle_models::kUsumLayerId;
    usum.name = "Pokemon Ultra Sun / Ultra Moon (3DS)";
    usum.gameIds = {"CTR-P-A2A", "CTR-P-A2B"};
    usum.dump = true;
    usum.needsPlainGameDisc = true;
    usum.identify = IdentifyN3dsDump;
    usum.provides = Tr("Ultra Sun / Ultra Moon models, Gen 4 and Gen 5-7 Pokemon");
    usum.makeLabel = Tr("Make models and Gen 4 + Gen 5-7 packs");
    usum.how = Tr("The program usum_models, which ships with the game, converts the Pokemon models in your own Ultra Sun or "
                  "Ultra Moon dump into XD's model format, in the folder extra_assets\\usum in your data folder. The dump must be a "
                  "decrypted copy: a cartridge dump (.3ds or .cci) or a digital copy (.cia). An encrypted copy, an update or DLC .cia, or a folder can't be used. Your "
                  "XD disc (the Disc page) is needed too: it says which models XD has. The models and the Gen 4 pack take about ten minutes; "
                  "the Gen 5, 6 and 7 Pokemon (models, forms, moves, abilities, cries, faces and Pokedex pictures) take much longer, but they are "
                  "made in the background: you can keep using the launcher or play meanwhile. "
                  "Afterwards pick \"Models: Ultra Sun / Ultra Moon\" or the Gen 4 / Gen 5-7 choices in a New Game's setup.");
    usum.discSetting = "system.extra_disc.usum";
    usum.manifestId = "ultra-sun-moon-models";
    usum.converterStem = "usum_models";
    usum.converterFolder = "usum_models";
    usum.converterOverrideEnv = "XD_USUM_MODELS_EXE";
    usum.converterTestStem = "usum_models_cpp";
    usum.converterTestEnv = "XD_USUM_CPP";
    usum.converterArguments = {"--rom", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}", "--gen4-pack", "{out2}",
                               "--gen57-pack", "{out3}", "--xd-exe", "{game_program}", "--data-folder", "{data}"};
    usum.secondFolder = "usum_gen4";
    usum.secondManifestFile = "gen4.json";
    usum.secondManifestId = "usum-gen4";
    usum.secondProvides = Tr("Gen 4 Pokemon");
    usum.secondVersion = 2;
    usum.thirdFolder = "usum_gen57";
    usum.thirdManifestFile = "gen57.json";
    usum.thirdManifestId = "ultra-sun-moon-gen57";
    usum.thirdProvides = Tr("Gen 5-7 Pokemon");
    usum.thirdVersion = 1;
    usum.thirdWarning = Tr("There are a lot of assets to unpack and convert for the Gen 5, 6 and 7 Pokemon. This may take a while "
                           "(often 45 minutes to 1.5 hours, longer on slower PCs). You can keep using the launcher or play while it works; "
                           "the launcher stays open until it is done. If you close it, it continues where it stopped next time.");
    usum.thirdCheck = [](const std::filesystem::path& assets) -> std::pair<int, std::string> {
      switch (xd::packmatch::CheckGen57(assets)) {
        case xd::packmatch::Problem::LegacyOtherGen4:
          return {1, Tr("These Gen 5-7 files were made together with the Gen 4 files of the Ultra Sun / Ultra Moon dump, so the game uses those "
                        "Gen 4 files whenever Gen 5-7 Pokemon are on. They still work; make them again to use the Battle Revolution Gen 4 files "
                        "with them too.")};
        case xd::packmatch::Problem::LegacyGen4Gone:
          return {2, Tr("These Gen 5-7 files were made together with Gen 4 files that are not installed any more, so the game will not use "
                        "them. Make the Ultra Sun / Ultra Moon files again.")};
        case xd::packmatch::Problem::TocDiffers:
        case xd::packmatch::Problem::TooManyCries:
          return {2, Tr("These Gen 5-7 files do not fit the Gen 4 files that are installed now, so the game will not use them. Make the "
                        "Ultra Sun / Ultra Moon files again.")};
        default: break;
      }
      return {0, std::string()};
    };
    usum.doneText = Tr("Pick \"Models: Ultra Sun / Ultra Moon\" in the New Game setup to play with them.");
    usum.tab = "models";
    usum.tabOrder = 0;
    v.push_back(usum);
    return v;
  }();
  return sources;
}

}
