#ifndef XD_ONLINE_BATTLE_H
#define XD_ONLINE_BATTLE_H

#include "online_lobby.h"
#include "online_room.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace xdnet { class Connection; }

namespace xd::online {

struct BattleTransport {
  virtual ~BattleTransport() {}
  virtual bool Send(const uint8_t* data, size_t size) = 0;
  virtual int Receive(uint8_t* buffer, size_t capacity) = 0;
  virtual bool Connected() const = 0;
};

bool Active();

struct Pick { int box = -1; int slot = 0; };
struct LobbyHandOver {
  bool host = false;
  xdnet::Connection* conn = nullptr;
  RoomRules rules;
  std::string code;
  std::string saveFile;
  std::vector<Pick> picks;
  std::vector<uint16_t> freeSpecies;
  std::vector<uint16_t> friendPreview;
  int mineCount = 3, friendCount = 3;
};
void SetLobbyRoom(LobbyHandOver& h);
uint32_t NewNonce();
void SetVsNonce(uint32_t nonce);
void LobbyRestoreVsStyle();
void LobbyApplyVsStyle();
int LobbyStartBattle();
bool LobbyStartBattleThread();

struct BattleSummary {
  bool inRoom = false;
  bool host = false;
  bool over = false;
  unsigned result = 0;
  bool desync = false, dead = false;
  std::string notice;
  std::string rulesText;
};
BattleSummary LobbySummary();
Capabilities LobbyCapabilities();
PcInfo LobbyPcInfo();
bool InFight();
BattleTransport* LobbyLink();
const RoomRules& LobbyRules();
void LobbyRematchReset();
void LobbyLeave();
void LobbyBattleOver();

void CosmeticSeedReset();
uint64_t BattleDigest(uint32_t turn);
bool TrainerIsAi(void* trainer);
int TrainerSide(void* trainer);
std::string DescribeActions(void* trainer);
bool ApplyActions(void* trainer, const std::string& text);

struct SpectatorFeed {
  std::string rulesKey;
  std::vector<std::string> hostTeam, guestTeam;
  uint32_t seed = 0;
  int partyHost = 0, partyGuest = 0;
  bool haveStart = false;
  bool needParty = false, haveParty = false;
  std::map<uint32_t, std::string> choices;
  std::map<uint32_t, int> switches;
  std::map<uint32_t, uint32_t> choiceSeeds, switchSeeds;
  std::map<uint32_t, uint64_t> digests;
  bool complete = false;
  unsigned result = 0;
  bool lost = false;
  std::function<void(SpectatorFeed&)> poll;
  unsigned turnsMatched = 0, turnsDifferent = 0, turnsSeen = 0;
  bool ended = false;
  unsigned endResult = 0;
  std::function<void(SpectatorFeed&)> onEnd;
};
bool SpectatorStart(SpectatorFeed* feed, std::string* why);
bool Spectating();
void SpectatorDriveVsMenus();
bool SpectatorJoin(SpectatorFeed* feed, const std::string& roomCode, std::string* why);
}

#endif
