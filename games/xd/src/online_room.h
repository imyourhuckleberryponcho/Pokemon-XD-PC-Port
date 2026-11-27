#ifndef XD_ONLINE_ROOM_H
#define XD_ONLINE_ROOM_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::online {

constexpr int kExtrasProtocol = 3;
constexpr int kBattleProtocol = 5;

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
  std::string rs;
  std::string gens;
  std::string details;
  std::string format;
  std::string formatHash;
  bool sleepClause = false;
  bool preview = false;
  int pick = 0;
  int previewSeconds = 0;
  int bankSeconds = 0;
};

inline bool RoomGen4Pokemon(const std::string& ruleSet) { return ruleSet != "classic"; }
inline bool RoomGen4Rules(const std::string& ruleSet) { return ruleSet == "gen4" || ruleSet == "gen4mt"; }

std::string RoomRuleSetId(const RoomRules& r);
std::string RoomGens(const RoomRules& r);
std::string RoomDetails(const RoomRules& r);
void SetRuleChoice(RoomRules& r, const std::string& rs, const std::string& gens, const std::string& details);
bool NeedsNewProtocol(const RoomRules& r);
bool NeedsExtras(const RoomRules& r);
std::string ExtrasKeyText(const RoomRules& r);
std::string RoomNumbers(const RoomRules& r);
int RoomMaxGen(const RoomRules& r);

struct Capabilities {
  int protocol = kBattleProtocol;
  std::string disc = "GXXE01";
  std::string edition = "xd";
  std::string ruleSet = "classic";
  std::string numbers = "3";
  std::string gen4Pack;
  std::string mods;
  std::string randomizer;
  uint64_t tables = 0;
  bool cheats = false;
  std::string formatsDigest;
  std::string knownRuleSets = "xd,gen4,gen5,gen6,gen7";
  int knownMaxGen = 4;
  std::string knownFormats;
};

struct Fingerprint {
  Capabilities caps;
  RoomRules rules;
};

bool ParseRules(const std::string& text, RoomRules& out, std::string* error);
bool RulesValid(const RoomRules& r, std::string* error);
std::string RulesText(const RoomRules& r, const std::string& edition = "xd");
std::string RulesKeyText(const RoomRules& r);

std::vector<uint8_t> Encode(const Fingerprint& f);
bool Decode(const std::vector<uint8_t>& blob, Fingerprint& out);

struct Verdict {
  std::vector<std::string> refuse;
  std::vector<std::string> restart;
  std::vector<std::string> notes;
  bool Ok() const { return refuse.empty(); }
  bool NeedsRestart() const { return !restart.empty(); }
};
Verdict Judge(const Capabilities& room, const RoomRules& rules, const Capabilities& mine, bool joiner);

int ActionNumbers(const std::string& text);

uint64_t Fnv64(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
std::string Hex(uint64_t v);
std::string PackBattleDigest(const std::filesystem::path& folder);

}

#endif
