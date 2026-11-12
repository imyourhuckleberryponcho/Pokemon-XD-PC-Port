#include "xd_battles_won.h"

#include <algorithm>
#include <cstdio>
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
  if (added) sChanges++;
  if (Log()) {
    std::fprintf(stderr, "[battles won] battle %u over: %s (result %u%s); %zu won in this playthrough\n", sBattle,
                 won ? (added ? "WON, noted" : "WON, was in the list") : "not won", sResult,
                 sDecided ? "" : ", none seen", sWon.size());
  }
}

bool Won(unsigned battle) { return sWon.count(battle) != 0; }
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
