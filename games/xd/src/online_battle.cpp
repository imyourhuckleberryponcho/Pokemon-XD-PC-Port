#include "online_battle.h"
#include "xd_mod_assets.h"
#include "xd_ruleset_texts.h"
#include "xd_rulesets.h"
#include "xd_forms.h"

#include "i18n.h"

#include "online_commit.h"
#include "battle_replay.h"
#include "online_draft_game.h"
#include "online_room.h"
#include "online_team.h"
#include "trainer_card_game.h"
#include "data/common_rel.h"
#include "xd_challenges.h"
#include "xd_gen4.h"
#include "xd_mod_battle.h"
#include "xd_gen_rules.h"
#include "xd_modern_types.h"
#include "xd_gen57.h"
#include "xd_xg.h"
#include "xd_playthrough.h"

#include <random>
#include <gc_memcard.h>
#include <port_disc.h>
#include <net_connection.h>
#include <xd_legality_disc.h>
#include <xd_save_convert.h>
#include <net_crypto.h>
#include <settings.h>

#include <algorithm>
#include <limits>
#include <array>
#include <chrono>
#include <system_error>
#include <utility>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
extern "C" int PortXD_CheatsUsedHere(void);

extern "C" {
uint8_t fightTrainerSelectFightAction(void* trainer, uint32_t side);
int16_t fightTrainerSelectIrekaeFightPokemon(void* trainer, uint32_t a, uint32_t b, uintptr_t c);
void fightTrainerAllInitFightActionBuff(void* trainer);
void* fightTrainerGetValidFightOutPokemonPtr(void* trainer, uint16_t index);
void fightOutPokemonInitFightActionBuff(void* fop);
void* fightOutPokemon_GetFightActionBuffPtr(void* fop);
uint8_t fightOutPokemon_GetFightActionFlag(void* fop);
uint8_t fightOutPokemonCheckFightOut(void* fop);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
uint8_t fightActionCheckValid(void* action);
uint16_t fightActionGetKindDataId(void* action);
int32_t fightActionBiosGetBuffDataId(void* action);
uint16_t fightWaza_GetTargetDataId(void* waza);
int8_t fightWaza_GetWazaBanme(void* waza);
uint8_t fightWaza_GetAutoMakeFlag(void* waza);
void* fightOutPokemonCreateFightAction(void* fop, int32_t a, int32_t kind, int32_t c, void* data, int32_t buffId);
void* fightOutPokemonCreateFightActionAttackWaza(void* fop, int32_t a, int32_t kind, int32_t c, void* data, int32_t waza,
                                                 uint16_t target, int8_t banme, uint8_t autoMake);
extern unsigned char fight_action_data_turn[];
extern unsigned char fight_action_data_item[];
void* fightOutPokemon_GetFightItemPtr(void* fop);
uint16_t fightItem_GetItemDataId(void* item);
uint16_t fightItem_GetTargetDataId(void* item);
int32_t fightItem_GetCount(void* item);
uint32_t fightItem_GetBuff(void* item);
void fightOutPokemonCreateFightActionUseItem(void* fop, int32_t a, int32_t kind, int32_t c, void* data, uint32_t item, uint16_t target, int32_t banme, int32_t buff);
void* fightTrainerBiosGetHeroPtr(void* trainer);
uint16_t pokemon_GetPokemonWazaDataId(void* pokemon, uint32_t slot);
uint8_t pokemonIsDarkPokemon(void* pokemon);
uint16_t pokemon_GetDarkpokemonDataId(void* pokemon);
void* fightTrainerBiosGetFightPokemonPtr(void* trainer, uint32_t slot);
void* fightPokemonGetPokemonPtr(void* fp);
uint16_t pokemon_GetPokemonDataId(void* poke);
int16_t fightPokemon_GetEntryId(void* fp);
bool fightPokemonCheckFightOut(void* fp);
uint8_t pokemonIsJoutaiDataId(void* pokemon, int32_t joutai);
uint8_t fightOutPokemonIsJoutaiDataId(void* fop, int32_t joutai);
int32_t itemUse2Pokemon(void* log, void* pokemon, void* fightPokemon, uint16_t item, uint8_t moveSlot);
void* savedataGetStatus(void* save, int32_t kind);
void* heroItemGetItemKindToItemAryPtr(void* hero, uint8_t kind, uint16_t* count, uint16_t* max, uint8_t* f1, uint8_t* f2);
void itemInitAry(void* ary, uint16_t count);
int32_t heroItemAddItemDataId(void* hero, uint32_t item, uint32_t num, int32_t slot);
int32_t heroItemDecItemDataId(void* hero, uint32_t item, uint32_t num, int32_t slot);
extern unsigned char fight_action_data_waza[];
uint16_t fightGetFightResultId(void);
uint8_t fightKind_CheckFightKind(uint8_t kind);
int32_t menuOpenCustom(int32_t id, int32_t a, void* cursor, int32_t b, int32_t c, int32_t d, void* work);
bool PortInput_LoadScript(const char* script);
int PortXD_ActiveWindowState(int* id);
uint32_t PortInput_ScriptNow(void);
uint16_t PortInput_DeliveredButtons(int port);
int32_t menuCloseCustom(int32_t id, int32_t a, int32_t b);
int32_t PortXD_MenuOpenCustom(int32_t id, int32_t a, void* cursor, int32_t b, int32_t c, int32_t d, void* work);
uint8_t fightFloor_GetFightKindDarkpokemonHypermodeFlag(void* floor);
void* fightFloorGetValidFightSidePtr(void* floor, uint16_t side);
void* fightSideGetValidFightTrainerPtr(void* side, uint16_t index);
uint8_t fightFloorSetFightResultId(void* floor, uint16_t id);
void* fightFloorGetFightOutPokemonPtrToFightTrainerPtr(void* floor, void* fop);
uint32_t fightFloorGetStatus(void* floor, uint16_t dataId, uint16_t status, uint16_t arg);
void fightPokemonCreate(void* trainer, void* fightPokemon, void* pokemon, int16_t entryId);
void* fightPokemon_GetPokemonBuffPtr(void* fp);
uint8_t pokemonBiosGetLevel(void* poke);
uint8_t pokemonCheckValid(void* poke);
void pokemonInit(void* poke);
uint8_t pokemonCheckFightOut(void* poke);
uint8_t menuCBBios_CurrentlyWorkingToolBattle(void);
void PortXD_PokemonSetLevelStats(void* poke, uint8_t level);
uint16_t* GSmsgGetGSchar(uint32_t id);
uint8_t fightFloorIsNowEncountTuusinTaisen(void* floor);
uint8_t fightFloorIsUseFightTimerCommand(void* floor);
uint8_t fightTimerCommandIsOver(void);
void fightFloorSetMenuFightAction(void* floor, void* fop, void* work);
uint32_t fightMenuFightTrainerGcHeroOpenMenu(void* trainer, uint16_t id, uint8_t cancel);
int16_t fightMenuFightTrainerGcHeroSelectIrekaeFightPokemon(void* trainer, void* fop, uint16_t id, uint8_t cancel, uint8_t nested);
void* fightTrainer_GetHeroPtr(void* trainer);
void* heroBiosGetPokemonPtr(void* hero, uint32_t index);
uint8_t fightFloorGetNowTenkouDataId(void* floor, uint8_t checkTokusei);
uint16_t pokemonGetTokuseiDataId(void* poke);
uint16_t pokemonGetSoubiItemDataId(void* poke);
void fightFloorSetTuusinErrorFightResult(void* floor);
void PortVideo_Notice(const char* text, double seconds);
uint16_t fightGetFightEncountDataId(void);
uint16_t fightFloor_GetFightType(void* floor);
uint16_t fightFloor_GetTurnCount(void* floor);
uint8_t fightTargetIsHostSide(void* trainer, uint16_t type);
void fightMainWaitFrame(uint32_t frames);
uint8_t fightIsFight(void);
void* fightFloorGetFightOutBanmeToValidFightOutPokemon(void* floor, uint16_t banme);
uint32_t figthOutPokemonGetPokemonDataId(void* fop);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t pokemon_GetHp(void* poke);
uint16_t pokemon_GetMaxHp(void* poke);
void pokemon_SetHp(void* poke, uint16_t hp);
extern uint32_t* __hsd_rand_seed_hold;
}

extern "C" void PortXD_OnlineBattleEnd(uint32_t result);
namespace {

using xd::online::BattleTransport;
using xd::online::Commit;
using xd::online::kNonceBytes;
using xd::online::ToHex;
using xd::online::Verify;

class FileTransport : public BattleTransport {
 public:
  FileTransport(const std::string& dir, bool host) {
    out_ = dir + (host ? "/host_to_guest.txt" : "/guest_to_host.txt");
    in_ = dir + (host ? "/guest_to_host.txt" : "/host_to_guest.txt");
  }
  bool Send(const uint8_t* data, size_t size) override {
    std::ofstream f(out_, std::ios::app | std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    f.put('\n');
    f.flush();
    return static_cast<bool>(f);
  }
  int Receive(uint8_t* buffer, size_t capacity) override {
    std::ifstream f(in_, std::ios::binary);
    if (!f) return 0;
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (pos_ >= end) return 0;
    f.seekg(pos_);
    std::string rest(static_cast<size_t>(end - pos_), 'x');
    f.read(&rest[0], static_cast<std::streamsize>(rest.size()));
    const size_t nl = rest.find(char(10));
    if (nl == std::string::npos) return 0;
    pos_ += static_cast<std::streamoff>(nl) + 1;
    if (nl > capacity) return -1;
    std::memcpy(buffer, rest.data(), nl);
    return static_cast<int>(nl);
  }
  bool Connected() const override { return true; }

 private:
  std::string out_, in_;
  std::streamoff pos_ = 0;
};

class NetTransport : public BattleTransport {
 public:
  NetTransport() : holder(new xdnet::Connection()), conn(*holder) {}
  explicit NetTransport(xdnet::Connection* adopt) : holder(adopt), conn(*holder) {}
  std::unique_ptr<xdnet::Connection> holder;
  xdnet::Connection& conn;
  bool Send(const uint8_t* data, size_t size) override { return conn.send(data, size, true); }
  int Receive(uint8_t* buffer, size_t capacity) override {
    std::vector<uint8_t> m;
    if (conn.receive(m)) {
      if (m.size() > capacity) return -1;
      std::memcpy(buffer, m.data(), m.size());
      return static_cast<int>(m.size());
    }
    const xdnet::Phase p = conn.status().phase;
    return (p == xdnet::Phase::Closed || p == xdnet::Phase::Failed) ? -1 : 0;
  }
  bool Connected() const override { return conn.status().phase == xdnet::Phase::Connected; }
};

struct State {
  bool room = false;
  bool connected = false;
  bool seeded = false;
  std::string code;
  std::string endNotice;
  uint32_t editedMine = 0, editedFriend = 0;
  std::vector<xd::save::Pokemon> teamMine, teamFriend;
  bool teamsDone = false;
  int created[2] = {0, 0};
  std::chrono::steady_clock::time_point deadline;
  bool timing = false;
  bool choosing = false, startRequested = false;
  bool timeUpDone = false;
  int64_t bankMs = -1;
  std::chrono::steady_clock::time_point choiceStart;
  bool bankOn = false;
  uint32_t timeouts[2] = {0, 0};
  int lastCount = 1 << 30;
  bool human = false;
  xd::online::RoomRules rules;
  bool active = false;
  bool host = false;
  std::unique_ptr<BattleTransport> link;
  uint32_t cnt[2] = {0, 0};
  uint32_t swSeq = 0;
  uint32_t faultTurn = 0;
  std::vector<std::pair<uint32_t, uint64_t>> mine;
  uint32_t digestsOk = 0, digestsBad = 0, sent = 0, applied = 0;
  bool dead = false;
  bool began = false;
  bool lobby = false;
  int lobbyMine = 3, lobbyFriend = 3;
  std::string lobbySave;
  std::vector<xd::online::Pick> lobbyPicks;
  std::vector<uint16_t> lobbySpecies;
  std::vector<uint16_t> friendPreview;
  bool battleOver = false;
  unsigned lastResult = 0;
  bool lastDesync = false;
  bool spectator = false;
  xd::online::SpectatorFeed* feed = nullptr;
};
State S;
std::map<uint32_t, uint32_t> sEntrySeed;
std::vector<std::pair<uint32_t, uint64_t>> sSpecPending;
bool sLobbyPending = false;
xd::online::LobbyHandOver sLobbyIn;

void Log(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "[online battle] %s\n", buf);
  std::fflush(stderr);
}

const char* Env(const char* name) {
  const char* v = std::getenv(name);
  return v && v[0] ? v : nullptr;
}

#ifndef XD_PORT_VERSION
#define XD_PORT_VERSION ""
#endif

xd::online::Capabilities MyCapabilities() {
  xd::online::Capabilities c;
  c.disc = "GXXE01";
  c.version = XD_PORT_VERSION;
  c.edition = xd::xg::Edition();
  c.cheats = PortXD_CheatsUsedHere() != 0;
  for (const engine::rulesets::Format& f : xd::rulesets::Formats()) c.knownFormats += (c.knownFormats.empty() ? "" : ",") + f.id + ":" + xd::rulesets::FormatDigest(f);
  c.formatsDigest = xd::online::Hex(xd::online::Fnv64(c.knownFormats.data(), c.knownFormats.size()));
  if (xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr && (xd::gen4::Loaded() || xd::gen4::PackPresent())) {
    if (xd::gen4::Loaded()) c.ruleSet = "gen4";
    c.gen4Pack = xd::online::PackBattleDigest(xd::gen4::PackFolder());
    if (xd::gen57::Loaded()) {
      c.gen4Pack += "+g57" + xd::online::PackBattleDigest(xd::gen57::PackFolder());
    }
    if (xd::xg::On()) c.gen4Pack += "+xg2";
  }
  std::vector<std::string> ids = xd::pt::LoadedModIds();
  std::sort(ids.begin(), ids.end());
  for (const std::string& id : ids) c.mods += (c.mods.empty() ? "" : ",") + id;
  for (const auto& [id, text] : xd::modassets::NeededBy()) c.modAssets[id] = text;
  for (const auto& b : xd::modassets::Blocked()) c.modsBlocked.insert(b.id);
  c.randomizer = xd::pt::LoadedRandomizer();
  c.tables = xd::modbattle::Digest();
  c.numbers = std::to_string(xd::rulegen::NumbersLoaded()) + (xd::modern::Loaded() && xd::rulegen::NumbersLoaded() >= 6 ? "f" : "");
  return c;
}

void EnsureInit() {
  static bool done = false;
  if (done) return;
  done = true;
  if (const char* f = std::getenv("XD_ONLINE_FAULT")) S.faultTurn = static_cast<uint32_t>(std::atoi(f));
  if (Env("XD_ONLINE_HUMAN")) S.human = true;
  if (Env("XD_ONLINE_TEST_SPLIT")) engine::settings::Global().Override("gameplay.physical_special_split", "on");
  if (const char* room = Env("XD_ONLINE_ROOM")) {
    if (std::strcmp(room, "host") != 0 && std::strcmp(room, "join") != 0) return;
    S.host = std::strcmp(room, "host") == 0;
    S.room = true;
    S.active = true;
    Log("room: %s", room);
    return;
  }
  const char* role = std::getenv("XD_ONLINE_BATTLE");
  const char* dir = std::getenv("XD_ONLINE_DIR");
  if (!role || !dir || (std::strcmp(role, "host") != 0 && std::strcmp(role, "guest") != 0)) return;
  S.host = std::strcmp(role, "host") == 0;
  S.link.reset(new FileTransport(dir, S.host));
  S.connected = true;
  S.active = true;
  Log("%s, stand-in transport in %s", role, dir);
}

std::string Fmt(const char* format, ...) {
  va_list args, again;
  va_start(args, format);
  va_copy(again, args);
  const int n = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) std::vsnprintf(out.data(), out.size() + 1, format, again);
  va_end(again);
  return out;
}
using engine::i18n::Tr;

void EndEarly(const std::string& why) {
  if (S.dead && S.endNotice.empty()) S.endNotice = why;
  if (S.endNotice.empty()) S.endNotice = why;
  S.dead = true;
  Log("%s", why.c_str());
  PortVideo_Notice(why.c_str(), 10.0);
}

void Send(const std::string& text) {
  if (!S.link) return;
  if (!S.link->Send(reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
    S.dead = true;
    Log("send failed");
  }
}

uint64_t Mix(uint64_t h, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    h ^= (v >> (8 * i)) & 0xFF;
    h *= 1099511628211ull;
  }
  return h;
}

uint64_t Digest(uint32_t turn) {
  const bool dlog = std::getenv("XD_DIGEST_LOG") != nullptr;
  uint64_t h = 1469598103934665603ull;
  if (dlog) Log("digest %u: seed %08x weather %u result %u", turn, __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u, static_cast<unsigned>(fightFloorGetNowTenkouDataId(nullptr, 0)), static_cast<unsigned>(fightGetFightResultId()));
  h = Mix(h, turn);
  h = Mix(h, __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u);
  h = Mix(h, fightFloorGetNowTenkouDataId(nullptr, 0));
  h = Mix(h, fightGetFightResultId());
  for (uint16_t b = 0; b < 4; b++) {
    void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
    if (!fop) { h = Mix(h, 0xFFFFFFFFu); continue; }
    void* p = fightOutPokemonGetPokemonPtr(fop);
    h = Mix(h, figthOutPokemonGetPokemonDataId(fop));
    h = Mix(h, p ? pokemon_GetHp(p) : 0u);
    h = Mix(h, p ? pokemon_GetMaxHp(p) : 0u);
    h = Mix(h, p ? pokemonGetTokuseiDataId(p) : 0u);
    h = Mix(h, p ? pokemonGetSoubiItemDataId(p) : 0u);
    if (dlog) Log("digest %u: banme %u species %u hp %u/%u ability %u item %u", turn, b, static_cast<unsigned>(figthOutPokemonGetPokemonDataId(fop)), p ? pokemon_GetHp(p) : 0u, p ? pokemon_GetMaxHp(p) : 0u, p ? static_cast<unsigned>(pokemonGetTokuseiDataId(p)) : 0u, p ? static_cast<unsigned>(pokemonGetSoubiItemDataId(p)) : 0u);
  }
  for (uint16_t b = 0; b < 4; b += 2) {
    void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
    void* tr = fop ? fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fop) : nullptr;
    for (uint32_t slot = 0; tr && slot < 6; slot++) {
      void* fp = fightTrainerBiosGetFightPokemonPtr(tr, slot);
      void* pk = fp ? fightPokemonGetPokemonPtr(fp) : nullptr;
      h = Mix(h, pk ? pokemon_GetHp(pk) : 0xFFFFFFFFu);
      if (dlog) Log("digest %u: side %u bench slot %u hp %d", turn, b, slot, pk ? static_cast<int>(pokemon_GetHp(pk)) : -1);
    }
  }
  if (xd::online::NeedsExtras(S.rules)) {
    for (uint16_t b = 0; b < 4; b++) {
      void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
      h = Mix(h, fop ? static_cast<uint32_t>(fightOutPokemonIsJoutaiDataId(fop, 8)) : 0xFFFFFFFFu);
    }
    h = Mix(h, S.timeouts[0]);
    h = Mix(h, S.timeouts[1]);
  }
  return h;
}

enum class SpecDelay { None, Seconds, Turn1, Turn2 };
constexpr SpecDelay kSpectatorDelayDefault = SpecDelay::Seconds;
constexpr int kSpectatorDelaySeconds = 5;
constexpr int kMaxSpectators = 2;
std::vector<std::string> SplitList(const char* text);
std::string WatchCode(const std::string& room, int place) {
  static const char* alphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
  uint64_t h = 1469598103934665603ull;
  const std::string text = "XDWATCH/" + room + "/" + std::to_string(place);
  for (char c : text) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ull; }
  std::string out;
  for (int i = 0; i < 6; i++) { out += alphabet[h % 31]; h /= 31; h ^= h >> 7; h *= 0x9E3779B97F4A7C15ull; }
  return out;
}
struct SpecPeer {
  std::unique_ptr<xdnet::Connection> conn;
  bool joined = false;
  size_t sent = 0;
  int place = 0;
};
struct SpecHub {
  bool on = false;
  SpecDelay mode = kSpectatorDelayDefault;
  int seconds = kSpectatorDelaySeconds;
  std::vector<std::chrono::steady_clock::time_point> turnEnded;
  std::string room;
  std::vector<SpecPeer> peers;
  std::vector<std::string> head;
  struct Item { uint32_t turn; std::string text; };
  std::vector<Item> items;
  uint32_t turnsDone = 0;
  bool ended = false;
  bool partySent = false;
};
SpecHub H;
bool HubSend(SpecPeer& p, const std::string& text) { return p.conn->send(reinterpret_cast<const uint8_t*>(text.data()), text.size(), true); }
void HubOpen(int place) {
  xdnet::ConnectionConfig cfg = xdnet::Connection::defaultConfig();
  xd::online::Fingerprint f;
  f.caps = MyCapabilities();
  cfg.session.fingerprint = xd::online::Encode(f);
  if (const char* d = Env("XD_ONLINE_WATCH_DIRECT")) cfg.directPeers = SplitList(d);
  if (Env("XD_ONLINE_NO_GO_BETWEEN")) cfg.useGoBetween = false;
  if (const char* lp = Env("XD_ONLINE_WATCH_PORT")) cfg.localPort = static_cast<uint16_t>(std::atoi(lp) + place);
  SpecPeer p;
  p.place = place;
  p.conn.reset(new xdnet::Connection());
  if (!p.conn->start(WatchCode(H.room, place), cfg)) { Log("spectating: place %d could not be opened", place); return; }
  H.peers.push_back(std::move(p));
}
void HubRelease() {
  for (SpecPeer& p : H.peers) {
    if (!p.conn || !p.joined) continue;
    while (p.sent < H.items.size()) {
      const uint32_t n = H.items[p.sent].turn;
      bool ok = H.ended;
      if (!ok && H.turnsDone > n) {
        const auto since = std::chrono::steady_clock::now() - H.turnEnded[n];
        switch (H.mode) {
        case SpecDelay::None: ok = true; break;
        case SpecDelay::Seconds: ok = since >= std::chrono::seconds(H.seconds); break;
        case SpecDelay::Turn1: ok = H.turnsDone >= n + 2; break;
        case SpecDelay::Turn2: ok = H.turnsDone >= n + 3; break;
        }
      }
      if (!ok || !HubSend(p, H.items[p.sent].text)) break;
      Log("spectating: record %zu (turn index %u) released to place %d (the host has finished %u turns)", p.sent, n, p.place, H.turnsDone);
      p.sent++;
    }
  }
}
void HubTick() {
  if (!H.on) return;
  std::vector<int> freed;
  for (SpecPeer& p : H.peers) {
    if (!p.conn) continue;
    const xdnet::Phase ph = p.conn->status().phase;
    if (ph == xdnet::Phase::Connected && !p.joined) {
      p.joined = true;
      Log("spectating: a spectator joined place %d (%zu records so far, %u turns done)", p.place, H.items.size(), H.turnsDone);
      for (const std::string& h : H.head) HubSend(p, h);
      PortVideo_Notice("Someone is watching this battle.", 4.0);
    }
    std::vector<uint8_t> junk;
    while (p.conn->receive(junk)) {}
    if (ph == xdnet::Phase::Closed || ph == xdnet::Phase::Failed) {
      Log("spectating: place %d is free again", p.place);
      freed.push_back(p.place);
      p.conn.reset();
    }
  }
  for (size_t i = 0; i < H.peers.size();) {
    if (!H.peers[i].conn) H.peers.erase(H.peers.begin() + static_cast<long>(i)); else i++;
  }
  if (!H.ended) for (int place : freed) HubOpen(place);
  HubRelease();
}
void HubBegin(const std::string& rulesKey, const std::vector<std::string>& hostTeam, const std::vector<std::string>& guestTeam, uint32_t seed) {
  H = SpecHub();
  const char* on = Env("XD_ONLINE_SPECTATE");
  if (!on || std::string(on) == "0" || !S.host || S.code.empty()) return;
  H.on = true;
  H.room = S.code;
  if (const char* d = Env("XD_SPECTATOR_DELAY")) {
    const std::string v = d;
    if (v == "none") H.mode = SpecDelay::None;
    else if (v == "1") H.mode = SpecDelay::Turn1;
    else if (v == "2") H.mode = SpecDelay::Turn2;
    else if (v.compare(0, 8, "seconds:") == 0) { H.mode = SpecDelay::Seconds; H.seconds = std::atoi(v.c_str() + 8); }
    else H.mode = SpecDelay::Seconds;
  }
  auto join = [](const std::vector<std::string>& v) { std::string o; for (const std::string& x : v) o += (o.empty() ? "" : ",") + x; return o; };
  H.head = {"WH " + rulesKey, "WT host " + join(hostTeam), "WT guest " + join(guestTeam), "WZ " + std::to_string(seed)};
  int places = kMaxSpectators;
  if (const char* n = Env("XD_ONLINE_SPECTATORS")) places = std::atoi(n);
  for (int i = 0; i < places; i++) HubOpen(i);
  Log("spectating: %d place(s) open on the watch code of this room, delay mode %d (0 none, 1 seconds: %d, 2 one turn, 3 two turns); only resolved turns are sent", places, static_cast<int>(H.mode), H.seconds);
}
void HubSeed(uint32_t seq, uint32_t seed) {
  if (H.on) { char b[32]; std::snprintf(b, sizeof b, "WK %u %08x", seq, seed); H.items.push_back({H.turnsDone, b}); }
}
void HubSwitchSeed(uint32_t seq, uint32_t seed) {
  if (H.on) { char b[32]; std::snprintf(b, sizeof b, "WX %u %08x", seq, seed); H.items.push_back({H.turnsDone, b}); }
}
void HubChoice(uint32_t seq, const std::string& line) {
  if (H.on) { H.items.push_back({H.turnsDone, "WC " + std::to_string(seq) + " " + line}); HubRelease(); }
}
void HubSwitch(uint32_t seq, int r) {
  if (H.on) { H.items.push_back({H.turnsDone, "WW " + std::to_string(seq) + " " + std::to_string(r)}); HubRelease(); }
}
void HubTurn(uint32_t turn, uint64_t digest) {
  if (!H.on) return;
  if (!H.partySent) {
    H.partySent = true;
    H.items.push_back({0, "WP " + std::to_string(S.created[0]) + " " + std::to_string(S.created[1])});
  }
  char buf[48];
  std::snprintf(buf, sizeof buf, "WD %u %llx", turn, static_cast<unsigned long long>(digest));
  H.items.push_back({H.turnsDone, buf});
  H.turnEnded.push_back(std::chrono::steady_clock::now());
  H.turnsDone++;
  HubRelease();
}
void HubEnd(uint32_t result) {
  if (!H.on) return;
  H.items.push_back({H.turnsDone, "WE " + std::to_string(result)});
  H.ended = true;
  HubRelease();
}

std::deque<std::string> sPending;
std::map<uint32_t, std::string> sPeerCommit;
std::map<uint32_t, std::string> sPeerReveal;

void HandleDigest(const std::string& line) {
  unsigned turn = 0;
  unsigned long long d = 0;
  std::sscanf(line.c_str(), "D %u %llx", &turn, &d);
  for (size_t i = 0; i < S.mine.size(); i++) {
    if (S.mine[i].first != turn) continue;
    if (S.mine[i].second == d) {
      S.digestsOk++;
      Log("turn %u digest %016llx = peer's: OK", turn, d);
    } else {
      S.digestsBad++;
      S.dead = true;
      Log("turn %u DESYNC: mine %016llx, peer's %016llx", turn, static_cast<unsigned long long>(S.mine[i].second), d);
      if (S.endNotice.empty()) {
        S.endNotice = Fmt(Tr("The two games disagreed (turn %d). This battle doesn't count."), static_cast<int>(turn));
        PortVideo_Notice(S.endNotice.c_str(), 10.0);
      }
    }
    S.mine.erase(S.mine.begin() + static_cast<long>(i));
    return;
  }
}

void WaitDigests() {
  const auto start = std::chrono::steady_clock::now();
  while (!S.mine.empty() && !S.dead) {
    uint8_t buf[8192];
    const int n = S.link->Receive(buf, sizeof buf - 1);
    if (n < 0) { S.dead = true; Log("connection closed"); return; }
    if (n > 0) {
      std::string line(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
      if (!line.empty() && line[0] == 'D') HandleDigest(line); else sPending.push_back(line);
      continue;
    }
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
      Log("no digest from the friend for 60 s: connection lost");
      S.dead = true;
      return;
    }
    fightMainWaitFrame(1);
  }
}

bool Pump(const std::string& wantedType, std::string& out, bool block) {
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    uint8_t buf[8192];
    int n = 0;
    std::string line;
    if (!sPending.empty()) { line = sPending.front(); sPending.pop_front(); n = static_cast<int>(line.size()); }
    else {
      n = S.link->Receive(buf, sizeof buf - 1);
      if (n < 0) { S.dead = true; return false; }
      if (n > 0) line.assign(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    }
    if (n > 0) {
      if (line.compare(0, 3, "L1 ") == 0) continue;
      char type = line.empty() ? 0 : line[0];
      if (type == 'D') {
        unsigned turn = 0;
        unsigned long long d = 0;
        std::sscanf(line.c_str(), "D %u %llx", &turn, &d);
        for (size_t i = 0; i < S.mine.size(); i++) {
          if (S.mine[i].first != turn) continue;
          if (S.mine[i].second == d) {
            S.digestsOk++;
            Log("turn %u digest %016llx = peer's: OK", turn, d);
          } else {
            S.digestsBad++;
            S.dead = true;
            Log("turn %u DESYNC: mine %016llx, peer's %016llx", turn, static_cast<unsigned long long>(S.mine[i].second), d);
          }
          S.mine.erase(S.mine.begin() + static_cast<long>(i));
          break;
        }
        if (S.dead) return false;
        continue;
      }
      if (type == 'C' || type == 'V') {
        unsigned q = 0;
        int used = 0;
        if (std::sscanf(line.c_str() + 2, "%u %n", &q, &used) >= 1 && used > 0) {
          const std::string rest = line.substr(2 + static_cast<size_t>(used));
          if (type == 'C') sPeerCommit[q] = rest; else sPeerReveal[q] = rest;
        } else {
          S.dead = true;
          Log("a damaged %c message", type);
          return false;
        }
        if (wantedType[0] == char(1)) { out = line; return true; }
        continue;
      }
      if (type == wantedType[0]) { out = line; return true; }
      Log("unexpected message '%s' (waiting for %s)", line.c_str(), wantedType.c_str());
      S.dead = true;
      return false;
    }
    if (!block) return false;
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(180)) {
      Log("no message from the friend for 180 s: connection lost");
      S.dead = true;
      return false;
    }
    fightMainWaitFrame(1);
  }
}

std::unique_ptr<xd::legality::DiscRules> sRules;
std::vector<xd::save::ItemSlot> sBagItems, sBagBerries;
bool sBagDone[2] = {false, false};
std::vector<std::string> sSaveShadowHex;
std::map<int, std::array<uint8_t, 0x48>> sShadowBackup;
std::vector<std::string> sSavedChallenges;
bool sRulesApplied = false;
void ApplyRoomRules() {
  if (sRulesApplied) return;
  sRulesApplied = true;
  engine::settings::Registry& reg = engine::settings::Global();
  reg.Override("gameplay.physical_special_split", S.rules.split ? "on" : "off");
  sSavedChallenges.clear();
  for (const xd::challenges::Rule& r : xd::challenges::Rules()) {
    if (xd::challenges::Active(r.id)) sSavedChallenges.push_back(r.id);
  }
  std::vector<std::string> room;
  if (xd::online::RoomGen4Pokemon(S.rules.ruleSet)) room.push_back("gen4_pokemon");
  if (xd::online::RoomGen4Rules(S.rules.ruleSet)) room.push_back("gen4_rules");
  const std::string roomRs = xd::online::RoomRuleSetId(S.rules);
  if (roomRs == "gen5" || roomRs == "gen6" || roomRs == "gen7") {
    room.push_back("gen4_pokemon");
    room.push_back("gen4_rules");
    room.push_back(roomRs + "_rules");
    if (roomRs != "gen5") room.push_back("mega_evolution");
  }
  xd::challenges::SetActive(room, "online battle: the room's rules");
  Log("the room's rules are in force for this battle (split %s, Gen 4 Pokemon %s, Gen 4 rules %s, own challenge rules off: %zu)", S.rules.split ? "on" : "off",
      xd::online::RoomGen4Pokemon(S.rules.ruleSet) ? "on" : "off", xd::online::RoomGen4Rules(S.rules.ruleSet) ? "on" : "off", sSavedChallenges.size());
}
void RestoreOwnRules() {
  if (!sRulesApplied) return;
  sRulesApplied = false;
  engine::settings::Registry& reg = engine::settings::Global();
  const size_t i = reg.Find("gameplay.physical_special_split");
  if (i != engine::settings::kNoSetting) reg.ClearOverride(i);
  xd::pt::ApplySplit("after an online battle");
  xd::challenges::SetActive(sSavedChallenges, "online battle over: this player's own rules again");
  Log("this player's own rules are back");
}

void EndWithError() {
  static bool set = false;
  if (S.endNotice.empty()) {
    S.endNotice = Tr("The connection to your friend was lost. The battle ends without a winner.");
    PortVideo_Notice(S.endNotice.c_str(), 10.0);
    Log("%s", S.endNotice.c_str());
  }
  RestoreOwnRules();
  if (!set) {
    set = true;
    fightFloorSetTuusinErrorFightResult(nullptr);
  }
  if (S.lobby && !S.battleOver) PortXD_OnlineBattleEnd(1);
}

std::vector<std::string> SplitList(const char* text);
bool LoadTeamFromSave(const std::string& path, std::vector<xd::save::Pokemon>& team, std::string& why) {
  namespace mc = engine::memcard;
  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(path, bytes)) { why = "can't read " + path; return false; }
  std::vector<mc::CardFile> files;
  mc::CardImageInfo info;
  std::string error;
  if (!mc::LoadCardFiles(bytes, files, info, error)) { why = error; return false; }
  const mc::CardFile* file = nullptr;
  for (const mc::CardFile& f : files) {
    if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) { file = &f; break; }
  }
  if (!file) { why = "no Pokemon XD save in that file"; return false; }
  xd::save::Save save;
  xd::save::ReadReport report;
  if (!xd::save::ReadSave(file->data, save, report, error)) { why = error; return false; }
  if (save.order != xd::save::ByteOrder::PC) {
    std::vector<uint8_t> pcFile = file->data;
    xd::save::ConvertReport convertReport;
    if (!xd::save::ConvertFile(pcFile, xd::save::ByteOrder::PC, convertReport, error)) { why = "converting the save: " + error; return false; }
    save = xd::save::Save();
    if (!xd::save::ReadSave(pcFile, save, report, error)) { why = error; return false; }
  }
  sSaveShadowHex.clear();
  for (const xd::save::ShadowEntry& e : save.shadows) sSaveShadowHex.push_back(xd::online::ToHex(e.raw.data(), e.raw.size()));
  sBagItems = save.items;
  sBagBerries = save.berries;
  team.clear();
  for (const xd::save::Pokemon& p : save.party) {
    if (!p.IsEmpty()) team.push_back(p);
  }
  return true;
}

bool LoadPicksFromSave(const std::string& path, const std::vector<xd::online::Pick>& picks, std::vector<xd::save::Pokemon>& team, std::string& why) {
  namespace mc = engine::memcard;
  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(path, bytes)) { why = "can't read " + path; return false; }
  std::vector<mc::CardFile> files;
  mc::CardImageInfo info;
  std::string error;
  if (!mc::LoadCardFiles(bytes, files, info, error)) { why = error; return false; }
  const mc::CardFile* file = nullptr;
  for (const mc::CardFile& f : files) {
    if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) { file = &f; break; }
  }
  if (!file) { why = "no Pokemon XD save in that file"; return false; }
  xd::save::Save save;
  xd::save::ReadReport report;
  if (!xd::save::ReadSave(file->data, save, report, error)) { why = error; return false; }
  if (save.order != xd::save::ByteOrder::PC) {
    std::vector<uint8_t> pcFile = file->data;
    xd::save::ConvertReport convertReport;
    if (!xd::save::ConvertFile(pcFile, xd::save::ByteOrder::PC, convertReport, error)) { why = "converting the save: " + error; return false; }
    save = xd::save::Save();
    if (!xd::save::ReadSave(pcFile, save, report, error)) { why = error; return false; }
  }
  sBagItems = save.items;
  sBagBerries = save.berries;
  const std::vector<std::string> editorOrders = xd::online::EditorOrdersBesideSave(path, file->data);
  team.clear();
  for (const xd::online::Pick& p : picks) {
    if (p.box < 0) {
      if (p.slot < 0 || p.slot >= static_cast<int>(save.party.size()) || save.party[static_cast<size_t>(p.slot)].IsEmpty()) { why = "a Pokemon you picked is no longer in the save"; return false; }
      team.push_back(save.party[static_cast<size_t>(p.slot)]);
    } else {
      if (p.box >= static_cast<int>(save.boxes.size()) || p.slot < 0 || p.slot >= static_cast<int>(save.boxes[static_cast<size_t>(p.box)].slots.size()) ||
          save.boxes[static_cast<size_t>(p.box)].slots[static_cast<size_t>(p.slot)].IsEmpty()) { why = "a Pokemon you picked is no longer in the save"; return false; }
      team.push_back(save.boxes[static_cast<size_t>(p.box)].slots[static_cast<size_t>(p.slot)]);
    }
  }
  S.editedMine = xd::online::EditedShadowMask(team, editorOrders);
  return true;
}

extern "C" {
extern void* pokemon_data;
extern uint32_t* pokemon_data_number;
extern uint8_t* item_data_prime;
extern uint32_t* item_data_prime_number;
extern uint16_t* item_data_index;
extern int32_t* item_data_index_number;
extern uint8_t pokemon_grow_data[];
extern uint8_t item_waza_machine_no[];
extern void** table_res;
extern uint32_t* table_res_number;
extern uint8_t* waza_data;
extern uint32_t* waza_data_number;
extern void* wazaoshie_data;
extern int32_t* wazaoshie_data_number;
}
bool MakeRunningRules(xd::legality::DiscRules& rules, std::string& error) {
  static XDCommonTables t;
  t = XDCommonTables{};
  auto add = [](const char* name, void* value) {
    if (t.numGlobals < XD_COMMON_MAX_GLOBALS) t.globals[t.numGlobals++] = XDRelGlobalValue{name, value, value != nullptr};
  };
  add("pokemon_data", pokemon_data);
  add("pokemon_data_number", pokemon_data_number);
  add("item_data_prime", item_data_prime);
  add("item_data_prime_number", item_data_prime_number);
  add("item_data_index", item_data_index);
  add("item_data_index_number", item_data_index_number);
  add("table_res", table_res);
  add("table_res_number", table_res_number);
  add("waza_data", waza_data);
  add("waza_data_number", waza_data_number);
  add("wazaoshie_data", wazaoshie_data);
  add("wazaoshie_data_number", wazaoshie_data_number);
  t.pokemon_data = static_cast<struct PokemonData*>(pokemon_data);
  t.pokemon_data_number = pokemon_data_number ? *pokemon_data_number : 0;
  static uint8_t grow[8 * 101 * 4], mach[58 * 8];
  {
    auto u32at = [](const uint8_t* p, bool big) { return big ? (uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]) : (uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]); };
    bool bigGrow = true;
    for (int l = 1; l <= 100; l++) if (u32at(pokemon_grow_data + l * 4, true) < u32at(pokemon_grow_data + (l - 1) * 4, true)) { bigGrow = false; break; }
    bool bigMach = u32at(item_waza_machine_no + 4, true) < 1000;
    for (int i = 0; i < 8 * 101; i++) { const uint32_t v = u32at(pokemon_grow_data + i * 4, bigGrow); for (int b = 0; b < 4; b++) grow[i * 4 + b] = static_cast<uint8_t>(v >> (24 - 8 * b)); }
    for (int i = 0; i < 58 * 2; i++) { const uint32_t v = u32at(item_waza_machine_no + i * 4, bigMach); for (int b = 0; b < 4; b++) mach[i * 4 + b] = static_cast<uint8_t>(v >> (24 - 8 * b)); }
    Log("main.dol tables of the running game: grow data %s, machines %s", bigGrow ? "big-endian" : "PC order", bigMach ? "big-endian" : "PC order");
  }
  const bool ok = xd::legality::BuildRulesFromRunningGame(&t, mach, grow, rules, error);
  size_t dolSize = 0;
  Log("rules of the running game: ok %d, species %zu, moves %zu, items %zu, exp curves %zu, species 135 growth %u (main.dol present: %d)", ok ? 1 : 0, rules.rules.species.size(),
      rules.rules.moves.size(), rules.rules.items.size(), rules.rules.expCurves.size(), rules.rules.species.size() > 135 ? static_cast<unsigned>(rules.rules.species[135].growth) : 999u,
      PortDisc_Dol(&dolSize) ? 1 : 0);
  return ok;
}

std::vector<std::string> SplitComma(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* c = text; *c; c++) {
    if (*c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += *c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

int FindItemByName(const char* want) {
  if (!sRules) {
    xd::legality::DiscRules rules;
    std::string error;
    if (!MakeRunningRules(rules, error)) return 0;
    sRules.reset(new xd::legality::DiscRules(rules));
  }
  const auto norm = [](std::string n) {
    std::string o;
    for (char c : n) if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
  };
  const std::string w = norm(want);
  for (size_t i = 1; i < sRules->rules.items.size(); i++) {
    if (sRules->rules.items[i].exists && norm(sRules->rules.itemName(static_cast<uint16_t>(i))) == w) return static_cast<int>(i);
  }
  return 0;
}

void SetupBag(void* trainer, bool mine) {
  if (S.rules.items == "none") return;
  void* hero = fightTrainerBiosGetHeroPtr(trainer);
  if (!hero) return;
  for (uint8_t kind = 1; kind <= 7; kind++) {
    uint16_t count = 0, max = 0;
    uint8_t f1 = 0, f2 = 0;
    if (void* ary = heroItemGetItemKindToItemAryPtr(hero, kind, &count, &max, &f1, &f2)) itemInitAry(ary, count);
  }
  if (S.rules.items == "kit") {
    const char* kit[3] = {"MAX POTION", "FULL HEAL", "REVIVE"};
    for (const char* name : kit) {
      const int id = FindItemByName(name);
      if (id) heroItemAddItemDataId(hero, static_cast<uint32_t>(id), 1, -1); else Log("kit: item %s not found in this game's list", name);
    }
    Log("items: the standard kit in this battle's bag copy (%s trainer)", mine ? "my" : "the friend's");
  } else if (S.rules.items == "bag" && mine) {
    int n = 0;
    for (const std::vector<xd::save::ItemSlot>* pocket : {&sBagItems, &sBagBerries}) {
      for (const xd::save::ItemSlot& it : *pocket) {
        if (it.item && it.count) { heroItemAddItemDataId(hero, it.item, it.count, -1); n++; }
      }
    }
    Log("items: %d stacks of my own bag copied into this battle's bag (the save is untouched; no Poke Balls)", n);
  }
}

void ApplyFriendItemEffect(void* trainer, unsigned item, unsigned slot) {
  void* fp = fightTrainerBiosGetFightPokemonPtr(trainer, slot);
  void* pk = fp ? fightPokemonGetPokemonPtr(fp) : nullptr;
  if (!pk) { Log("the friend's item %u: no Pokemon in party slot %u", item, slot); return; }
  if (pokemonIsJoutaiDataId(pk, 0x3E)) { Log("the friend's item %u: that Pokemon cannot be given an item (as in the game's bag)", item); return; }
  alignas(8) unsigned char log[8 * 32];
  std::memset(log, 0, sizeof log);
  const unsigned before = pokemon_GetHp(pk);
  const int32_t result = itemUse2Pokemon(log, pk, fp, static_cast<uint16_t>(item), 0);
  Log("the friend's item %u applied to his party slot %u: result %d, HP %u -> %u", item, slot, static_cast<int>(result), before, static_cast<unsigned>(pokemon_GetHp(pk)));
}

bool ExchangeTeams() {
  if (S.teamsDone) return true;
  S.teamsDone = true;
  S.teamMine.clear();
  S.teamFriend.clear();
  S.editedMine = S.editedFriend = 0;
  if (S.spectator) {
    for (int sideNo = 0; sideNo < 2; sideNo++) {
      std::vector<xd::save::Pokemon>& team = sideNo == 0 ? S.teamMine : S.teamFriend;
      for (const std::string& hex : sideNo == 0 ? S.feed->hostTeam : S.feed->guestTeam) {
        std::string bytes;
        if (!xd::online::FromHex(hex, bytes) || bytes.size() != xd::save::kPokemonSize) { EndEarly("The replay's team records are damaged."); return false; }
        team.push_back(xd::save::ParsePokemon(reinterpret_cast<const uint8_t*>(bytes.data()), xd::save::ByteOrder::PC));
      }
    }
    if (!sRules) {
      xd::legality::DiscRules rules;
      std::string error;
      if (!MakeRunningRules(rules, error)) { EndEarly(Tr("The rules for the safety check could not be made.")); return false; }
      sRules.reset(new xd::legality::DiscRules(rules));
    }
    S.created[0] = S.created[1] = 0;
    sBagDone[0] = sBagDone[1] = false;
    Log("spectator: teams from the feed: host side %zu Pokemon, guest side %zu", S.teamMine.size(), S.teamFriend.size());
    return true;
  }
  if (S.lobby && !S.lobbySave.empty()) {
    std::string why;
    if (!LoadPicksFromSave(S.lobbySave, S.lobbyPicks, S.teamMine, why)) { EndEarly(Fmt(Tr("Your team could not be read: %s"), why.c_str())); return false; }
  } else if (S.lobby && !S.lobbySpecies.empty()) {
    std::string why;
    if (!xd::online::BuildTeamFromSpecies(S.rules, S.lobbySpecies, S.teamMine, why)) { EndEarly(why); return false; }
  } else if (const char* path = Env("XD_ONLINE_TEAM_SAVE")) {
    std::string why;
    if (!LoadTeamFromSave(path, S.teamMine, why)) { EndEarly(Fmt(Tr("Your team could not be read: %s"), why.c_str())); return false; }
    if (const char* slots = Env("XD_ONLINE_TEAM_SLOTS")) {
      std::vector<xd::save::Pokemon> pick;
      for (const std::string& n : SplitList(slots)) {
        const size_t k = static_cast<size_t>(std::atoi(n.c_str()));
        if (k < S.teamMine.size()) pick.push_back(S.teamMine[k]);
      }
      S.teamMine = pick;
    } else if (S.teamMine.size() > 2 && !Env("XD_ONLINE_TEAM_ALL")) {
      S.teamMine.resize(2);
    }
    if (Env("XD_ONLINE_TEAM_SHADOW") && !S.teamMine.empty()) {
      xd::save::Pokemon& p0 = S.teamMine[0];
      p0.raw[0xBA] = 1;
      p0.raw[0xBB] = 0;
      p0 = xd::save::ParsePokemon(p0.raw.data(), xd::save::ByteOrder::PC);
    }
  }
  if (Env("XD_ONLINE_TEST_NOSHADOWID")) {
    for (xd::save::Pokemon& m : S.teamMine) { m.raw[0xBA] = 0; m.raw[0xBB] = 0; m = xd::save::ParsePokemon(m.raw.data(), xd::save::ByteOrder::PC); }
  }
  if (const char* fm = Env("XD_ONLINE_TEST_FIRST_MOVE")) {
    const unsigned mv = static_cast<unsigned>(std::atoi(fm));
    for (xd::save::Pokemon& m : S.teamMine) {
      m.raw[0x80] = static_cast<uint8_t>(mv & 0xFF); m.raw[0x81] = static_cast<uint8_t>(mv >> 8); m.raw[0x82] = 15; m.raw[0x83] = 0;
      m = xd::save::ParsePokemon(m.raw.data(), xd::save::ByteOrder::PC);
    }
    Log("test: move slot 1 of every member of my team is move %u", mv);
  }
  std::vector<uint16_t> draftFriend;
  const bool drafted = S.rules.team == "draft" || S.rules.team == "offer";
  if (drafted) {
    std::string draftWhy;
    if (!xd::online::RunDraft(S.host, S.rules, S.link.get(), S.teamMine, draftFriend, draftWhy)) { EndEarly(draftWhy); return false; }
  }
  if (!S.teamMine.empty()) {
    xd::legality::DiscRules rules;
    std::string error;
    if (!MakeRunningRules(rules, error)) {
      EndEarly(Fmt(Tr("The rules for the safety check could not be made: %s"), error.c_str()));
      return false;
    }
    sRules.reset(new xd::legality::DiscRules(rules));
    const std::vector<std::string> mine = xd::online::CheckTeam(S.teamMine, sRules->rules, S.rules, S.editedMine);
    if (!mine.empty()) { EndEarly(Fmt(Tr("Your team is not allowed in this room: %s"), mine.front().c_str())); return false; }
  }
  Send(xd::online::EncodeTeam(S.teamMine, S.editedMine));
  std::string line;
  if (!Pump("T", line, true)) { EndWithError(); return false; }
  std::string why;
  if (!xd::online::DecodeTeam(line, S.teamFriend, &why, &S.editedFriend)) { EndEarly(Fmt(Tr("Your friend's team message is damaged: %s"), why.c_str())); return false; }
  {
    std::string mineList;
    for (const xd::save::Pokemon& m : S.teamMine) {
      std::string e = "-";
      if (m.shadowId != 0 && m.shadowId < sSaveShadowHex.size()) e = sSaveShadowHex[m.shadowId];
      mineList += (mineList.empty() ? "" : ",") + e;
    }
    Send("H " + (mineList.empty() ? std::string("-") : mineList));
    std::string hline;
    if (!Pump("H", hline, true)) { EndWithError(); return false; }
    std::vector<std::string> friendList;
    { std::string cur; for (char c : hline.size() > 2 ? hline.substr(2) : std::string()) { if (c == ',') { friendList.push_back(cur); cur.clear(); } else cur += c; } friendList.push_back(cur); }
    uint8_t* live = static_cast<uint8_t*>(savedataGetStatus(nullptr, 0xF));
    std::vector<xd::save::Pokemon>& hostTeam = S.host ? S.teamMine : S.teamFriend;
    std::vector<xd::save::Pokemon>& guestTeam = S.host ? S.teamFriend : S.teamMine;
    const std::vector<std::string> hostEntries = S.host ? [&] { std::vector<std::string> v; std::string cur; for (char c : mineList + ",") { if (c == ',') { v.push_back(cur); cur.clear(); } else cur += c; } return v; }() : friendList;
    const std::vector<std::string> guestEntries = S.host ? friendList : [&] { std::vector<std::string> v; std::string cur; for (char c : mineList + ",") { if (c == ',') { v.push_back(cur); cur.clear(); } else cur += c; } return v; }();
    for (int sideNo = 0; sideNo < 2 && live; sideNo++) {
      std::vector<xd::save::Pokemon>& team = sideNo == 0 ? hostTeam : guestTeam;
      const std::vector<std::string>& entries = sideNo == 0 ? hostEntries : guestEntries;
      for (size_t i = 0; i < team.size() && i < entries.size(); i++) {
        std::string bytes;
        if (team[i].shadowId == 0 || entries[i] == "-" || !xd::online::FromHex(entries[i], bytes) || bytes.size() != 0x48) continue;
        const int id = static_cast<int>(team[i].shadowId);
        if (id <= 0 || id >= 0x80) continue;
        if (sShadowBackup.count(id)) {
          Log("Shadow list: side %d member %zu (species %u) has the same Shadow id %d as an earlier member: that entry (the host's team first) is used for both", sideNo, i,
              static_cast<unsigned>(team[i].species), id);
          continue;
        }
        std::array<uint8_t, 0x48> old;
        std::memcpy(old.data(), live + id * 0x48, 0x48);
        sShadowBackup[id] = old;
        std::memcpy(live + id * 0x48, bytes.data(), 0x48);
        Log("Shadow list: side %d member %zu (species %u) puts its entry into this game's list place %d for the battle (id unchanged)", sideNo, i,
            static_cast<unsigned>(team[i].species), id);
      }
    }
  }
  if (drafted) {
    bool same = S.teamFriend.size() == draftFriend.size();
    for (size_t i = 0; same && i < draftFriend.size(); i++) same = S.teamFriend[i].species == draftFriend[i];
    if (!same) { EndEarly(Tr("Your friend's team is not the team he drafted.")); return false; }
  }
  if (!S.friendPreview.empty()) {
    bool same = S.teamFriend.size() == S.friendPreview.size();
    for (size_t i = 0; same && i < S.friendPreview.size(); i++) same = S.teamFriend[i].species == S.friendPreview[i];
    if (!same) { EndEarly(Tr("Your friend's team is not the one they picked in the team preview. The battle was not started.")); return false; }
    Log("team preview: the friend's %zu Pokemon are the ones they picked, in their order", S.friendPreview.size());
  }
  if (!S.teamFriend.empty()) {
    if (!sRules) {
      xd::legality::DiscRules rules;
      std::string error;
      if (!MakeRunningRules(rules, error)) { EndEarly(Tr("The rules for the safety check could not be made.")); return false; }
      sRules.reset(new xd::legality::DiscRules(rules));
    }
    const std::vector<std::string> theirs = xd::online::CheckTeam(S.teamFriend, sRules->rules, S.rules, S.editedFriend);
    if (!theirs.empty()) {
      for (const std::string& f : theirs) Log("the friend's team is refused: %s", f.c_str());
      EndEarly(Fmt(Tr("Your friend's team is not allowed in this room: %s"), theirs.front().c_str()));
      return false;
    }
  }
  {
    const xd::card::Card mine = xd::card::MyCard();
    Send(xd::card::Encode(mine));
    std::string cardLine;
    xd::card::Card theirs;
    if (!Pump("K", cardLine, true)) { EndWithError(); return false; }
    std::string cardWhy;
    if (!xd::card::Decode(cardLine, theirs, &cardWhy)) { Log("the friend's card is damaged (%s): the default card is used", cardWhy.c_str()); theirs = xd::card::Card(); }
    xd::card::SetRoom(S.host, mine, theirs);
  }
  {
    const std::vector<xd::save::Pokemon>& ht = S.host ? S.teamMine : S.teamFriend;
    const std::vector<xd::save::Pokemon>& gt = S.host ? S.teamFriend : S.teamMine;
    uint64_t d = 1469598103934665603ull;
    auto eat = [&](const std::vector<xd::save::Pokemon>& t) {
      d = Mix(d, static_cast<uint32_t>(t.size()));
      for (const xd::save::Pokemon& m : t) for (uint8_t b : m.raw) d = Mix(d, b);
    };
    eat(ht);
    eat(gt);
    char buf[48];
    std::snprintf(buf, sizeof buf, "Y %016llx", static_cast<unsigned long long>(d));
    Send(buf);
    std::string yline;
    if (!Pump("Y", yline, true)) { EndWithError(); return false; }
    unsigned long long theirs = std::strtoull(yline.c_str() + 2, nullptr, 16);
    if (theirs != d) {
      Log("the two sides' battle records differ before turn 1: mine %016llx, the friend's %016llx", static_cast<unsigned long long>(d), theirs);
      EndEarly(Tr("The two games do not hold the same Pokemon for this battle (a team was changed on the way). The battle was not started."));
      return false;
    }
    Log("both sides hold the same records for the battle (digest %016llx; fight type 0x%x, encounter %u)", static_cast<unsigned long long>(d), static_cast<unsigned>(fightFloor_GetFightType(nullptr)), static_cast<unsigned>(fightGetFightEncountDataId()));
  }
  {
    const std::vector<xd::save::Pokemon>& ht = S.host ? S.teamMine : S.teamFriend;
    const std::vector<xd::save::Pokemon>& gt = S.host ? S.teamFriend : S.teamMine;
    std::vector<std::string> hh, gh;
    for (const xd::save::Pokemon& m : ht) hh.push_back(ToHex(m.raw.data(), m.raw.size()));
    for (const xd::save::Pokemon& m : gt) gh.push_back(ToHex(m.raw.data(), m.raw.size()));
    xd::replay::OnlineBegin(S.host, RulesText(S.rules), hh, gh, __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u);
    xd::replay::OnlineRulesKey(xd::online::RulesKeyText(S.rules));
    {
      std::string pv;
      if (S.rules.preview) {
        auto list = [](const std::vector<xd::save::Pokemon>& t) { std::string o; for (const xd::save::Pokemon& m : t) o += (o.empty() ? "" : ",") + std::to_string(m.species); return o; };
        pv = "host " + list(ht) + " guest " + list(gt);
      }
      xd::replay::OnlineExtras(xd::online::NeedsExtras(S.rules) ? xd::online::kExtrasProtocol : 0, pv);
    }
    HubBegin(xd::online::RulesKeyText(S.rules), hh, gh, __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u);
  }
  S.created[0] = S.created[1] = 0;
  sBagDone[0] = sBagDone[1] = false;
  Log("teams exchanged and checked: mine %zu Pokemon, the friend's %zu", S.teamMine.size(), S.teamFriend.size());
  return true;
}

std::vector<std::string> SplitList(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* c = text; *c; c++) {
    if (*c == ';') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += *c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool ConnectRoom() {
  using namespace xd::online;
  std::unique_ptr<NetTransport> t(new NetTransport());
  xdnet::Connection& conn = t->conn;
  xdnet::ConnectionConfig cfg = xdnet::Connection::defaultConfig();
  Fingerprint mine;
  mine.caps = MyCapabilities();
  std::string why;
  if (S.host) {
    const char* text = Env("XD_ONLINE_RULES");
    if (!ParseRules(text ? text : "", mine.rules, &why)) {
      EndEarly(Fmt(Tr("The room's rules are not valid: %s"), why.c_str()));
      return false;
    }
    if (!mine.rules.format.empty() && mine.rules.formatHash.empty() && !ApplyFormat(mine.rules, &why)) {
      EndEarly(Fmt(Tr("The room's rules are not valid: %s"), why.c_str()));
      return false;
    }
    if (xd::online::RoomGen4Pokemon(mine.rules.ruleSet) != xd::online::RoomGen4Pokemon(mine.caps.ruleSet)) {
      EndEarly(Fmt(Tr("The room's rules say \"%s\" but this game is set up for \"%s\": start the game with the room's rules first."), mine.rules.ruleSet.c_str(),
                   mine.caps.ruleSet.c_str()));
      return false;
    }
  }
  cfg.session.fingerprint = Encode(mine);
  if (const char* d = Env("XD_ONLINE_DIRECT")) cfg.directPeers = SplitList(d);
  if (Env("XD_ONLINE_NO_GO_BETWEEN")) cfg.useGoBetween = false;
  if (const char* lp = Env("XD_ONLINE_LOCAL_PORT")) cfg.localPort = static_cast<uint16_t>(std::atoi(lp));
  std::string code;
  if (const char* c = Env("XD_ONLINE_CODE")) code = xdnet::normalizeRoomCode(c);
  if (code.empty() && S.host) code = xdnet::newRoomCode();
  if (code.empty()) {
    EndEarly(Tr("The room code is not valid. Check it and try again."));
    return false;
  }
  S.code = code;
  if (!conn.start(code, cfg)) {
    EndEarly(conn.status().text);
    return false;
  }
  Log("room code %s (%s), %s", xdnet::formatRoomCode(code).c_str(), S.host ? "hosting" : "joining", xdnet::ipWarningText());
  xdnet::Phase last = xdnet::Phase::Idle;
  for (;;) {
    xdnet::ConnectionStatus st = conn.status();
    if (st.phase != last) {
      last = st.phase;
      Log("connection: %s - %s", xdnet::phaseName(st.phase), st.text.c_str());
    }
    if (st.phase == xdnet::Phase::Connected) break;
    if (st.phase == xdnet::Phase::Failed || st.phase == xdnet::Phase::Closed) {
      Log("%s", st.diagnostics.c_str());
      EndEarly(st.text + (st.detail.empty() ? "" : " " + st.detail));
      return false;
    }
    fightMainWaitFrame(1);
  }
  xdnet::ConnectionStatus st = conn.status();
  Log("connected (%s, round trip %d ms), the friend's fingerprint is %zu bytes", S.host ? "hosting" : "joining", static_cast<int>(st.stats.rttMs),
      st.peerFingerprint.size());
  Fingerprint peer;
  if (!Decode(st.peerFingerprint, peer)) {
    EndEarly(Tr("Your friend's game does not speak the same online battle version. Update both games."));
    conn.close();
    return false;
  }
  Verdict verdict;
  if (S.host) {
    verdict = Judge(mine.caps, mine.rules, peer.caps, false);
    S.rules = mine.rules;
    if (verdict.Ok()) {
      Verdict own = Judge(mine.caps, mine.rules, mine.caps, true);
      for (const std::string& r : own.restart) verdict.restart.push_back(r);
    }
  } else {
    verdict = Judge(peer.caps, peer.rules, mine.caps, true);
    S.rules = peer.rules;
  }
  if (!verdict.Ok()) {
    for (const std::string& p : verdict.refuse) Log("cannot play: %s", p.c_str());
    EndEarly(verdict.refuse.front());
    conn.close();
    return false;
  }
  if (verdict.NeedsRestart()) {
    for (const std::string& r : verdict.restart) Log("needs a restart: %s", r.c_str());
    EndEarly(Fmt(Tr("%s (not built yet)"), verdict.restart.front().c_str()));
    conn.close();
    return false;
  }
  Log("the two games match; room rules: %s", RulesText(S.rules).c_str());
  ApplyRoomRules();
  S.link = std::move(t);
  S.connected = true;
  return true;
}

bool Begin() {
  if (S.dead) return false;
  if (S.spectator) {
    if (!S.seeded) {
      S.seeded = true;
      if (__hsd_rand_seed_hold) *__hsd_rand_seed_hold = S.feed->seed;
      Log("spectator: the logic stream starts from the players' seed %08x", static_cast<unsigned>(S.feed->seed));
    }
    ApplyRoomRules();
    if (!S.dead && !ExchangeTeams()) return false;
    return !S.dead;
  }
  if (!S.connected && !ConnectRoom()) return false;
  if (S.lobby) ApplyRoomRules();
  if (!S.seeded) {
    S.seeded = true;
    if (S.host) {
      Send("Z " + std::to_string(__hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u));
    } else {
      if (const char* shift = Env("XD_ONLINE_TEST_SEED_SHIFT")) { if (__hsd_rand_seed_hold) *__hsd_rand_seed_hold += static_cast<uint32_t>(std::atoi(shift)); }
      std::string line;
      if (!Pump("Z", line, true)) return false;
      unsigned long long v = std::strtoull(line.c_str() + 2, nullptr, 10);
      if (__hsd_rand_seed_hold) *__hsd_rand_seed_hold = static_cast<uint32_t>(v);
      Log("random seed taken from the host: %08x", static_cast<unsigned>(v));
    }
  }
  if (!S.dead && !ExchangeTeams()) return false;
  return !S.dead;
}

bool Mine(void* trainer) {
  if (S.spectator) return false;
  const bool hostSide = fightTargetIsHostSide(trainer, fightFloor_GetFightType(nullptr)) != 0;
  return hostSide == S.host;
}

struct SeedKeep {
  uint32_t* p; uint32_t v;
  SeedKeep() : p(__hsd_rand_seed_hold), v(p ? *p : 0) {}
  ~SeedKeep() { if (p) *p = v; }
};

}

namespace {
uint32_t sVsNonce = 0;
uint32_t sVsCalls = 0;
}
namespace xd::online {
uint32_t NewNonce() {
  static std::random_device rd;
  uint32_t n = rd();
  return n == 0 ? 1u : n;
}
void SetVsNonce(uint32_t nonce) {
  sVsNonce = nonce;
  sVsCalls = 0;
  Log("the host's number for this battle's VS menu choices (stage, music): %08x", static_cast<unsigned>(nonce));
}
bool Active() {
  EnsureInit();
  if (sLobbyPending) {
    sLobbyPending = false;
    S = State();
    S.room = true;
    S.lobby = true;
    S.active = true;
    S.connected = true;
    S.host = sLobbyIn.host;
    S.human = !S.host;
    S.rules = sLobbyIn.rules;
    S.code = sLobbyIn.code;
    S.lobbySave = sLobbyIn.saveFile;
    S.lobbyMine = sLobbyIn.mineCount;
    S.lobbyFriend = sLobbyIn.friendCount;
    S.lobbyPicks = sLobbyIn.picks;
    S.lobbySpecies = sLobbyIn.freeSpecies;
    S.friendPreview = sLobbyIn.friendPreview;
    S.link.reset(new NetTransport(sLobbyIn.conn));
    sLobbyIn.conn = nullptr;
    Log("lobby room taken over: %s, rules: %s", S.host ? "hosting" : "joined", RulesText(S.rules).c_str());
  }
  return S.active;
}
bool Spectating() { return S.spectator && S.active; }
void SetLobbyRoom(LobbyHandOver& h) {
  sLobbyIn = h;
  h.conn = nullptr;
  sLobbyPending = true;
}
BattleSummary LobbySummary() {
  BattleSummary b;
  b.inRoom = S.lobby && S.active;
  b.host = S.host;
  b.over = S.battleOver;
  b.result = S.lastResult;
  b.desync = S.lastDesync;
  b.dead = S.dead;
  b.notice = S.endNotice;
  b.rulesText = RulesText(S.rules, xd::xg::Edition());
  return b;
}
Capabilities LobbyCapabilities() { return MyCapabilities(); }
PcInfo LobbyPcInfo() {
  PcInfo p;
  p.edition = xd::xg::Edition();
  if (const char* why = xd::xg::Off(xd::xg::Feature::Gen4)) p.gen4RulesOff = why;
  if (const char* why = xd::xg::Off(xd::xg::Feature::Gen4Pokemon)) p.gen4Off = why;
  p.gen4PackPresent = xd::gen4::PackPresent() && p.gen4Off.empty();
  p.gen4Loaded = xd::gen4::Loaded();
  for (const engine::rulesets::Format& f : xd::rulesets::Formats()) {
    PcInfo::FormatChoice c;
    c.id = f.id;
    c.name = xd::rulesets::text::FormatName(f.id, f.name);
    const bool needs4 = f.ruleSet == "gen4" || std::find(f.gens.begin(), f.gens.end(), 4) != f.gens.end();
    if (needs4 && !p.gen4PackPresent) c.enabled = false, c.why = "Needs the Gen 4 pack: add your Battle Revolution disc in the launcher (Extra assets page).";
    p.formats.push_back(c);
  }
  p.applyFormat = [](RoomRules& r) { std::string why; return ApplyFormat(r, &why); };
  p.formatLine = [](const RoomRules& r, const char* what) -> std::string {
    const engine::rulesets::Format* f = xd::rulesets::FindFormat(r.format);
    if (f == nullptr) return Tr("none");
    namespace tx = xd::rulesets::text;
    const std::string w = what;
    std::string out;
    if (w == "clauses") {
      for (const char* c : {"species", "item", "sleep", "ohko", "evasion"}) { if (f->clauses.count(c)) out += (out.empty() ? "" : ", ") + std::string(tx::ClauseName(c)); }
      return out.empty() ? std::string(Tr("none")) : out;
    }
    if (w == "bans") {
      for (const std::string& c : f->bannedCategories) out += (out.empty() ? "" : ", ") + std::string(Tr("No")) + " " + tx::CategoryName(c);
      const size_t n = f->bannedSpecies.size() + f->bannedItems.size() + f->bannedMoves.size() + f->bannedAbilities.size();
      if (n > 0) out += (out.empty() ? "" : ", ") + Fmt(Tr("%d on the ban list"), static_cast<int>(n));
      return out.empty() ? std::string(Tr("none")) : out;
    }
    if (w == "preview") {
      if (!f->teamPreview) return Tr("Off");
      return Fmt(Tr("Bring %d, pick %d"), f->teamMax, f->pick > 0 ? f->pick : f->teamMax);
    }
    if (f->timerPerChoice == 0 && f->timerPerBattle == 0) return Tr("No limit");
    return Fmt(Tr("%d s a turn, %d min each"), f->timerPerChoice, f->timerPerBattle / 60);
  };
  return p;
}
bool InFight() { return fightIsFight() != 0; }
BattleTransport* LobbyLink() { return S.lobby && S.active ? S.link.get() : nullptr; }
const RoomRules& LobbyRules() { return S.rules; }
void LobbyRematchReset() {
  const bool host = S.host;
  std::unique_ptr<BattleTransport> link = std::move(S.link);
  RoomRules rules = S.rules;
  std::string code = S.code, save = S.lobbySave;
  std::vector<Pick> picks = S.lobbyPicks;
  std::vector<uint16_t> species = S.lobbySpecies;
  std::vector<uint16_t> friendPreview = S.friendPreview;
  const int lm = S.lobbyMine, lf = S.lobbyFriend;
  S = State();
  S.friendPreview = friendPreview;
  S.lobbyMine = lm;
  S.lobbyFriend = lf;
  S.room = S.lobby = S.active = S.connected = true;
  S.host = host;
  S.human = !host;
  S.link = std::move(link);
  S.rules = rules;
  S.code = code;
  S.lobbySave = save;
  S.lobbyPicks = picks;
  S.lobbySpecies = species;
  Log("rematch: the same room again");
}
namespace {
extern "C" {
struct PortXD_VsSetup {
  int32_t encountId;
  uint32_t fightKind;
  uint32_t fightType;
  uint32_t floorId;
  uint32_t bgm;
  int32_t state;
  int32_t trainerCount;
  uint16_t dataId[4];
  int32_t controller[4];
  const void* hero[4];
  int32_t freeTitle;
};
int32_t PortXD_StartVsBattle(const PortXD_VsSetup* setup);
void* fightTypeDataBiosGetPtr(uint32_t type);
uint8_t fightTypeDataBiosGetFightoutPokemonNum(void* data);
}
alignas(16) uint8_t sHero[2][0x1000];
}

extern "C" {
void* fightEncountDataBiosGetPtr(uint16_t id);
}
static void SetQuickBattleStyle(bool single) {
  static uint8_t saved[64][3];
  static bool have = false, applied = false;
  const uint8_t* e5 = static_cast<const uint8_t*>(fightEncountDataBiosGetPtr(5));
  if (!e5) return;
  if (!have) {
    for (uint16_t id = 7; id <= 54; id++) { const uint8_t* e = static_cast<const uint8_t*>(fightEncountDataBiosGetPtr(id)); if (e) std::memcpy(saved[id], e + 1, 3); }
    have = true;
  }
  if (single == applied) return;
  for (uint16_t id = 7; id <= 54; id++) {
    if (id > 7 && id < 15) continue;
    uint8_t* e = static_cast<uint8_t*>(fightEncountDataBiosGetPtr(id));
    if (e) std::memcpy(e + 1, single ? e5 + 1 : saved[id], 3);
  }
  applied = single;
  Log("VS menu: Quick Battle's encounters now make a %s battle (%s)", single ? "single" : "double", single ? "from the room" : "as the game has it");
}
void LobbyApplyVsStyle() {
  if (!S.lobby || !S.active) return;
  SetQuickBattleStyle(S.rules.style != "double");
}
void LobbyRestoreVsStyle() { SetQuickBattleStyle(false); }

int LobbyStartBattle() {
  if (!S.lobby || !S.active) return -1;
  const int want = S.rules.style == "double" ? 2 : 1;
  uint32_t type = 0, state = 0;
  for (uint32_t k = 0; k < 2; k++) {
    const uint32_t t = k == 0 ? 0x17u : 0x18u;
    void* data = fightTypeDataBiosGetPtr(t);
    const int n = data ? fightTypeDataBiosGetFightoutPokemonNum(data) : 0;
    Log("fight type 0x%x puts %d Pokemon out per side", t, n);
    if (n == want && type == 0) type = t, state = k;
  }
  if (type == 0) { Log("no fight type for a %s battle: not started", want == 2 ? "double" : "single"); return -1; }
  if (S.spectator) { S.lobbyMine = S.feed->partyHost > 0 ? S.feed->partyHost : static_cast<int>(S.feed->hostTeam.size()); S.lobbyFriend = S.feed->partyGuest > 0 ? S.feed->partyGuest : static_cast<int>(S.feed->guestTeam.size()); }
  if (S.lobbyMine < want || S.lobbyFriend < want) { Log("a %s battle needs at least %d Pokemon a side", want == 2 ? "double" : "single", want); return -1; }
  if (!xd::online::MakePlaceholderHero(sHero[0], S.host ? S.lobbyMine : S.lobbyFriend) || !xd::online::MakePlaceholderHero(sHero[1], S.host ? S.lobbyFriend : S.lobbyMine)) {
    Log("the heroes for the battle could not be made");
    return -1;
  }
  if (S.link && __hsd_rand_seed_hold) {
    if (S.host) {
      Send("Z " + std::to_string(*__hsd_rand_seed_hold));
    } else {
      std::string line;
      if (!Pump("Z", line, true)) { Log("the host's battle seed did not arrive: not started"); return -1; }
      *__hsd_rand_seed_hold = static_cast<uint32_t>(std::strtoull(line.c_str() + 2, nullptr, 10));
    }
    Log("battle seed before the battle is built: %08x (%s)", static_cast<unsigned>(*__hsd_rand_seed_hold), S.host ? "mine, sent" : "the host's");
  }
  PortXD_VsSetup v;
  std::memset(&v, 0, sizeof v);
  v.encountId = 0;
  v.fightKind = 0xB;
  v.fightType = type;
  v.floorId = 0x24;
  v.bgm = 0x4F3;
  v.state = static_cast<int32_t>(state);
  v.trainerCount = 2;
  v.dataId[0] = 5000;
  v.dataId[1] = 5000;
  v.controller[0] = S.host ? 1 : 2;
  v.controller[1] = S.host ? 2 : 1;
  v.hero[0] = sHero[0];
  v.hero[1] = sHero[1];
  v.freeTitle = 1;
  Log("starting the %s battle through PortXD_StartVsBattle (fight type 0x%x)", want == 2 ? "double" : "single", type);
  const int32_t r = PortXD_StartVsBattle(&v);
  Log("PortXD_StartVsBattle returned %d", r);
  return r;
}

extern "C" {
void* GSthreadCreate(int32_t affinity, int32_t group, int32_t stackSize, int32_t usesFPU, int32_t autoStart, void* entry);
uint32_t GSfloorGetCurrentFloorGroupID(void);
}
namespace {
void BattleThreadMain() { LobbyStartBattle(); }
}
bool LobbyStartBattleThread() {
  void* t = GSthreadCreate(1, static_cast<int32_t>(GSfloorGetCurrentFloorGroupID()), 0x10000, 1, 1, reinterpret_cast<void*>(&BattleThreadMain));
  Log("battle thread %s", t ? "created" : "NOT created");
  return t != nullptr;
}

bool SpectatorStart(SpectatorFeed* feed, std::string* why) {
  EnsureInit();
  RoomRules rules;
  std::string err;
  if (!feed || !feed->haveStart || !ParseRules(feed->rulesKey, rules, &err)) {
    if (why) *why = "The room's rules in this replay are not readable" + (err.empty() ? std::string(".") : ": " + err);
    return false;
  }
  if (feed->hostTeam.empty() || feed->guestTeam.empty()) {
    if (why) *why = "This replay holds no teams.";
    return false;
  }
  S = State();
  sSpecPending.clear();
  S.room = S.lobby = S.active = S.connected = true;
  S.host = true;
  S.spectator = true;
  S.feed = feed;
  S.rules = rules;
  Log("spectator: the room's rules: %s; %zu + %zu Pokemon", RulesText(S.rules).c_str(), feed->hostTeam.size(), feed->guestTeam.size());
  const bool direct = std::getenv("XD_ONLINE_DIRECT_START") != nullptr;
  if (!direct) SpectatorDriveVsMenus();
  if (direct && !LobbyStartBattleThread()) {
    S.active = S.spectator = false;
    if (why) *why = "The battle could not be started.";
    return false;
  }
  return true;
}

namespace {
struct SpecClient {
  std::unique_ptr<xdnet::Connection> conn;
  std::string room;
  int place = 0, tries = 0;
  bool everConnected = false;
  std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
};
void SpecClientOpen(SpecClient& c) {
  xdnet::ConnectionConfig cfg = xdnet::Connection::defaultConfig();
  xd::online::Fingerprint f;
  f.caps = MyCapabilities();
  cfg.session.fingerprint = xd::online::Encode(f);
  if (const char* d = Env("XD_ONLINE_DIRECT")) cfg.directPeers = SplitList(d);
  if (Env("XD_ONLINE_NO_GO_BETWEEN")) cfg.useGoBetween = false;
  if (const char* lp = Env("XD_ONLINE_LOCAL_PORT")) cfg.localPort = static_cast<uint16_t>(std::atoi(lp));
  cfg.connectTimeoutMs = 8000;
  c.conn.reset(new xdnet::Connection());
  const std::string code = WatchCode(c.room, c.place);
  c.tries++;
  if (!c.conn->start(code, cfg)) Log("spectator: could not start looking for the room (try %d)", c.tries);
}
std::vector<std::string> SplitCommaList(const std::string& t) {
  std::vector<std::string> v;
  std::string cur;
  for (char ch : t) { if (ch == ',') { v.push_back(cur); cur.clear(); } else cur += ch; }
  if (!cur.empty()) v.push_back(cur);
  return v;
}
void SpecClientPoll(SpecClient& c, xd::online::SpectatorFeed& F) {
  if (F.complete && F.lost) return;
  const xdnet::Phase ph = c.conn ? c.conn->status().phase : xdnet::Phase::Closed;
  if (ph == xdnet::Phase::Connected && !c.everConnected) { c.everConnected = true; Log("spectator: connected to the host's watch place %d", c.place); }
  std::vector<uint8_t> m;
  while (c.conn && c.conn->receive(m)) {
    const std::string line(m.begin(), m.end());
    std::istringstream in(line);
    std::string w;
    in >> w;
    if (w == "WH") { F.rulesKey = line.size() > 3 ? line.substr(3) : std::string(); }
    else if (w == "WT") { std::string who, list; in >> who >> list; (who == "host" ? F.hostTeam : F.guestTeam) = SplitCommaList(list); }
    else if (w == "WZ") { unsigned long long v = 0; in >> v; F.seed = static_cast<uint32_t>(v); F.haveStart = !F.rulesKey.empty() && !F.hostTeam.empty() && !F.guestTeam.empty() && (!F.needParty || F.haveParty); }
    else if (w == "WP") { in >> F.partyHost >> F.partyGuest; F.haveParty = true; F.haveStart = !F.rulesKey.empty() && !F.hostTeam.empty() && !F.guestTeam.empty() && F.seed != 0; }
    else if (w == "WC") { uint32_t seq = 0; in >> seq; std::string rest; std::getline(in, rest); while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1); F.choices[seq] = rest; }
    else if (w == "WK" || w == "WX") { uint32_t seq = 0; std::string hex; in >> seq >> hex; (w == "WK" ? F.choiceSeeds : F.switchSeeds)[seq] = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)); }
    else if (w == "WW") { uint32_t seq = 0; int r = 0; in >> seq >> r; F.switches[seq] = r; }
    else if (w == "WD") { uint32_t t = 0; std::string hex; in >> t >> hex; F.digests[t] = std::strtoull(hex.c_str(), nullptr, 16); }
    else if (w == "WE") { in >> F.result; F.complete = true; }
  }
  if (ph == xdnet::Phase::Closed || ph == xdnet::Phase::Failed) {
    if (c.everConnected) { if (!F.complete) Log("spectator: the connection to the host ended"); F.lost = true; }
    else if (std::chrono::steady_clock::now() - c.startedAt < std::chrono::seconds(600)) {
      const char* np = Env("XD_ONLINE_SPECTATORS");
      c.place = (c.place + 1) % (np ? std::max(1, std::atoi(np)) : kMaxSpectators);
      SpecClientOpen(c);
    } else {
      F.lost = true;
    }
  }
}
}

bool SpectatorJoin(SpectatorFeed* feed, const std::string& roomCode, std::string* why) {
  const std::string code = xdnet::normalizeRoomCode(roomCode);
  if (code.empty()) {
    if (why) *why = "The room code is not valid.";
    return false;
  }
  feed->needParty = true;
  std::shared_ptr<SpecClient> c(new SpecClient());
  c->room = code;
  SpecClientOpen(*c);
  feed->poll = [c](SpectatorFeed& f) { SpecClientPoll(*c, f); };
  Log("spectator: looking for room %s (its watch places)", xdnet::formatRoomCode(code).c_str());
  return true;
}

void LobbyLeave() {
  LobbyRestoreVsStyle();
  if (S.link) {
    std::string bye = "L1 LEAVE";
    S.link->Send(reinterpret_cast<const uint8_t*>(bye.data()), bye.size());
  }
  S.link.reset();
  S = State();
  Log("left the room");
}
}

namespace {
bool StoryMenuOn();
struct MyChoice {
  bool have = false;
  uint32_t seq = 0;
  uint8_t r = 0;
  std::string record;
  std::string nonceHex;
  bool revealed = false;
  bool busy = false;
  bool consumed = false;
};
MyChoice sMine;
int sCurChoiceSeq = -1;

void* FindLocalTrainer(void* except) {
  for (uint16_t sd = 0; sd < 2; sd++) {
    void* sideRec = fightFloorGetValidFightSidePtr(nullptr, sd);
    if (!sideRec) continue;
    for (uint16_t t = 0; t < 2; t++) {
      void* tr = fightSideGetValidFightTrainerPtr(sideRec, t);
      if (tr && tr != except && Mine(tr)) return tr;
    }
  }
  return nullptr;
}

bool WaitMap(std::map<uint32_t, std::string>& m, uint32_t key) {
  while (m.find(key) == m.end()) {
    std::string d;
    if (!Pump(std::string(1, '\x01'), d, true)) return false;
  }
  return true;
}

bool ApplyChoiceLine(void* trainer, const std::string& line, uint32_t seq, int* rOut, int side) {
  std::istringstream in(line);
  char t;
  uint32_t got = 0;
  int r = 0, count = 0;
  in >> t >> got >> r >> count;
  if (got != seq) { Log("choice number %u, expected %u: out of step", got, seq); S.dead = true; return false; }
  std::vector<std::string> rest;
  for (std::string w; in >> w;) rest.push_back(w);
  const size_t numeric = rest.size() - ((!rest.empty() && rest.back() == "TO") ? 1 : 0);
  const size_t width = count > 0 && numeric == static_cast<size_t>(count) * 9 ? 9 : 8;
  if (numeric < static_cast<size_t>(count) * width) { Log("choice %u: the record is cut short", seq); S.dead = true; return false; }
  for (int i = 0; i < count; i++) {
    unsigned j, kind, target, item, ibuff, mega = 0;
    int buff, banme, autoMake;
    auto num = [&](size_t k) { return std::strtoll(rest[static_cast<size_t>(i) * width + k].c_str(), nullptr, 10); };
    j = static_cast<unsigned>(num(0)), kind = static_cast<unsigned>(num(1)), buff = static_cast<int>(num(2)), target = static_cast<unsigned>(num(3));
    banme = static_cast<int>(num(4)), autoMake = static_cast<int>(num(5)), item = static_cast<unsigned>(num(6)), ibuff = static_cast<unsigned>(num(7));
    if (width == 9) mega = static_cast<unsigned>(num(8));
    void* fop = fightTrainerGetValidFightOutPokemonPtr(trainer, static_cast<uint16_t>(j));
    if (!fop) { Log("no Pokemon %u to give an action to", j); S.dead = true; return false; }
    fightOutPokemonInitFightActionBuff(fop);
    if (kind == 0x13) {
      xd::forms::SetFriendApplying(true);
      fightOutPokemonCreateFightActionAttackWaza(fop, 0, 0x13, 0, fight_action_data_waza, buff, static_cast<uint16_t>(target),
                                                 static_cast<int8_t>(banme), static_cast<uint8_t>(autoMake));
      xd::forms::SetFriendApplying(false);
      if (mega) xd::forms::FriendMega(fop);
    } else if (kind == 0x12) {
      fightOutPokemonCreateFightActionUseItem(fop, 0, 0x12, 0, fight_action_data_item, item, static_cast<uint16_t>(target), banme, static_cast<int32_t>(ibuff));
      if (void* hero = fightTrainerBiosGetHeroPtr(trainer)) heroItemDecItemDataId(hero, item, 1, banme);
      ApplyFriendItemEffect(trainer, item, target);
      {
        void* act = fightOutPokemon_GetFightActionBuffPtr(fop);
        void* fi = fightOutPokemon_GetFightItemPtr(fop);
        Log("the friend's item %u replayed (target %u, bag slot %d, buff %u); action valid %d kind %u, item %u target %u slot %d buff %u", item, target, banme, ibuff,
            act ? static_cast<int>(fightActionCheckValid(act)) : -1, act ? static_cast<unsigned>(fightActionGetKindDataId(act)) : 0u,
            fi ? static_cast<unsigned>(fightItem_GetItemDataId(fi)) : 0u, fi ? static_cast<unsigned>(fightItem_GetTargetDataId(fi)) : 0u,
            fi ? static_cast<int>(fightItem_GetCount(fi)) : -9, fi ? static_cast<unsigned>(fightItem_GetBuff(fi)) : 0u);
      }
    } else if (kind == 8 || kind == 9 || kind == 0xA) {
      fightOutPokemonCreateFightAction(fop, 0, static_cast<int32_t>(kind), 0, fight_action_data_turn, buff);
    } else {
      Log("action kind %u not carried by the prototype", kind);
      S.dead = true;
      return 0;
    }
  }
  {
    const std::string tail = rest.size() > numeric ? rest.back() : std::string();
    if (tail == "TO") { S.timeouts[side]++; Log("choice %u was a time-out on side %d (their clock ended it)", seq, side); }
  }
  *rOut = r;
  return true;
}

void SpectatorCompare() {
  xd::online::SpectatorFeed& F = *S.feed;
  if (F.poll) F.poll(F);
  for (size_t i = 0; i < sSpecPending.size();) {
    auto it = F.digests.find(sSpecPending[i].first);
    if (it == F.digests.end()) { i++; continue; }
    if (it->second == sSpecPending[i].second) { F.turnsMatched++; Log("spectator: turn %u check number %016llx = the players': OK", sSpecPending[i].first, static_cast<unsigned long long>(it->second)); }
    else { F.turnsDifferent++; Log("spectator: turn %u DIFFERENT: mine %016llx, the players' %016llx", sSpecPending[i].first, static_cast<unsigned long long>(sSpecPending[i].second), static_cast<unsigned long long>(it->second)); }
    sSpecPending.erase(sSpecPending.begin() + static_cast<long>(i));
  }
}
template <class Ready> bool SpectatorWait(Ready ready) {
  xd::online::SpectatorFeed& F = *S.feed;
  for (;;) {
    if (F.poll) F.poll(F);
    if (ready()) return true;
    if (F.lost || S.dead || F.complete) return false;
    fightMainWaitFrame(1);
  }
}
uint8_t SpectatorSelect(void* trainer) {
  xd::online::SpectatorFeed& F = *S.feed;
  const int sideNo = fightTargetIsHostSide(trainer, fightFloor_GetFightType(nullptr)) != 0 ? 0 : 1;
  const uint32_t seq = 2 * S.cnt[sideNo] + static_cast<uint32_t>(sideNo);
  SeedKeep keepWait;
  fightTrainerAllInitFightActionBuff(trainer);
  if (fightGetFightResultId() == 1) return 0;
  if (!SpectatorWait([&] { return F.choices.count(seq) != 0; })) {
    Log("spectator: the stream ended before choice %u", seq);
    S.dead = true;
    EndWithError();
    return 0;
  }
  if (auto it = F.choiceSeeds.find(seq); it != F.choiceSeeds.end() && it->second != 0) keepWait.v = it->second;
  int r = 0;
  if (!ApplyChoiceLine(trainer, F.choices[seq], seq, &r, sideNo)) { EndWithError(); return 0; }
  S.applied++;
  S.cnt[sideNo]++;
  Log("spectator: choice %u (side %d) applied", seq, sideNo);
  return static_cast<uint8_t>(r);
}
int16_t SpectatorSwitch(uint32_t seq) {
  xd::online::SpectatorFeed& F = *S.feed;
  SeedKeep keepWait;
  if (!SpectatorWait([&] { return F.switches.count(seq) != 0; })) {
    Log("spectator: the stream ended before switch %u", seq);
    S.dead = true;
    EndWithError();
    return -2;
  }
  if (auto it = F.switchSeeds.find(seq); it != F.switchSeeds.end() && it->second != 0) keepWait.v = it->second;
  S.applied++;
  Log("spectator: switch %u applied: %d", seq, F.switches[seq]);
  return static_cast<int16_t>(F.switches[seq]);
}

void LocalChoose(void* trainer, uint32_t side, uint32_t seq) {
    sMine.busy = true;
    sMine.consumed = false;
    uint8_t r;
    {
      SeedKeep keep;
      S.choiceStart = std::chrono::steady_clock::now();
      S.bankOn = S.rules.bankSeconds > 0;
      if (S.rules.timerSeconds > 0 || S.bankOn) {
        if (S.bankOn && S.bankMs < 0) S.bankMs = static_cast<int64_t>(Env("XD_ONLINE_TEST_BANK_MS") ? std::atoi(Env("XD_ONLINE_TEST_BANK_MS")) : S.rules.bankSeconds * 1000);
        int64_t limit = S.rules.timerSeconds > 0 ? static_cast<int64_t>(S.rules.timerSeconds) * 1000 : (std::numeric_limits<int64_t>::max)() / 4;
        if (S.rules.timerSeconds > 0 && Env("XD_ONLINE_TEST_TIMER_MS")) limit = std::atoi(Env("XD_ONLINE_TEST_TIMER_MS"));
        if (S.bankOn) limit = std::min<int64_t>(limit, std::max<int64_t>(1, S.bankMs));
        S.deadline = S.choiceStart + std::chrono::milliseconds(limit);
        S.timing = true;
        S.timeUpDone = false;
        S.lastCount = 1 << 30;
        Log("choice %u: the limit is %lld ms%s", seq, static_cast<long long>(limit), S.bankOn ? " (the battle clock counts)" : "");
      }
      if (const char* pr = Env("XD_ONLINE_TEST_PRESSES")) {
        std::string all = pr;
        size_t from = 0;
        while (from <= all.size()) {
          size_t bar = all.find('|', from);
          if (bar == std::string::npos) bar = all.size();
          const std::string part = all.substr(from, bar - from);
          from = bar + 1;
          const size_t eq = part.find('=');
          if (eq == std::string::npos || static_cast<uint32_t>(std::atoi(part.c_str())) != seq) continue;
          const uint32_t now = PortInput_ScriptNow();
          std::string out;
          size_t f2 = eq + 1;
          while (f2 <= part.size()) {
            size_t sl = part.find('/', f2);
            if (sl == std::string::npos) sl = part.size();
            const std::string st = part.substr(f2, sl - f2);
            f2 = sl + 1;
            const size_t c = st.find(':');
            if (c == std::string::npos) continue;
            out += (out.empty() ? "" : ",") + std::to_string(now + static_cast<uint32_t>(std::atoi(st.c_str()))) + st.substr(c);
          }
          PortInput_LoadScript(out.c_str());
          Log("test: presses for choice %u scheduled from frame %u", seq, now);
        }
      }
      sCurChoiceSeq = static_cast<int>(seq);
      if (const char* gi = Env("XD_ONLINE_TEST_GIVEIN_TIMED")) {
        if (seq == static_cast<uint32_t>(std::atoi(gi))) {
          const uint32_t now = PortInput_ScriptNow();
          char steps[400];
          std::snprintf(steps, sizeof steps, "%u:right:6,%u:a:6,%u:left:6,%u:a:6", now + 90, now + 130, now + 220, now + 260);
          if (const char* custom = Env("XD_ONLINE_TEST_GIVEIN_STEPS")) {
            std::string out;
            for (const std::string& st : SplitComma(custom)) {
              const size_t c = st.find(':');
              if (c == std::string::npos) continue;
              out += (out.empty() ? "" : ",") + std::to_string(now + static_cast<uint32_t>(std::atoi(st.c_str()))) + st.substr(c);
            }
            std::snprintf(steps, sizeof steps, "%s", out.c_str());
          }
          PortInput_LoadScript(steps);
          Log("test: the presses for GIVE IN and its confirmation are scheduled from frame %u", now);
        }
      }
      const char* quitAt = Env("XD_ONLINE_TEST_QUIT_CHOICE");
      const auto makeQuit = [&](bool setResult) {
        fightTrainerAllInitFightActionBuff(trainer);
        void* fop0 = fightTrainerGetValidFightOutPokemonPtr(trainer, 0);
        if (!fop0) fop0 = fightTrainerGetValidFightOutPokemonPtr(trainer, 1);
        if (fop0) fightOutPokemonCreateFightAction(fop0, 0, 8, 0, fight_action_data_turn, 0);
        if (setResult) fightFloorSetFightResultId(nullptr, S.host ? 4 : 5);
      };
      for (;;) {
        if (quitAt && seq == static_cast<uint32_t>(std::atoi(quitAt))) {
          makeQuit(Env("XD_ONLINE_TEST_QUIT_NOW") != nullptr);
          r = 1;
        } else {
          S.choosing = StoryMenuOn();
          S.startRequested = false;
          if (S.human && !S.host) {
            fightTrainerAllInitFightActionBuff(trainer);
            fightMenuFightTrainerGcHeroOpenMenu(trainer, static_cast<uint16_t>(side), 0);
            r = 1;
          } else {
            if (Env("XD_ONLINE_TEST_STATELOG")) Log("state: select starts (script clock %u, choosing %d)", PortInput_ScriptNow(), S.choosing ? 1 : 0);
            r = fightTrainerSelectFightAction(trainer, side);
            if (Env("XD_ONLINE_TEST_STATELOG")) Log("state: select returned %u (script clock %u)", static_cast<unsigned>(r), PortInput_ScriptNow());
          }
          S.choosing = false;
          if (S.startRequested && fightGetFightResultId() != 1) {
            S.startRequested = false;
            int32_t zero = 0;
            const int32_t answer = PortXD_MenuOpenCustom(0x8f, 0, &zero, 0, 1, 0, nullptr);
            menuCloseCustom(0x8f, 0, 1);
            Log("START: the question was answered with %d", static_cast<int>(answer));
            if (answer == 0x31) { Log("START: quit confirmed (the RUN action is the choice; the game ends the battle when it runs it, on both PCs)"); makeQuit(false); r = 1; }
            else { Log("START: not quitting, the menus come back"); fightTrainerAllInitFightActionBuff(trainer); continue; }
          }
        }
        break;
      }
      S.timing = false;
    }
    const bool timedOut = S.timeUpDone && (S.rules.timerSeconds > 0 || S.bankOn);
    if (S.bankOn) {
      const int64_t spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - S.choiceStart).count();
      S.bankMs = std::max<int64_t>(0, S.bankMs - spent);
      Log("battle clock: %lld ms left after choice %u", static_cast<long long>(S.bankMs), seq);
    }
    S.timeUpDone = false;
    std::ostringstream m;
    m << "A " << seq;
    int count = 0;
    std::vector<std::string> recList;
    bool anyMega = false;
    std::vector<int> megaList;
    for (uint16_t j = 0; j < 2; j++) {
      void* fop = fightTrainerGetValidFightOutPokemonPtr(trainer, j);
      if (!fop) continue;
      void* act = fightOutPokemon_GetFightActionBuffPtr(fop);
      if (!act || !fightActionCheckValid(act)) continue;
      const unsigned kind = fightActionGetKindDataId(act);
      const int buff = fightActionBiosGetBuffDataId(act);
      unsigned target = 0;
      int banme = 0, autoMake = 0;
      unsigned item = 0, ibuff = 0;
      if (kind == 0x13) {
        void* w = fightOutPokemon_GetFightWazaPtr(fop);
        target = fightWaza_GetTargetDataId(w);
        banme = fightWaza_GetWazaBanme(w);
        autoMake = fightWaza_GetAutoMakeFlag(w);
      } else if (kind == 0x12) {
        void* fi = fightOutPokemon_GetFightItemPtr(fop);
        item = fightItem_GetItemDataId(fi);
        target = fightItem_GetTargetDataId(fi);
        banme = fightItem_GetCount(fi);
        ibuff = fightItem_GetBuff(fi);
        Log("item choice: item %u, target %u, bag slot %d, buff %u", item, target, banme, ibuff);
      }
      {
        std::ostringstream one;
        one << ' ' << j << ' ' << kind << ' ' << buff << ' ' << target << ' ' << banme << ' ' << autoMake << ' ' << item << ' ' << ibuff;
        recList.push_back(one.str());
        const int mega = xd::forms::TakeChoiceMega(fop) ? 1 : 0;
        megaList.push_back(mega);
        anyMega = anyMega || mega != 0;
      }
      count++;
    }
    {
      const uint16_t res = fightGetFightResultId();
      if ((res == 4 || res == 5) && count == 0) {
        const unsigned jq = fightTrainerGetValidFightOutPokemonPtr(trainer, 0) ? 0u : 1u;
        recList.push_back(" " + std::to_string(jq) + " 8 0 0 0 0 0 0");
        megaList.push_back(0);
        count = 1;
        Log("this player quits: the RUN choice is sent");
      }
    }
    std::string recsText;
    for (size_t i = 0; i < recList.size(); i++) recsText += recList[i] + (anyMega ? " " + std::to_string(megaList[i]) : std::string());
    m << ' ' << static_cast<int>(r) << ' ' << count << recsText;
    if (timedOut) { m << " TO"; Log("choice %u ended by the clock: the game's time-out choice (the first usable move) is what is sent", seq); }
    sMine.have = true;
    sMine.busy = false;
    sMine.seq = seq;
    sMine.r = r;
    sMine.record = m.str();
    xd::replay::OnlineChoice(seq, sMine.record);
    xd::replay::OnlineSeed(seq, sEntrySeed.count(seq) ? sEntrySeed[seq] : 0u);
    sMine.revealed = false;
    unsigned char nonce[kNonceBytes];
    xdnet::randomBytes(nonce, sizeof nonce);
    sMine.nonceHex = ToHex(nonce, sizeof nonce);
    Send("C " + std::to_string(seq) + " " + Commit(sMine.nonceHex, sMine.record));
    S.sent++;
    if (timedOut) S.timeouts[S.host ? 0 : 1]++;
    Log("choice %u made here and COMMITTED (%d action(s)); nothing else of it has left this PC", seq, count);
}
}

extern "C" {

uint8_t PortXD_NetSelectAction(void* trainer, uint32_t side) {
  EnsureInit();
  if (!S.active) return xd::replay::SelectAction(trainer, side, fightTrainerSelectFightAction);
  S.began = true;
  if (!Begin()) { EndWithError(); return 0; }
  if (S.spectator) return SpectatorSelect(trainer);
  const bool mine = Mine(trainer);
  const int ownSide = S.host ? 0 : 1, frSide = 1 - ownSide;
  const int sideNo = mine ? ownSide : frSide;
  const uint32_t seq = 2 * S.cnt[sideNo] + static_cast<uint32_t>(sideNo);
  const uint32_t ownSeq = 2 * S.cnt[ownSide] + static_cast<uint32_t>(ownSide);
  { const uint32_t es = __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u; sEntrySeed.emplace(seq, es); sEntrySeed.emplace(ownSeq, es); }
  Log("choice asked for: the %s trainer (side %d), choice number %u (this PC's own would be %u)", mine ? "local" : "friend's", sideNo, seq, ownSeq);
  if (mine) {
    while (sMine.busy && !S.dead) fightMainWaitFrame(1);
    if (sMine.have && sMine.seq == seq) {
      sMine.consumed = true;
      if (sMine.revealed) sMine.have = false;
      S.cnt[ownSide]++;
      return sMine.r;
    }
    LocalChoose(trainer, side, seq);
    sMine.consumed = true;
    S.cnt[ownSide]++;
    return sMine.r;
  }
  SeedKeep keepWait;
  fightTrainerAllInitFightActionBuff(trainer);
  if (fightGetFightResultId() == 1) return 0;
  if (!sMine.have && !sMine.busy) {
    if (void* lt = FindLocalTrainer(trainer)) LocalChoose(lt, side, ownSeq);
  }
  while (sMine.busy && !S.dead) fightMainWaitFrame(1);
  if (!WaitMap(sPeerCommit, seq)) { EndWithError(); return 0; }
  Log("the friend's commitment for choice %u is in", seq);
  if (sMine.have && !sMine.revealed) {
    std::string rec = sMine.record;
    if (Env("XD_ONLINE_TEST_TAMPER") && !S.host && !rec.empty()) rec.back() = rec.back() == '0' ? '1' : '0';
    Send("V " + std::to_string(sMine.seq) + " " + sMine.nonceHex + " " + rec);
    sMine.revealed = true;
    HubSeed(sMine.seq, sEntrySeed.count(sMine.seq) ? sEntrySeed[sMine.seq] : 0u);
    HubChoice(sMine.seq, sMine.record);
  }
  if (!WaitMap(sPeerReveal, seq)) { EndWithError(); return 0; }
  const std::string reveal = sPeerReveal[seq];
  const std::string commit = sPeerCommit[seq];
  sPeerReveal.erase(seq);
  sPeerCommit.erase(seq);
  const size_t sp = reveal.find(' ');
  const std::string nonceHex = sp == std::string::npos ? std::string() : reveal.substr(0, sp);
  const std::string line = sp == std::string::npos ? std::string() : reveal.substr(sp + 1);
  if (!Verify(commit, nonceHex, line)) {
    S.digestsBad++;
    S.dead = true;
    S.endNotice = Tr("Your friend's choice does not match what he committed to. The battle stops (a changed or cheating game).");
    Log("choice %u: the reveal does NOT match the friend's commitment: cheat / desync", seq);
    PortVideo_Notice(S.endNotice.c_str(), 10.0);
    EndWithError();
    return 0;
  }
  Log("choice %u from the friend revealed and verified against his commitment", seq);
  xd::replay::OnlineChoice(seq, line);
  xd::replay::OnlineSeed(seq, sEntrySeed.count(seq) ? sEntrySeed[seq] : 0u);
  HubSeed(seq, sEntrySeed.count(seq) ? sEntrySeed[seq] : 0u);
  HubChoice(seq, line);
  int r = 0;
  if (!ApplyChoiceLine(trainer, line, seq, &r, frSide)) return 0;
  S.applied++;
  Log("choice %u from the friend applied", seq);
  S.cnt[frSide]++;
  if (sMine.have && sMine.consumed) sMine.have = false;
  return static_cast<uint8_t>(r);
}

static void LogSwitchMembers(const char* when, void* trainer, uintptr_t out) {
  char buf[400];
  int n = std::snprintf(buf, sizeof buf, "forced switch, %s:", when);
  for (uint16_t i = 0; i < 6 && n > 0 && n < 330; i++) {
    void* fp = fightTrainerBiosGetFightPokemonPtr(trainer, i);
    void* pk = fp ? fightPokemonGetPokemonPtr(fp) : nullptr;
    if (!pk) continue;
    n += std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), " [%u] entry %d species %u HP %u %s;", static_cast<unsigned>(i), static_cast<int>(fightPokemon_GetEntryId(fp)),
                       static_cast<unsigned>(pokemon_GetPokemonDataId(pk)), static_cast<unsigned>(pokemon_GetHp(pk)), fightPokemonCheckFightOut(fp) ? "can fight" : "can't fight");
  }
  void* fop = reinterpret_cast<void*>(out);
  if (fop) std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), " out: species %u", static_cast<unsigned>(figthOutPokemonGetPokemonDataId(fop)));
  Log("%s", buf);
}

int16_t PortXD_NetSelectIrekae(void* trainer, uint32_t a, uint32_t b, uintptr_t c) {
  EnsureInit();
  if (!S.active) return xd::replay::SelectIrekae(trainer, a, b, c, fightTrainerSelectIrekaeFightPokemon);
  if (!Begin()) { EndWithError(); return -2; }
  const uint32_t seq = S.swSeq++;
  if (S.spectator) return SpectatorSwitch(seq);
  { const uint32_t es = __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u; xd::replay::OnlineSwitchSeed(seq, es); HubSwitchSeed(seq, es); }
  if (Mine(trainer)) {
    int16_t r;
    {
      SeedKeep keep;
      LogSwitchMembers("asked", trainer, c);
      r = (S.human && !S.host) ? fightMenuFightTrainerGcHeroSelectIrekaeFightPokemon(trainer, nullptr, static_cast<uint16_t>(a), static_cast<uint8_t>(b), 0)
                               : fightTrainerSelectIrekaeFightPokemon(trainer, a, b, c);
    }
    Send("S " + std::to_string(seq) + " " + std::to_string(r));
    S.sent++;
    Log("switch choice %u made here: %d", seq, r);
    xd::replay::OnlineSwitch(seq, r);
    HubSwitch(seq, r);
    return r;
  }
  SeedKeep keepWait;
  std::string line;
  if (!Pump("S", line, true)) { EndWithError(); return -2; }
  unsigned got = 0;
  int r = 0;
  std::sscanf(line.c_str(), "S %u %d", &got, &r);
  if (got != seq) { Log("switch number %u, expected %u: out of step", got, seq); S.dead = true; return -2; }
  S.applied++;
  Log("switch choice %u from the friend applied: %d", seq, r);
  xd::replay::OnlineSwitch(seq, r);
  HubSwitch(seq, r);
  return static_cast<int16_t>(r);
}

void PortXD_OnlineTurnEnd(void) {
  if (!S.active) return;
  if (S.dead) { EndWithError(); return; }
  {
    const uint16_t res = fightGetFightResultId();
    if (res == 4 || res == 5) { Log("a player quit (result %u): the battle ends", static_cast<unsigned>(res)); return; }
  }
  uint32_t turn = fightFloor_GetTurnCount(nullptr);
  uint64_t d = Digest(turn);
  {
    char dbg[300];
    int n = std::snprintf(dbg, sizeof dbg, "turn %u state: seed %08x", turn, __hsd_rand_seed_hold ? *__hsd_rand_seed_hold : 0u);
    for (uint16_t b = 0; b < 4 && n > 0 && n < 240; b++) {
      void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
      void* p = fop ? fightOutPokemonGetPokemonPtr(fop) : nullptr;
      n += std::snprintf(dbg + n, sizeof dbg - static_cast<size_t>(n), " | %u/%u/%u%s", fop ? figthOutPokemonGetPokemonDataId(fop) : 0u,
                         p ? pokemon_GetHp(p) : 0u, p ? pokemon_GetMaxHp(p) : 0u, fop && fightOutPokemonIsJoutaiDataId(fop, 0x3E) == 1 ? " HYPER" : (fop && fightOutPokemonIsJoutaiDataId(fop, 8) == 1 ? " ASLEEP" : ""));
    }
    Log("%s", dbg);
  }
  if (S.spectator) {
    S.feed->turnsSeen++;
    sSpecPending.emplace_back(turn, d);
    xd::replay::NoteTurn(turn);
    SpectatorCompare();
    return;
  }
  if (const char* ft = Env("XD_ONLINE_TEST_FAINT")) {
    if (turn == static_cast<uint32_t>(std::atoi(ft))) {
      void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, 0);
      void* tr = fop ? fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fop) : nullptr;
      void* fp = tr ? fightTrainerBiosGetFightPokemonPtr(tr, 2) : nullptr;
      void* pk = fp ? fightPokemonGetPokemonPtr(fp) : nullptr;
      if (pk) { pokemon_SetHp(pk, 0); Log("test: the first side's party slot 2 is fainted now (HP 0)"); }
      d = Digest(turn);
    }
  }
  static const uint32_t envFault = std::getenv("XD_ONLINE_FAULT") ? static_cast<uint32_t>(std::atoi(std::getenv("XD_ONLINE_FAULT"))) : 0;
  if (((S.faultTurn != 0 && turn == S.faultTurn) || (envFault != 0 && turn == envFault)) && !S.host) d ^= 1;
  if (const char* q = Env("XD_ONLINE_TEST_QUIT_TURN")) {
    if (!S.host && turn == static_cast<uint32_t>(std::atoi(q))) { Log("test: this program stops now"); std::fflush(stderr); std::_Exit(0); }
  }
  xd::replay::OnlineParty(S.created[0], S.created[1]);
  xd::replay::OnlineTurn(turn, d);
  HubTurn(turn, d);
  S.mine.emplace_back(turn, d);
  char buf[64];
  std::snprintf(buf, sizeof buf, "D %u %llx", turn, static_cast<unsigned long long>(d));
  Send(buf);
  WaitDigests();
  if (S.dead) EndWithError();
}

extern "C" void OnlineMirror_Reset(void);
void PortXD_OnlineBattleEnd(uint32_t result) {
  if (!S.active) return;
  if (S.lobby && S.battleOver) return;
  RestoreOwnRules();
  if (!sShadowBackup.empty()) {
    if (uint8_t* live = static_cast<uint8_t*>(savedataGetStatus(nullptr, 0xF))) {
      for (const auto& kv : sShadowBackup) std::memcpy(live + kv.first * 0x48, kv.second.data(), 0x48);
    }
    Log("Shadow list: %zu slot(s) put back as they were", sShadowBackup.size());
    sShadowBackup.clear();
  }
  OnlineMirror_Reset();
  if (!S.dead) {
    for (int i = 0; i < 300 && !S.mine.empty() && !S.dead; i++) {
      std::string dummy;
      uint8_t b[256];
      int n = S.link->Receive(b, sizeof b - 1);
      if (n > 0) {
        std::string line(reinterpret_cast<char*>(b), static_cast<size_t>(n));
        if (line[0] == 'D') {
          unsigned t2 = 0; unsigned long long d2 = 0;
          std::sscanf(line.c_str(), "D %u %llx", &t2, &d2);
          for (size_t k = 0; k < S.mine.size(); k++) {
            if (S.mine[k].first != t2) continue;
            if (S.mine[k].second == d2) { S.digestsOk++; Log("turn %u digest %016llx = peer's: OK", t2, d2); }
            else { S.digestsBad++; S.dead = true; Log("turn %u DESYNC: mine %016llx, peer's %016llx", t2, (unsigned long long)S.mine[k].second, d2); }
            S.mine.erase(S.mine.begin() + static_cast<long>(k));
            break;
          }
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
  }
  Log("battle over: result %u, turn digests equal %u, different %u, choices sent %u, applied %u%s", result, S.digestsOk, S.digestsBad,
      S.sent, S.applied, S.dead ? " (connection ended early / out of step)" : "");
  xd::replay::OnlineEnd(result, S.dead || S.digestsBad > 0);
  HubEnd(result);
  sEntrySeed.clear();
  S.cnt[0] = S.cnt[1] = 0;
  S.swSeq = 0;
  S.mine.clear();
  if (S.spectator) {
    SpectatorCompare();
    S.feed->ended = true;
    S.feed->endResult = result;
    S.battleOver = true;
    if (S.feed->onEnd) S.feed->onEnd(*S.feed);
  } else if (S.lobby) {
    S.battleOver = true;
    S.lastResult = result;
    S.lastDesync = S.digestsBad > 0;
    xd::online::LobbyBattleOver();
  }
}

}

extern "C" {
uint32_t PortXD_RawRand(void);
float PortXD_RawRandf(void);
int32_t PortXD_RawRandi(int32_t max);
uint32_t HSD_Rand(void);
}
namespace {
uint32_t sCosSeed = 0x2545F491u;
inline void CosNext() { sCosSeed = sCosSeed * 214013u + 2531011u; }
}
extern "C" {
int32_t PortXD_VsRand(void) {
  if (!xd::online::Active() || sVsNonce == 0) return static_cast<int32_t>(HSD_Rand());
  uint32_t h = sVsNonce ^ (0x9E3779B9u * (++sVsCalls));
  h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
  return static_cast<int32_t>(h & 0x7FFF);
}
uint32_t PortXD_CosRand(void) {
  if (!xd::online::Active() && !xd::replay::SplitOn()) {
    xd::replay::CosmeticDrawn();
    return PortXD_RawRand();
  }
  CosNext();
  return sCosSeed >> 16;
}
float PortXD_CosRandf(void) {
  if (!xd::online::Active() && !xd::replay::SplitOn()) {
    xd::replay::CosmeticDrawn();
    return PortXD_RawRandf();
  }
  CosNext();
  return static_cast<float>(sCosSeed >> 16) / 65536.0f;
}
int32_t PortXD_CosRandi(int32_t max) {
  if (!xd::online::Active() && !xd::replay::SplitOn()) {
    xd::replay::CosmeticDrawn();
    return PortXD_RawRandi(max);
  }
  CosNext();
  return max * static_cast<int32_t>(sCosSeed >> 16) / 65536;
}
}

extern "C" {
uint8_t fightIsFight(void);
void menuFightCloseAll(void);
void GScameraSetPosition(void* cam, const float* pos);
void GScameraLookAt(void* cam, const float* up, const float* target);
void GScameraSetRotation(void* cam, const float* rot);
}
namespace {

void GiveInTestTick() {
  {
    static const bool on = Env("XD_ONLINE_TEST_STATELOG") != nullptr;
    static int lc = -2, lid = -2, lst = -2, lstart = -1, lseq = -9;
    if (on && S.active) {
      int wid = -1;
      const int ws = PortXD_ActiveWindowState(&wid);
      const int sb = (PortInput_DeliveredButtons(0) & 0x1000) ? 1 : 0;
      const int c = S.choosing ? 1 : 0;
      if (c != lc || wid != lid || ws != lst || sb != lstart || sCurChoiceSeq != lseq) {
        lc = c; lid = wid; lst = ws; lstart = sb; lseq = sCurChoiceSeq;
        Log("state: script clock %u: choosing %d, window %d state %d, START down %d, choice seq %d, fight %d, startRequested %d", PortInput_ScriptNow(), c, wid, ws, sb, sCurChoiceSeq, static_cast<int>(fightIsFight()), S.startRequested ? 1 : 0);
      }
    }
  }
  static const char* gi = Env("XD_ONLINE_TEST_GIVEIN");
  if (gi == nullptr || !S.active) return;
  static int phase = 0, wait = 0;
  int id = -1;
  const int st = PortXD_ActiveWindowState(&id);
  const bool up = st >= 0 && (st & 255) == 2;
  auto press = [&](const char* name, const char* why) {
    char s[64];
    std::snprintf(s, sizeof s, "%u:%s:6", PortInput_ScriptNow() + 2, name);
    PortInput_LoadScript(s);
    Log("test give-in: %s (%s; window %d state %d)", name, why, id, st);
  };
  static int dbg = 0;
  if (sCurChoiceSeq == std::atoi(gi) && (++dbg % 90) == 1 && dbg < 900) Log("test give-in: waiting (phase %d, window %d state %d, choosing %d)", phase, id, st, S.choosing ? 1 : 0);
  if (phase == 0) {
    if (sCurChoiceSeq == std::atoi(gi) && up && id == 65) {
      ++wait;
      if (wait == 40) press("right", "the command menu (window 65) is open: GIVE IN");
      if (wait == 70) { press("a", "GIVE IN chosen: the confirmation window follows"); phase = 1; wait = 0; }
    } else if (wait < 40) {
      wait = 0;
    }
  } else if (phase == 1) {
    if (up && id != 65) {
      ++wait;
      if (wait == 30) press("left", "the confirmation window is open: cursor on No, move to Yes");
      if (wait == 60) { press("a", "Yes chosen"); phase = 2; }
    }
  }
}
void TimerTick() {
  HubTick();
  if (S.timing && fightIsFight() != 0) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(S.deadline - std::chrono::steady_clock::now()).count();
    const int sec = left <= 0 ? 0 : static_cast<int>((left + 999) / 1000);
    if (sec != S.lastCount && sec > 0 && (sec <= 10 || sec % 10 == 0)) {
      S.lastCount = sec;
      char t[96];
      if (S.bankOn) {
        const int64_t bankLeft = std::max<int64_t>(0, S.bankMs - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - S.choiceStart).count());
        const int bs = static_cast<int>((bankLeft + 999) / 1000);
        std::snprintf(t, sizeof t, Tr("Time left: %d s - battle clock %d:%02d"), sec, bs / 60, bs % 60);
      } else {
        std::snprintf(t, sizeof t, Tr("Time left: %d s"), sec);
      }
      PortVideo_Notice(t, 1.2);
    }
  }
  {
    static bool prev = false;
    const bool down = (PortInput_DeliveredButtons(0) & 0x1000) != 0;
    if (down && !prev && Env("XD_ONLINE_TEST_STATELOG")) Log("state: START pressed (choosing %d, startRequested %d, fight %d)", S.choosing ? 1 : 0, S.startRequested ? 1 : 0, static_cast<int>(fightIsFight()));
    if (down && !prev && S.choosing && !S.startRequested && fightIsFight() != 0) {
      S.startRequested = true;
      Log("START pressed during the choice: the quit question follows");
      menuFightCloseAll();
    }
    prev = down;
  }
  GiveInTestTick();
  if (S.timing && !S.timeUpDone && fightIsFight() != 0 && std::chrono::steady_clock::now() >= S.deadline) {
    S.timeUpDone = true;
    Log("time is up for this choice: the open menus are closed, the game's own time-out choice follows");
    menuFightCloseAll();
  }
}
}
extern "C" {
int PortXD_MirrorNow(void) {
  if (!xd::online::Active() || S.host) return 0;
  static int enabled = -1;
  if (enabled < 0) {
    const char* v = std::getenv("XD_ONLINE_MIRROR");
    enabled = (v && v[0] == '0') ? 0 : 1;
  }
  return enabled == 1 && fightIsFight() != 0;
}
void OnlineMirror_Frame(void);
void PortXD_CamSetPosition(void* cam, const float* pos) { TimerTick(); OnlineMirror_Frame(); GScameraSetPosition(cam, pos); }
void PortXD_CamLookAt(void* cam, const float* up, const float* target) { GScameraLookAt(cam, up, target); }
void PortXD_CamSetRotation(void* cam, const float* rot) { GScameraSetRotation(cam, rot); }
}

namespace {
bool StoryMenuOn() {
  if (!xd::online::Active() || fightIsFight() == 0) return false;
  static int enabled = -1;
  if (enabled < 0) {
    const char* v = std::getenv("XD_ONLINE_STORY_MENU");
    enabled = (v && v[0] == '0') ? 0 : 1;
  }
  return enabled == 1;
}
bool TimeIsUp() { return (S.timing && std::chrono::steady_clock::now() >= S.deadline) || S.startRequested; }
}
extern "C" {
uint8_t PortXD_MenuKind(uint8_t kind) {
  if (StoryMenuOn() && (kind == 0xA || kind == 0x11 || kind == 0xB)) return 0;
  return fightKind_CheckFightKind(kind);
}
uint8_t PortXD_MenuTuusin(void* floor) {
  if (StoryMenuOn()) return TimeIsUp() ? 1 : 0;
  return fightFloorIsNowEncountTuusinTaisen(floor);
}
uint8_t PortXD_MenuTimerUse(void* floor) {
  if (StoryMenuOn() && (S.timing || S.startRequested)) return 1;
  return fightFloorIsUseFightTimerCommand(floor);
}
uint8_t PortXD_MenuTimerOver(void) {
  if (StoryMenuOn() && (S.timing || S.startRequested)) return TimeIsUp() ? 1 : 0;
  return fightTimerCommandIsOver();
}
void PortXD_MenuSetAction(void* floor, void* fop, void* work) {
  fightFloorSetMenuFightAction(floor, fop, work);
  if (StoryMenuOn()) static_cast<uint8_t*>(work)[0x16] = S.rules.items == "none" ? 0 : 1;
}
}

extern "C" {
int PortXD_HyperModeAppliesTo(void* fop) {
  if (!xd::online::Active() || fightIsFight() == 0 || !S.connected) return -1;
  static int logged = 0;
  const int answer = S.rules.shadowObey == "purified" ? 0 : 1;
  if (logged < 8) {
    logged++;
    void* tr = fop ? fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fop) : nullptr;
    Log("Hyper Mode asked for a Pokemon of side %d: %s (room rule: %s)", tr && fightTargetIsHostSide(tr, fightFloor_GetFightType(nullptr)) ? 0 : 1, answer ? "yes" : "no",
        S.rules.shadowObey.c_str());
  }
  return answer;
}

int PortXD_TargetCursorOtherSide(void* user) {
  if (PortXD_MirrorNow()) return 1;
  if (!xd::online::Active() || fightIsFight() == 0 || !S.connected || !user) return 0;
  void* tr = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, user);
  const int other = tr && !fightTargetIsHostSide(tr, fightFloor_GetFightType(nullptr)) ? 1 : 0;
  static int logged = 0;
  if (logged < 400) { logged++; Log("target menu: chooser on side %d, cursor starts on the %s", other ? 1 : 0, other ? "other side's first Pokemon" : "game default"); }
  return other;
}

uint8_t PortXD_HyperFlag(void* floor) {
  if (!xd::online::Active() || fightIsFight() == 0 || !S.connected) return fightFloor_GetFightKindDarkpokemonHypermodeFlag(floor);
  return S.rules.shadowAllowed && S.rules.shadowObey != "purified" ? 1 : 0;
}

uint8_t fightFloor_GetFightKindRegulationFlag(void* floor);
uint8_t menuCBRule_EnableSbToSleep(void);
uint8_t menuCBRule_EnableSbToIceed(void);
uint8_t menuCBRule_EnableSkillSwap(void);
uint8_t menuCBRule_EnableSkill_Michidure(void);
uint8_t menuCBRule_EnableSkill_SonicBoom(void);
}
namespace {
bool SleepClauseOn() { return S.active && S.connected && S.rules.sleepClause && fightIsFight() != 0; }
void SleepClauseNote(const char* what) {
  static int logged = 0;
  if (logged < 6) { logged++; Log("Sleep clause: the game's regulation question \"%s\" is answered by the room's rule", what); }
}
}
extern "C" {
uint8_t PortXD_SleepClauseFlag(void* floor) { if (SleepClauseOn()) { SleepClauseNote("regulation on"); return 1; } return fightFloor_GetFightKindRegulationFlag(floor); }
uint8_t PortXD_SleepClauseSleep(void) { return SleepClauseOn() ? 0 : menuCBRule_EnableSbToSleep(); }
uint8_t PortXD_SleepClauseIce(void) { return SleepClauseOn() ? 1 : menuCBRule_EnableSbToIceed(); }
uint8_t PortXD_SleepClauseSwap(void) { return SleepClauseOn() ? 1 : menuCBRule_EnableSkillSwap(); }
uint8_t PortXD_SleepClauseMichidure(void) { return SleepClauseOn() ? 1 : menuCBRule_EnableSkill_Michidure(); }
uint8_t PortXD_SleepClauseSonic(void) { return SleepClauseOn() ? 1 : menuCBRule_EnableSkill_SonicBoom(); }

uint8_t pokemonGetDarkPokemonHyperJoutaiKakuritu(void* pokemon);
uint8_t PortXD_HyperChance(void* pokemon) {
  const uint8_t real = pokemonGetDarkPokemonHyperJoutaiKakuritu(pokemon);
  static int logged = 0;
  if (logged < 12) { logged++; Log("Hyper Mode chance asked: the game's table says %u%s", static_cast<unsigned>(real), Env("XD_ONLINE_TEST_HYPER") ? " (test: 100)" : ""); }
  if (xd::online::Active() && fightIsFight() != 0 && S.connected && Env("XD_ONLINE_TEST_HYPER")) return 100;
  return real;
}

int32_t PortXD_MenuOpenCustom(int32_t id, int32_t a, void* cursor, int32_t b, int32_t c, int32_t d, void* work) {
  if (id == 0x8f && StoryMenuOn()) {
    Log("GIVE IN: the quit confirmation is open");
    PortVideo_Notice(Tr("Are you sure you want to quit this online battle?"), 12.0);
  }
  if ((id == 0x39 || id == 0x90 || id == 0x9e || id == 0xa1) && work && StoryMenuOn()) {
    struct EntryPC { uint16_t* name; uint16_t* zokuseiName; uint16_t zokusei; uint8_t maxPp, pp; };
    struct WazaStatusPC { uint16_t* nickname; EntryPC waza[4]; uint8_t pad[0xC]; void* fightOut; uint8_t tuusin; };
    const WazaStatusPC* ws = static_cast<const WazaStatusPC*>(work);
    void* pk = ws->fightOut ? fightOutPokemonGetPokemonPtr(ws->fightOut) : nullptr;
    Log("move menu %d: names %d%d%d%d, pp %u/%u %u/%u; the Pokemon's moves %u %u %u %u, Shadow %d (list id %u)", id, ws->waza[0].name != nullptr, ws->waza[1].name != nullptr,
        ws->waza[2].name != nullptr, ws->waza[3].name != nullptr, ws->waza[0].pp, ws->waza[0].maxPp, ws->waza[1].pp, ws->waza[1].maxPp,
        pk ? pokemon_GetPokemonWazaDataId(pk, 0) : 0u, pk ? pokemon_GetPokemonWazaDataId(pk, 1) : 0u, pk ? pokemon_GetPokemonWazaDataId(pk, 2) : 0u,
        pk ? pokemon_GetPokemonWazaDataId(pk, 3) : 0u, pk ? static_cast<int>(pokemonIsDarkPokemon(pk)) : -1, pk ? static_cast<unsigned>(pokemon_GetDarkpokemonDataId(pk)) : 0u);
  }
  return menuOpenCustom(id, a, cursor, b, c, d, work);
}

static void FillHeroWithTeam(void* trainer, const std::vector<xd::save::Pokemon>& team, int sideNo) {
  if (team.empty() || Env("XD_ONLINE_TEST_NOAPPLY")) return;
  void* hero = fightTrainer_GetHeroPtr(trainer);
  if (!hero) return;
  size_t n = team.size();
  if (S.spectator) {
    const int party = S.feed ? (sideNo == 0 ? S.feed->partyHost : S.feed->partyGuest) : 0;
    if (party <= 0) return;
    n = static_cast<size_t>(party);
  }
  if (n > 6) n = 6;
  int filled = 0, cleared = 0;
  for (size_t i = 1; i < 6; i++) {
    void* slot = heroBiosGetPokemonPtr(hero, static_cast<uint32_t>(i));
    if (!slot) continue;
    if (i < n && i < team.size()) {
      std::memcpy(slot, team[i].raw.data(), xd::save::kPokemonSize);
      filled++;
    } else if (i >= n && pokemonCheckValid(slot)) {
      pokemonInit(slot);
      cleared++;
    }
  }
  Log("side %d: the trainer's party is the whole team now: %zu (%d put in, %d taken out)", sideNo, n, filled + 1, cleared);
}

void PortXD_NetPokemonCreate(void* trainer, void* fightPokemon, void* pokemon, int16_t entryId) {
  if (xd::online::Active() && pokemon != nullptr) {
    if (!Begin()) { EndWithError(); }
    else {
      const bool hostSide = fightTargetIsHostSide(trainer, fightFloor_GetFightType(nullptr)) != 0;
      const int sideNo = hostSide ? 0 : 1;
      const std::vector<xd::save::Pokemon>& team = (hostSide == S.host) ? S.teamMine : S.teamFriend;
      const size_t k = static_cast<size_t>(S.created[sideNo]++);
      if (k == 0) FillHeroWithTeam(trainer, team, sideNo);
      if (!sBagDone[sideNo]) { sBagDone[sideNo] = true; SetupBag(trainer, hostSide == S.host); }
      if (k < team.size() && !Env("XD_ONLINE_TEST_NOAPPLY")) {
        {
          const unsigned char* o = static_cast<const unsigned char*>(pokemon);
          char hexs[128];
          std::snprintf(hexs, sizeof hexs, "%02x%02x %02x%02x %02x%02x %02x%02x ... lvl@11=%u", o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[0x11]);
          Log("side %d member %zu: the game's record starts %s, level by the game %u", sideNo, k, hexs, static_cast<unsigned>(pokemonBiosGetLevel(pokemon)));
        }
        std::array<uint8_t, xd::save::kPokemonSize> rec = team[k].raw;
        Log("side %d member %zu: record HP %u of %u, status %u -> full HP", sideNo, k, static_cast<unsigned>(team[k].hp), static_cast<unsigned>(team[k].stats[0]),
            static_cast<unsigned>(team[k].status));
        rec[0x04] = static_cast<uint8_t>(team[k].stats[0] & 0xFF);
        rec[0x05] = static_cast<uint8_t>(team[k].stats[0] >> 8);
        rec[0x16] = 0;
        std::memcpy(pokemon, rec.data(), xd::save::kPokemonSize);
        Log("side %d member %zu: pokemonCheckValid %u, pokemonCheckFightOut %u, tool-battle flag %u, attest bytes %02x%02x%02x", sideNo, k, static_cast<unsigned>(pokemonCheckValid(pokemon)),
            static_cast<unsigned>(pokemonCheckFightOut(pokemon)), static_cast<unsigned>(menuCBBios_CurrentlyWorkingToolBattle()), 0u, 0u, 0u);
        Log("side %d member %zu: moves in the battle's record %u %u %u %u (Shadow %d, id %u); the save's %u %u %u %u", sideNo, k, static_cast<unsigned>(pokemon_GetPokemonWazaDataId(pokemon, 0)),
            static_cast<unsigned>(pokemon_GetPokemonWazaDataId(pokemon, 1)), static_cast<unsigned>(pokemon_GetPokemonWazaDataId(pokemon, 2)), static_cast<unsigned>(pokemon_GetPokemonWazaDataId(pokemon, 3)),
            static_cast<int>(pokemonIsDarkPokemon(pokemon)), static_cast<unsigned>(pokemon_GetDarkpokemonDataId(pokemon)), static_cast<unsigned>(team[k].moves[0].id),
            static_cast<unsigned>(team[k].moves[1].id), static_cast<unsigned>(team[k].moves[2].id), static_cast<unsigned>(team[k].moves[3].id));
        Log("side %d member %zu: taken from the team (species %u, level %u%s)", sideNo, k, static_cast<unsigned>(team[k].species),
            static_cast<unsigned>(team[k].level), team[k].shadowId ? ", SHADOW" : "");
      }
    }
    if (S.connected && S.rules.level != "asis") {
      const int n = std::atoi(S.rules.level.c_str() + 4);
      const int now = pokemonBiosGetLevel(pokemon);
      const bool all = S.rules.level.compare(0, 4, "all:") == 0;
      const int target = all ? n : (now > n ? n : now);
      if (target >= 1 && target <= 100 && target != now) {
        PortXD_PokemonSetLevelStats(pokemon, static_cast<uint8_t>(target));
        Log("level %d -> %d (room: %s)", now, target, S.rules.level.c_str());
      }
    }
  }
  fightPokemonCreate(trainer, fightPokemon, pokemon, entryId);
  if (fightPokemon != nullptr) xd::modbattle::ForgetCopy(fightPokemon_GetPokemonBuffPtr(fightPokemon));
  if (xd::modbattle::Rules() && fightPokemon != nullptr) xd::modbattle::RuleLevel(fightPokemon_GetPokemonBuffPtr(fightPokemon));
}

uint32_t PortXD_MenuStatus(void* floor, uint16_t dataId, uint16_t status, uint16_t arg) {
  if (StoryMenuOn()) {
    if (status == 0x22) return 1;
    if (status == 0x20) {
      const uint32_t answer = S.rules.items == "none" ? 0 : 1;
      static int sAsked = 0;
      if (sAsked++ < 6) Log("items allowed asked by the battle menu: answer %u (room rule: %s)", answer, S.rules.items.c_str());
      return answer;
    }
  }
  return fightFloorGetStatus(floor, dataId, status, arg);
}

int32_t GSfloorGetCurrent(void);
uint8_t GSfloorPush(uint32_t id);
uint8_t GSfloorPop(void);
void floorWaitForChange(void);
void menuPocketMain(void);
void PortXD_OnlineTurnActions(void) {
  if (!xd::online::Active()) return;
  for (uint16_t b = 0; b < 4; b++) {
    void* fop = fightFloorGetFightOutBanmeToValidFightOutPokemon(nullptr, b);
    void* act = fop ? fightOutPokemon_GetFightActionBuffPtr(fop) : nullptr;
    Log("turn actions: slot %u species %u out %d action valid %d kind %u flag %d", b, fop ? static_cast<unsigned>(figthOutPokemonGetPokemonDataId(fop)) : 0u,
        fop ? static_cast<int>(fightOutPokemonCheckFightOut(fop)) : -1, act ? static_cast<int>(fightActionCheckValid(act)) : -1, act ? static_cast<unsigned>(fightActionGetKindDataId(act)) : 0u,
        fop ? static_cast<int>(fightOutPokemon_GetFightActionFlag(fop)) : -1);
  }
}

static bool sPocketInline = false, sPocketRunning = false;
uint8_t PortXD_FloorPushRoom(uint32_t id);
int32_t PortXD_FloorDepth(void);
static uint8_t RoomPush(uint32_t id, const char* what) {
  if (!xd::online::Active() || fightIsFight() == 0) return GSfloorPush(id);
  const int32_t depth = PortXD_FloorDepth();
  const uint8_t ok = PortXD_FloorPushRoom(id);
  Log("%s: floor 0x%x pushed at depth %d: %s", what, static_cast<unsigned>(id), static_cast<int>(depth), ok ? "ok" : "REFUSED");
  return ok;
}
uint8_t PortXD_PocketPush(uint32_t id) {
  const uint8_t ok = RoomPush(id, "bag window");
  if (!ok && xd::online::Active()) { sPocketInline = true; Log("bag window: the floor stack is full, the bag runs inside the battle's task"); return 1; }
  return ok;
}
void PortXD_PocketWait(void) {
  if (sPocketInline) { sPocketInline = false; sPocketRunning = true; menuPocketMain(); sPocketRunning = false; return; }
  floorWaitForChange();
}
uint8_t PortXD_PocketPop(void) { return sPocketRunning ? 1 : GSfloorPop(); }

int32_t menuPokemonMain(void);
static bool sPartyInline = false, sPartyRunning = false;
uint8_t PortXD_PartyPush(uint32_t id) {
  const uint8_t ok = RoomPush(id, "party menu");
  if (!ok && xd::online::Active() && fightIsFight() != 0) { sPartyInline = true; Log("party menu: the floor stack is full, the menu runs inside the battle's task"); return 1; }
  return ok;
}
void PortXD_PartyWait(void) {
  if (sPartyInline) { sPartyInline = false; sPartyRunning = true; menuPokemonMain(); sPartyRunning = false; return; }
  if (sPartyRunning) return;
  floorWaitForChange();
}
uint8_t PortXD_PartyPop(void) { return sPartyRunning ? 1 : GSfloorPop(); }

bool GSfloorIsFloorPushed(void);
uint8_t GSfloorIsPush(void);
void PortXD_OnlineBagNote(int phase, int mode, int slot, unsigned item) {
  if (!xd::online::Active()) return;
  if (phase == 0) Log("bag window: opening (mode %d, party slot %d; floor 0x%x, floor pushed %d, push busy %d)", mode, slot, static_cast<unsigned>(GSfloorGetCurrent()),
                      static_cast<int>(GSfloorIsFloorPushed()), static_cast<int>(GSfloorIsPush()));
  else Log("bag window: closed, item %u chosen", item);
}

uint16_t* PortXD_ResultMsg(uint32_t id) {
  static uint16_t text[200];
  if ((id == 0x442B || id == 0x442D) && xd::online::Active()) {
    const std::string why = S.endNotice.empty() ? std::string(Tr("The battle ended without a winner.")) : S.endNotice;
    size_t k = 0;
    for (const unsigned char* q = reinterpret_cast<const unsigned char*>(why.c_str()); *q != 0 && k < 198;) {
      const unsigned c = *q++;
      const int extra = c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
      unsigned cp = extra == 0 ? c : extra == 1 ? (c & 0x1Fu) : (c & 0x0Fu);
      for (int i = 0; i < extra && (*q & 0xC0) == 0x80; i++, q++) cp = cp << 6 | (*q & 0x3Fu);
      text[k++] = static_cast<uint16_t>(cp);
    }
    text[k] = 0;
    return text;
  }
  return GSmsgGetGSchar(id);
}
}

extern "C" {
uint16_t fightTrainer_GetFightTrainerDataId(void* trainer);
uint16_t fightTrainerDB_GetAiDataId(uint16_t id);
int32_t fightTrainer_GetControllerId(void* trainer);
}
namespace xd::online {

uint64_t BattleDigest(uint32_t turn) { return Digest(turn); }

bool TrainerIsAi(void* trainer) {
  return fightTrainerDB_GetAiDataId(fightTrainer_GetFightTrainerDataId(trainer)) != 0 && fightTrainer_GetControllerId(trainer) == 0;
}

int TrainerSide(void* trainer) { return fightTargetIsHostSide(trainer, fightFloor_GetFightType(nullptr)) != 0 ? 0 : 1; }

std::string DescribeActions(void* trainer) {
  std::vector<std::string> recList;
  std::vector<int> megaList;
  bool anyMega = false;
  int count = 0;
  for (uint16_t j = 0; j < 2; j++) {
    void* fop = fightTrainerGetValidFightOutPokemonPtr(trainer, j);
    if (!fop) continue;
    void* act = fightOutPokemon_GetFightActionBuffPtr(fop);
    if (!act || !fightActionCheckValid(act)) continue;
    const unsigned kind = fightActionGetKindDataId(act);
    const int buff = fightActionBiosGetBuffDataId(act);
    unsigned target = 0;
    int banme = 0, autoMake = 0;
    unsigned item = 0, ibuff = 0;
    if (kind == 0x13) {
      void* w = fightOutPokemon_GetFightWazaPtr(fop);
      target = fightWaza_GetTargetDataId(w);
      banme = fightWaza_GetWazaBanme(w);
      autoMake = fightWaza_GetAutoMakeFlag(w);
    } else if (kind == 0x12) {
      void* fi = fightOutPokemon_GetFightItemPtr(fop);
      item = fightItem_GetItemDataId(fi);
      target = fightItem_GetTargetDataId(fi);
      banme = fightItem_GetCount(fi);
      ibuff = fightItem_GetBuff(fi);
    }
    std::ostringstream one;
    one << ' ' << j << ' ' << kind << ' ' << buff << ' ' << target << ' ' << banme << ' ' << autoMake << ' ' << item << ' ' << ibuff;
    const int mega = xd::forms::TakeChoiceMega(fop) ? 1 : 0;
    anyMega = anyMega || mega != 0;
    recList.push_back(one.str());
    megaList.push_back(mega);
    count++;
  }
  const uint16_t res = fightGetFightResultId();
  if ((res == 4 || res == 5) && count == 0) {
    const unsigned jq = fightTrainerGetValidFightOutPokemonPtr(trainer, 0) ? 0u : 1u;
    recList.push_back(" " + std::to_string(jq) + " 8 0 0 0 0 0 0");
    megaList.push_back(0);
    count = 1;
  }
  std::string recs;
  for (size_t i = 0; i < recList.size(); i++) recs += recList[i] + (anyMega ? " " + std::to_string(megaList[i]) : std::string());
  return std::to_string(count) + recs;
}

bool ApplyActions(void* trainer, const std::string& text) {
  std::istringstream in(text);
  int count = 0;
  in >> count;
  const int perAction = ActionNumbers(text);
  if (perAction == 0 || count < 0) return false;
  const bool withMega = perAction == 9;
  for (int i = 0; i < count; i++) {
    unsigned j, kind, target, item, ibuff, mega = 0;
    int buff, banme, autoMake;
    in >> j >> kind >> buff >> target >> banme >> autoMake >> item >> ibuff;
    if (withMega) in >> mega;
    if (!in) return false;
    void* fop = fightTrainerGetValidFightOutPokemonPtr(trainer, static_cast<uint16_t>(j));
    if (!fop) return false;
    fightOutPokemonInitFightActionBuff(fop);
    if (kind == 0x13) {
      xd::forms::SetFriendApplying(true);
      fightOutPokemonCreateFightActionAttackWaza(fop, 0, 0x13, 0, fight_action_data_waza, buff, static_cast<uint16_t>(target), static_cast<int8_t>(banme), static_cast<uint8_t>(autoMake));
      xd::forms::SetFriendApplying(false);
      if (mega) xd::forms::FriendMega(fop);
    } else if (kind == 0x12) {
      fightOutPokemonCreateFightActionUseItem(fop, 0, 0x12, 0, fight_action_data_item, item, static_cast<uint16_t>(target), banme, static_cast<int32_t>(ibuff));
      if (void* hero = fightTrainerBiosGetHeroPtr(trainer)) heroItemDecItemDataId(hero, item, 1, banme);
      ApplyFriendItemEffect(trainer, item, target);
    } else if (kind == 8 || kind == 9 || kind == 0xA) {
      fightOutPokemonCreateFightAction(fop, 0, static_cast<int32_t>(kind), 0, fight_action_data_turn, buff);
    } else {
      return false;
    }
  }
  return true;
}

}

namespace xd::online {
void CosmeticSeedReset() { sCosSeed = 0x2545F491u; }
}
