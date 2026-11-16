#ifndef XD_ONLINE_BATTLE_H
#define XD_ONLINE_BATTLE_H

#include "online_lobby.h"
#include "online_room.h"

#include <cstddef>
#include <cstdint>
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

}

#endif
