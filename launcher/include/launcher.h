#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "launcher_icon.h"

namespace engine::modcheck {
struct GameRules;
}
#include <mods.h>
#include <settings.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace launcher {

struct DiscVerdict {
  enum class Kind { Good, Usable, Warning, Problem };
  Kind kind = Kind::Problem;
  std::string text;
  std::string gameId;
  std::string title;
  std::string edition;
};

struct SaveSummary {
  enum class State { None, Found, Unreadable };
  State state = State::None;
  std::string where;
  std::string message;
  std::vector<std::pair<std::string, std::string>> facts;
  std::vector<std::string> partyColumns;
  std::vector<std::vector<std::string>> party;
  std::vector<std::string> notes;
  std::vector<std::pair<std::string, std::string>> setup;
  std::vector<std::string> setupNotes;
  std::string shareCode;
  std::filesystem::path spoilerFile;
  std::filesystem::path setupFile;
  std::string looksStyle;
  std::vector<std::pair<std::string, std::string>> looksStyles;
};

struct GameInfo {
  std::string title;
  std::string windowTitle;
  std::string subtitle;
  std::string gameId;
  std::string textureGameId;
  std::string discHelp;
  std::string discOverride;
  std::vector<std::string> restartArguments;
  std::filesystem::path settingsFolder;
  std::filesystem::path modsFolder;
  std::filesystem::path modListFile;
  std::filesystem::path program;
  std::string script;
  bool offscreen = false;
  bool playerFilesReadOnly = false;
  std::string readOnlyNote;

  struct Presence {
    std::string clientId;
    std::string endpoint;
    std::string largeImage, largeText;
    std::function<std::string()> details;
    std::string state;
    bool allowed = true;
  } presence;

  std::function<void(engine::settings::Registry&)> registerSettings;
  std::function<DiscVerdict(const std::string& path, const std::function<bool(uint64_t, uint64_t)>& progress)>
      checkDisc;
  std::function<SaveSummary(const std::string& discPath)> readSave;
  std::function<bool(const std::filesystem::path& setupFile, const std::string& style, std::string& error)> changeLooks;
  engine::mods::LooksOrSound modLooks;
  const engine::modcheck::GameRules* modRules = nullptr;
  std::function<bool(const std::string& discPath, engine::mods::LoadOptions& load, std::string& error)> openModDisc;
  std::function<void()> closeModDisc;

  struct DiscPicture {
    std::string id;
    std::string name;
    icon::Image image;
    icon::Image forSmall;
    bool background = false;
  };
  struct OwnIcon {
    std::string id, name;
    int resource = 1;
  };
  std::vector<OwnIcon> ownIcons;
  std::filesystem::path backgroundFolder;
  std::function<bool(const std::string& discPath, std::vector<DiscPicture>& pictures, icon::Image& banner,
                     std::string& error)>
      readDiscPictures;
  std::function<bool(const std::string& discPath, const std::string& discId, const std::function<void()>& starting,
                     const std::function<bool()>& stop, std::string& error)>
      renderPictures;
  std::function<bool(const std::string& discPath, const std::function<void(uint32_t, uint32_t)>& progress,
                     const std::function<bool()>& stop, std::string& error)>
      prepareShaders;
  std::filesystem::path iconFolder;
  std::filesystem::path shortcutTarget;
  std::string appId;
  std::filesystem::path shortcutFolder;

  std::filesystem::path reportsFolder;
  std::filesystem::path logsFolder, recordingsFolder;
  std::filesystem::path saveBackupsFolder;
  std::function<std::filesystem::path(const std::string& card)> cardFolder;
  struct SaveRow {
    bool found = false;
    std::string who;
    std::string time;
    std::string setup;
    std::string edition;
    std::string warn;
  };
  struct ImportCheck {
    std::string name;
    std::filesystem::path cardFolder;
    std::string cardFileName;
    std::vector<uint8_t> gci;
    SaveSummary preview;
    bool editionUnknown = false;
  };
  struct ImportOffer {
    std::filesystem::path path;
    std::string where;
    ImportCheck check;
  };
  struct Saves {
    std::filesystem::path dataFolder;
    std::function<std::filesystem::path(const std::string& edition)> dataFolderFor;
    std::function<void(const std::string& edition)> setEdition;
    std::function<void()> changed;
    std::string editionQuestion;
    std::vector<std::string> editionAnswers;
    std::function<std::string(int answer)> editionRefusal;
    std::function<std::string(const std::filesystem::path& gciFile)> wrongEdition;
    std::function<SaveRow(const std::filesystem::path& cardRoot)> row;
    std::function<SaveSummary(const std::filesystem::path& cardRoot, const std::string& discPath)> read;
    std::function<std::filesystem::path(const std::filesystem::path& cardRoot, const std::string& card)> cardFolder;
    std::function<std::filesystem::path(const std::filesystem::path& cardRoot)> saveFile;
    std::function<bool(const std::filesystem::path& file, const std::string& discPath, ImportCheck& out,
                       std::string& error)>
        checkImport;
    std::function<std::vector<ImportOffer>(const std::string& discPath)> findImportable;
    struct TradeMon {
      std::string species, nickname, level, item;
      std::string problem;
    };
    struct TradeView {
      std::string mineWho, theirsWho;
      std::vector<TradeMon> mine, theirs;
      std::vector<std::string> notes;
    };
    struct TradeResult {
      bool ok = false;
      std::string message;
    };
    std::function<bool(const std::filesystem::path& cardRoot, const std::filesystem::path& file,
                       const std::string& discPath, TradeView& out, std::string& error)>
        tradeOpen;
    std::function<TradeResult(const std::filesystem::path& cardRoot, const std::filesystem::path& file,
                              const std::string& discPath, int mineSlot, int theirSlot)>
        tradeDo;
    struct MonRow {
      std::string where, species, nickname, level;
      std::string problem;
      std::vector<std::string> lost;
    };
    struct MonsView {
      std::string who;
      std::vector<MonRow> rows;
      std::vector<std::string> notes;
      int room = 0;
    };
    std::function<bool(const std::filesystem::path& cardRoot, const std::filesystem::path& file, const std::string& discPath,
                       MonsView& out, std::string& error)>
        monsOpen;
    std::function<TradeResult(const std::filesystem::path& cardRoot, const std::filesystem::path& file,
                              const std::string& discPath, const std::vector<int>& rows)>
        monsDo;
    std::string monsName = "Colosseum import";
    std::function<bool(const std::filesystem::path& cardRoot, const std::filesystem::path& file, const std::string& discPath,
                       MonsView& out, std::string& error)>
        monsOpen2;
    std::function<TradeResult(const std::filesystem::path& cardRoot, const std::filesystem::path& file,
                              const std::string& discPath, const std::vector<int>& rows)>
        monsDo2;
    std::string monsName2;
    std::string monsHelp2;
    std::string monsChoose2;
    std::string monsSaveLabel2;
    std::string monsStays2;
    std::string monsCopy2;
    std::string tradeFileTypes = "sav";
    std::string tradeName = "GBA trade";
    std::string exportName = "save.gci";
    std::filesystem::path binFolder;
  } saves;
  struct ExtraSource {
    std::string id;
    std::string name;
    std::vector<std::string> gameIds;
    std::string provides;
    std::string makeLabel = "Make";
    std::string how;
    std::string discSetting;
    std::string manifestId;
    std::string switchSetting;
    std::string modelsFolder = "fsys";
    bool needsPlainGameDisc = false;
    bool needsGameDisc = true;
    std::string contentsFile;
    std::string doneText;
    bool dump = false;
    std::function<bool(const std::string& path, std::string& gameId, std::string& title, std::string& error)> identify;
    std::string tab;
    int tabOrder = 0;
    std::string converterStem;
    std::string converterFolder;
    std::string versionArgument;
    std::string converterOverrideEnv;
    std::string converterTestStem;
    std::string converterTestEnv;
    std::vector<std::string> converterArguments;
    std::string secondFolder;
    std::string secondManifestFile;
    std::string secondManifestId;
    std::string secondProvides;
    int secondVersion = 0;
    std::string thirdFolder;
    std::string thirdManifestFile;
    std::string thirdManifestId;
    std::string thirdProvides;
    int thirdVersion = 0;
    std::string thirdWarning;
    std::function<std::pair<int, std::string>(const std::filesystem::path&)> thirdCheck;
  };
  struct ExtraAssets {
    std::vector<ExtraSource> sources;
    std::function<bool(const std::string& path, std::string& gameId, std::string& title, std::string& error)>
        identifyDisc;
    std::function<bool(const std::string& gameId)> otherVersion;
  } extra;
  struct Languages {
    std::string setting;
    std::function<std::vector<std::pair<std::string, std::string>>()> offered;
    std::string noPackHint;
    std::string whenHint;
  } languages;
  struct Updates {
    std::string version;
    std::string feedUrl;
    std::string publicKey;
    std::string product;
    std::filesystem::path stagingFolder;
    std::string starterName;
    std::vector<std::string> requiredFiles;
    bool Active() const { return !feedUrl.empty() && !publicKey.empty() && !product.empty(); }
  } updates;
};

int Run(const GameInfo& game);

}

#endif
