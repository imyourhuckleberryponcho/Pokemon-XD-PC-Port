#ifndef LAUNCHER_MODTOOLS_H
#define LAUNCHER_MODTOOLS_H

#include "launcher_modupdate.h"

#include <modcheck.h>
#include <modpack.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace launcher::modtools {

struct Host {
  std::filesystem::path modsFolder;
  const engine::modcheck::GameRules* rules = nullptr;
  const std::vector<engine::mods::ModFolder>* found = nullptr;
  std::function<std::string()> discPath;
  std::function<bool()> discBusy;
  std::function<bool(const std::string& disc, engine::mods::LoadOptions& load, std::string& error)> openDisc;
  std::function<void()> closeDisc;
  std::function<void(const std::string& proposedName, std::function<void(const std::string& path)> done)> chooseZip;
  std::function<void(const std::filesystem::path&)> openFolder;
  std::function<void(const std::string&)> turnOn;
  std::function<void(const std::string&)> say;
  std::function<void(const std::string&)> log;
  std::filesystem::path scratchFolder;
};

class Tools {
 public:
  Tools() = default;
  ~Tools();
  void Init(const Host& host);
  void Poll();

  void StartCheck(const std::string& id);
  void StartPack(const std::string& id);
  void PackTo(const std::string& id, const std::string& zipPath);
  void BeforeOn(const std::string& id);
  bool Busy() const { return busy_.load(); }
  bool WaitPending() const;
  void DoStep(const std::string& verb, const std::string& arg, bool& ok);
  void Draw(const launcher::modupd::Ui& ui);

  struct Last {
    std::string id, verdict;
    bool problems = false, packed = false;
    std::filesystem::path zip;
  };
  Last LastResult() const;

  static std::string Verdict(const engine::modcheck::Report& report);
  static std::string DeepNote(const engine::modcheck::Report& report, bool discGiven, const std::string& discWhy);

 private:
  enum class Kind { Check, Pack, BeforeOn };
  struct Job {
    Kind kind = Kind::Check;
    std::string id, name, version, zipPath;
    std::filesystem::path folder;
  };
  struct Result {
    Job job;
    engine::modcheck::Report report;
    bool discGiven = false;
    std::string discWhy;
    bool packed = false;
    engine::modcheck::PackResult pack;
    std::string copyError;
    std::filesystem::path finalZip;
  };
  void Start(const Job& job);
  void Run(Job job);
  const engine::mods::ModFolder* Find(const std::string& id) const;
  void Log(const std::string& text) const { if (host_.log) host_.log("modtools: " + text); }

  Host host_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::atomic<bool> busy_{false};
  std::atomic<uint32_t> serial_{0};
  uint32_t seen_ = 0;
  Result result_;
  bool openWindow_ = false, windowOpen_ = false;
  int answer_ = 0;
  std::set<std::string> cleanIds_;
};

}

#endif
