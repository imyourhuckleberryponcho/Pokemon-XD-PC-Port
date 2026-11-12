#ifndef ENGINE_RANDOMIZER_H
#define ENGINE_RANDOMIZER_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::randomizer {

class Rng {
public:
  explicit Rng(uint64_t seed);
  static Rng Stream(uint64_t seed, std::string_view category);
  uint64_t Next();
  uint32_t Below(uint32_t n);
  bool Chance(uint32_t percent);
  template <class T> void Shuffle(std::vector<T>& v) {
    for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[Below(static_cast<uint32_t>(i))]);
  }

private:
  uint64_t s_[4];
};

struct SwitchDef {
  std::string id;
  std::string label;
  std::vector<std::string> choiceNames;
  uint32_t since = 1;
  uint32_t Choices() const { return static_cast<uint32_t>(choiceNames.size()); }
};

struct Settings;

struct Schema {
  std::string prefix;
  uint32_t version = 1;
  std::vector<SwitchDef> switches;
  int Find(std::string_view id) const;
  uint32_t CodeVersion(const Settings& s) const;
};

struct Settings {
  uint64_t seed = 0;
  std::vector<uint32_t> values;
  uint32_t Get(const Schema& s, std::string_view id) const;
  void Set(const Schema& s, std::string_view id, uint32_t v);
};

std::string EncodeCode(const Schema& schema, const Settings& s);
bool DecodeCode(const Schema& schema, std::string_view code, Settings& out, bool& seedOnly, std::string& error);

struct ModOutput {
  std::map<std::string, std::map<uint32_t, std::map<std::string, uint32_t>>> tables;
  std::vector<std::string> spoiler;
  void Set(const std::string& table, uint32_t entry, const std::string& field, uint32_t value) {
    tables[table][entry][field] = value;
  }
  size_t Count() const;
  std::string ToJson(const std::string& note) const;
};

uint32_t PickSimilar(Rng& rng, const std::vector<uint32_t>& pool, const std::vector<uint32_t>& strength, uint32_t target,
                     uint32_t percent, const std::vector<bool>* used);

uint32_t ScaleLevel(uint32_t level, uint32_t percent, uint32_t maxLevel);
uint32_t LevelForStrength(uint32_t level, uint32_t oldStrength, uint32_t nowStrength, uint32_t maxLevel);

}

#endif
