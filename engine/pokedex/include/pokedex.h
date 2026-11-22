#ifndef ENGINE_POKEDEX_H
#define ENGINE_POKEDEX_H

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace engine::pokedex {

enum class MoveSource : uint8_t { Level, Machine, Tutor };
struct LearnedMove {
  MoveSource source = MoveSource::Level;
  uint8_t level = 0;
  uint16_t number = 0;
  uint16_t move = 0;
  std::string name;
};

enum class EvoKind : uint8_t { Level, Friendship, FriendshipDay, FriendshipNight, Stone, Trade, TradeItem, Stats, Personality, Other };
struct Evolution {
  uint16_t to = 0;
  EvoKind kind = EvoKind::Other;
  uint16_t param = 0;
  std::string text;
};

struct Species {
  uint16_t index = 0;
  uint16_t national = 0;
  bool mod = false;
  std::string modName;
  std::string name, category, entry;
  uint8_t types[2] = {0, 0};
  std::string typeNames[2];
  std::string abilities[2];
  std::string abilityText[2];
  uint16_t heightDm = 0, weightHg = 0;
  uint8_t genderRatio = 0;
  uint8_t catchRate = 0;
  std::string growth;
  uint16_t base[6] = {0, 0, 0, 0, 0, 0};
  uint8_t gen = 3;
  bool shadow = false;
  std::vector<LearnedMove> moves;
  std::vector<Evolution> evolutions;
  uint16_t evolvesFrom = 0;
  uint16_t formOf = 0;
  std::string formName;
  std::vector<uint16_t> forms;
};

struct WhereLine {
  std::string who, place, note;
  int level = 0;
  char sex = 0;
  uint32_t battle = 0;
};
struct Where {
  std::vector<WhereLine> shadow;
  std::vector<WhereLine> spots;
  std::vector<WhereLine> trainers;
  std::vector<WhereLine> yours;
};

enum class Seen : uint8_t { Unseen, Seen, Caught };

struct PlayerState {
  std::set<uint16_t> seen, caught, shinyOwned;
  std::function<bool(uint32_t)> battleFought;
};

struct Settings {
  bool showAll = false;
  bool showAllTrainers = false;
  bool showAllShiny = false;
};

Seen Status(const PlayerState& p, uint16_t index);
inline bool CanOpen(const PlayerState& p, uint16_t index, const Settings& s) { return s.showAll || Status(p, index) != Seen::Unseen; }
inline bool ShinyAllowed(const PlayerState& p, uint16_t index, const Settings& s) {
  return s.showAllShiny || s.showAll || p.shinyOwned.count(index) != 0;
}
bool TrainerLineVisible(const PlayerState& p, const WhereLine& l, const Settings& s);

std::string NumberText(const Species& s);
std::string RowName(const Species& s, Seen st, const Settings& set);
std::string EvolutionTargetName(const Species& s, Seen st, const Settings& set);

enum class Order : uint8_t { National, AToZ, Type, Height, Weight, OrderMet, CaughtFirst };
enum class Show : uint8_t { All, AllSeen, Caught, NotCaught, Shadow, FromMods };
struct Query {
  Order order = Order::National;
  Show show = Show::All;
  int typeId = -1;
  int generation = 0;
  std::string nameContains;
};

std::vector<uint16_t> Arrange(const std::vector<Species>& all, const PlayerState& p, const Settings& set, const Query& q,
                              const std::vector<uint16_t>& met);

struct Counts {
  int seen = 0, caught = 0, total = 0;
};
Counts Count(const std::vector<Species>& all, const PlayerState& p);

std::vector<LearnedMove> MovesOf(const Species& s, MoveSource kind);

std::vector<std::string> Wrap(const std::string& text, float maxWidth, const std::function<float(const std::string&)>& width);

std::string ListToText(const std::set<uint16_t>& s);
std::set<uint16_t> ListFromText(const std::string& text);

std::string HeightText(uint16_t heightDm, bool imperial);
std::string WeightText(uint16_t weightHg, bool imperial);
std::string GenderText(uint8_t ratio);

}

#endif
