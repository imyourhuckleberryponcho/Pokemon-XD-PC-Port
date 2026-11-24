#ifndef XD_TRADE_H
#define XD_TRADE_H

#include <net_trade.h>
#include <xd_legality.h>
#include <xd_save.h>
#include <xd_save_names.h>

#include <functional>
#include <string>
#include <vector>

namespace xd::trade {

using Bytes = std::vector<uint8_t>;
using xdnet::trade::Verdict;

constexpr size_t kWireSize = xd::save::kPokemonSize + 2;

enum class ShadowState : uint8_t { Never = 0, Purified = 1, Unpurified = 2 };

struct Card {
  std::string name;
  std::string species;
  int level = 0;
  std::string nature;
  std::string item;
  std::string ability;
  std::string moves[4];
  bool shiny = false;
  bool egg = false;
  std::string ot;
  uint32_t otId = 0;
  uint16_t speciesNumber = 0;
  int hp = 0, maxHp = 0;
  bool fromCheats = false;
};

struct Entry {
  int box = -1;
  int slot = 0;
  xd::save::Pokemon pokemon;
  Bytes wire;
  Card card;
  bool ok = false;
  std::string reason;
};

struct Config {
  const xd::legality::Rules* rules = nullptr;
  const xd::save::GameNames* names = nullptr;
  const xd::legality::Rules* originalRules = nullptr;
  bool fromCheats = false;
  bool original = false;
};

struct Result {
  bool evolved = false;
  std::string evolvedFrom, evolvedInto;
  std::string received;
  bool newEntry = false;
  std::string where;
  bool receivedFromCheats = false;
};

class Trader {
 public:
  explicit Trader(const Config& cfg) : cfg_(cfg) {}
  virtual ~Trader() = default;
  bool Load(const Bytes& file, std::string& error);
  bool loaded() const { return loaded_; }
  const xd::save::Save& save() const { return save_; }
  virtual const std::string& trainerName() const { return trainer_; }

  Card Describe(const xd::save::Pokemon& p) const;
  virtual std::vector<Entry> List() const;
  virtual Verdict CheckIncoming(const Bytes& wire) const;
  virtual Card DescribeWire(const Bytes& wire) const;

  virtual Verdict Prepare(const Bytes& give, const Bytes& get);
  virtual Verdict Commit(const std::function<bool(const Bytes& newFile, std::string& error)>& writer);
  virtual const Result& result() const { return result_; }
  const Bytes& preparedFile() const { return prepared_; }

  Bytes Wire(const xd::save::Pokemon& p) const;
  static bool Parse(const Bytes& wire, xd::save::Pokemon& out, ShadowState& shadow);

  static Verdict Judge(const xd::legality::Rules& rules, const xd::save::Pokemon& p, ShadowState sh);
  static uint16_t TradeEvolution(const xd::legality::Rules& rules, const xd::save::Pokemon& p, uint8_t& kind);

 protected:
  Verdict JudgeFor(const xd::save::Pokemon& p, ShadowState sh) const;
  std::string Name(uint16_t species) const;
  Config cfg_;
  bool loaded_ = false;
  Bytes file_;
  xd::save::Save save_;
  xd::save::ReadReport report_;
  std::string trainer_;
  Bytes prepared_;
  Result result_;
};

}

#endif
