#ifndef XD_ONLINE_LOBBY_H
#define XD_ONLINE_LOBBY_H

#include "online_room.h"

#include <string>
#include <vector>

namespace xd::online {

struct PcInfo {
  std::string edition = "xd";
  std::string gen4RulesOff;
  std::string gen4Off;
  bool gen4PackPresent = false;
  bool gen4Loaded = false;
  struct FormatChoice { std::string id, name; bool enabled = true; std::string why; };
  std::vector<FormatChoice> formats;
  bool (*applyFormat)(RoomRules& room) = nullptr;
  std::string (*formatLine)(const RoomRules& room, const char* what) = nullptr;
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
bool SelectFormat(RoomRules& r, const std::string& id, const PcInfo& pc);
RoomRules Sanitized(RoomRules r, const PcInfo& pc);
RoomRules DefaultRoom();

std::vector<std::string> RoomLines(const RoomRules& r, const std::string& edition = "xd");

std::string RulesToText(const RoomRules& r);

std::string Msg(const std::string& verb, const std::string& arg = "");
std::string JoinInts(const std::vector<int>& v);
std::vector<int> SplitInts(const std::string& text);
bool PickValid(const std::vector<int>& pick, int teamSize, int want);
std::vector<int> TimeoutPick(const std::vector<int>& chosen, int teamSize, int want);
struct Peer {
  bool accepted = false, declined = false, ready = false, rematch = false, left = false;
  int teamCount = -1;
  std::vector<std::string> refusals;
  bool pvTeam = false, pvCommit = false, pvReveal = false;
  std::vector<int> pvIds;
  std::string pvCommitHex;
  std::string pvNonce, pvList;
  uint32_t nonce = 0;
};
bool Apply(Peer& peer, const std::string& line);

enum class Outcome { Won, Lost, YouQuit, FriendQuit, ConnectionLost, Desync, Draw };
Outcome OutcomeOf(unsigned result, bool host, bool desync);
std::string OutcomeText(Outcome o);

}

#endif
