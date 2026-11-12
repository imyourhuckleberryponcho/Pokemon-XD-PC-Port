#include <aurora/card.h>
#include <dolphin/card.h>

#include <save_backups.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" uint32_t VIGetRetraceCount(void);

namespace fs = std::filesystem;
namespace bk = engine::memcard::backups;

namespace {

constexpr uint32_t kOneSave = 600;

struct Job {
  std::string card, fileName;
  std::vector<uint8_t> bytes;
  std::time_t when = 0;
};

std::mutex sLock;
std::condition_variable sWake;
std::deque<Job> sJobs;
std::thread sWorker;
bool sStop = false;
bool sOn = false;
fs::path sFolder;
std::map<std::string, uint32_t> sLastWrite;

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

void Work() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(sLock);
      sWake.wait(lock, [] { return sStop || !sJobs.empty(); });
      if (sJobs.empty()) return;
      job = std::move(sJobs.front());
      sJobs.pop_front();
    }
    std::string error;
    const fs::path made = bk::Add(sFolder, job.card, job.fileName, job.bytes, job.when, bk::kKeep, &error);
    if (!made.empty()) {
      std::fprintf(stderr, "[card backup] %s backed up to %s\n", job.fileName.c_str(), Utf8(made).c_str());
    } else if (!error.empty()) {
      std::fprintf(stderr, "[card backup] %s: %s\n", job.fileName.c_str(), error.c_str());
    }
  }
}

void StopAtExit() {
  {
    std::lock_guard<std::mutex> lock(sLock);
    sStop = true;
  }
  sWake.notify_all();
  if (sWorker.joinable()) sWorker.join();
}

bool ReadAll(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0) return false;
  in.seekg(0);
  out.resize(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(in);
}

}

extern "C" {

void PortCardBackup_Start(const char* folder) {
  std::lock_guard<std::mutex> lock(sLock);
  if (sOn || folder == nullptr || folder[0] == '\0') return;
  sFolder = fs::path(std::u8string(folder, folder + std::strlen(folder)));
  sOn = true;
  sWorker = std::thread(Work);
  std::atexit(StopAtExit);
}

void PortCardBackup_BeforeWrite(s32 chan, s32 fileNo) {
  if (!sOn || chan < 0 || chan > 1 || aurora_card_get_type(chan) != AURORA_CARD_GCI_DIRECTORY) return;
  CARDStat stat{};
  if (CARDGetStatus(chan, fileNo, &stat) != CARD_RESULT_READY) return;
  char game[5] = {}, maker[3] = {}, name[CARD_FILENAME_MAX + 1] = {};
  std::memcpy(game, stat.gameName, 4);
  std::memcpy(maker, stat.company, 2);
  std::memcpy(name, stat.fileName, CARD_FILENAME_MAX);
  char gci[128] = {}, folder[1024] = {};
  if (aurora_card_gci_filename(game, maker, name, gci, sizeof gci) == 0 || gci[0] == '\0') return;
  if (aurora_card_get_path(game, AURORA_CARD_GCI_DIRECTORY, chan, folder, sizeof folder) == 0 || folder[0] == '\0') {
    return;
  }
  const fs::path cardFolder = fs::path(std::u8string(folder, folder + std::strlen(folder)));
  const fs::path file = cardFolder / fs::path(std::u8string(gci, gci + std::strlen(gci)));
  const uint32_t now = VIGetRetraceCount();
  {
    std::lock_guard<std::mutex> lock(sLock);
    auto it = sLastWrite.find(Utf8(file));
    const bool sameSave = it != sLastWrite.end() && now - it->second < kOneSave;
    sLastWrite[Utf8(file)] = now;
    if (sameSave) return;
  }
  std::vector<uint8_t> bytes;
  if (!ReadAll(file, bytes)) return;
  bool empty = true;
  for (size_t i = 0x40; i < bytes.size() && empty; i++) empty = bytes[i] == 0;
  if (empty) return;
  {
    std::lock_guard<std::mutex> lock(sLock);
    sJobs.push_back(Job{Utf8(cardFolder.filename()), gci, std::move(bytes), std::time(nullptr)});
  }
  sWake.notify_one();
}

}
