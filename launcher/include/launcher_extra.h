#pragma once

#include "launcher.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace launcher::extra {

using Source = GameInfo::ExtraSource;

struct Line {
  enum class Kind { None, Progress, Done, Error } kind = Kind::None;
  uint64_t done = 0, total = 0;
  std::string name;
  uint64_t converted = 0, keptOriginal = 0, totalBytes = 0;
  std::string message;
};
Line ParseLine(std::string_view line);

std::string ParseVersionOutput(std::string_view output);

const Source* FindSource(const std::vector<Source>& sources, std::string_view id);
bool AcceptsGameId(const Source& source, std::string_view gameId);
std::filesystem::path ConverterPath(const Source& source, const std::filesystem::path& programFolder);
std::vector<std::string> ConverterArguments(const Source& source, const std::string& disc, const std::string& gameDisc,
                                            const std::filesystem::path& out, const std::filesystem::path& out2 = {});

struct Status {
  enum class Kind { NotSetUp, Ready, OutOfDate, ConverterMissing } kind = Kind::NotSetUp;
  int models = 0;
  std::string version;
  std::string text;
  std::string detail;
};
Status ComputeStatus(const std::filesystem::path& modFolder, const std::string& expectedModId, bool converterFound,
                     const std::string& converterVersion);

struct SecondStatus {
  enum class Kind { NotMade, Ready, OutOfDate } kind = Kind::NotMade;
  std::string text, detail;
  int species = 0;
};
SecondStatus ComputeSecondStatus(const std::filesystem::path& folder, const std::string& manifestFile, const std::string& expectedId,
                                 int wantedVersion, const std::string& provides, bool firstOutOfDate);

std::string QueryVersion(const std::filesystem::path& converter, const std::filesystem::path& logFile, int waitMs);

class DiscCheck {
 public:
  enum class State { None, Checking, Good, WrongGame, Unreadable };
  ~DiscCheck();
  void Start(const Source& source, const std::string& path, const GameInfo::ExtraAssets& assets);
  void Clear();
  State state() const { return state_.load(); }
  std::string message() const;
  std::string forPath() const;

 private:
  void Join();
  std::thread thread_;
  std::atomic<State> state_{State::None};
  mutable std::mutex mutex_;
  std::string message_, path_;
};

struct MakeRequest {
  std::filesystem::path converter;
  std::vector<std::string> arguments;
  std::filesystem::path outFolder;
  std::filesystem::path outFolder2;
  std::filesystem::path logFile;
};

class MakeJob {
 public:
  enum class Phase { Idle, Running, Done, Failed, WrongDisc, Cancelled };
  struct Snapshot {
    Phase phase = Phase::Idle;
    uint64_t done = 0, total = 0;
    std::string current;
    uint64_t converted = 0, keptOriginal = 0, totalBytes = 0;
    std::string message;
  };
  ~MakeJob();
  bool Start(const MakeRequest& request, std::string& error);
  void Cancel();
  bool Running() const { return running_.load(); }
  Snapshot Get() const;

 private:
  void Run(MakeRequest request, void* process);
  std::thread thread_;
  std::atomic<bool> running_{false}, cancel_{false};
  mutable std::mutex mutex_;
  Snapshot snap_;
};

void RemoveLeftovers(const std::filesystem::path& outFolder);

}
