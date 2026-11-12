#ifndef LAUNCHER_REPORTS_H
#define LAUNCHER_REPORTS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher::reports {

struct File {
  std::string name;
  uint64_t size = 0;
  std::string description;
  bool dump = false;
  bool text = false;
};

struct Report {
  enum class Kind { Key, Crash, Stop, Stuck };
  std::filesystem::path folder;
  std::string name;
  Kind kind = Kind::Key;
  std::string when;
  std::string moment;
  std::string happened;
  uint64_t size = 0;
  std::vector<File> files;
  std::string info;
};

const char* KindName(Report::Kind kind);
const char* KindExplained(Report::Kind kind);
std::string SizeText(uint64_t bytes);

std::vector<Report> List(const std::filesystem::path& folder);
bool Read(const std::filesystem::path& folder, Report& report);

struct Identity {
  std::string userName;
  std::vector<std::string> profilePaths;
};
Identity CurrentIdentity();

std::string Scrub(const std::string& text, const Identity& who);

struct ExportOptions {
  bool hideUserName = true;
  bool includeDump = false;
};

std::string ExportName(const Report& report);
bool Export(const Report& report, const std::filesystem::path& zipPath, const ExportOptions& options,
            const Identity& who, std::string& error);
bool Delete(const Report& report, std::string& error);

struct Session {
  std::string id;
  std::string when;
  std::filesystem::path log, recording, cards;
  uint64_t logSize = 0, recordingSize = 0, cardsSize = 0;
  int cardFiles = 0;
  int64_t seconds = -1;
  uint32_t gameFrames = 0;
  bool ended = false;
  bool shortRun = false;
  std::vector<std::filesystem::path> reports;
  std::string port, snapshot;
};
constexpr uint32_t kShortFrames = 600;

std::vector<Session> ListSessions(const std::filesystem::path& logsFolder,
                                  const std::filesystem::path& recordingsFolder,
                                  const std::filesystem::path& reportsFolder);
std::string DurationText(int64_t seconds);

struct BundleFile {
  std::string name;
  std::filesystem::path from;
  uint64_t size = 0;
  std::string description;
  bool text = false;
};
struct SessionBundle {
  std::vector<BundleFile> files;
  uint64_t size = 0;
  std::string choice;
  bool withPrevious = false;
};
SessionBundle BundleFor(const std::vector<Session>& sessions, size_t index, const std::filesystem::path& settingsIni,
                        const std::filesystem::path& modsIni);
std::string SessionExportName(const Session& session);
bool ExportSession(const std::vector<Session>& sessions, size_t index, const SessionBundle& bundle,
                   const std::filesystem::path& zipPath, bool hideUserName, const std::string& version,
                   const Identity& who, std::string& error);

}

#endif
