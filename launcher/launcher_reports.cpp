#include "launcher_reports.h"

#include <port_zip.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#if defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace launcher::reports {

namespace {

namespace fs = std::filesystem;

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

#if defined(_WIN32)
std::string Utf8(const wchar_t* w) {
  if (w == nullptr || w[0] == L'\0') {
    return "";
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) {
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
  }
  return out;
}
#endif

char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string Lower(std::string s) {
  for (char& c : s) {
    c = LowerAscii(c);
  }
  return s;
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

constexpr size_t kStampEnd = 26;

bool IsReportName(const std::string& name) {
  if (name.size() < kStampEnd || name.rfind("report_", 0) != 0) {
    return false;
  }
  static const char kShape[] = "dddd-dd-dd_dd-dd-dd";
  for (size_t i = 0; i < 19; i++) {
    const char c = name[7 + i];
    if (kShape[i] == 'd' ? (c < '0' || c > '9') : c != kShape[i]) {
      return false;
    }
  }
  return name.size() == kStampEnd || name[kStampEnd] == '_';
}

bool IsTextFile(const fs::path& file) {
  const std::string ext = Lower(Utf8(file.extension()));
  return ext == ".txt" || ext == ".ini" || ext == ".log" || ext == ".json" || ext == ".csv" || ext == ".cfg" ||
         ext == ".md";
}

int Describe(File& f) {
  const std::string n = Lower(f.name);
  if (n == "info.txt") {
    f.description = "When and where it happened, and which version of the game program was running.";
    return 0;
  }
  if (n == "screenshot.png") {
    f.description = "A picture of the game window at that moment (only the game, not the rest of your screen).";
    return 1;
  }
  if (n == "recording.txt") {
    f.description = "Your button presses up to that moment, so a developer can play the same moment again.";
    return 2;
  }
  if (n == "log.txt") {
    f.description = "What the game program wrote about itself while it ran (its log).";
    return 3;
  }
  if (n == "settings.ini") {
    f.description = "Your settings.";
    return 4;
  }
  if (n == "mods.ini") {
    f.description = "Your mod list: which mods were switched on (not the mods themselves).";
    return 5;
  }
  if (Lower(Utf8(fs::path(f.name).extension())) == ".dmp") {
    f.dump = true;
    f.description = "Crash dump: a snapshot of the game program's memory at that moment. It helps a developer find "
                    "the cause of a crash, but may contain bits of whatever was in memory.";
    return 7;
  }
  f.description = "Another file the report holds.";
  return 6;
}

bool NameChar(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' ||
         c == '.' || c >= 0x80;
}

bool Separator(char c) { return c == '\\' || c == '/'; }

size_t FindNoCase(const std::string& text, const std::string& needle, size_t from) {
  if (needle.empty() || text.size() < needle.size()) {
    return std::string::npos;
  }
  for (size_t i = from; i + needle.size() <= text.size(); i++) {
    size_t k = 0;
    while (k < needle.size() && LowerAscii(text[i + k]) == LowerAscii(needle[k])) {
      k++;
    }
    if (k == needle.size()) {
      return i;
    }
  }
  return std::string::npos;
}

template <typename Fits>
std::string ReplaceWhere(const std::string& text, const std::string& needle, const std::string& with, Fits fits) {
  std::string out;
  size_t at = 0;
  for (size_t i = FindNoCase(text, needle, 0); i != std::string::npos; i = FindNoCase(text, needle, at)) {
    if (fits(i, i + needle.size())) {
      out.append(text, at, i - at);
      out += with;
      at = i + needle.size();
    } else {
      out.append(text, at, i + 1 - at);
      at = i + 1;
    }
  }
  out.append(text, at, std::string::npos);
  return out;
}

std::string ShortForm(const std::string& name) {
  std::string base;
  for (char c : name) {
    if (c != ' ' && c != '.' && base.size() < 6) {
      base += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
  }
  return base + "~";
}

}

const char* KindName(Report::Kind kind) {
  switch (kind) {
  case Report::Kind::Crash: return "Crash";
  case Report::Kind::Stop: return "Stopped";
  case Report::Kind::Stuck: return "Stuck";
  default: return "F10 report";
  }
}

const char* KindExplained(Report::Kind kind) {
  switch (kind) {
  case Report::Kind::Crash: return "The game crashed, and made this report by itself.";
  case Report::Kind::Stop: return "The game stopped itself because something went wrong, and made this report.";
  case Report::Kind::Stuck: return "The game stopped moving (it hung), and made this report by itself.";
  default: return "You pressed F10 in the game.";
  }
}

std::string SizeText(uint64_t bytes) {
  char text[32];
  if (bytes >= 1024ull * 1024) {
    std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    std::snprintf(text, sizeof text, "%llu KB", static_cast<unsigned long long>((bytes + 512) / 1024));
  } else {
    std::snprintf(text, sizeof text, "%llu bytes", static_cast<unsigned long long>(bytes));
  }
  return text;
}

bool Read(const fs::path& folder, Report& r) {
  r = Report();
  r.folder = folder;
  r.name = Utf8(folder.filename());
  std::error_code ec;
  if (!IsReportName(r.name) || !fs::is_directory(folder, ec)) {
    return false;
  }
  const std::string& n = r.name;
  r.when = n.substr(7, 10) + " " + n.substr(18, 2) + ":" + n.substr(21, 2) + ":" + n.substr(24, 2);
  const std::string rest = n.substr(kStampEnd);
  if (rest.rfind("_crash", 0) == 0) {
    r.kind = Report::Kind::Crash;
  } else if (rest.rfind("_stop", 0) == 0) {
    r.kind = Report::Kind::Stop;
  } else if (rest.rfind("_stuck", 0) == 0) {
    r.kind = Report::Kind::Stuck;
  }
  std::vector<std::pair<int, File>> files;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (!e.is_regular_file(ec)) {
      continue;
    }
    File f;
    f.name = Utf8(e.path().filename());
    f.size = e.file_size(ec);
    f.text = IsTextFile(e.path());
    const int order = Describe(f);
    r.size += f.size;
    files.emplace_back(order, std::move(f));
  }
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first < b.first : a.second.name < b.second.name;
  });
  for (auto& [order, f] : files) {
    r.files.push_back(std::move(f));
  }
  r.info = ReadText(folder / L"info.txt");
  std::string map, frame;
  std::istringstream lines(r.info);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (map.empty() && line.rfind("map ", 0) == 0) {
      map = line.substr(4);
    }
    if (r.happened.empty() && line.rfind("what happened: ", 0) == 0) {
      r.happened = line.substr(15);
    }
    const size_t gf = line.find("game frame ");
    if (frame.empty() && gf != std::string::npos) {
      size_t end = gf + 11;
      while (end < line.size() && line[end] >= '0' && line[end] <= '9') {
        end++;
      }
      if (end > gf + 11) {
        frame = line.substr(gf + 11, end - gf - 11);
      }
    }
  }
  if (!map.empty()) {
    r.moment = map.rfind("0x", 0) == 0 ? "map " + map : map;
  }
  if (!frame.empty()) {
    r.moment += (r.moment.empty() ? "game frame " : ", game frame ") + frame;
  }
  return true;
}

std::vector<Report> List(const fs::path& folder) {
  std::vector<Report> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    Report r;
    if (e.is_directory(ec) && Read(e.path(), r)) {
      out.push_back(std::move(r));
    }
  }
  std::sort(out.begin(), out.end(), [](const Report& a, const Report& b) {
    const std::string sa = a.name.substr(0, kStampEnd), sb = b.name.substr(0, kStampEnd);
    return sa != sb ? sa > sb : a.name > b.name;
  });
  return out;
}

Identity CurrentIdentity() {
  Identity who;
#if !defined(_WIN32)
  for (const char* name : {"USER", "LOGNAME"}) {
    const char* value = std::getenv(name);
    if (who.userName.empty() && value != nullptr) {
      who.userName = value;
    }
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    who.profilePaths.push_back(home);
  }
  return who;
#else
  who.userName = Utf8(_wgetenv(L"USERNAME"));
  const std::string homePath = Utf8(_wgetenv(L"HOMEPATH"));
  for (const std::string& p : {Utf8(_wgetenv(L"USERPROFILE")),
                               homePath.empty() ? std::string() : Utf8(_wgetenv(L"HOMEDRIVE")) + homePath}) {
    if (!p.empty() &&
        std::find(who.profilePaths.begin(), who.profilePaths.end(), p) == who.profilePaths.end()) {
      who.profilePaths.push_back(p);
    }
  }
  return who;
#endif
}

std::string Scrub(const std::string& text, const Identity& who) {
  std::string out = text;
  for (std::string profile : who.profilePaths) {
    while (!profile.empty() && Separator(profile.back())) {
      profile.pop_back();
    }
    if (profile.size() < 3) {
      continue;
    }
    std::string slash = profile, doubled;
    std::replace(slash.begin(), slash.end(), '\\', '/');
    for (char c : slash) {
      doubled += c == '/' ? std::string("\\\\") : std::string(1, c);
    }
    std::string back = slash;
    std::replace(back.begin(), back.end(), '/', '\\');
    for (const std::string& form : {doubled, back, slash}) {
      out = ReplaceWhere(out, form, "%USERPROFILE%", [&](size_t start, size_t end) {
        return (start == 0 || !NameChar(static_cast<unsigned char>(out[start - 1]))) &&
               (end == out.size() || !NameChar(static_cast<unsigned char>(out[end])));
      });
    }
  }
  const std::string& name = who.userName;
  if (name.empty()) {
    return out;
  }
  const auto segmentEnd = [&](size_t end) {
    return end == out.size() || !NameChar(static_cast<unsigned char>(out[end]));
  };
  out = ReplaceWhere(out, name, "%USERNAME%", [&](size_t start, size_t end) {
    return start > 0 && Separator(out[start - 1]) && segmentEnd(end);
  });
  if (name.size() > 8 || name.find_first_of(" .") != std::string::npos) {
    const std::string shortForm = ShortForm(name);
    std::string scanned;
    size_t at = 0;
    for (size_t i = FindNoCase(out, shortForm, 0); i != std::string::npos; i = FindNoCase(out, shortForm, at)) {
      size_t end = i + shortForm.size();
      while (end < out.size() && out[end] >= '0' && out[end] <= '9') {
        end++;
      }
      if (i > 0 && Separator(out[i - 1]) && end > i + shortForm.size() && segmentEnd(end)) {
        scanned.append(out, at, i - at);
        scanned += "%USERNAME%";
        at = end;
      } else {
        scanned.append(out, at, i + 1 - at);
        at = i + 1;
      }
    }
    scanned.append(out, at, std::string::npos);
    out = scanned;
  }
  std::string result;
  size_t lineStart = 0;
  while (lineStart < out.size()) {
    size_t lineEnd = out.find('\n', lineStart);
    lineEnd = lineEnd == std::string::npos ? out.size() : lineEnd + 1;
    std::string line = out.substr(lineStart, lineEnd - lineStart);
    const size_t split = line.find_first_of("=:");
    if (split != std::string::npos && split > 0) {
      const std::string value = line.substr(split + 1);
      const std::string scrubbed = ReplaceWhere(value, name, "%USERNAME%", [&](size_t start, size_t end) {
        return (start == 0 || !NameChar(static_cast<unsigned char>(value[start - 1]))) &&
               (end == value.size() || !NameChar(static_cast<unsigned char>(value[end])));
      });
      line = line.substr(0, split + 1) + scrubbed;
    }
    result += line;
    lineStart = lineEnd;
  }
  return result;
}

std::string ExportName(const Report& report) {
  return report.name.substr(0, std::min(report.name.size(), kStampEnd)) + ".zip";
}

bool Export(const Report& report, const fs::path& zipPath, const ExportOptions& options, const Identity& who,
            std::string& error) {
  std::vector<port_zip::File> files;
  for (const File& f : report.files) {
    if (f.dump && !options.includeDump) {
      continue;
    }
    const fs::path path = report.folder / fs::u8path(f.name);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      error = "Could not read " + f.name + " in the report.";
      return false;
    }
    port_zip::File z;
    z.name = f.name;
    z.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (f.text && options.hideUserName) {
      z.data = Scrub(z.data, who);
    }
    if (Lower(f.name) == "info.txt") {
      const std::string eol = z.data.find("\r\n") != std::string::npos ? "\r\n" : "\n";
      z.data += eol + "Exported from the launcher: " +
                (options.hideUserName ? "Windows user name hidden" : "Windows user name not hidden") + ", " +
                (options.includeDump ? "crash dump included" : "crash dump left out") + "." + eol;
    }
    port_zip::Time(path, &z.time, &z.date);
    files.push_back(std::move(z));
  }
  std::error_code ec;
  if (fs::equivalent(zipPath.parent_path(), report.folder, ec)) {
    error = "Please save the export somewhere outside the report's own folder.";
    return false;
  }
  if (!port_zip::Store(zipPath, files)) {
    error = "Could not write " + Utf8(zipPath) + " (is the place writable, and is there room?).";
    return false;
  }
  return true;
}

namespace {

bool ParseSessionName(const std::string& name, const char* ext, std::string& stamp, uint32_t& pid, std::string& stem) {
  const std::string e = ext;
  if (name.size() < 5 + 19 + 3 + e.size() || name.rfind("play_", 0) != 0 ||
      Lower(name.substr(name.size() - e.size())) != e) {
    return false;
  }
  stem = name.substr(0, name.size() - e.size());
  static const char kShape[] = "dddd-dd-dd_dd-dd-dd";
  for (size_t i = 0; i < 19; i++) {
    const char c = stem[5 + i];
    if (kShape[i] == 'd' ? (c < '0' || c > '9') : c != kShape[i]) {
      return false;
    }
  }
  if (stem.compare(24, 2, "_p") != 0) {
    return false;
  }
  size_t end = 26;
  while (end < stem.size() && stem[end] >= '0' && stem[end] <= '9') {
    end++;
  }
  if (end == 26 || (end < stem.size() && stem[end] != '_')) {
    return false;
  }
  stamp = stem.substr(5, 19);
  pid = static_cast<uint32_t>(std::strtoul(stem.c_str() + 26, nullptr, 10));
  return true;
}

int64_t StampTime(const std::string& s) {
  std::tm t{};
  t.tm_year = std::atoi(s.c_str()) - 1900;
  t.tm_mon = std::atoi(s.c_str() + 5) - 1;
  t.tm_mday = std::atoi(s.c_str() + 8);
  t.tm_hour = std::atoi(s.c_str() + 11);
  t.tm_min = std::atoi(s.c_str() + 14);
  t.tm_sec = std::atoi(s.c_str() + 17);
  t.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&t));
}

int64_t WriteTime(const fs::path& file) {
#if defined(__APPLE__)
  struct stat st {};
  if (::stat(file.c_str(), &st) != 0) {
    return -1;
  }
  return static_cast<int64_t>(st.st_mtime);
#else
  std::error_code ec;
  const auto t = fs::last_write_time(file, ec);
  if (ec) {
    return -1;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch())
      .count();
#endif
}

std::string ReadPart(const fs::path& file, uint64_t from, size_t count) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return "";
  }
  in.seekg(static_cast<std::streamoff>(from));
  std::string data(count, '\0');
  in.read(data.data(), static_cast<std::streamsize>(count));
  data.resize(static_cast<size_t>(in.gcount()));
  return data;
}

void ReadRecordingFacts(Session& s) {
  std::istringstream head(ReadPart(s.recording, 0, 8192));
  for (std::string line; std::getline(head, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("# port commit ", 0) == 0 && s.port.empty()) {
      s.port = line.substr(2);
    } else if (line.rfind("# decomp snapshot ", 0) == 0 && s.snapshot.empty()) {
      s.snapshot = line.substr(2);
    }
  }
  constexpr uint64_t kTail = 256 * 1024;
  const std::string tail =
      ReadPart(s.recording, s.recordingSize > kTail ? s.recordingSize - kTail : 0, static_cast<size_t>(kTail));
  for (size_t at = tail.find("frame "); at != std::string::npos; at = tail.find("frame ", at + 6)) {
    if (at > 0 && tail[at - 1] != ' ' && tail[at - 1] != '\n') {
      continue;
    }
    const char* p = tail.c_str() + at + 6;
    if (*p >= '0' && *p <= '9') {
      s.gameFrames = std::max(s.gameFrames, static_cast<uint32_t>(std::strtoul(p, nullptr, 10)));
    }
  }
  s.ended = tail.find("\nEND frame ") != std::string::npos || tail.rfind("END frame ", 0) == 0;
}

void CardFacts(Session& s) {
  std::error_code ec;
  if (!fs::is_directory(s.cards, ec)) {
    s.cards.clear();
    return;
  }
  for (auto it = fs::recursive_directory_iterator(s.cards, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      s.cardFiles++;
      s.cardsSize += it->file_size(ec);
    }
  }
}

constexpr uint64_t kLogCap = 16ull << 20;

std::string Plural(uint64_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

void AddSessionFiles(const Session& s, const std::string& prefix, const char* whose, SessionBundle& b) {
  std::error_code ec;
  if (!s.log.empty()) {
    BundleFile f;
    f.name = prefix + Utf8(s.log.filename());
    f.from = s.log;
    f.size = std::min(s.logSize, kLogCap);
    f.text = true;
    f.description = std::string("What the game program wrote about itself while ") + whose + " ran (its log)" +
                    (s.logSize > kLogCap ? ": its last 16 MB of " + SizeText(s.logSize) + "." : ".");
    b.files.push_back(std::move(f));
  }
  if (!s.recording.empty()) {
    BundleFile f;
    f.name = prefix + Utf8(s.recording.filename());
    f.from = s.recording;
    f.size = s.recordingSize;
    f.text = true;
    f.description = std::string("Your button presses in ") + whose +
                    ", so a developer can play it again exactly as it happened.";
    b.files.push_back(std::move(f));
  }
  if (!s.cards.empty()) {
    const std::string cardsName = Utf8(s.cards.filename());
    std::vector<BundleFile> cards;
    for (auto it = fs::recursive_directory_iterator(s.cards, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      std::string rel = Utf8(it->path().lexically_relative(s.cards));
      std::replace(rel.begin(), rel.end(), '\\', '/');
      BundleFile f;
      f.name = prefix + cardsName + "/" + rel;
      f.from = it->path();
      f.size = it->file_size(ec);
      f.description = rel.rfind("start-card/", 0) == 0
                          ? "Your memory card when it started (so the recording can be played from the same save)."
                          : "Your memory card after a save in it.";
      cards.push_back(std::move(f));
    }
    std::sort(cards.begin(), cards.end(), [](const BundleFile& x, const BundleFile& y) { return x.name < y.name; });
    for (BundleFile& f : cards) {
      b.files.push_back(std::move(f));
    }
  }
  for (const fs::path& folder : s.reports) {
    Report r;
    if (!Read(folder, r)) {
      continue;
    }
    for (const File& rf : r.files) {
      if (rf.dump) {
        continue;
      }
      BundleFile f;
      f.name = prefix + r.name + "/" + rf.name;
      f.from = folder / fs::u8path(rf.name);
      f.size = rf.size;
      f.text = rf.text;
      f.description = "The report " + r.when + " (" + KindName(r.kind) + "): " + rf.description;
      b.files.push_back(std::move(f));
    }
  }
}

}

std::vector<Session> ListSessions(const fs::path& logsFolder, const fs::path& recordingsFolder,
                                  const fs::path& reportsFolder) {
  struct Found {
    std::string stamp, stem;
    uint32_t pid = 0;
    int64_t time = 0;
    fs::path path;
    bool used = false;
  };
  std::vector<Found> logs, recordings;
  std::error_code ec;
  const auto collect = [&](const fs::path& folder, const char* ext, std::vector<Found>& out) {
    for (const auto& e : fs::directory_iterator(folder, ec)) {
      Found f;
      if (e.is_regular_file(ec) && ParseSessionName(Utf8(e.path().filename()), ext, f.stamp, f.pid, f.stem)) {
        f.time = StampTime(f.stamp);
        f.path = e.path();
        out.push_back(std::move(f));
      }
    }
    std::sort(out.begin(), out.end(), [](const Found& a, const Found& b) { return a.stem < b.stem; });
  };
  if (!logsFolder.empty()) {
    collect(logsFolder, ".log", logs);
  }
  if (!recordingsFolder.empty()) {
    collect(recordingsFolder, ".txt", recordings);
  }

  std::vector<Session> out;
  const auto start = [](Session& s, const Found& f) {
    s.id = f.stem.substr(5);
    s.when = f.stamp.substr(0, 10) + " " + f.stamp.substr(11, 2) + ":" + f.stamp.substr(14, 2) + ":" + f.stamp.substr(17, 2);
  };
  const auto useRecording = [&](Session& s, Found& r) {
    r.used = true;
    s.recording = r.path;
    s.recordingSize = fs::file_size(r.path, ec);
    if (ec) {
      s.recordingSize = 0;
    }
    s.cards = r.path;
    s.cards.replace_extension(L".cards");
    CardFacts(s);
    ReadRecordingFacts(s);
  };
  std::vector<int64_t> starts;
  for (size_t i = 0; i < logs.size(); i++) {
    const Found& l = logs[i];
    Session s;
    start(s, l);
    s.log = l.path;
    s.logSize = fs::file_size(l.path, ec);
    if (ec) {
      s.logSize = 0;
    }
    const int64_t next = i + 1 < logs.size() ? logs[i + 1].time : INT64_MAX;
    for (Found& r : recordings) {
      if (!r.used && r.pid == l.pid && r.time >= l.time && r.time <= l.time + 600 && r.time <= next) {
        useRecording(s, r);
        break;
      }
    }
    starts.push_back(l.time);
    out.push_back(std::move(s));
  }
  for (Found& r : recordings) {
    if (!r.used) {
      Session s;
      start(s, r);
      useRecording(s, r);
      starts.push_back(r.time);
      out.push_back(std::move(s));
    }
  }
  for (size_t i = 0; i < out.size(); i++) {
    Session& s = out[i];
    int64_t last = -1;
    for (const fs::path& p : {s.log, s.recording}) {
      if (!p.empty()) {
        last = std::max(last, WriteTime(p));
      }
    }
    s.seconds = last >= starts[i] ? last - starts[i] : -1;
    s.shortRun = s.gameFrames < kShortFrames;
  }
  std::vector<size_t> order(out.size());
  for (size_t i = 0; i < order.size(); i++) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return starts[a] < starts[b]; });
  if (!reportsFolder.empty()) {
    for (const Report& r : List(reportsFolder)) {
      const int64_t t = StampTime(r.name.substr(7, 19));
      for (size_t k = order.size(); k-- > 0;) {
        const size_t i = order[k];
        if (starts[i] <= t) {
          const int64_t end = out[i].seconds >= 0 ? starts[i] + out[i].seconds : starts[i];
          if (t <= end + 120) {
            out[i].reports.insert(out[i].reports.begin(), r.folder);
          }
          break;
        }
      }
    }
  }
  std::vector<Session> sorted;
  for (size_t k = order.size(); k-- > 0;) {
    sorted.push_back(std::move(out[order[k]]));
  }
  return sorted;
}

std::string DurationText(int64_t seconds) {
  if (seconds < 0) {
    return "?";
  }
  char text[32];
  if (seconds < 60) {
    std::snprintf(text, sizeof text, "%lld s", static_cast<long long>(seconds));
  } else if (seconds < 3600) {
    std::snprintf(text, sizeof text, "%lld min", static_cast<long long>(seconds / 60));
  } else {
    std::snprintf(text, sizeof text, "%lld h %02lld min", static_cast<long long>(seconds / 3600),
                  static_cast<long long>(seconds / 60 % 60));
  }
  return text;
}

SessionBundle BundleFor(const std::vector<Session>& sessions, size_t index, const fs::path& settingsIni,
                        const fs::path& modsIni) {
  SessionBundle b;
  if (index >= sessions.size()) {
    return b;
  }
  const Session& s = sessions[index];
  BundleFile info;
  info.name = "info.txt";
  info.text = true;
  info.description = "When the session was, which version of the game program ran, and what each file here is.";
  b.files.push_back(info);
  AddSessionFiles(s, "", "the game", b);
  std::error_code ec;
  for (const fs::path& p : {settingsIni, modsIni}) {
    if (!p.empty() && fs::is_regular_file(p, ec)) {
      BundleFile f;
      f.name = Utf8(p.filename());
      f.from = p;
      f.size = fs::file_size(p, ec);
      f.text = true;
      f.description = Lower(f.name) == "mods.ini" ? "Your mod list as it is now: which mods are switched on (not the "
                                                    "mods themselves)."
                                                  : "Your settings as they are now.";
      b.files.push_back(std::move(f));
    }
  }
  if (s.shortRun && index + 1 < sessions.size()) {
    b.withPrevious = true;
    b.choice = "This session is very short (" +
               (s.recording.empty() ? std::string("the game never got to play") : "under 10 seconds of game") +
               "), so the session before it (" + sessions[index + 1].when +
               ") is put in too, in \"previous session\": the problem was most likely in that one.";
    AddSessionFiles(sessions[index + 1], "previous session/", "the session before", b);
  }
  if (s.recording.empty()) {
    b.choice += std::string(b.choice.empty() ? "" : " ") + "This session has no recording of button presses.";
  }
  for (const BundleFile& f : b.files) {
    b.size += f.size;
  }
  return b;
}

std::string SessionExportName(const Session& session) {
  return "session_" + session.id.substr(0, std::min<size_t>(session.id.size(), 19)) + ".zip";
}

namespace {

constexpr const char* kShortenedName = "shortened.txt";
constexpr int kJpegQuality = 90;
constexpr size_t kMinPrefix = 1024;

struct Shortened {
  std::string manifest;
  std::vector<std::string> listing;
};

bool EndsWithNoCase(const std::string& name, const std::string& suffix) {
  return name.size() >= suffix.size() && Lower(name.substr(name.size() - suffix.size())) == suffix;
}

std::string FolderOf(const std::string& name) {
  const size_t slash = name.rfind('/');
  return slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
}

Shortened Shorten(std::vector<port_zip::File>& files, std::vector<std::string>& descriptions) {
  Shortened out;
  std::vector<std::string> lines;
  for (size_t i = 0; i < files.size(); i++) {
    port_zip::File& f = files[i];
    std::string jpeg;
    if (EndsWithNoCase(f.name, "screenshot.png") && port_zip::AsJpeg(f.data, kJpegQuality, jpeg) &&
        jpeg.size() < f.data.size()) {
      const std::string png = f.name;
      f.name = png.substr(0, png.size() - 4) + ".jpg";
      f.data = std::move(jpeg);
      descriptions[i] += " (As JPEG, to keep this file small; the report's own Export keeps the PNG.)";
      lines.push_back("jpeg\t" + f.name + "\t" + png);
    }
  }
  std::vector<bool> drop(files.size(), false);
  auto depth = [&](size_t i) { return std::count(files[i].name.begin(), files[i].name.end(), '/'); };
  for (size_t i = 0; i < files.size(); i++) {
    if (drop[i]) {
      continue;
    }
    std::vector<size_t> same{i};
    for (size_t j = i + 1; j < files.size(); j++) {
      if (!drop[j] && files[j].data.size() == files[i].data.size() && files[j].data == files[i].data) {
        same.push_back(j);
      }
    }
    if (same.size() < 2) {
      continue;
    }
    const size_t keep = *std::min_element(same.begin(), same.end(), [&](size_t a, size_t b) {
      return depth(a) != depth(b) ? depth(a) < depth(b) : a < b;
    });
    for (size_t j : same) {
      if (j != keep) {
        drop[j] = true;
        lines.push_back("same\t" + files[j].name + "\t" + files[keep].name);
        out.listing.push_back(files[j].name + "  left out: the same as " + files[keep].name);
      }
    }
  }
  std::vector<size_t> recordings;
  for (size_t i = 0; i < files.size(); i++) {
    const std::string base = files[i].name.substr(FolderOf(files[i].name).size());
    if (!drop[i] && base.rfind("play_", 0) == 0 && EndsWithNoCase(base, ".txt")) {
      recordings.push_back(i);
    }
  }
  for (size_t i = 0; i < files.size(); i++) {
    if (drop[i] || !EndsWithNoCase(files[i].name, "/recording.txt")) {
      continue;
    }
    const std::string& data = files[i].data;
    size_t best = 0, from = 0;
    for (size_t r : recordings) {
      const std::string& whole = files[r].data;
      const auto diff = std::mismatch(data.begin(), data.begin() + std::min(data.size(), whole.size()), whole.begin());
      size_t shared = static_cast<size_t>(diff.first - data.begin());
      if (shared < data.size()) {
        const size_t eol = shared == 0 ? std::string::npos : data.rfind('\n', shared - 1);
        shared = eol == std::string::npos ? 0 : eol + 1;
      }
      if (shared > best) {
        best = shared;
        from = r;
      }
    }
    if (best < kMinPrefix) {
      continue;
    }
    const std::string name = files[i].name;
    files[i].name = FolderOf(name) + "recording.rest.txt";
    files[i].data = data.substr(best);
    lines.push_back("prefix\t" + name + "\t" + files[from].name + "\t" + std::to_string(best) + "\t" + files[i].name);
    descriptions[i] = "The end of " + name + ", which starts with the first " + std::to_string(best) +
                      " bytes of " + files[from].name + " (not repeated; shortened.txt).";
  }
  for (size_t i = files.size(); i-- > 0;) {
    if (drop[i]) {
      files.erase(files.begin() + static_cast<std::ptrdiff_t>(i));
      descriptions.erase(descriptions.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  if (!lines.empty()) {
    out.manifest = "# What the launcher's session export left out to keep the zip small. To get the files back as they\r\n"
                   "# were:  python tools/unpack_export.py <this zip> <folder>   (Pokemon XD PC port, developers).\r\n"
                   "# Tab-separated lines:\r\n"
                   "#   jpeg <file> <was>                       the screenshot <was> (PNG) saved as JPEG <file>\r\n"
                   "#   same <file> <copy>                      <file> was byte for byte the same as <copy>\r\n"
                   "#   prefix <file> <source> <bytes> <rest>   <file> = the first <bytes> bytes of <source>, then <rest>\r\n";
    for (const std::string& line : lines) {
      out.manifest += line + "\r\n";
    }
  }
  return out;
}

}

bool ExportSession(const std::vector<Session>& sessions, size_t index, const SessionBundle& bundle,
                   const fs::path& zipPath, bool hideUserName, const std::string& version, const Identity& who,
                   std::string& error) {
  if (index >= sessions.size()) {
    error = "Choose a session first.";
    return false;
  }
  const Session& s = sessions[index];
  std::vector<port_zip::File> files;
  std::vector<std::string> descriptions;
  for (const BundleFile& f : bundle.files) {
    if (f.from.empty()) {
      continue;
    }
    port_zip::File z;
    z.name = f.name;
    std::error_code ec;
    const uint64_t size = fs::file_size(f.from, ec);
    if (ec) {
      error = "Could not read " + Utf8(f.from) + " (was it deleted meanwhile?).";
      return false;
    }
    const uint64_t from = f.from == s.log && size > kLogCap ? size - kLogCap : 0;
    z.data = ReadPart(f.from, from, static_cast<size_t>(size - from));
    if (f.text && hideUserName) {
      z.data = Scrub(z.data, who);
    }
    port_zip::Time(f.from, &z.time, &z.date);
    files.push_back(std::move(z));
    descriptions.push_back(f.description);
  }
  const Shortened shortened = Shorten(files, descriptions);
  std::string listing;
  for (size_t i = 0; i < files.size(); i++) {
    std::string padded = files[i].name;
    padded.resize(std::max<size_t>(padded.size() + 2, 28), ' ');
    listing += padded + descriptions[i] + "\r\n";
  }
  for (const std::string& line : shortened.listing) {
    listing += line + "\r\n";
  }
  if (!shortened.manifest.empty()) {
    port_zip::File z;
    z.name = kShortenedName;
    z.data = shortened.manifest;
    port_zip::Time(fs::path(), &z.time, &z.date);
    files.push_back(std::move(z));
  }
  std::string info = "Play session, saved from the launcher\r\n";
  info += "launcher version " + (version.empty() ? std::string("unknown") : version) + "\r\n";
  info += "session " + s.id + ": started " + s.when + ", ran " + DurationText(s.seconds) +
          (s.recording.empty() ? std::string(", no recording")
                               : ", recorded up to game frame " + std::to_string(s.gameFrames)) +
          (s.recording.empty() ? std::string()
                               : s.ended ? ", closed normally (END line)"
                                         : ", no END line: the game was killed, crashed or is still running") +
          "\r\n";
  if (!s.port.empty()) {
    info += s.port + "\r\n";
  }
  if (!s.snapshot.empty()) {
    info += s.snapshot + "\r\n";
  }
  if (!s.reports.empty()) {
    info += Plural(s.reports.size(), "report was", "reports were") +
            " made during it (included; crash dumps left out: use Export on the report's own page to add one)\r\n";
  }
  if (!bundle.choice.empty()) {
    info += bundle.choice + "\r\n";
  }
  if (!shortened.manifest.empty()) {
    info += "To keep this file small, repeated copies are left out and screenshots are JPEG: shortened.txt says which\r\n"
            "(python tools/unpack_export.py puts the copies back).\r\n";
  }
  info += std::string("Windows user name ") + (hideUserName ? "hidden" : "not hidden") +
          " in the text files. Nothing was sent anywhere: the player saved this file.\r\n\r\n";
  info += listing;
  if (!s.recording.empty()) {
    info += "\r\nTo play it again: the same build (port commit above), the same disc, settings.ini and mods.ini in the data\r\n"
            "folder, " +
            (s.cardFiles > 0 ? "the card in " + Utf8(s.cards.filename()) + "/start-card"
                             : std::string("an empty card (it held no files at the start)")) +
            " as the memory card, then replay\r\n" +
            Utf8(s.recording.filename()) + " with the game program (its replay switch).\r\n";
  }
  if (hideUserName) {
    info = Scrub(info, who);
  }
  port_zip::File z;
  z.name = "info.txt";
  z.data = info;
  port_zip::Time(fs::path(), &z.time, &z.date);
  files.insert(files.begin(), std::move(z));
  std::error_code ec;
  for (const fs::path& f : {s.log, s.recording}) {
    if (!f.empty() && fs::equivalent(zipPath.parent_path(), f.parent_path(), ec)) {
      error = "Please save it somewhere else than the game's own logs or recordings folder (Documents, for example).";
      return false;
    }
  }
  if (!port_zip::Store(zipPath, files)) {
    error = "Could not write " + Utf8(zipPath) + " (is the place writable, and is there room?).";
    return false;
  }
  return true;
}

bool Delete(const Report& report, std::string& error) {
  std::error_code ec;
  fs::remove_all(report.folder, ec);
  if (ec) {
    error = "Could not delete the report (" + ec.message() + "). Is one of its files open in another program?";
    return false;
  }
  fs::path zip = report.folder;
  zip += L".zip";
  fs::remove(zip, ec);
  return true;
}

}
