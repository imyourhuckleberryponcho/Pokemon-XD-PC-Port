#ifndef XD_ONLINE_ROOM_H
#define XD_ONLINE_ROOM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::online {

constexpr int kBattleProtocol = 1;

struct RoomRules {
  std::string ruleSet = "classic";
  std::string items = "none";
  int timerSeconds = 0;
  bool shadowAllowed = false;
  std::string shadowObey = "story";
  std::string level = "asis";
  std::string team = "preset";
  std::string draft;
  std::string style = "single";
  bool split = false;
};

inline bool RoomGen4Pokemon(const std::string& ruleSet) { return ruleSet != "classic"; }
inline bool RoomGen4Rules(const std::string& ruleSet) { return ruleSet == "gen4" || ruleSet == "gen4mt"; }

struct Capabilities {
  int protocol = kBattleProtocol;
  std::string disc = "GXXE01";
  std::string edition = "xd";
  std::string ruleSet = "classic";
  std::string gen4Pack;
  std::string mods;
  std::string randomizer;
  uint64_t tables = 0;
};

struct Fingerprint {
  Capabilities caps;
  RoomRules rules;
};

bool ParseRules(const std::string& text, RoomRules& out, std::string* error);
bool RulesValid(const RoomRules& r, std::string* error);
std::string RulesText(const RoomRules& r, const std::string& edition = "xd");

std::vector<uint8_t> Encode(const Fingerprint& f);
bool Decode(const std::vector<uint8_t>& blob, Fingerprint& out);

struct Verdict {
  std::vector<std::string> refuse;
  std::vector<std::string> restart;
  bool Ok() const { return refuse.empty(); }
  bool NeedsRestart() const { return !restart.empty(); }
};
Verdict Judge(const Capabilities& room, const RoomRules& rules, const Capabilities& mine, bool joiner);

uint64_t Fnv64(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
std::string Hex(uint64_t v);

}

#endif
