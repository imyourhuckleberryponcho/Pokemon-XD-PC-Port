#ifndef LAUNCHER_MODUPDATE_H
#define LAUNCHER_MODUPDATE_H

#include <modupdate.h>

#include <imgui.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher::modupd {

struct Host {
  std::filesystem::path modsFolder, stateFolder;
  const engine::modcheck::GameRules* rules = nullptr;
  bool testRun = false;
  engine::mods::ModList* mods = nullptr;
  const std::vector<engine::mods::ModFolder>* found = nullptr;
  std::function<std::string()> discGameId;
  std::function<std::vector<std::pair<std::string, std::filesystem::path>>()> cardRoots;
  std::function<void()> saveList;
  std::function<void()> reload;
  std::function<void(const std::string&)> log;
  std::function<void(const std::filesystem::path& file, bool save, std::function<void(const std::string& path)> done)> chooseFile;
  std::function<void(const std::string&)> say;
};

struct Ui {
  float scale = 1;
  ImVec4 good, warn, bad, dim;
  std::function<void(const char*)> heading;
  std::function<void(const std::string&)> dimText;
  std::function<bool(const char*)> primary;
  std::function<bool()> back;
  std::function<bool()> closeNow;
};

class Updater {
 public:
  Updater() = default;
  ~Updater();
  void Init(const Host& host);
  void Poll();

  void RequestCheck(bool manual);
  void OnStart();
  bool Busy() const { return busy_.load(); }
  bool WaitPending() const;
  bool Offers(const std::string& id, std::string& version, std::string* note = nullptr) const;
  engine::modupdate::PreviousInfo Previous(const std::string& id) const;
  void AskRollBack(const std::string& id);
  void BeginTurnOn(const std::string& id);
  void SortList(bool say);
  void ExportList(const std::string& file);
  void ImportList(const std::string& file);
  void StartExport();
  void StartImport();
  void DoStep(const std::string& verb, const std::string& arg, bool& ok);

  void Draw(const Ui& ui);

 private:
  enum class Phase { Idle, Checking, Results, Downloading, Staged, Failed };
  struct Item {
    std::string id, name, installedVersion;
    std::filesystem::path folder;
    engine::modupdate::Offer offer;
    std::string authorKey;
    bool isNew = false;
    bool keyConfirmed = false;
  };
  static bool Allowed(const Item& it) {
    return it.offer.trust.trust != engine::modupdate::Trust::Refused &&
           (it.offer.trust.trust != engine::modupdate::Trust::NeedsConfirm || it.keyConfirmed);
  }
  void StartCheck();
  void StartFetchNew(const std::string& id, const std::string& link);
  void StartDownload(size_t index);
  void FinishInstall(bool keepOld);
  void TurnOnNow(const std::string& id, const std::vector<std::string>& more);
  void NextImportDownload();
  void RePlan();
  void Log(const std::string& text) const { if (host_.log) host_.log("modupdates: " + text); }
  void SaveChoices();
  engine::modupdate::Folders Folders() const;

  Host host_;
  engine::modupdate::Choices choices_;
  engine::modupdate::KeyStore keys_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::atomic<bool> busy_{false}, stop_{false};
  std::atomic<uint64_t> done_{0}, total_{0};
  std::atomic<uint32_t> serial_{0};
  uint32_t seen_ = 0;
  Phase phase_ = Phase::Idle;
  std::vector<Item> items_;
  std::vector<std::string> notes_;
  size_t current_ = 0;
  engine::modupdate::Staged staged_;
  std::string failure_;
  engine::modupdate::Change change_;
  std::vector<engine::modupdate::Playthrough> users_;
  bool openQuestion_ = false, openResults_ = false, openStaged_ = false, openRollback_ = false, openNeeds_ = false, openImport_ = false,
       openFailed_ = false;
  std::string rollbackId_;
  std::string installNote_;
  struct Needs {
    std::string id, name;
    engine::mods::Requirements req;
    std::vector<std::string> turnOnIds;
  } needs_;
  std::string pendingTurnOn_;
  engine::modupdate::SharedList list_;
  engine::modupdate::ImportPlan plan_;
  std::vector<size_t> importQueue_;
  bool importing_ = false;
  bool closeQuestion_ = false, closeAll_ = false, skipAnswer_ = false;
  int pendingGet_ = -1, installAnswer_ = 0, rollbackAnswer_ = 0, needsAnswer_ = 0, importAnswer_ = 0;
  bool manualCheck_ = true;
  bool checkAfterAnswer_ = false;
};

}

#endif
