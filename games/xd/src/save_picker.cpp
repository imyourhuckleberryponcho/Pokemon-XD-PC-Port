#include "xd_save_picker.h"

#include "xd_playthrough_files.h"
#include "xd_randomizer.h"
#include "xd_user_folder.h"

#include <gc_memcard.h>
#include <port_window_shot.h>
#include <playthrough.h>
#include <randomizer.h>
#include <save_slots.h>
#include <save_thumbnail.h>
#include <xd_save.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
void PortInput_LatchHeld(void);
int PortXD_ActiveWindowState(int* id);
void menuTitleSetStartStatus(int32_t value);
uint8_t GSmoviePlayCheck(uint32_t movie);
void GSmovieStop(uint32_t movie);
}

namespace xd::picker {

namespace fs = std::filesystem;
namespace ep = engine::playthrough;
namespace th = engine::memcard::thumbnail;

namespace {

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}
bool EnvSet(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
  SetEnvironmentVariableA(name, value);
#else
  if (value != nullptr) setenv(name, value, 1);
  else unsetenv(name);
#endif
}

Intent sIntent = Intent::None;
bool sIntentRead = false;
bool (*sSkipQuery)() = nullptr;

}

const char* IntentName(Intent i) {
  switch (i) {
  case Intent::Title: return "title";
  case Intent::NewGame: return "newgame";
  case Intent::Continue: return "continue";
  default: return "";
  }
}

void ReadIntent() {
  if (sIntentRead) return;
  sIntentRead = true;
  const char* v = std::getenv("XD_RESTART_INTENT");
  if (v != nullptr) {
    if (std::strcmp(v, "title") == 0) sIntent = Intent::Title;
    else if (std::strcmp(v, "newgame") == 0) sIntent = Intent::NewGame;
    else if (std::strcmp(v, "continue") == 0) sIntent = Intent::Continue;
    if (sIntent != Intent::None) std::fprintf(stderr, "[restart] started again to land at: %s (logo movies and title lead-in skipped)\n", v);
  }
  SetEnv("XD_RESTART_INTENT", nullptr);
}

Intent Current() {
  ReadIntent();
  return sIntent;
}

void SetForRestart(Intent intent) { SetEnv("XD_RESTART_INTENT", IntentName(intent)[0] != 0 ? IntentName(intent) : nullptr); }

void RegisterSkipLogosQuery(bool (*query)()) { sSkipQuery = query; }

bool SkipLogos() { return Current() != Intent::None || (sSkipQuery != nullptr && sSkipQuery()); }

namespace {
int sTick = 0, sSettle = 0, sSteer = 0, sPressed = 0;
bool sStartStatusSet = false;
bool sNewGamePressed = false;
constexpr int kTitleMenuId = 0x116;
}

void Done(const char* why) {
  if (sIntent == Intent::None) return;
  std::fprintf(stderr, "[restart] intent %s done: %s\n", IntentName(sIntent), why);
  sIntent = Intent::None;
}

bool WantsNewGameNow() { return sIntent == Intent::NewGame; }
bool WantsContinueNow() { return sIntent == Intent::Continue; }

void Drive(PADStatus* status, int menuId, bool titleOpen, int select) {
  if (sIntent == Intent::None) return;
  if (!sStartStatusSet) {
    sStartStatusSet = true;
    menuTitleSetStartStatus(1);
  }
  PADStatus* pad = nullptr;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
    if (status[i].err == PAD_ERR_NONE) {
      pad = &status[i];
      break;
    }
  }
  if (pad == nullptr) return;
  sTick++;
  if (!titleOpen) {
    sSettle = 0;
    if (menuId == kTitleMenuId) return;
    if (sNewGamePressed) return;
    if ((sTick / 4) % 2 == 0) pad->button |= PAD_BUTTON_A;
    return;
  }
  if (sIntent == Intent::Title) {
    Done("the title menu is up");
    return;
  }
  if (++sSettle < 20) return;
  const int target = sIntent == Intent::NewGame ? 0 : 1;
  if (select != target) {
    if (++sSteer % 8 == 1) pad->button |= select < target ? PAD_BUTTON_DOWN : PAD_BUTTON_UP;
    return;
  }
  if (sPressed < 3) {
    pad->button |= PAD_BUTTON_A;
    sPressed++;
    if (sIntent == Intent::NewGame) sNewGamePressed = true;
    if (sPressed == 3) Done(sIntent == Intent::NewGame ? "NEW GAME pressed" : "CONTINUE pressed");
  }
}

extern "C" uint8_t PortXD_MoviePlayCheck(uint32_t movie) {
  if ((movie == 0x7d9 || movie == 0x7da) && SkipLogos()) {
    if (GSmoviePlayCheck(movie) == 1) {
      GSmovieStop(movie);
      std::fprintf(stderr, "[intro] logo movie 0x%x skipped\n", movie);
    }
    return 0;
  }
  return static_cast<uint8_t>(GSmoviePlayCheck(movie));
}

namespace {

bool sPossible = false, sAnswered = false;

std::string RandomizerStyle(const std::string& code) {
  namespace xr = xd::randomizer;
  engine::randomizer::Settings rs;
  xr::ApplyStyle(rs, xr::Style::Classic);
  bool seedOnly = false;
  std::string decodeError;
  if (code.empty() || !engine::randomizer::DecodeCode(xr::GetSchema(), code, rs, seedOnly, decodeError)) return {};
  switch (xr::StyleOf(rs)) {
  case xr::Style::Light: return "Light";
  case xr::Style::Classic: return "Classic";
  case xr::Style::Chaos: return "Chaos";
  default: return "Custom";
  }
}

std::string PlayTimeText(double seconds) {
  const long long s = static_cast<long long>(seconds);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 3600, (s / 60) % 60);
  return buf;
}

std::vector<std::string> Items(const std::string& label) {
  std::vector<std::string> out;
  const std::string dot = "\xC2\xB7";
  size_t at = 0;
  for (;;) {
    const size_t next = label.find(dot, at);
    std::string item = label.substr(at, next == std::string::npos ? std::string::npos : next - at);
    while (!item.empty() && item.back() == ' ') item.pop_back();
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    if (!item.empty()) out.push_back(item);
    if (next == std::string::npos) break;
    at = next + dot.size();
  }
  return out;
}

bool ReadRow(const fs::path& root, Row& row) {
  const xd::CardRootScope scope(root);
  const xd::playthrough::SaveId id = xd::playthrough::SaveIdOnCard();
  if (!id.found) return false;
  namespace mc = engine::memcard;
  std::ifstream in(id.file, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<mc::CardFile> files;
  mc::CardImageInfo info;
  std::string error;
  xd::save::Save save;
  xd::save::ReadReport report;
  bool read = false;
  if (mc::LoadCardFiles(bytes, files, info, error)) {
    for (const mc::CardFile& f : files) {
      if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0 && xd::save::ReadSave(f.data, save, report, error)) {
        read = true;
        break;
      }
    }
  }
  if (!read) return false;
  row.trainer = id.trainer;
  row.key = id.key;
  row.seconds = save.playTimeSeconds;
  row.playTime = PlayTimeText(save.playTimeSeconds);
  row.thumbnail = xd::playthrough::SetupFolder() / (id.key + ".png");
  std::string label;
  ep::Setup setup;
  std::error_code ec;
  if (!fs::exists(xd::playthrough::SetupFileFor(id), ec)) {
    label = "Faithful \xC2\xB7 Vanilla \xC2\xB7 no setup file (a save from Dolphin, a GameCube or an older port)";
  } else if (!ep::ReadFile(xd::playthrough::SetupFileFor(id), setup, error)) {
    label = "its setup file can't be read";
  } else {
    label = ep::Label(setup);
    const std::string style = RandomizerStyle(setup.seed);
    const size_t at = label.find("Randomizer ");
    if (!style.empty() && at != std::string::npos) label.insert(at + 11, style + " ");
  }
  const std::vector<std::string> items = Items(label);
  for (size_t i = 0; i < items.size(); i++) {
    row.rules += (i > 0 ? ", " : "") + items[i];
    row.rulesFull += (i > 0 ? "\n" : "") + items[i];
  }
  return true;
}

}

std::vector<Row> Rows() {
  std::vector<Row> rows;
  if (!xd::SaveSlotsInUse()) return rows;
  namespace sl = engine::memcard::slots;
  const sl::Index index = sl::Load(xd::UserFolder());
  const fs::path active = xd::CardRoot();
  for (const sl::Slot& slot : index.slots) {
    Row row;
    row.id = slot.id;
    row.name = slot.name;
    row.root = sl::RootOf(xd::UserFolder(), slot.id);
    std::error_code ec;
    row.active = fs::equivalent(row.root, active, ec);
    if (ReadRow(row.root, row)) rows.push_back(row);
  }
  return rows;
}

void DecideStartUp() {
  const char* why = nullptr;
  if (EnvIs("XD_SAVE_PICKER", "0")) why = "XD_SAVE_PICKER=0";
  else if (Current() == Intent::Continue) why = "started again for a chosen save";
  else if (!xd::SaveSlotsInUse()) why = "one memory card folder (XD_CARD_DIR)";
  else if (EnvIs("XD_SAVE_PICKER", "1")) why = nullptr;
  else if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) why = "XD_REQUIRE_ORIGINAL (comparison run)";
  else if (EnvSet("XD_REPLAY")) why = "a replay";
  else if (EnvSet("XD_INPUT_SCRIPT")) why = "scripted presses";
  else if (EnvSet("XD_RENDER_ICON")) why = "icon pictures";
  if (why == nullptr) {
    namespace sl = engine::memcard::slots;
    why = sl::Load(xd::UserFolder()).slots.size() < 2 ? "only one save" : nullptr;
  }
  if (EnvIs("XD_SAVE_PICKER_LIST", "1")) {
    for (const Row& r : Rows()) {
      std::fprintf(stderr, "[save picker] row id=%s name=\"%s\" key=%s trainer=\"%s\" time=%s active=%d rules=\"%s\"\n", r.id.c_str(),
                   r.name.c_str(), r.key.c_str(), r.trainer.c_str(), r.playTime.c_str(), r.active ? 1 : 0, r.rules.c_str());
    }
  }
  sPossible = why == nullptr;
  std::fprintf(stderr, "[save picker] %s%s%s\n", sPossible ? "CONTINUE offers the saves to choose from (when two or more hold a save)" : "off this run: ",
               sPossible ? "" : why, "");
}

bool Possible() { return sPossible; }
bool AnsweredThisRun() { return sAnswered; }
void SetAnswered(bool b) { sAnswered = b; }

bool SwitchTo(const Row& row) {
  namespace sl = engine::memcard::slots;
  std::string error;
  if (!sl::SetActive(xd::UserFolder(), row.id, error)) {
    std::fprintf(stderr, "[save picker] could not make %s the active save: %s\n", row.name.c_str(), error.c_str());
    return false;
  }
  std::fprintf(stderr, "[save picker] switching to \"%s\" (%s): starting the game again, straight into CONTINUE\n", row.name.c_str(),
               row.trainer.c_str());
  return true;
}

namespace {
std::mutex sThumbLock;
fs::path sThumbFile;
uint32_t sThumbWait = 0;
bool sThumbBusy = false, sKeepBusy = false;
std::vector<std::pair<uint32_t, th::Picture>> sHist;
uint32_t sKeepStamp = 0, sChosenAt = 0;
th::Picture sChosen;
constexpr uint32_t kBefore = 240;
uint32_t sLastKeep = 0;
char sKeepTag;
constexpr uint32_t kKeepEvery = 40;

bool MenuClosed() {
  int id = 0;
  const int st = PortXD_ActiveWindowState(&id);
  return st < 0;
}

void WriteIt(const fs::path& file, const th::Picture& small) {
  std::string error;
  if (th::WriteFile(file, small, error)) {
    std::fprintf(stderr, "[thumbnail] %ux%u kept next to the save: %ls\n", small.width, small.height, file.wstring().c_str());
  } else {
    std::fprintf(stderr, "[thumbnail] %s\n", error.c_str());
  }
}

void PictureArrived(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h) {
  if (ctx == &sKeepTag) {
    std::lock_guard<std::mutex> lock(sThumbLock);
    sKeepBusy = false;
    if (rgba != nullptr && w > 0 && h > 0) {
      sHist.emplace_back(sKeepStamp, th::Shrink(rgba, w, h));
      if (sHist.size() > 12) sHist.erase(sHist.begin());
    }
    if (EnvIs("XD_THUMBNAIL_LOG", "1")) std::fprintf(stderr, "[thumbnail] field picture kept (%u x %u)\n", w, h);
    return;
  }
  fs::path file;
  {
    std::lock_guard<std::mutex> lock(sThumbLock);
    file = sThumbFile;
    sThumbFile.clear();
    sThumbBusy = false;
  }
  if (rgba == nullptr || w == 0 || h == 0 || file.empty()) {
    std::fprintf(stderr, "[thumbnail] no picture could be made\n");
    return;
  }
  WriteIt(file, th::Shrink(rgba, w, h));
}
}

void ThumbnailAfterSave(const fs::path& file) {
  if (EnvIs("XD_THUMBNAILS", "0")) return;
  std::lock_guard<std::mutex> lock(sThumbLock);
  sThumbFile = file;
  sThumbWait = PortVI_GetFlipCount() + 5;
}

void ThumbnailFrame() {
  if (EnvIs("XD_THUMBNAILS", "0")) return;
  const uint32_t now = PortVI_GetFlipCount();
  fs::path file;
  th::Picture keep;
  bool grab = false;
  {
    std::lock_guard<std::mutex> lock(sThumbLock);
    if (sThumbFile.empty()) {
      static uint32_t sLogged = 0;
      if (EnvIs("XD_THUMBNAIL_LOG", "1") && now - sLogged >= kKeepEvery * 4) {
        sLogged = now;
        int wid = 0;
        const int st = PortXD_ActiveWindowState(&wid);
        std::fprintf(stderr, "[thumbnail] active window %d state %d\n", wid, st);
      }
      if (!sKeepBusy && now - sLastKeep >= kKeepEvery && MenuClosed()) {
        sLastKeep = now;
        sKeepStamp = now;
        sKeepBusy = true;
        grab = true;
      }
    } else if (!sThumbBusy && static_cast<int32_t>(now - sThumbWait) >= 0) {
      file = sThumbFile;
      if (!sHist.empty()) {
        size_t pick = 0;
        for (size_t i = 0; i < sHist.size(); i++) {
          if (static_cast<int32_t>(sThumbWait - sHist[i].first) >= static_cast<int32_t>(kBefore)) pick = i;
        }
        keep = std::move(sHist[pick].second);
        sChosen = keep;
        sChosenAt = now;
        sHist.clear();
        sThumbFile.clear();
      } else if (!sChosen.Empty() && now - sChosenAt < 900) {
        keep = sChosen;
        sThumbFile.clear();
      } else {
        sThumbBusy = true;
        grab = true;
      }
    }
  }
  if (!keep.Empty()) WriteIt(file, keep);
  else if (grab) PortShot_GamePicture(PictureArrived, file.empty() ? static_cast<void*>(&sKeepTag) : nullptr);
}

Picture LoadThumbnail(const fs::path& file) {
  Picture out;
  th::Picture p;
  std::string error;
  if (!th::ReadFile(file, p, error)) return out;
  out.width = p.width, out.height = p.height, out.rgba = std::move(p.rgba);
  return out;
}

}
