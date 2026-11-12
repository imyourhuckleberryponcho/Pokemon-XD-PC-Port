


#include "launcher_reports.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;
namespace rp = launcher::reports;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
  do {                                                                                                                 \
    const std::string _a = (a), _b = (b);                                                                              \
    if (_a != _b) {                                                                                                    \
      std::fprintf(stderr, "FAILED line %d: %s\n  got:      [%s]\n  expected: [%s]\n", __LINE__, #a, _a.c_str(),     \
                   _b.c_str());                                                                                        \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static void Write(const fs::path& file, const std::string& data) {
  std::ofstream out(file, std::ios::binary);
  out << data;
}

static std::string ReadAll(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

static std::string Lower(std::string s) {
  for (char& c : s) {
    c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return s;
}

static uint32_t Crc32(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char ch : data) {
    crc ^= ch;
    for (int k = 0; k < 8; k++) {
      crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

static uint32_t Get16(const std::string& s, size_t at) {
  return static_cast<uint8_t>(s[at]) | static_cast<uint8_t>(s[at + 1]) << 8;
}
static uint32_t Get32(const std::string& s, size_t at) { return Get16(s, at) | Get16(s, at + 2) << 16; }



static std::map<std::string, uint32_t> sMethods;
static std::map<std::string, std::string> ReadZip(const fs::path& file) {
  sMethods.clear();
  std::map<std::string, std::string> out;
  const std::string z = ReadAll(file);
  if (z.size() < 22 || Get32(z, z.size() - 22) != 0x06054b50) {
    CHECK(!"zip end record");
    return out;
  }
  const uint32_t count = Get16(z, z.size() - 12), dirSize = Get32(z, z.size() - 10), dir = Get32(z, z.size() - 6);
  CHECK(dir + dirSize + 22 == z.size());
  size_t at = dir;
  for (uint32_t i = 0; i < count; i++) {
    if (at + 46 > z.size() || Get32(z, at) != 0x02014b50) {
      CHECK(!"central directory entry");
      return {};
    }
    const uint32_t method = Get16(z, at + 10);
    CHECK(method == 0 || method == 8);
    const uint32_t crc = Get32(z, at + 16), packed = Get32(z, at + 20), size = Get32(z, at + 24);
    const uint32_t nameLen = Get16(z, at + 28);
    const uint32_t extra = Get16(z, at + 30), comment = Get16(z, at + 32), local = Get32(z, at + 42);
    const std::string name = z.substr(at + 46, nameLen);
    CHECK(Get32(z, local) == 0x04034b50);
    CHECK(z.substr(local + 30, Get16(z, local + 26)) == name);
    std::string data = z.substr(local + 30 + Get16(z, local + 26) + Get16(z, local + 28), packed);
    if (method == 8) {
      std::string plain(size, '\0');
      z_stream s{};
      inflateInit2(&s, -MAX_WBITS);
      s.next_in = reinterpret_cast<Bytef*>(data.data());
      s.avail_in = static_cast<uInt>(data.size());
      s.next_out = reinterpret_cast<Bytef*>(plain.data());
      s.avail_out = static_cast<uInt>(plain.size());
      CHECK(inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == size);
      inflateEnd(&s);
      data = plain;
    } else {
      CHECK(packed == size);
    }
    sMethods[name] = method;
    CHECK(Crc32(data) == crc);
    out[name] = data;
    at += 46 + nameLen + extra + comment;
  }
  return out;
}

static void TestScrub() {
  rp::Identity who;
  who.userName = "sam";
  who.profilePaths = {"C:\\Users\\sam"};


  CHECK_EQ(rp::Scrub("log C:\\Users\\sam\\AppData\\Roaming\\Pokemon XD\\x.log", who),
           "log %USERPROFILE%\\AppData\\Roaming\\Pokemon XD\\x.log");
  CHECK_EQ(rp::Scrub("c:/users/SAM/Documents/disc.iso", who), "%USERPROFILE%/Documents/disc.iso");
  CHECK_EQ(rp::Scrub("{\"path\": \"C:\\\\Users\\\\sam\\\\x\"}", who), "{\"path\": \"%USERPROFILE%\\\\x\"}");
  CHECK_EQ(rp::Scrub("at C:\\Users\\sam", who), "at %USERPROFILE%");
  CHECK_EQ(rp::Scrub("\"C:\\Users\\sam\"", who), "\"%USERPROFILE%\"");

  CHECK_EQ(rp::Scrub("C:\\Users\\samuel\\x and C:\\Users\\sam2\\y", who), "C:\\Users\\samuel\\x and C:\\Users\\sam2\\y");
  CHECK_EQ(rp::Scrub("C:\\Users\\sam.old\\x", who), "C:\\Users\\sam.old\\x");

  CHECK_EQ(rp::Scrub("D:\\Users\\sam\\games and /c/Users/sam/x and F:\\backup\\sam", who),
           "D:\\Users\\%USERNAME%\\games and /c/Users/%USERNAME%/x and F:\\backup\\%USERNAME%");
  CHECK_EQ(rp::Scrub("F:\\sampling\\x F:\\x\\sam_files", who), "F:\\sampling\\x F:\\x\\sam_files");

  CHECK_EQ(rp::Scrub("user=sam\r\nowner = sam (main)\r\nname: Sam\r\n", who),
           "user=%USERNAME%\r\nowner = %USERNAME% (main)\r\nname: %USERNAME%\r\n");
  CHECK_EQ(rp::Scrub("style=sampling\nmode=sam-mode\nnote=Busam\n", who), "style=sampling\nmode=sam-mode\nnote=Busam\n");

  CHECK_EQ(rp::Scrub("sam was here, balsamic", who), "sam was here, balsamic");

  const std::string plain = "frame 100\r\nMARK frame 9312\r\n\xC3\xA9t\xC3\xA9\r\n";
  CHECK_EQ(rp::Scrub(plain, who), plain);


  rp::Identity longName;
  longName.userName = "Alexander Smith";
  longName.profilePaths = {"C:\\Users\\Alexander Smith"};
  CHECK_EQ(rp::Scrub("C:\\Users\\Alexander Smith\\x C:\\Users\\ALEXAN~1\\AppData C:\\Users\\ALEXAN~X", longName),
           "%USERPROFILE%\\x C:\\Users\\%USERNAME%\\AppData C:\\Users\\ALEXAN~X");

  rp::Identity none;
  none.profilePaths = {"C:\\Users\\sam\\"};
  CHECK_EQ(rp::Scrub("C:\\Users\\sam\\x user=sam", none), "%USERPROFILE%\\x user=sam");

  rp::Identity accent;
  accent.userName = "Ren\xC3\xA9";
  accent.profilePaths = {"C:\\Users\\Ren\xC3\xA9"};
  CHECK_EQ(rp::Scrub("C:\\Users\\Ren\xC3\xA9\\x user=Ren\xC3\xA9 C:\\Users\\Ren\xC3\xA9" "e\\y", accent),
           "%USERPROFILE%\\x user=%USERNAME% C:\\Users\\Ren\xC3\xA9" "e\\y");
}

static void TestFolder(const fs::path& root) {
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  const fs::path key = root / L"report_2026-11-08_10-00-05";
  const fs::path crash = root / L"report_2026-11-08_12-30-00_crash";
  const fs::path stuck = root / L"report_2026-11-07_23-59-59_stuck";
  for (const fs::path& f : {key, crash, stuck}) {
    fs::create_directories(f);
  }
  fs::create_directories(root / L"not_a_report");
  fs::create_directories(root / L"report_2026-11-08_bad");
  Write(root / L"report_2026-11-08_10-00-05.zip", "zip of the key report");

  Write(key / L"info.txt", "Pokemon XD PC port: bug report (F10)\r\ntime 2026-11-08 10:00:05\r\n"
                           "video field 9500, game frame 9312 (at the key press)\r\nmap Phenac City (0x064)\r\n"
                           "disc C:\\Users\\sam\\Games\\xd.iso\r\n");
  Write(key / L"log.txt", "[user folder] C:\\Users\\sam\\AppData\\Roaming\\Pokemon XD\r\n");
  Write(key / L"settings.ini", "[system]\r\ndisc_path=C:/Users/sam/Games/xd.iso\r\nname=sam\r\n");
  std::string png("\x89PNG\r\n\x1a\n C:\\Users\\sam binary", 28);
  Write(key / L"screenshot.png", png);
  Write(key / L"recording.txt", "FORMAT 2\nMARK frame 9312\n");

  Write(crash / L"info.txt", "Pokemon XD PC port: automatic report (crash)\r\nwhat happened: access violation\r\n"
                             "video field 100, game frame 42 (when it happened)\r\n");
  Write(crash / L"crash.dmp", std::string("MDMP C:\\Users\\sam\0\x01", 21));
  Write(crash / L"log.txt", "x");


  const auto list = rp::List(root);
  CHECK(list.size() == 3);
  if (list.size() == 3) {
    CHECK_EQ(list[0].name, "report_2026-11-08_12-30-00_crash");
    CHECK(list[0].kind == rp::Report::Kind::Crash);
    CHECK_EQ(list[0].moment, "game frame 42");
    CHECK_EQ(list[0].happened, "access violation");
    CHECK_EQ(list[1].name, "report_2026-11-08_10-00-05");
    CHECK(list[1].kind == rp::Report::Kind::Key);
    CHECK_EQ(list[1].when, "2026-11-08 10:00:05");
    CHECK_EQ(list[1].moment, "Phenac City (0x064), game frame 9312");
    CHECK(list[2].kind == rp::Report::Kind::Stuck);
    CHECK_EQ(list[2].moment, "");
    CHECK(list[2].files.empty());

    std::string order;
    for (const rp::File& f : list[1].files) {
      order += f.name + " ";
    }
    CHECK_EQ(order, "info.txt screenshot.png recording.txt log.txt settings.ini ");
    CHECK(list[0].files.back().dump && list[0].files.back().name == "crash.dmp");
    CHECK(list[1].size == ReadAll(key / L"info.txt").size() + ReadAll(key / L"log.txt").size() +
                              ReadAll(key / L"settings.ini").size() + png.size() + ReadAll(key / L"recording.txt").size());
    CHECK_EQ(rp::ExportName(list[0]), "report_2026-11-08_12-30-00.zip");
  }
  CHECK_EQ(rp::SizeText(12), "12 bytes");
  CHECK_EQ(rp::SizeText(2048), "2 KB");
  CHECK_EQ(rp::SizeText(1536 * 1024), "1.5 MB");

  rp::Identity who;
  who.userName = "sam";
  who.profilePaths = {"C:\\Users\\sam"};
  if (list.size() == 3) {

    const fs::path out = root / L"out" / L"my report.zip";
    fs::create_directories(out.parent_path());
    std::string error;
    CHECK(rp::Export(list[1], out, rp::ExportOptions(), who, error));
    auto zip = ReadZip(out);
    CHECK(zip.size() == 5);
    CHECK(zip["log.txt"] == "[user folder] %USERPROFILE%\\AppData\\Roaming\\Pokemon XD\r\n");
    CHECK(zip["settings.ini"] == "[system]\r\ndisc_path=%USERPROFILE%/Games/xd.iso\r\nname=%USERNAME%\r\n");
    CHECK(zip["screenshot.png"] == png);
    CHECK(zip["info.txt"].find("disc %USERPROFILE%\\Games\\xd.iso\r\n") != std::string::npos);
    CHECK(zip["info.txt"].find("Exported from the launcher: Windows user name hidden, crash dump left out.") !=
          std::string::npos);
    CHECK(zip["info.txt"].find("sam") == std::string::npos);

    CHECK(ReadAll(key / L"log.txt") == "[user folder] C:\\Users\\sam\\AppData\\Roaming\\Pokemon XD\r\n");


    CHECK(rp::Export(list[0], out, rp::ExportOptions(), who, error));
    zip = ReadZip(out);
    CHECK(zip.count("crash.dmp") == 0 && zip.size() == 2);
    rp::ExportOptions all;
    all.includeDump = true;
    all.hideUserName = false;
    CHECK(rp::Export(list[0], out, all, who, error));
    zip = ReadZip(out);
    CHECK(zip.size() == 3);
    CHECK(zip["crash.dmp"] == std::string("MDMP C:\\Users\\sam\0\x01", 21));
    CHECK(zip["info.txt"].find("user name not hidden, crash dump included") != std::string::npos);


    CHECK(rp::Export(list[2], out, rp::ExportOptions(), who, error));
    CHECK(ReadZip(out).empty());
    error.clear();
    CHECK(!rp::Export(list[1], root / L"no such folder" / L"x.zip", rp::ExportOptions(), who, error));
    CHECK(!error.empty());


    CHECK(rp::Delete(list[1], error));
    CHECK(!fs::exists(key) && !fs::exists(root / L"report_2026-11-08_10-00-05.zip"));
    CHECK(rp::List(root).size() == 2);
  }
  fs::remove_all(root, ec);
}


static void SetTime(const fs::path& file, int year, int mon, int day, int h, int m, int s) {
  std::tm t{};
  t.tm_year = year - 1900, t.tm_mon = mon - 1, t.tm_mday = day, t.tm_hour = h, t.tm_min = m, t.tm_sec = s;
  t.tm_isdst = -1;
  const auto sys = std::chrono::system_clock::from_time_t(std::mktime(&t));
  std::error_code ec;
  fs::last_write_time(file, std::chrono::clock_cast<std::chrono::file_clock>(sys), ec);
  CHECK(!ec);
}


static void TestSessions(const fs::path& root) {
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path logs = root / L"logs", recs = root / L"recordings", reps = root / L"reports";
  for (const fs::path& f : {logs, recs, reps}) {
    fs::create_directories(f);
  }

  const fs::path logA = logs / L"play_2026-11-08_09-00-00_p100.log";
  Write(logA, "[user folder] C:\\Users\\sam\\AppData\\Roaming\\Pokemon XD (default)\r\nframe stuff\r\n");
  const fs::path recA = recs / L"play_2026-11-08_09-00-01_p100.txt";
  Write(recA, "# Input recording\n# port commit abc1234 (when the build was configured), release build\n"
              "# decomp snapshot snap42\n# disc C:\\Users\\sam\\Games\\xd.iso\nFORMAT 2\n"
              "frame 0 poll 0 field 1 P1 buttons - stick 0 0 cstick 0 0 triggers 0 0\n"
              "ANCHOR 4 fade id 0x2 tick 130 frame 801 seed 0x1\n"
              "frame 5000 poll 1 field 10010 P1 buttons A stick 0 0 cstick 0 0 triggers 0 0\nfra");
  const std::string gci("GCI\0\x01 C:\\Users\\sam", 18);
  fs::create_directories(recs / L"play_2026-11-08_09-00-01_p100.cards" / L"start-card" / L"USA" / L"Card A");
  fs::create_directories(recs / L"play_2026-11-08_09-00-01_p100.cards" / L"saves" / L"01-anchor000010" / L"USA" / L"Card A");
  Write(recs / L"play_2026-11-08_09-00-01_p100.cards" / L"start-card" / L"USA" / L"Card A" / L"save.gci", gci);
  Write(recs / L"play_2026-11-08_09-00-01_p100.cards" / L"saves" / L"01-anchor000010" / L"USA" / L"Card A" / L"save.gci",
        gci + "2");
  const fs::path stuck = reps / L"report_2026-11-08_09-10-00_stuck";
  fs::create_directories(stuck);
  Write(stuck / L"info.txt", "automatic report (stuck)\r\ngame frame 4000\r\n");
  Write(stuck / L"stuck.dmp", "MDMP");
  fs::create_directories(reps / L"report_2026-11-08_07-00-00");
  SetTime(logA, 2026, 11, 8, 9, 20, 0);
  SetTime(recA, 2026, 11, 8, 9, 19, 30);

  const fs::path logB = logs / L"play_2026-11-08_10-00-00_p200.log";
  Write(logB, "XD: no disc\r\n");
  SetTime(logB, 2026, 11, 8, 10, 0, 3);

  const fs::path recC = recs / L"play_2026-11-08_08-00-00_p300.txt";
  Write(recC, "# Input recording\nframe 0 poll 0 field 1 P1 none\nframe 900 poll 0 field 1801 P1 none\n"
              "END frame 1200 field 2400\n");
  SetTime(recC, 2026, 11, 8, 8, 1, 0);

  Write(logs / L"test_2026-11-08_10-30-00_p400.log", "test");
  Write(recs / L"test_2026-11-08_10-30-00_p400.txt", "frame 9999");
  Write(recs / L"notes.txt", "x");
  Write(root / L"settings.ini", "[system]\r\ndisc_path=C:/Users/sam/Games/xd.iso\r\n");
  Write(root / L"mods.ini", "[mods]\r\n");

  const auto sessions = rp::ListSessions(logs, recs, reps);
  CHECK(sessions.size() == 3);
  if (sessions.size() != 3) {
    return;
  }
  const rp::Session &b = sessions[0], &a = sessions[1], &c = sessions[2];
  CHECK_EQ(b.id, "2026-11-08_10-00-00_p200");
  CHECK_EQ(b.when, "2026-11-08 10:00:00");
  CHECK(b.recording.empty() && b.shortRun && b.seconds == 3 && b.reports.empty());
  CHECK_EQ(a.id, "2026-11-08_09-00-00_p100");
  CHECK(a.recording == recA && a.log == logA);
  CHECK(a.gameFrames == 5000 && !a.ended && !a.shortRun);
  CHECK(a.seconds == 20 * 60);
  CHECK(a.cardFiles == 2);
  CHECK(a.reports.size() == 1 && a.reports[0] == stuck);
  CHECK_EQ(a.port, "port commit abc1234 (when the build was configured), release build");
  CHECK_EQ(a.snapshot, "decomp snapshot snap42");
  CHECK_EQ(c.id, "2026-11-08_08-00-00_p300");
  CHECK(c.log.empty() && c.ended && c.gameFrames == 1200 && c.seconds == 60);
  CHECK_EQ(rp::SessionExportName(b), "session_2026-11-08_10-00-00.zip");
  CHECK_EQ(rp::DurationText(3), "3 s");
  CHECK_EQ(rp::DurationText(20 * 60), "20 min");
  CHECK_EQ(rp::DurationText(3600 + 5 * 60), "1 h 05 min");
  CHECK_EQ(rp::DurationText(-1), "?");


  const auto bundle = rp::BundleFor(sessions, 0, root / L"settings.ini", root / L"mods.ini");
  CHECK(bundle.withPrevious && !bundle.choice.empty());
  std::string names;
  for (const rp::BundleFile& f : bundle.files) {
    names += f.name + "\n";
  }
  CHECK_EQ(names, "info.txt\n"
                  "play_2026-11-08_10-00-00_p200.log\n"
                  "settings.ini\nmods.ini\n"
                  "previous session/play_2026-11-08_09-00-00_p100.log\n"
                  "previous session/play_2026-11-08_09-00-01_p100.txt\n"
                  "previous session/play_2026-11-08_09-00-01_p100.cards/saves/01-anchor000010/USA/Card A/save.gci\n"
                  "previous session/play_2026-11-08_09-00-01_p100.cards/start-card/USA/Card A/save.gci\n"
                  "previous session/report_2026-11-08_09-10-00_stuck/info.txt\n");

  const auto bundleA = rp::BundleFor(sessions, 1, root / L"settings.ini", fs::path());
  CHECK(!bundleA.withPrevious && bundleA.choice.empty());
  CHECK(bundleA.files.size() == 1 + 2 + 2 + 1 + 1);

  rp::Identity who;
  who.userName = "sam";
  who.profilePaths = {"C:\\Users\\sam"};
  const fs::path out = root / L"out" / L"session.zip";
  fs::create_directories(out.parent_path());
  std::string error;
  CHECK(rp::ExportSession(sessions, 0, bundle, out, true, "0.2.1", who, error));
  auto zip = ReadZip(out);
  CHECK(zip.size() == bundle.files.size());
  CHECK(zip["previous session/play_2026-11-08_09-00-00_p100.log"] ==
        "[user folder] %USERPROFILE%\\AppData\\Roaming\\Pokemon XD (default)\r\nframe stuff\r\n");
  CHECK(zip["previous session/play_2026-11-08_09-00-01_p100.txt"].find("# disc %USERPROFILE%\\Games\\xd.iso\n") !=
        std::string::npos);
  CHECK(zip["previous session/play_2026-11-08_09-00-01_p100.txt"].find("\nfra") != std::string::npos);
  CHECK(zip["previous session/play_2026-11-08_09-00-01_p100.cards/start-card/USA/Card A/save.gci"] == gci);
  CHECK(zip["settings.ini"] == "[system]\r\ndisc_path=%USERPROFILE%/Games/xd.iso\r\n");
  CHECK(zip.count("previous session/report_2026-11-08_09-10-00_stuck/stuck.dmp") == 0);
  const std::string info = zip["info.txt"];
  CHECK(info.find("launcher version 0.2.1") != std::string::npos);
  CHECK(info.find("session 2026-11-08_10-00-00_p200: started 2026-11-08 10:00:00, ran 3 s, no recording") !=
        std::string::npos);
  CHECK(info.find("previous session") != std::string::npos);
  CHECK(info.find("Windows user name hidden") != std::string::npos);
  CHECK(Lower(info).find("users\\sam") == std::string::npos && Lower(info).find("users/sam") == std::string::npos);
  for (const auto& [name, data] : zip) {
    if (name.find(".gci") == std::string::npos) {
      CHECK(Lower(data).find("users\\sam") == std::string::npos);
    }
  }

  CHECK(rp::ExportSession(sessions, 1, bundleA, out, false, "", who, error));
  zip = ReadZip(out);
  CHECK(zip["info.txt"].find("recorded up to game frame 5000, no END line") != std::string::npos);
  CHECK(zip["info.txt"].find("port commit abc1234") != std::string::npos);
  CHECK(zip["info.txt"].find("1 report was made during it") != std::string::npos);
  CHECK(zip["play_2026-11-08_09-00-00_p100.log"].find("C:\\Users\\sam\\AppData") != std::string::npos);
  CHECK(ReadAll(logA).find("C:\\Users\\sam") != std::string::npos);

  CHECK(!rp::ExportSession(sessions, 1, bundleA, recs / L"x.zip", true, "", who, error) && !error.empty());
  fs::remove(recs / L"play_2026-11-08_09-00-01_p100.cards" / L"start-card" / L"USA" / L"Card A" / L"save.gci");
  error.clear();
  CHECK(!rp::ExportSession(sessions, 1, bundleA, out, true, "", who, error) && !error.empty());

  CHECK(rp::ListSessions(root / L"none", fs::path(), fs::path()).empty());
  fs::remove_all(root, ec);
}

static rp::Identity TestIdentity() {
  rp::Identity who;
  who.userName = "sam";
  who.profilePaths = {"C:\\Users\\sam"};
  return who;
}


static std::string MakePng(uint32_t w, uint32_t h) {
  auto be32 = [](std::string& s, uint32_t v) {
    for (int k = 3; k >= 0; k--) {
      s.push_back(static_cast<char>((v >> (8 * k)) & 0xFF));
    }
  };
  auto chunk = [&](std::string& png, const char* type, const std::string& data) {
    be32(png, static_cast<uint32_t>(data.size()));
    const std::string body = std::string(type, 4) + data;
    png += body;
    be32(png, static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
  };
  std::string raw;
  uint32_t seed = 12345;
  for (uint32_t y = 0; y < h; y++) {
    raw.push_back(0);
    for (uint32_t x = 0; x < w; x++) {
      seed = seed * 1103515245u + 12345u;
      const int noise = static_cast<int>((seed >> 16) % 24);
      raw.push_back(static_cast<char>((x * 255 / w + noise) & 0xFF));
      raw.push_back(static_cast<char>((y * 255 / h + noise) & 0xFF));
      raw.push_back(static_cast<char>((128 + noise) & 0xFF));
    }
  }
  uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
  std::string packed(packedSize, '\0');
  compress2(reinterpret_cast<Bytef*>(packed.data()), &packedSize, reinterpret_cast<const Bytef*>(raw.data()),
            static_cast<uLong>(raw.size()), 9);
  packed.resize(packedSize);
  std::string ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr += std::string("\x08\x02\x00\x00\x00", 5);
  std::string png("\x89PNG\r\n\x1a\n", 8);
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", packed);
  chunk(png, "IEND", "");
  return png;
}



static void TestShortenedSession(const fs::path& root) {
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path logs = root / L"logs", recs = root / L"recordings", reps = root / L"reports";
  for (const fs::path& f : {logs, recs, reps}) {
    fs::create_directories(f);
  }
  const fs::path log = logs / L"play_2026-11-09_01-00-00_p500.log";
  Write(log, "started\r\n");
  std::string recording = "# Input recording\r\nFORMAT 2\r\n";
  for (int frame = 0; frame < 2000; frame++) {
    recording += "frame " + std::to_string(frame * 3) + " poll 0 field " + std::to_string(frame * 6) +
                 " P1 buttons A stick 0 0 cstick 0 0 triggers 0 0\r\n";
  }
  const size_t cut = recording.find("frame 3000 ");
  recording += "END frame 6000 field 12000\r\n";
  const fs::path rec = recs / L"play_2026-11-09_01-00-01_p500.txt";
  Write(rec, recording);
  SetTime(log, 2026, 11, 9, 1, 30, 0);
  SetTime(rec, 2026, 11, 9, 1, 29, 0);
  const std::string settings = "[system]\r\ntext_speed=original\r\n", mods = "[mods]\r\n";
  Write(root / L"settings.ini", settings);
  Write(root / L"mods.ini", mods);
  const std::string tail = "# copied for a bug report: the recording up to mark #1, ending there\nEND frame 2999\n";
  const std::string png = MakePng(320, 240);
  for (const wchar_t* name : {L"report_2026-11-09_01-10-00", L"report_2026-11-09_01-20-00"}) {
    const fs::path r = reps / name;
    fs::create_directories(r);
    Write(r / L"info.txt", std::string("bug report (F10)\r\ngame frame 2999 ") + (name[21] == L'1' ? "a" : "b") + "\r\n");
    Write(r / L"screenshot.png", png);
    Write(r / L"recording.txt", recording.substr(0, cut) + tail);
    Write(r / L"settings.ini", settings);
    Write(r / L"mods.ini", mods);
  }
  const auto sessions = rp::ListSessions(logs, recs, reps);
  CHECK(sessions.size() == 1);
  if (sessions.size() != 1) {
    return;
  }
  CHECK(sessions[0].reports.size() == 2);
  const auto bundle = rp::BundleFor(sessions, 0, root / L"settings.ini", root / L"mods.ini");
  const fs::path out = root / L"out" / L"session.zip";
  fs::create_directories(out.parent_path());
  std::string error;
  CHECK(rp::ExportSession(sessions, 0, bundle, out, true, "0.2.2", TestIdentity(), error));
  auto zip = ReadZip(out);
  const std::string r1 = "report_2026-11-09_01-10-00/", r2 = "report_2026-11-09_01-20-00/";

  CHECK(zip["play_2026-11-09_01-00-01_p500.txt"] == recording);
  CHECK(sMethods["play_2026-11-09_01-00-01_p500.txt"] == 8);
  CHECK(fs::file_size(out) < recording.size() / 4 + 2 * png.size());
  CHECK(zip["settings.ini"] == settings && zip["mods.ini"] == mods);

  CHECK(zip.count(r1 + "screenshot.png") == 0 && zip.count(r1 + "screenshot.jpg") == 1);
  const std::string jpg = zip[r1 + "screenshot.jpg"];
  CHECK(jpg.size() > 2 && static_cast<uint8_t>(jpg[0]) == 0xFF && static_cast<uint8_t>(jpg[1]) == 0xD8);
  CHECK(jpg.size() < png.size() / 2);
  CHECK(sMethods[r1 + "screenshot.jpg"] == 0);

  CHECK(zip.count(r2 + "screenshot.jpg") == 0);

  CHECK(zip.count(r1 + "settings.ini") == 0 && zip.count(r2 + "mods.ini") == 0);
  CHECK(zip.count(r1 + "info.txt") == 1 && zip.count(r2 + "info.txt") == 1);

  CHECK(zip.count(r1 + "recording.txt") == 0);
  CHECK(zip[r1 + "recording.rest.txt"] == tail);
  const std::string manifest = zip["shortened.txt"];
  CHECK(manifest.find("jpeg\t" + r1 + "screenshot.jpg\t" + r1 + "screenshot.png\r\n") != std::string::npos);
  CHECK(manifest.find("same\t" + r1 + "settings.ini\tsettings.ini\r\n") != std::string::npos);
  CHECK(manifest.find("same\t" + r2 + "screenshot.jpg\t" + r1 + "screenshot.jpg\r\n") != std::string::npos);
  CHECK(manifest.find("prefix\t" + r1 + "recording.txt\tplay_2026-11-09_01-00-01_p500.txt\t" + std::to_string(cut) +
                      "\t" + r1 + "recording.rest.txt\r\n") != std::string::npos);

  CHECK(manifest.find("same\t" + r2 + "recording.txt\t" + r1 + "recording.txt\r\n") != std::string::npos);

  {
    std::map<std::string, std::string> back = zip;
    std::istringstream lines(manifest);
    std::string line;
    std::vector<std::vector<std::string>> same;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty() || line[0] == '#') {
        continue;
      }
      std::vector<std::string> parts;
      for (size_t at = 0;;) {
        const size_t tab = line.find('\t', at);
        parts.push_back(line.substr(at, tab - at));
        if (tab == std::string::npos) {
          break;
        }
        at = tab + 1;
      }
      if (parts[0] == "prefix" && parts.size() == 5) {
        back[parts[1]] = back[parts[2]].substr(0, std::stoul(parts[3])) + back[parts[4]];
      } else if (parts[0] == "same" && parts.size() == 3) {
        same.push_back(parts);
      }
    }
    for (const auto& parts : same) {
      back[parts[1]] = back[parts[2]];
    }
    for (const std::string& r : {r1, r2}) {
      CHECK(back[r + "recording.txt"] == recording.substr(0, cut) + tail);
      CHECK(back[r + "settings.ini"] == settings && back[r + "mods.ini"] == mods);
    }
  }
  CHECK(zip["info.txt"].find("shortened.txt") != std::string::npos);
  CHECK(zip["info.txt"].find(r1 + "settings.ini  left out: the same as settings.ini") != std::string::npos);

  const auto list = rp::List(reps);
  CHECK(list.size() == 2);
  if (!list.empty()) {
    CHECK(rp::Export(list[0], out, rp::ExportOptions(), TestIdentity(), error));
    zip = ReadZip(out);
    CHECK(zip["screenshot.png"] == png && zip.count("shortened.txt") == 0);
  }
  fs::remove_all(root, ec);
}

int main(int argc, char** argv) {


  if (argc == 4 && std::strcmp(argv[1], "--export-session") == 0) {
    const fs::path data = fs::u8path(argv[2]);
    const auto sessions = rp::ListSessions(data / L"logs", data / L"recordings", data / L"reports");
    if (sessions.empty()) {
      std::printf("no session in %s\n", argv[2]);
      return 1;
    }
    const auto bundle = rp::BundleFor(sessions, 0, data / L"settings.ini", data / L"mods.ini");
    std::string error;
    const bool ok = rp::ExportSession(sessions, 0, bundle, fs::u8path(argv[3]), true, "measure", TestIdentity(), error);
    std::printf("%s: %zu files, %llu bytes in the session; %s\n", sessions[0].id.c_str(), bundle.files.size(),
                static_cast<unsigned long long>(bundle.size), ok ? "exported" : error.c_str());
    return ok ? 0 : 1;
  }
  TestScrub();
  const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::absolute(fs::path(argv[0])).parent_path() / L"reports_test_tmp";
  TestFolder(root);
  TestSessions(root);
  TestShortenedSession(root);
  if (sFailures == 0) {
    std::printf("reports_test: all passed\n");
    return 0;
  }
  std::printf("reports_test: %d failed\n", sFailures);
  return 1;
}
