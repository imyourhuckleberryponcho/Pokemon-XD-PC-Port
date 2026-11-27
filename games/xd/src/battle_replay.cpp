#include "xd_mod_assets.h"
#include "battle_replay.h"

#include "online_battle.h"
#include "xd_challenges.h"
#include "xd_playthrough.h"
#include "xd_scripts.h"

#include <battle_replay_file.h>
#include <port_message.h>
#include <port_photo.h>
#include <port_speedups.h>
#include <port_user_folder.h>
#include <settings.h>

#include <dolphin/pad.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
void* savedataGetStatus(void* save, int32_t kind);
extern uint32_t* __hsd_rand_seed_hold;
void* fightEncountGetStrObj(void);
void* fightEncountDataBiosGetPtr(uint16_t id);
uint8_t fightEncountDataBiosGetFightKind(void* data);
uint16_t fightEncountDataBiosGetFightTrainerDataId(void* data, uint16_t index);
uint8_t fightEncountDataBiosGetTrainer(void* data);
uint32_t fightEncount(uint16_t id);
uint16_t fightGetFightResultId(void);
uint16_t fightFloor_GetTurnCount(void* floor);
uint8_t fightFloorIsGcHeroWin(void* floor, uint16_t result);
void fightTrainerAllInitFightActionBuff(void* trainer);
void* fightFloorGetFightOutBanmeToValidFightOutPokemon(void* floor, uint16_t banme);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint32_t figthOutPokemonGetPokemonDataId(void* fop);
uint16_t pokemon_GetHp(void* poke);
void* GSthreadCreate(int32_t affinity, int32_t group, int32_t stackSize, int32_t usesFPU, int32_t autoStart, void* entry);
uint32_t GSfloorGetCurrentFloorGroupID(void);
uint32_t PortVI_GetFlipCount(void);
void PortVideo_Notice(const char* text, double seconds);
void PortXD_SaveReplayBarSetting(void);
const char* PortXD_BattleMods(void);
const char* PortXD_BattleModsWhy(void);
const char* PortXD_ActiveMods(void);
const char* PortXD_ExtraAssetsText(void);
const char* PortXD_PortCommit(void);
const char* PortXD_SnapshotId(void);
const char* PortXD_PortVersion(void);
void PortInput_StopRecording(void);
void PortLog_Flush(void);
extern void (*PortXD_OnBattleStart)(uint32_t battleID);
extern void (*PortXD_OnBattleEnd)(uint32_t battleID, uint32_t result, int32_t lost);
}

namespace xd::replay {

namespace {

namespace fs = std::filesystem;
namespace es = engine::settings;
using port_replay::File;

constexpr size_t kSaveImage = 0x27FD0;
constexpr size_t kEncountSize = 0x1C;
const char kStateMagic[4] = {'X', 'D', 'S', '1'};

void Log(const char* fmt, ...) {
  char buf[600];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "[replay] %s\n", buf);
  std::fflush(stderr);
}

bool EnvSet(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}
void SetEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

std::string U8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

std::string Hex8(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "%08x", v);
  return b;
}

std::string ActiveChallenges() {
  std::string out;
  for (const xd::challenges::Rule& r : xd::challenges::Rules()) {
    if (xd::challenges::Active(r.id)) out += (out.empty() ? "" : ",") + std::string(r.id);
  }
  return out;
}

fs::path ReplayFolder() {
  if (const char* d = std::getenv("XD_REPLAY_DIR"); d != nullptr && d[0] != '\0') return fs::u8path(d);
  return PortUser_Folder("Pokemon XD") / "replays";
}

struct Recording {
  bool on = false;
  bool online = false;
  File file;
  uint32_t encounter = 0, seed0 = 0, choices = 0, turns = 0;
  int fightKind = 0;
  bool important = false;
  std::vector<std::string> body;
  std::string state;
  uint32_t frame0 = 0, result = 0;
};
Recording R, sLast;
uint32_t sCosCount = 0;
std::vector<uint32_t> sGaps;
std::mutex sLock;
void (*sChainStart)(uint32_t) = nullptr;
void (*sChainEnd)(uint32_t, uint32_t, int32_t) = nullptr;

std::string SettingText() {
  const es::Registry& r = es::Global();
  const size_t i = r.Find("gameplay.keep_battle_replays");
  return i == es::kNoSetting ? std::string("off") : r.ValueText(i);
}

int KeptCount() {
  const es::Registry& r = es::Global();
  const size_t i = r.Find("gameplay.battle_replays_kept");
  if (i == es::kNoSetting) return 50;
  const int n = std::atoi(r.ValueText(i).c_str());
  return n < 1 ? 1 : n;
}

std::string SettingsForFile() {
  std::string text = es::Global().ReplayText();
  std::string out;
  size_t at = 0;
  while (at <= text.size()) {
    size_t end = text.find(';', at);
    if (end == std::string::npos) end = text.size();
    const std::string item = text.substr(at, end - at);
    if (!item.empty() && item.rfind("preset=", 0) != 0 && item.rfind("video.", 0) != 0 && item.rfind("audio.", 0) != 0 &&
        item.rfind("controls.", 0) != 0 && item.rfind("accessibility.", 0) != 0 && item.rfind("system.dev_menu", 0) != 0) {
      out += (out.empty() ? "" : ";") + item;
    }
    at = end + 1;
  }
  return out;
}

std::string Now(const char* format) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof buf, format, &tm);
  return buf;
}

std::string ProbeText() {
  std::string s;
  for (uint16_t b = 0; b < 4; b++) {
    void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
    void* p = fop ? fightOutPokemonGetPokemonPtr(fop) : nullptr;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s%u/%u", b ? "," : "", fop ? figthOutPokemonGetPokemonDataId(fop) : 0u, p ? pokemon_GetHp(p) : 0u);
    s += buf;
  }
  return s;
}

void FillHeader(File& f, const Recording& r, uint32_t result, bool complete) {
  f.header.clear();
  f.Set("kind", r.online ? "online" : "story");
  f.Set("encounter", std::to_string(r.encounter));
  f.Set("fight_kind", std::to_string(r.fightKind));
  f.Set("created", Now("%Y-%m-%d %H:%M:%S"));
  f.Set("port_version", PortXD_PortVersion());
  f.Set("port_commit", PortXD_PortCommit());
  f.Set("snapshot", PortXD_SnapshotId());
  f.Set("disc", "GXXE01");
  f.Set("endian", "pc");
  f.Set("settings", SettingsForFile());
  f.Set("mods", PortXD_ActiveMods() ? PortXD_ActiveMods() : "");
  f.Set("battle_mods", PortXD_BattleMods() ? PortXD_BattleMods() : "");
  f.Set("models", PortXD_ExtraAssetsText() ? PortXD_ExtraAssetsText() : "");
  f.Set("randomizer", xd::pt::LoadedRandomizer());
  if (!xd::pt::LoadedMoreShadows().empty()) f.Set("more_shadows", xd::pt::LoadedMoreShadows());
  f.Set("challenges", ActiveChallenges());
  f.Set("seed", Hex8(r.seed0));
  f.Set("turns", std::to_string(r.turns));
  f.Set("choices", std::to_string(r.choices));
  f.Set("complete", complete ? "yes" : "no");
  f.Set("result", std::to_string(result));
  f.Set("won", complete && fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) ? "yes" : "no");
}

bool SaveFile(const Recording& r, uint32_t result, bool complete, const fs::path& folder, const std::string& prefix, std::string* pathOut) {
  File f;
  FillHeader(f, r, result, complete);
  f.state = r.state;
  f.body = r.body;
  std::string bytes, why;
  if (!port_replay::Write(f, bytes, &why)) {
    Log("could not make the file: %s", why.c_str());
    return false;
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path path = folder / (prefix + Now("%Y%m%d_%H%M%S") + "_" + (r.online ? "online" : "e" + std::to_string(r.encounter)) + ".xdreplay");
  const fs::path tmp = fs::path(path).replace_extension(".part");
  {
    std::ofstream out(tmp, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      Log("could not write %s", U8(tmp).c_str());
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    Log("could not finish %s", U8(path).c_str());
    return false;
  }
  Log("saved %s (%zu bytes; state %zu bytes, %u choices, %u turns, result %u)", U8(path).c_str(), bytes.size(), r.state.size(), r.choices, r.turns, result);
  if (pathOut) *pathOut = U8(path);
  xd::scripts::NotifyReplayKept(U8(path.filename()).c_str(), r.encounter, complete && fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) ? 1 : 0);
  return true;
}

void Prune(const fs::path& folder, int keep) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (e.is_regular_file() && e.path().extension() == ".xdreplay" && e.path().filename().string().rfind("auto_", 0) == 0) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  while (static_cast<int>(files.size()) > keep) {
    fs::remove(files.front(), ec);
    files.erase(files.begin());
  }
}

bool IsImportant(uint32_t encounter) {
  void* d = fightEncountDataBiosGetPtr(static_cast<uint16_t>(encounter));
  return d != nullptr && fightEncountDataBiosGetTrainer(d) != 0;
}

void BeginRecording(uint32_t id) {
  R = Recording();
  void* d = fightEncountDataBiosGetPtr(static_cast<uint16_t>(id));
  const int kind = d ? fightEncountDataBiosGetFightKind(d) : -1;
  if (kind == 0xA || kind == 0xB || kind == 0x11) return;
  void* save = savedataGetStatus(nullptr, 0);
  void* enc = fightEncountGetStrObj();
  if (!save || !enc || !__hsd_rand_seed_hold) return;
  R.on = true;
  sCosCount = 0;
  sGaps.clear();
  R.encounter = id;
  R.fightKind = kind;
  R.important = IsImportant(id);
  R.seed0 = *__hsd_rand_seed_hold;
  R.frame0 = PortVI_GetFlipCount();
  std::string st(kStateMagic, 4);
  const uint32_t lens[2] = {static_cast<uint32_t>(kSaveImage), static_cast<uint32_t>(kEncountSize)};
  st.append(reinterpret_cast<const char*>(lens), sizeof lens);
  st.append(static_cast<const char*>(save), kSaveImage);
  st.append(static_cast<const char*>(enc), kEncountSize);
  R.state = std::move(st);
  R.body.push_back("start encounter " + std::to_string(id) + " seed " + Hex8(R.seed0) + " fightkind " + std::to_string(kind));
  Log("recording battle %u (fight kind %d, %s) seed %s", id, kind, R.important ? "trainer battle" : "wild", Hex8(R.seed0).c_str());
}

struct Choice {
  bool sw = false;
  int side = 0, ai = 0, r = 0;
  uint32_t seed = 0;
  std::string actions;
};
struct Turn {
  uint32_t seed = 0;
  uint64_t digest = 0;
};
enum class Phase { WaitTitle, Running, Finished };

struct Play {
  bool on = false;
  File file;
  std::string path;
  std::vector<Choice> choices;
  std::vector<std::vector<uint32_t>> segs;
  size_t seg = 0, segPos = 0;
  uint32_t jumpTurn = 0, curTurn = 1;
  bool jumping = false, jumpPause = true;
  bool liveNet = false;
  bool online = false;
  bool live = false;
  std::vector<Turn> turns;
  uint32_t seed0 = 0, encounter = 0, recordedResult = 0;
  std::string state;
  Phase phase = Phase::WaitTitle;
  uint32_t nextChoice = 0, turnsSeen = 0, turnsMatched = 0;
  bool diverged = false, started = false, exitAtEnd = false, refusedChecked = false;
  uint32_t tick = 0;
  float speed = 1.0f;
  std::chrono::steady_clock::time_point finishedAt;
  std::string divergeText;
};
Play P;
xd::online::SpectatorFeed sFeed;

bool Recording_() { return R.on && !R.online; }
bool Playing_() { return P.on && P.phase == Phase::Running && P.live && !P.online; }
inline void StepSeed(uint32_t n) {
  uint32_t* s = __hsd_rand_seed_hold;
  if (!s) return;
  uint32_t v = *s;
  while (n--) v = v * 214013u + 2531011u;
  *s = v;
}
inline void JournalGap() {
  if (Recording_()) {
    sGaps.push_back(sCosCount);
    sCosCount = 0;
  } else if (Playing_()) {
    uint32_t g = 0;
    if (P.seg < P.segs.size() && P.segPos < P.segs[P.seg].size()) g = P.segs[P.seg][P.segPos++];
    StepSeed(g);
  }
}
void JournalFlush() {
  if (!Recording_()) return;
  std::string line = "rng " + std::to_string(sGaps.size());
  for (uint32_t g : sGaps) line += " " + std::to_string(g);
  R.body.push_back(line);
  sGaps.clear();
  sCosCount = 0;
}

bool ParseBody(Play& p, std::string* why) {
  bool haveStart = false;
  for (const std::string& line : p.file.body) {
    std::istringstream in(line);
    std::string word;
    in >> word;
    if (word == "start") {
      std::string a, b;
      std::string seedText;
      in >> a >> p.encounter >> b >> seedText;
      p.seed0 = static_cast<uint32_t>(std::strtoul(seedText.c_str(), nullptr, 16));
      haveStart = true;
    } else if (word == "choice" || word == "switch") {
      Choice c;
      c.sw = word == "switch";
      uint32_t n;
      std::string s1, s2, s3, s4, seedText, bar;
      in >> n >> s1 >> c.side >> s2 >> c.ai >> s3 >> c.r >> s4 >> seedText;
      c.seed = static_cast<uint32_t>(std::strtoul(seedText.c_str(), nullptr, 16));
      if (!in || n != p.choices.size()) {
        if (why) *why = "A choice line of the replay is damaged.";
        return false;
      }
      in >> bar;
      std::getline(in, c.actions);
      while (!c.actions.empty() && c.actions.front() == ' ') c.actions.erase(0, 1);
      p.choices.push_back(c);
    } else if (word == "rng") {
      size_t n = 0;
      in >> n;
      std::vector<uint32_t> gaps;
      for (size_t i = 0; i < n && n < 10000000; ++i) {
        uint32_t g = 0;
        in >> g;
        gaps.push_back(g);
      }
      if (!in && n != 0) {
        if (why) *why = "A random-number line of the replay is damaged.";
        return false;
      }
      p.segs.push_back(std::move(gaps));
    } else if (word == "turn") {
      uint32_t t;
      std::string s1, s2, seedText, digestText;
      in >> t >> s1 >> seedText >> s2 >> digestText;
      if (!in || t > 5000) {
        if (why) *why = "A turn line of the replay is damaged.";
        return false;
      }
      if (p.turns.size() <= t) p.turns.resize(t + 1);
      p.turns[t].seed = static_cast<uint32_t>(std::strtoul(seedText.c_str(), nullptr, 16));
      p.turns[t].digest = std::strtoull(digestText.c_str(), nullptr, 16);
    } else if (word == "end") {
      std::string a;
      in >> a >> p.recordedResult;
    }
  }
  if (!haveStart) {
    if (why) *why = "The replay has no start line.";
    return false;
  }
  return true;
}

bool ParseOnline(std::string* why) {
  sFeed = xd::online::SpectatorFeed();
  sFeed.rulesKey = P.file.Get("rules_key");
  {
    int a = 0, b = 0;
    if (std::sscanf(P.file.Get("party").c_str(), "%d,%d", &a, &b) == 2) {
      sFeed.partyHost = a;
      sFeed.partyGuest = b;
    }
  }
  if (sFeed.rulesKey.empty()) {
    if (why) *why = "This online replay was made before replays kept the room's rules; it cannot be played.";
    return false;
  }
  for (const std::string& line : P.file.body) {
    std::istringstream in(line);
    std::string word;
    in >> word;
    if (word == "start") {
      std::string a, b, seedText;
      in >> a >> b >> seedText;
      sFeed.seed = static_cast<uint32_t>(std::strtoul(seedText.c_str(), nullptr, 16));
      sFeed.haveStart = true;
    } else if (word == "team") {
      std::string who, hex;
      in >> who >> hex;
      (who == "host" ? sFeed.hostTeam : sFeed.guestTeam).push_back(hex);
    } else if (word == "choice") {
      uint32_t seq = 0;
      in >> seq;
      std::string rest;
      std::getline(in, rest);
      while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
      if (!in && rest.empty()) {
        if (why) *why = "A choice line of the replay is damaged.";
        return false;
      }
      sFeed.choices[seq] = rest;
    } else if (word == "switch") {
      uint32_t seq = 0;
      int r = 0;
      in >> seq >> r;
      sFeed.switches[seq] = r;
    } else if (word == "cseed" || word == "sseed") {
      uint32_t seq = 0;
      std::string hex;
      in >> seq >> hex;
      (word == "cseed" ? sFeed.choiceSeeds : sFeed.switchSeeds)[seq] = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
    } else if (word == "turn") {
      uint32_t t = 0;
      std::string d, hex;
      in >> t >> d >> hex;
      sFeed.digests[t] = std::strtoull(hex.c_str(), nullptr, 16);
    } else if (word == "end") {
      std::string a;
      in >> a >> sFeed.result;
      sFeed.complete = true;
    }
  }
  if (!sFeed.haveStart || sFeed.hostTeam.empty() || sFeed.guestTeam.empty()) {
    if (why) *why = "This online replay has no teams or start line.";
    return false;
  }
  P.recordedResult = sFeed.result;
  return true;
}

bool LoadFile(const std::string& path, File& file, std::string* why) {
  std::ifstream in(fs::u8path(path), std::ios::binary);
  if (!in) {
    if (why) *why = "The replay file could not be opened: " + path;
    return false;
  }
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return port_replay::Parse(bytes, file, why);
}

void Refuse(const std::string& text) {
  std::fprintf(stderr, "[replay] REFUSED: %s\n", text.c_str());
  std::fflush(stderr);
  PortLog_Flush();
  if (!EnvSet("XD_NO_MESSAGE_BOXES") && !EnvSet("XD_WINDOW_OFFSCREEN") && !EnvSet("XD_WINDOW_HIDDEN")) {
    PortMessage_Show(PORT_MESSAGE_WARNING, "Pokemon XD", ("This battle replay cannot be played.\n\n" + text).c_str());
  }
}

fs::path sCardDir;

void Quit(int code) {
  if (!sCardDir.empty()) {
    std::error_code ec;
    fs::remove_all(sCardDir, ec);
  }
  PortInput_StopRecording();
  std::fflush(stdout);
  std::fflush(stderr);
  PortLog_Flush();
  std::_Exit(code);
}

void Diverge(const std::string& text) {
  if (P.diverged) return;
  P.diverged = true;
  P.divergeText = text;
  Log("DIVERGED: %s", text.c_str());
  PortVideo_Notice(text.c_str(), 12.0);
  PortPhoto_Request(true);
  if (P.exitAtEnd) {
    Log("the run ends here: the replay cannot go on (XD_BATTLE_REPLAY_EXIT)");
    Quit(4);
  }
}

void JumpBegin() {
  if (P.jumpTurn <= 1) return;
  P.jumping = true;
  PortSpeedups_SetReplayFactor(16.0f);
  Log("jumping to turn %u: running fast from the start", P.jumpTurn);
}

}

void NoteTurn(uint32_t turn) {
  if (!P.on) return;
  P.curTurn = turn;
  if (P.jumping && turn >= P.jumpTurn) {
    P.jumping = false;
    PortSpeedups_SetReplayFactor(P.speed);
    Log("jumped to turn %u at game frame %u", turn, PortVI_GetFlipCount());
    PortVideo_Notice(("Turn " + std::to_string(turn) + ".  Space = go on.").c_str(), 5.0);
    if (P.jumpPause && !P.exitAtEnd) PortPhoto_Request(true);
  }
}

bool Watching() { return P.on; }
bool KeysScripted() {
  static const bool scripted = std::getenv("XD_BATTLE_REPLAY_KEYS") != nullptr;
  return scripted;
}

bool Prepare() {
  const bool live = !EnvSet("XD_BATTLE_REPLAY") && EnvSet("XD_SPECTATE");
  if (!EnvSet("XD_BATTLE_REPLAY") && !live) return true;
  P = Play();
  P.on = true;
  PortPhoto_PadAResumes(true);
  P.liveNet = live;
  std::string why;
  if (live) {
    P.online = true;
    P.path = "(live room)";
    sFeed = xd::online::SpectatorFeed();
    if (!xd::online::SpectatorJoin(&sFeed, std::getenv("XD_SPECTATE"), &why)) {
      Refuse(why);
      return false;
    }
  } else {
  P.path = std::getenv("XD_BATTLE_REPLAY");
  const bool loaded = LoadFile(P.path, P.file, &why);
  P.online = loaded && P.file.Get("kind") == "online";
  if (!loaded || (P.online ? !ParseOnline(&why) : !ParseBody(P, &why))) {
    Refuse(why);
    PortVideo_Notice(why.c_str(), 1.0);
    return false;
  }
  P.state = P.file.state;
  if (!P.online && (P.state.size() < 12 || std::memcmp(P.state.data(), kStateMagic, 4) != 0)) {
    Refuse("The replay's starting state is not one this game can read.");
    return false;
  }
  std::string snapshotWanted = P.file.Get("snapshot"), snapshotHave = PortXD_SnapshotId();
  if (snapshotWanted != snapshotHave) {
    Refuse("This replay was made with another game build (decomp snapshot " + snapshotWanted + "; this game is built from " + snapshotHave +
           "). Its battle would not play the same. Watch it with the version that made it (" + P.file.Get("port_version") + ").");
    return false;
  }
  if (P.file.Get("endian") != "pc") {
    Refuse("This replay has a byte order this game does not read.");
    return false;
  }
  if (P.file.Get("kind") != "story" && !P.online) {
    Refuse("This replay is of a kind this game does not know (\"" + P.file.Get("kind") + "\").");
    return false;
  }
  }
  std::string list = live ? std::string() : P.file.Get("settings");
  list += std::string(list.empty() ? "" : ";") + "video.photo_mode=on;system.settings_entry=off";
  SetEnvVar("XD_PRESET", "original");
  if (const char* own = std::getenv("XD_SETTINGS"); own != nullptr && own[0] != '\0') list = std::string(own) + ";" + list;
  SetEnvVar("XD_SETTINGS", list);
  {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("xd_replay_card_" +
#ifdef _WIN32
                                                       std::to_string(_getpid()));
#else
                                                       std::to_string(getpid()));
#endif
    fs::create_directories(dir, ec);
    sCardDir = dir;
    SetEnvVar("XD_CARD_DIR", U8(dir));
  }
  if (!live && !P.file.Get("randomizer").empty() && !EnvSet("XD_RANDOMIZER")) SetEnvVar("XD_RANDOMIZER", P.file.Get("randomizer"));
  SetEnvVar("XD_SETTINGS_ENTRY", "0");
  if (!EnvSet("XD_RESTART_INTENT")) SetEnvVar("XD_RESTART_INTENT", "title");
  P.exitAtEnd = EnvSet("XD_BATTLE_REPLAY_EXIT");
  if (const char* t = std::getenv("XD_BATTLE_REPLAY_TURN"); t != nullptr && std::atoi(t) > 1) P.jumpTurn = static_cast<uint32_t>(std::atoi(t));
  if (const char* jp = std::getenv("XD_BATTLE_REPLAY_JUMP_PAUSE"); jp != nullptr && jp[0] == '0') P.jumpPause = false;
  if (live) {
    Log("watching a room live (XD_SPECTATE): the battle starts when the host's start arrives");
  } else if (P.online) {
    Log("watching %s: an online battle, %zu choices, %zu switches, %zu turns, recorded by the %s, made with %s", P.path.c_str(), sFeed.choices.size(), sFeed.switches.size(),
        sFeed.digests.size(), P.file.Get("recorded_by").c_str(), P.file.Get("port_version").c_str());
  } else {
    Log("watching %s: battle %u, %zu choices, %zu turns, made with %s", P.path.c_str(), P.encounter, P.choices.size(), P.turns.size() ? P.turns.size() - 1 : 0,
        P.file.Get("port_version").c_str());
  }
  return true;
}

namespace {

void OnStart(uint32_t id) {
  if (P.on) {
    if (P.phase == Phase::Running && !P.online && id == P.encounter && __hsd_rand_seed_hold) {
      *__hsd_rand_seed_hold = P.seed0;
      xd::online::CosmeticSeedReset();
      P.seg = 0;
      P.segPos = 0;
      P.live = true;
      JumpBegin();
    }
  } else if (SettingText() != "off") {
    std::lock_guard<std::mutex> lock(sLock);
    BeginRecording(id);
  }
  if (sChainStart) sChainStart(id);
}

void OnEnd(uint32_t id, uint32_t result, int32_t lost) {
  if (sChainEnd) sChainEnd(id, result, lost);
  std::lock_guard<std::mutex> lock(sLock);
  if (R.on) {
    Log("battle %u ended without a finish record: not kept", id);
    R = Recording();
  }
}

}

bool SplitOn() { return Playing_(); }
void CosmeticDrawn() {
  if (Recording_()) ++sCosCount;
}
void LogicDraw() {
  if (R.on || P.on) JournalGap();
}

void HookBattles() {
  if (PortXD_OnBattleStart != OnStart) {
    sChainStart = PortXD_OnBattleStart;
    PortXD_OnBattleStart = OnStart;
  }
  if (PortXD_OnBattleEnd != OnEnd) {
    sChainEnd = PortXD_OnBattleEnd;
    PortXD_OnBattleEnd = OnEnd;
  }
}

namespace {

bool PlayChoice(uint32_t n, bool sw, void* trainer, Choice** out) {
  if (n >= P.choices.size()) {
    Diverge("The battle asked for more choices than the replay holds (choice " + std::to_string(n + 1) + ").");
    return false;
  }
  Choice& c = P.choices[n];
  if (c.sw != sw || c.side != xd::online::TrainerSide(trainer)) {
    Diverge("Choice " + std::to_string(n + 1) + " comes from another trainer or kind than recorded.");
    return false;
  }
  *out = &c;
  return true;
}

}

uint8_t SelectAction(void* trainer, uint32_t fightType, uint8_t (*real)(void*, uint32_t)) {
  const bool recording = R.on;
  const bool playing = P.on && P.phase == Phase::Running && !P.diverged && !P.online;
  if (!recording && !playing) return real(trainer, fightType);
  uint32_t* seed = __hsd_rand_seed_hold;
  if (recording) {
    const uint32_t n = R.choices++;
    const int side = xd::online::TrainerSide(trainer);
    const int ai = xd::online::TrainerIsAi(trainer) ? 1 : 0;
    const uint8_t r = real(trainer, fightType);
    const std::string actions = xd::online::DescribeActions(trainer);
    JournalFlush();
    char head[96];
    std::snprintf(head, sizeof head, "choice %u side %d ai %d r %d seed %s | ", n, side, ai, static_cast<int>(r), Hex8(seed ? *seed : 0).c_str());
    std::lock_guard<std::mutex> lock(sLock);
    R.body.push_back(std::string(head) + actions);
    Log("choice %u: side %d %s: %s", n, side, ai ? "AI" : "menu", actions.c_str());
    return r;
  }
  const uint32_t n = P.nextChoice++;
  Choice* c = nullptr;
  if (!PlayChoice(n, false, trainer, &c)) return real(trainer, fightType);
  uint8_t r;
  if (c->ai) {
    r = real(trainer, fightType);
    const std::string mine = xd::online::DescribeActions(trainer);
    if (mine != c->actions) Log("choice %u: the AI chose %s, recorded %s: the recorded one is used", n, mine.c_str(), c->actions.c_str());
  } else {
    fightTrainerAllInitFightActionBuff(trainer);
    r = 1;
  }
  if (c->actions.empty() || !xd::online::ApplyActions(trainer, c->actions)) {
    Diverge("Choice " + std::to_string(n + 1) + " could not be given to the battle.");
    return real(trainer, fightType);
  }
  r = static_cast<uint8_t>(c->r);
  if (seed) *seed = c->seed;
  P.seg++;
  P.segPos = 0;
  Log("choice %u given: side %d %s", n, c->side, c->ai ? "AI" : "menu");
  return r;
}

int16_t SelectIrekae(void* trainer, uint32_t a, uint32_t b, uintptr_t c, int16_t (*real)(void*, uint32_t, uint32_t, uintptr_t)) {
  const bool recording = R.on;
  const bool playing = P.on && P.phase == Phase::Running && !P.diverged && !P.online;
  if (!recording && !playing) return real(trainer, a, b, c);
  uint32_t* seed = __hsd_rand_seed_hold;
  if (recording) {
    const uint32_t n = R.choices++;
    const int side = xd::online::TrainerSide(trainer);
    const int ai = xd::online::TrainerIsAi(trainer) ? 1 : 0;
    const int16_t r = real(trainer, a, b, c);
    JournalFlush();
    char head[120];
    std::snprintf(head, sizeof head, "switch %u side %d ai %d r %d seed %s | ", n, side, ai, static_cast<int>(r), Hex8(seed ? *seed : 0).c_str());
    std::lock_guard<std::mutex> lock(sLock);
    R.body.push_back(head);
    Log("switch choice %u: side %d %s: %d", n, side, ai ? "AI" : "menu", static_cast<int>(r));
    return r;
  }
  const uint32_t n = P.nextChoice++;
  Choice* ch = nullptr;
  if (!PlayChoice(n, true, trainer, &ch)) return real(trainer, a, b, c);
  if (ch->ai) real(trainer, a, b, c);
  if (seed) *seed = ch->seed;
  P.seg++;
  P.segPos = 0;
  Log("switch choice %u given: %d", n, ch->r);
  return static_cast<int16_t>(ch->r);
}

void TurnEnd() {
  if (P.online) return;
  if (!R.on && !(P.on && P.phase == Phase::Running)) return;
  const uint32_t turn = fightFloor_GetTurnCount(nullptr);
  JournalGap();
  const uint64_t digest = xd::online::BattleDigest(turn);
  const uint32_t seed = __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0;
  const std::string probe = ProbeText();
  if (R.on) {
    char line[160];
    std::snprintf(line, sizeof line, "turn %u seed %s digest %llx hp %s", turn, Hex8(seed).c_str(), static_cast<unsigned long long>(digest), probe.c_str());
    std::lock_guard<std::mutex> lock(sLock);
    R.body.push_back(line);
    R.turns = turn;
    Log("turn %u seed %s digest %llx hp %s", turn, Hex8(seed).c_str(), static_cast<unsigned long long>(digest), probe.c_str());
    return;
  }
  P.turnsSeen++;
  NoteTurn(turn);
  const bool have = turn < P.turns.size() && P.turns[turn].digest != 0;
  if (have && P.turns[turn].digest == digest && P.turns[turn].seed == seed) {
    P.turnsMatched++;
    Log("turn %u seed %s digest %llx hp %s: matches the recording", turn, Hex8(seed).c_str(), static_cast<unsigned long long>(digest), probe.c_str());
  } else if (have) {
    Log("turn %u seed %s digest %llx hp %s: DIFFERENT (recorded seed %s digest %llx)", turn, Hex8(seed).c_str(), static_cast<unsigned long long>(digest), probe.c_str(),
        Hex8(P.turns[turn].seed).c_str(), static_cast<unsigned long long>(P.turns[turn].digest));
    Diverge("From turn " + std::to_string(turn) + " this replay plays differently on this PC. Watching stopped here; turns before it were as recorded.");
  } else {
    Log("turn %u: the recording has no turn %u", turn, turn);
    Diverge("The battle went on longer than the recording (turn " + std::to_string(turn) + ").");
  }
}

void BattleEnd(uint32_t result) {
  if (P.online) return;
  if (P.on && P.phase == Phase::Running) {
    P.phase = Phase::Finished;
    P.finishedAt = std::chrono::steady_clock::now();
    uint32_t recordedTurns = 0;
    for (const Turn& t : P.turns) recordedTurns += t.digest != 0 ? 1u : 0u;
    const bool same = !P.diverged && result == P.recordedResult && P.turnsMatched == recordedTurns;
    Log("RESULT: %s (battle result %u, recorded %u; %u of %u turns matched the recording) at game frame %u", same ? "SAME as recorded" : "DIFFERENT from recorded", result,
        P.recordedResult, P.turnsMatched, recordedTurns, PortVI_GetFlipCount());
    if (same) PortVideo_Notice("Replay finished: it played exactly as recorded.  R = again, Esc = quit.", 8.0);
    if (P.exitAtEnd) Quit(same ? 0 : 4);
    PortPhoto_Request(true);
    return;
  }
  std::lock_guard<std::mutex> lock(sLock);
  if (!R.on) return;
  const bool complete = true;
  JournalFlush();
  R.body.push_back("end result " + std::to_string(result) + " turns " + std::to_string(R.turns));
  Recording done = R;
  done.result = result;
  R = Recording();
  sLast = done;
  const std::string setting = SettingText();
  if (setting == "every" || (setting == "important" && (done.important || done.online))) {
    const fs::path folder = ReplayFolder();
    std::string path;
    if (SaveFile(done, result, complete, folder, "auto_", &path)) {
      Prune(folder, KeptCount());
      PortVideo_Notice("Battle replay kept.", 3.0);
    }
  } else {
    Log("battle finished (not kept: setting %s%s); F7 keeps it", setting.c_str(), done.important ? "" : ", a wild battle");
  }
}

namespace {
struct OnlineRec {
  bool on = false, host = false;
  std::string rules, rulesKey, previewText;
  int protocol = 0;
  int partyHost = 0, partyGuest = 0;
  std::vector<std::string> hostTeam, guestTeam;
  uint32_t seed = 0;
  std::map<uint32_t, std::string> choices, switches;
  std::map<uint32_t, uint32_t> choiceSeeds, switchSeeds;
  std::vector<std::pair<uint32_t, uint64_t>> turns;
  std::string fileName;
};
OnlineRec O;
}

void OnlineBegin(bool host, const std::string& rulesText, const std::vector<std::string>& hostTeamHex, const std::vector<std::string>& guestTeamHex, uint32_t seed) {
  O = OnlineRec();
  if (SettingText() == "off") return;
  O.on = true;
  O.host = host;
  O.rules = rulesText;
  O.hostTeam = hostTeamHex;
  O.guestTeam = guestTeamHex;
  O.seed = seed;
  O.fileName = "auto_" + Now("%Y%m%d_%H%M%S") + "_online.xdreplay";
  Log("recording the online battle (%s)", host ? "hosting" : "joined");
}
void OnlineParty(int hostCount, int guestCount) {
  if (O.on) {
    O.partyHost = hostCount;
    O.partyGuest = guestCount;
  }
}
void OnlineSeed(uint32_t seq, uint32_t seed) {
  if (O.on) O.choiceSeeds.emplace(seq, seed);
}
void OnlineSwitchSeed(uint32_t seq, uint32_t seed) {
  if (O.on) O.switchSeeds.emplace(seq, seed);
}
void OnlineExtras(int protocol, const std::string& previewText) {
  if (O.on) { O.protocol = protocol; O.previewText = previewText; }
}
void OnlineRulesKey(const std::string& key) {
  if (O.on) O.rulesKey = key;
}
void OnlineChoice(uint32_t seq, const std::string& line) {
  if (O.on) O.choices[seq] = line;
}
void OnlineSwitch(uint32_t seq, int result) {
  if (O.on) O.switches[seq] = std::to_string(result);
}
namespace {
void OnlineSave(const OnlineRec& r, uint32_t result, bool complete, bool prune) {
  File f;
  f.Set("kind", "online");
  f.Set("created", Now("%Y-%m-%d %H:%M:%S"));
  f.Set("port_version", PortXD_PortVersion());
  f.Set("port_commit", PortXD_PortCommit());
  f.Set("snapshot", PortXD_SnapshotId());
  f.Set("disc", "GXXE01");
  f.Set("endian", "pc");
  f.Set("settings", SettingsForFile());
  f.Set("mods", PortXD_ActiveMods() ? PortXD_ActiveMods() : "");
  f.Set("battle_mods", PortXD_BattleMods() ? PortXD_BattleMods() : "");
  f.Set("models", PortXD_ExtraAssetsText() ? PortXD_ExtraAssetsText() : "");
  f.Set("rules", r.rules);
  f.Set("rules_key", r.rulesKey);
  if (r.protocol > 0) f.Set("protocol", std::to_string(r.protocol));
  if (!r.previewText.empty()) f.Set("preview", r.previewText);
  f.Set("party", std::to_string(r.partyHost) + "," + std::to_string(r.partyGuest));
  f.Set("recorded_by", r.host ? "host" : "guest");
  f.Set("seed", Hex8(r.seed));
  f.Set("turns", std::to_string(r.turns.size()));
  f.Set("choices", std::to_string(r.choices.size()));
  f.Set("result", std::to_string(result));
  f.Set("complete", complete ? "yes" : "no");
  f.body.push_back("start online seed " + Hex8(r.seed));
  for (const std::string& t : r.hostTeam) f.body.push_back("team host " + t);
  for (const std::string& t : r.guestTeam) f.body.push_back("team guest " + t);
  for (const auto& kv : r.choices) f.body.push_back("choice " + std::to_string(kv.first) + " " + kv.second);
  for (const auto& kv : r.switches) f.body.push_back("switch " + std::to_string(kv.first) + " " + kv.second);
  for (const auto& kv : r.choiceSeeds) f.body.push_back("cseed " + std::to_string(kv.first) + " " + Hex8(kv.second));
  for (const auto& kv : r.switchSeeds) f.body.push_back("sseed " + std::to_string(kv.first) + " " + Hex8(kv.second));
  for (const auto& t : r.turns) f.body.push_back("turn " + std::to_string(t.first) + " digest " + port_replay::Hex(t.second));
  if (complete) f.body.push_back("end result " + std::to_string(result) + " turns " + std::to_string(r.turns.size()));
  std::string bytes, why;
  if (!port_replay::Write(f, bytes, &why)) {
    Log("could not make the online file: %s", why.c_str());
    return;
  }
  const fs::path folder = ReplayFolder();
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path path = folder / r.fileName;
  const fs::path tmp = fs::path(path).replace_extension(".part");
  {
    std::ofstream out(tmp, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  fs::rename(tmp, path, ec);
  if (complete) {
    Log("saved %s (%zu bytes, %zu choices of both sides, %zu turns, result %u)", U8(path).c_str(), bytes.size(), r.choices.size(), r.turns.size(), result);
    xd::scripts::NotifyReplayKept(U8(path.filename()).c_str(), 0, fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) ? 1 : 0);
  }
  if (prune) Prune(folder, KeptCount());
}
}

void OnlineTurn(uint32_t turn, uint64_t digest) {
  if (!O.on) return;
  O.turns.emplace_back(turn, digest);
  OnlineSave(O, 0, false, false);
}
void OnlineEnd(uint32_t result, bool desync) {
  if (!O.on) return;
  OnlineRec done = O;
  O = OnlineRec();
  OnlineSave(done, result, !desync, true);
}

namespace {

void BattleThreadMain() {
  void* save = savedataGetStatus(nullptr, 0);
  void* enc = fightEncountGetStrObj();
  uint32_t lens[2];
  std::memcpy(lens, P.state.data() + 4, sizeof lens);
  if (lens[0] != kSaveImage || lens[1] != kEncountSize || P.state.size() != 12 + lens[0] + lens[1] || !save || !enc) {
    Refuse("The replay's starting state does not fit this game.");
    Quit(3);
  }
  std::memcpy(save, P.state.data() + 12, kSaveImage);
  std::memcpy(enc, P.state.data() + 12 + kSaveImage, kEncountSize);
  Log("starting battle %u from the saved state (seed %s) at game frame %u", P.encounter, Hex8(P.seed0).c_str(), PortVI_GetFlipCount());
  P.phase = Phase::Running;
  const uint32_t r = fightEncount(static_cast<uint16_t>(P.encounter));
  Log("the battle call returned %u", r);
  if (P.phase == Phase::Running) {
    P.phase = Phase::Finished;
    Log("RESULT: the battle ended without a finish (call returned %u)", r);
    if (P.exitAtEnd) Quit(5);
  }
}

bool CheckEnvironment() {
  const std::string have = PortXD_BattleMods() ? PortXD_BattleMods() : "";
  const std::string wanted = P.file.Get("battle_mods", "?") != "?" ? P.file.Get("battle_mods") : P.file.Get("mods");
  if (wanted != have) {
    const std::string why = PortXD_BattleModsWhy() ? PortXD_BattleModsWhy() : "";
    const std::string text = "This replay was made with the battle-changing mods \"" + (wanted.empty() ? std::string("none") : wanted) + "\"; this game has \"" +
                             (have.empty() ? std::string("none") : have) + "\" switched on" + (why.empty() ? std::string("") : " (here: " + why + ")") +
                             ". Mods that only change looks or sound never matter, but these change what a battle computes. Switch them to match (the Mods page of the launcher) and watch it again.";
    std::string assets;
    for (const auto& b : xd::modassets::Blocked()) {
      if (wanted.find(b.id) != std::string::npos) {
        assets += (assets.empty() ? "" : "; ") + b.id + " (needs " + engine::mods::AssetNames(b.assets) + ")";
      }
    }
    Refuse(text + (assets.empty() ? std::string() : " Not loaded here because it needs extra assets of your own that are not ready: " + assets + ". Make them on the launcher's Extra assets page."));
    return false;
  }
  const std::string wantRando = P.file.Get("randomizer"), haveRando = xd::pt::LoadedRandomizer();
  if (wantRando != haveRando) {
    Refuse("This replay was made with the randomizer code \"" + (wantRando.empty() ? std::string("none") : wantRando) + "\"; this game's data was made with \"" +
           (haveRando.empty() ? std::string("none") : haveRando) + "\". Its battle would not play the same.");
    return false;
  }
  if (P.file.Get("more_shadows") != xd::pt::LoadedMoreShadows()) {
    const std::string want = P.file.Get("more_shadows"), have = xd::pt::LoadedMoreShadows();
    Refuse("This replay was made with More Shadow Pokemon \"" + (want.empty() ? std::string("off") : want) + "\"; this game has \"" + (have.empty() ? std::string("off") : have) +
           "\". Its battle would not play the same.");
    return false;
  }
  const std::string wantRules = P.file.Get("challenges");
  if (!P.file.Get("kind").empty() && P.file.Get("kind") == "story") {
    std::vector<std::string> ids;
    for (size_t at = 0; at < wantRules.size();) {
      size_t end = wantRules.find(',', at);
      if (end == std::string::npos) end = wantRules.size();
      if (end > at) ids.push_back(wantRules.substr(at, end - at));
      at = end + 1;
    }
    xd::challenges::SetActive(ids, "a battle replay: the rules it was played with");
    if (ActiveChallenges() != wantRules) {
      Refuse("This replay was made with the challenge rules \"" + (wantRules.empty() ? std::string("none") : wantRules) + "\"; this game could not switch on the same ones (" +
             (ActiveChallenges().empty() ? std::string("none") : ActiveChallenges()) + ").");
      return false;
    }
  }
  return true;
}

void RestartSelf() {
  Log("starting the replay again");
  PortInput_StopRecording();
  std::fflush(stderr);
  PortLog_Flush();
  SetEnvVar("XD_CARD_CLAIM", "0");
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / ("xd_replay_card_again_" + std::to_string(std::time(nullptr)));
  fs::create_directories(dir, ec);
  SetEnvVar("XD_CARD_DIR", U8(dir));
  SetEnvVar("XD_BATTLE_REPLAY_AGAIN", "1");
#ifdef _WIN32
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = GetCommandLineW();
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::_Exit(0);
  }
#elif defined(__APPLE__)
  char** argv = *_NSGetArgv();
  char self[4096];
  uint32_t selfSize = sizeof self;
  execv(_NSGetExecutablePath(self, &selfSize) == 0 ? self : argv[0], argv);
#else
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::string> args;
  for (size_t at = 0; at < all.size();) {
    const size_t z = all.find('\0', at);
    args.push_back(all.substr(at, z == std::string::npos ? std::string::npos : z - at));
    if (z == std::string::npos) break;
    at = z + 1;
  }
  std::vector<char*> argv;
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  execv("/proc/self/exe", argv.data());
#endif
  Log("could not start again");
}

void ToggleFreeCamera() {
  if (PortPhoto_Active() || !PortPhoto_Enabled()) return;
  PortPhoto_LiveSet(!PortPhoto_LiveActive());
  PortVideo_Notice(PortPhoto_LiveActive() ? "Free camera: W A S D Q E move, right mouse button looks, H hides menus, R resets, F off.  Pad: sticks, triggers, Y resets, right stick click off"
                                          : "Free camera off", PortPhoto_LiveActive() ? 9.0 : 3.0);
}

const char kBarSetting[] = "gameplay.replay_bar";
bool BarWanted() {
  const es::Registry& r = es::Global();
  return r.Find(kBarSetting) == es::kNoSetting || r.Bool(kBarSetting).Get();
}
void ToggleBar() {
  es::Registry& r = es::Global();
  if (r.Find(kBarSetting) == es::kNoSetting) return;
  r.SetBool(kBarSetting, !BarWanted());
  Log("the replay bar is now %s", BarWanted() ? "shown" : "hidden");
  PortXD_SaveReplayBarSetting();
}
uint32_t TotalTurns() {
  if (P.online) return P.liveNet ? 0u : static_cast<uint32_t>(sFeed.digests.size());
  uint32_t n = P.turns.size() > 1 ? static_cast<uint32_t>(P.turns.size() - 1) : 0u;
  if (n == 0) n = static_cast<uint32_t>(std::strtoul(P.file.Get("turns", "0").c_str(), nullptr, 10));
  return n;
}
void TurnOn() {
  if (P.phase == Phase::Running) {
    P.jumpTurn = P.curTurn + 1;
    P.jumping = true;
    PortPhoto_Request(false);
    PortSpeedups_SetReplayFactor(16.0f);
    Log("jumping on to turn %u", P.jumpTurn);
  }
}
void TurnBack() {
  if (P.curTurn > 2) {
    SetEnvVar("XD_BATTLE_REPLAY_TURN", std::to_string(P.curTurn - 1));
    RestartSelf();
  }
}

float StepSpeed(float now, int direction) {
  static const float kSteps[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
  int at = 2;
  for (int i = 0; i < 6; i++) {
    if (kSteps[i] <= now + 0.01f) at = i;
  }
  at = std::clamp(at + direction, 0, 5);
  return kSteps[at];
}

}

void BarFrame() {
  const uint32_t presses = PortSpeedups_ReplayBarPresses();
  if (!P.on || presses == 0) return;
  if (PortPhoto_Active() || PortPhoto_LiveActive()) return;
  ToggleBar();
}
bool BarGet(BarView* out) {
  if (!P.on || P.phase == Phase::WaitTitle) return false;
  out->turns = static_cast<int>(TotalTurns());
  out->turn = static_cast<int>(P.curTurn);
  if (out->turns > 0) out->turn = std::min(out->turn, out->turns);
  out->speed = P.speed;
  out->paused = PortPhoto_Active();
  out->camera = PortPhoto_LiveActive();
  out->finished = P.phase == Phase::Finished;
  out->full = BarWanted();
  return true;
}

void KeyScriptPoll() {
  static std::vector<std::pair<uint32_t, std::string>> script;
  static size_t next = 0;
  static bool read = false;
  if (!read) {
    read = true;
    if (const char* t = std::getenv("XD_BATTLE_REPLAY_KEYS")) {
      std::string all = t;
      for (size_t at = 0; at < all.size();) {
        size_t end = all.find(',', at);
        if (end == std::string::npos) end = all.size();
        const std::string part = all.substr(at, end - at);
        const size_t colon = part.find(':');
        if (colon != std::string::npos) script.emplace_back(static_cast<uint32_t>(std::strtoul(part.c_str(), nullptr, 10)), part.substr(colon + 1));
        at = end + 1;
      }
    }
  }
  while (next < script.size() && PortVI_GetFlipCount() >= script[next].first) {
    const std::string& k = script[next++].second;
    if (k == "bar") {
      Log("test key: bar at game frame %u", PortVI_GetFlipCount());
      if (P.on && !PortPhoto_Active() && !PortPhoto_LiveActive()) ToggleBar();
      continue;
    }
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = k == "space" ? SDL_SCANCODE_SPACE : k == "slower" ? SDL_SCANCODE_LEFTBRACKET : k == "faster" ? SDL_SCANCODE_RIGHTBRACKET : k == "restart" ? SDL_SCANCODE_R : k == "keep" ? SDL_SCANCODE_F7 : k == "freecam" ? SDL_SCANCODE_F : k == "jumpon" ? SDL_SCANCODE_PERIOD : k == "jumpback" ? SDL_SCANCODE_COMMA : SDL_SCANCODE_ESCAPE;
    Log("test key: %s at game frame %u", k.c_str(), PortVI_GetFlipCount());
    HandleEvent(&e);
  }
}

void StartOnlineWatch() {
  if (!P.liveNet) sFeed.lost = true;
  sFeed.onEnd = [](xd::online::SpectatorFeed& f) {
    P.phase = Phase::Finished;
    P.finishedAt = std::chrono::steady_clock::now();
    const bool complete = P.liveNet ? f.complete : P.file.Get("complete") == "yes";
    const uint32_t recorded = P.liveNet ? f.result : P.recordedResult;
    const bool same = f.turnsDifferent == 0 && f.turnsMatched == f.digests.size() && (!complete || f.endResult == recorded);
    Log("RESULT: %s (battle result %u, recorded %u; %u of %zu turns matched the %s, %u different) at game frame %u", same ? "SAME as recorded" : "DIFFERENT from recorded",
        f.endResult, recorded, f.turnsMatched, f.digests.size(), P.liveNet ? "players' numbers" : "recording", f.turnsDifferent, PortVI_GetFlipCount());
    if (same) PortVideo_Notice("Replay finished: it played exactly as recorded.  R = again, Esc = quit.", 8.0);
    if (P.exitAtEnd) Quit(same ? 0 : 4);
    PortPhoto_Request(true);
  };
  P.phase = Phase::Running;
  JumpBegin();
  if (P.liveNet) {
    auto inner = sFeed.poll;
    sFeed.poll = [inner](xd::online::SpectatorFeed& f) {
      if (inner) inner(f);
      static float lastFactor = 0.0f;
      const bool behind = f.digests.size() > f.turnsSeen + 2u;
      const float factor = behind ? 8.0f : P.speed;
      if (factor != lastFactor) {
        lastFactor = factor;
        PortSpeedups_SetReplayFactor(factor);
        Log("spectator: %s (%zu turns have arrived, %u played)", behind ? "catching up at 8x" : "at the viewer's speed", f.digests.size(), f.turnsSeen);
      }
    };
  }
  std::string why;
  if (!xd::online::SpectatorStart(&sFeed, &why)) {
    Refuse(why);
    Quit(3);
  }
  Log("the online battle is starting from the file at game frame %u", PortVI_GetFlipCount());
}

void PadPoll(void* statusArray, bool titleOpen) {
  KeyScriptPoll();
  if (!P.on) return;
  PADStatus* status = static_cast<PADStatus*>(statusArray);
  if (P.phase == Phase::WaitTitle) {
    if ((++P.tick) % 900 == 1) Log("waiting for the title menu (open %d)", titleOpen ? 1 : 0);
    if (!titleOpen || P.started) return;
    if (P.liveNet) {
      if (sFeed.poll) sFeed.poll(sFeed);
      if (!sFeed.haveStart) {
        if (sFeed.lost) {
          Refuse("The room could not be found (or the host's places are all taken).");
          Quit(3);
        }
        return;
      }
      P.refusedChecked = true;
    }
    if (!P.refusedChecked) {
      P.refusedChecked = true;
      if (!CheckEnvironment()) {
        PortVideo_Notice("This replay needs other mods than this game has on. See the log.", 1.0);
        Quit(2);
      }
    }
    P.started = true;
    if (P.online) {
      StartOnlineWatch();
      return;
    }
    void* t = GSthreadCreate(1, static_cast<int32_t>(GSfloorGetCurrentFloorGroupID()), 0x10000, 1, 1, reinterpret_cast<void*>(&BattleThreadMain));
    Log("battle thread %s", t ? "created" : "NOT created");
    if (!t) Quit(3);
    return;
  }
  if (P.phase == Phase::Running) {
    PADStatus* pad = nullptr;
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
      if (status[i].err == PAD_ERR_NONE) {
        pad = &status[i];
        break;
      }
    }
    if (PortPhoto_LiveActive()) {
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err != PAD_ERR_NONE) continue;
        status[i].button = 0;
        status[i].stickX = status[i].stickY = status[i].substickX = status[i].substickY = 0;
        status[i].triggerLeft = status[i].triggerRight = 0;
      }
    }
    const bool menusPressed = P.online && !xd::online::InFight();
    if (pad && !menusPressed && ((++P.tick) % 40) < 4) pad->button |= PAD_BUTTON_A;
  }
}

void HandleEvent(const void* sdlEvent) {
  const auto* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    if (!P.on) return;
    const auto b = e->gbutton.button;
    if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) ToggleFreeCamera();
    if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) TurnOn();
    else if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) TurnBack();
    else if (!PortPhoto_LiveActive() && !PortPhoto_Active()) {
      if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
        P.speed = StepSpeed(P.speed, b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? -1 : 1);
        PortSpeedups_SetReplayFactor(P.speed);
        Log("speed %gx", static_cast<double>(P.speed));
      } else if (b == SDL_GAMEPAD_BUTTON_SOUTH && P.phase == Phase::Running) {
        PortPhoto_Request(true);
      }
    }
    return;
  }
  if (e->type != SDL_EVENT_KEY_DOWN || e->key.repeat) return;
  const SDL_Scancode k = e->key.scancode;
  if (k == SDL_SCANCODE_F7) {
    std::lock_guard<std::mutex> lock(sLock);
    const Recording& r = R.on ? R : sLast;
    if (r.state.empty()) {
      PortVideo_Notice("No battle to keep yet (turn 'Keep battle replays' on in the settings).", 4.0);
      return;
    }
    std::string path;
    if (SaveFile(r, r.result, !R.on, ReplayFolder() / "saved", "saved_", &path)) PortVideo_Notice("Battle replay saved.", 3.0);
    return;
  }
  if (!P.on) return;
  switch (k) {
  case SDL_SCANCODE_SPACE:
    PortPhoto_Request(!PortPhoto_Active());
    break;
  case SDL_SCANCODE_LEFTBRACKET:
  case SDL_SCANCODE_MINUS:
    P.speed = StepSpeed(P.speed, -1);
    PortSpeedups_SetReplayFactor(P.speed);
    Log("speed %gx", static_cast<double>(P.speed));
    break;
  case SDL_SCANCODE_RIGHTBRACKET:
  case SDL_SCANCODE_EQUALS:
    P.speed = StepSpeed(P.speed, 1);
    PortSpeedups_SetReplayFactor(P.speed);
    Log("speed %gx", static_cast<double>(P.speed));
    break;
  case SDL_SCANCODE_RIGHT:
  case SDL_SCANCODE_PERIOD:
    TurnOn();
    break;
  case SDL_SCANCODE_LEFT:
  case SDL_SCANCODE_COMMA:
    TurnBack();
    break;
  case SDL_SCANCODE_F:
    ToggleFreeCamera();
    break;
  case SDL_SCANCODE_R:
    if (!PortPhoto_Active() && !PortPhoto_LiveActive()) RestartSelf();
    break;
  case SDL_SCANCODE_ESCAPE:
    if (!PortPhoto_Active()) Quit(0);
    break;
  default:
    break;
  }
}

std::string DescribeFile(const std::string& path, std::string* why) {
  File f;
  if (!LoadFile(path, f, why)) return std::string();
  std::string out;
  for (const auto& kv : f.header) out += kv.first + ": " + kv.second + "\n";
  out += "state bytes: " + std::to_string(f.state.size()) + "\nbody lines: " + std::to_string(f.body.size()) + "\n";
  return out;
}

}

extern "C" {
int PortXD_BattleReplayPrepare(void) { return xd::replay::Prepare() ? 1 : 0; }
int PortXD_BattleReplayWatching(void) { return xd::replay::Watching() ? 1 : 0; }
void PortXD_BattleReplayHandleEvent(const void* sdlEvent) { xd::replay::HandleEvent(sdlEvent); }
void PortXD_BattleReplayTurnEnd(void) { xd::replay::TurnEnd(); }
void PortXD_BattleReplayBattleEnd(uint32_t result) { xd::replay::BattleEnd(result); }
}

extern "C" {
uint32_t PortXD_RawRand(void);
float PortXD_RawRandf(void);
int32_t PortXD_RawRandi(int32_t max);
uint32_t HSD_Rand(void) {
  xd::replay::LogicDraw();
  return PortXD_RawRand();
}
float HSD_Randf(void) {
  xd::replay::LogicDraw();
  return PortXD_RawRandf();
}
int32_t HSD_Randi(int32_t max) {
  xd::replay::LogicDraw();
  return PortXD_RawRandi(max);
}
}
