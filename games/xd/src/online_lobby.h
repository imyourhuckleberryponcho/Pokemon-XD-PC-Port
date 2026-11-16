#ifndef XD_ONLINE_LOBBY_H
#define XD_ONLINE_LOBBY_H

#include "online_room.h"

#include <string>
#include <vector>

namespace xd::online {

struct PcInfo {
  bool gen4PackPresent = false;
  bool gen4Loaded = false;
};

struct Option {
  std::string value;
  std::string label;
  bool enabled = true;
  std::string why;
};

struct Row {
  std::string id;
  std::string title;
  std::vector<Option> options;
  int current = 0;
  bool number = false;
  const Option& Current() const { return options[static_cast<size_t>(current)]; }
};

std::vector<Row> SettingRows(const RoomRules& r, const PcInfo& pc);
bool Cycle(RoomRules& r, const std::string& id, int dir, const PcInfo& pc);
RoomRules Sanitized(RoomRules r, const PcInfo& pc);
RoomRules DefaultRoom();

std::vector<std::string> RoomLines(const RoomRules& r);

std::string RulesToText(const RoomRules& r);

std::string Msg(const std::string& verb, const std::string& arg = "");
struct Peer {
  bool accepted = false, declined = false, ready = false, rematch = false, left = false;
  int teamCount = -1;
  uint32_t nonce = 0;
};
bool Apply(Peer& peer, const std::string& line);

enum class Outcome { Won, Lost, YouQuit, FriendQuit, ConnectionLost, Desync };
Outcome OutcomeOf(unsigned result, bool host, bool desync);
std::string OutcomeText(Outcome o);

}

#endif
