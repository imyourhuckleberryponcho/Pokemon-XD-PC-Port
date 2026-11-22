#ifndef XD_BATTLE_DEFS_H
#define XD_BATTLE_DEFS_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace xd::battle {

enum class Cond : uint8_t {
  FullHp, NotFullHp, HpBelowHalf, HpAboveHalf, HpBelowThird,
  TargetFullHp, TargetHpBelowHalf,
  StatusMove, DamagingMove, Contact, Physical, Special,
  MoveType,
  UserType, TargetType,
  Weather,
  HasStatus, NoStatus, TargetHasStatus, TargetNoStatus,
  UserHasItem, UserNoItem, TargetHasItem, TargetNoItem,
  FirstTurn,
  MoveHasSecondary,
  SuperEffective, NotVeryEffective,
  MoveIs,
  Field,
  Sound, Punch,
  TargetStatusKind, UserStatusKind,
  Terrain,
  TargetGrounded,
  UserGrounded,
  MoveInList,
  HpCrossedHalf,
  SlowerThanTarget,
  TargetFainted,
  TargetFirstTurn,
};
struct Condition {
  Cond kind = Cond::FullHp;
  int32_t arg = 0;
  bool negate = false;
  std::vector<int> list;
};

enum class BlockKind : uint8_t {
  Stat,
  Status,
  Heal,
  Hurt,
  Recoil,
  Drain,
  SwitchOut,
  ForceSwitch,
  Weather,
  RemoveItem,
  Message,
  SetType,
  Field,
  SetAbility,
  Terrain,
  CureStatus,
  Side,
  StealItem,
  HurtDealt,
  Moody,
};

struct Block {
  BlockKind kind = BlockKind::Message;
  bool onUser = true;
  bool onFoes = false;
  int stat = 0;
  int stages = 0;
  int chance = 100;
  int code = 0;
  int permille = 0;
  int weather = 0;
  int type = -1;
  int ability = 0;
  int turns = 0;
  int field = -1;
  int messageId = -1;
  std::string message;
  std::string endMessage;
  std::vector<Condition> when;
  int line = 0;
};

struct PowerRule {
  std::vector<Condition> when;
  int percent = 100;
  int accuracy = -1;
  int add = 0;
};

struct MoveDef {
  std::string name;
  int64_t id = 0;
  std::vector<Block> effects;
  std::vector<PowerRule> power;
  std::vector<PowerRule> accuracy;
  bool script = false;
  int crit = 0;
  bool ignoreBoosts = false;
  bool useTargetAttack = false;
  bool defendWithDef = false;
  bool firstTurnOnly = false;
  int powerFormula = 0;
  bool neverMiss = false;
  int protectKind = 0;
  std::string mod;
  int line = 0;
};

struct StatMod { int stat = 0; int percent = 100; std::vector<Condition> when; };
struct Convert {
  int from = -1;
  int to = 0;
  int percent = 100;
  std::vector<Condition> when;
};
struct Absorb { int type = 0; int healPermille = 0; int stat = 0; int stages = 0; std::string message; int messageId = -1; };

struct AbilityDef {
  std::string name;
  int id = 0;
  std::string text;
  std::vector<Block> switchIn, endOfTurn, ko, contact, switchOut, beforeMove;
  std::vector<PowerRule> damageDealt;
  std::vector<PowerRule> damageTaken;
  std::vector<StatMod> statMods;
  std::vector<Absorb> absorb;
  std::vector<Block> anyFaint;
  std::vector<Block> hit, hitDealt, fainted, statDropped;
  std::vector<Convert> convert;
  int auraType = -1, auraPercent = 133;
  bool breaksAuras = false;
  std::vector<int> immuneMoves;
  std::vector<PowerRule> accuracyAgainst, accuracyDealt;
  std::vector<Condition> critWhen;
  bool blocksPriority = false;
  bool contrary = false;
  bool noContact = false;
  std::vector<int> statDropBlock;
  std::vector<int> statusBlock;
  std::vector<PowerRule> priority;
  int actsAs = 0;
  bool reflectStatus = false;
  bool removesSecondary = false;
  bool script = false;
  std::string mod;
  int line = 0;
};

struct RuleSet {
  std::string name;
  int levelCap = 0;
  int levelAll = 0;
  std::vector<int> bannedItems;
  bool forceDouble = false;
  bool inverse = false;
  int weather = 0;
  std::string mod;
};

struct Defs {
  std::vector<MoveDef> moves;
  std::vector<AbilityDef> abilities;
  std::vector<RuleSet> ruleSets;
  std::vector<std::string> fieldNames;
  std::vector<std::string> problems;
};

using Resolver = std::function<bool(const std::string& category, const std::string& name, int64_t& value)>;

void ParseFile(const std::string& mod, const std::string& file, const std::string& text, const Resolver& resolve, Defs& defs);

void Finish(Defs& defs);

uint64_t Digest(const Defs& defs);

int StatNumber(const std::string& word);
int StatusCode(const std::string& word);
int WeatherNumber(const std::string& word);

}

#endif
