#include "xd_battles_won.h"
#include "xd_speedrun.h"

#include <algorithm>
#include <any>
#include <cstdio>
#include "savestate_features.h"
#include <cstdlib>
#include <set>
#include <string>

extern "C" {
uint8_t fightIsFight(void);
uint32_t fightGetFightResultId(void);
uint16_t fightGetFightEncountDataId(void);
uint8_t fightFloorIsGcHeroWin(void* floor, uint16_t result);
}

namespace xd::battles {

namespace {
std::set<uint32_t> sWon;
std::set<uint32_t> sSinceStart;
bool sComplete = false;
unsigned sChanges = 0;

bool sInFight = false;
unsigned sBattle = 0;
bool sSawNoResult = false;
bool sDecided = false;
unsigned sResult = 0;
bool sHeroWon = false;

bool Log() {
  static const bool on = std::getenv("XD_CHALLENGE_LOG") != nullptr;
  return on;
}

struct Saved {
  std::set<uint32_t> won, sinceStart;
  bool complete, inFight, sawNoResult, decided, heroWon;
  unsigned changes, battle, result;
};
const xd::savestate::Feature sStateFeature(
    "battles_won",
    [] {
      return std::any(Saved{sWon, sSinceStart, sComplete, sInFight, sSawNoResult, sDecided, sHeroWon, sChanges, sBattle, sResult});
    },
    [](const std::any& a) {
      const Saved& s = std::any_cast<const Saved&>(a);
      sWon = s.won;
      sSinceStart = s.sinceStart;
      sComplete = s.complete;
      sInFight = s.inFight;
      sSawNoResult = s.sawNoResult;
      sDecided = s.decided;
      sHeroWon = s.heroWon;
      sChanges = s.changes + 1;
      sBattle = s.battle;
      sResult = s.result;
      if (Log()) std::fprintf(stderr, "[battles won] state put back by a save state: %zu won, battle %u, in fight %d\n", sWon.size(), sBattle, sInFight ? 1 : 0);
    },
    [](const std::any& a, portstate::Out& out) {
      const Saved& s = std::any_cast<const Saved&>(a);
      out.u64(s.won.size());
      for (uint32_t v : s.won) out.pod(v);
      out.u64(s.sinceStart.size());
      for (uint32_t v : s.sinceStart) out.pod(v);
      out.pod(s.complete); out.pod(s.inFight); out.pod(s.sawNoResult); out.pod(s.decided); out.pod(s.heroWon);
      out.pod(s.changes); out.pod(s.battle); out.pod(s.result);
    },
    [](portstate::In& in, std::any& a) {
      Saved s{};
      const uint64_t nw = in.u64();
      if (nw > (1u << 20)) return false;
      for (uint64_t i = 0; i < nw && in.ok(); i++) s.won.insert(in.pod<uint32_t>());
      const uint64_t ns = in.u64();
      if (ns > (1u << 20)) return false;
      for (uint64_t i = 0; i < ns && in.ok(); i++) s.sinceStart.insert(in.pod<uint32_t>());
      s.complete = in.pod<bool>(); s.inFight = in.pod<bool>(); s.sawNoResult = in.pod<bool>(); s.decided = in.pod<bool>(); s.heroWon = in.pod<bool>();
      s.changes = in.pod<unsigned>(); s.battle = in.pod<unsigned>(); s.result = in.pod<unsigned>();
      if (!in.ok()) return false;
      a = std::move(s);
      return true;
    });
}

void Frame() {
  if (fightIsFight() != 0) {
    if (!sInFight) {
      sInFight = true;
      sBattle = 0;
      sSawNoResult = sDecided = sHeroWon = false;
      sResult = 0;
    }
    const unsigned battle = fightGetFightEncountDataId();
    if (battle != 0) sBattle = battle;
    const unsigned result = fightGetFightResultId() & 0xFFFFu;
    if (result == 0) {
      sSawNoResult = true;
    } else if (sSawNoResult && (!sDecided || result != sResult)) {
      sDecided = true;
      sResult = result;
      sHeroWon = fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) == 1;
    }
    return;
  }
  if (!sInFight) return;
  sInFight = false;
  const bool won = sDecided && sHeroWon && sBattle != 0;
  const bool added = won && sWon.insert(sBattle).second;
  if (won) sSinceStart.insert(sBattle);
  if (sBattle != 0) xd::speedrun::BattleEnded(won, sDecided ? sResult : 0, sBattle);
  if (added) sChanges++;
  if (Log()) {
    std::fprintf(stderr, "[battles won] battle %u over: %s (result %u%s); %zu won in this playthrough\n", sBattle,
                 won ? (added ? "WON, noted" : "WON, was in the list") : "not won", sResult,
                 sDecided ? "" : ", none seen", sWon.size());
  }
}

bool Won(unsigned battle) { return sWon.count(battle) != 0; }
bool InFight() { return sInFight; }
bool Complete() { return sComplete; }
std::vector<uint32_t> List() { return std::vector<uint32_t>(sWon.begin(), sWon.end()); }
std::vector<uint32_t> SinceStart() { return std::vector<uint32_t>(sSinceStart.begin(), sSinceStart.end()); }
unsigned Changes() { return sChanges; }

void Set(const std::vector<uint32_t>& list, bool complete, const char* why) {
  sWon = std::set<uint32_t>(list.begin(), list.end());
  sComplete = complete;
  sChanges++;
  if (Log()) {
    std::string text;
    for (uint32_t b : sWon) text += " " + std::to_string(b);
    std::fprintf(stderr, "[battles won] this playthrough (%s): %zu battle(s) won%s%s%s\n", why, sWon.size(), sWon.empty() ? "" : ":",
                 text.c_str(), complete ? "" : " (a save from before the list was kept: earlier wins are not known)");
  }
}

}
