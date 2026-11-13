#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "launcher_icon.h"

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
};

struct GameInfo {
  std::string title;
  std::string windowTitle;
  std::string subtitle;
  std::string gameId;
  std::string discHelp;
  std::filesystem::path settingsFolder;
  std::filesystem::path modsFolder;
  std::filesystem::path modListFile;
  std::filesystem::path program;
  std::string script;
  bool offscreen = false;
  bool playerFilesReadOnly = false;
  std::string readOnlyNote;

  std::function<void(engine::settings::Registry&)> registerSettings;
  std::function<DiscVerdict(const std::string& path, const std::function<bool(uint64_t, uint64_t)>& progress)>
      checkDisc;
  std::function<SaveSummary(const std::string& discPath)> readSave;
  engine::mods::LooksOrSound modLooks;

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
  };
  struct ImportCheck {
    std::string name;
    std::filesystem::path cardFolder;
    std::string cardFileName;
  };
  struct Saves {
    std::filesystem::path dataFolder;
    std::function<void()> changed;
    std::function<SaveRow(const std::filesystem::path& cardRoot)> row;
    std::function<SaveSummary(const std::filesystem::path& cardRoot, const std::string& discPath)> read;
    std::function<std::filesystem::path(const std::filesystem::path& cardRoot, const std::string& card)> cardFolder;
    std::function<std::filesystem::path(const std::filesystem::path& cardRoot)> saveFile;
    std::function<bool(const std::filesystem::path& file, ImportCheck& out, std::string& error)> checkImport;
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
    std::string converterStem;
    std::string converterFolder;
    std::string converterOverrideEnv;
    std::vector<std::string> converterArguments;
  };
  struct ExtraAssets {
    std::vector<ExtraSource> sources;
    std::function<bool(const std::string& path, std::string& gameId, std::string& title, std::string& error)>
        identifyDisc;
  } extra;
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
