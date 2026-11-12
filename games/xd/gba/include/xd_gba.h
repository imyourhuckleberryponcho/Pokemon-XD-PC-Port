#ifndef XD_GBA_H
#define XD_GBA_H

#include <gen3_pokemon.h>
#include <gen3_save.h>
#include <xd_save.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::gba {

using xd::save::ByteOrder;

class GameRules {
public:
  virtual ~GameRules() = default;
  virtual bool SpeciesValid(uint16_t species) const = 0;
  virtual uint8_t SecondAbility(uint16_t species) const = 0;
  virtual uint8_t GrowthGroup(uint16_t species) const = 0;
  virtual std::array<uint16_t, 6> BaseStats(uint16_t species) const = 0;
  virtual uint16_t NationalDex(uint16_t species) const = 0;
  virtual uint16_t TradeEvolution(uint16_t species, uint16_t heldItem) const = 0;
  virtual bool GrowthKnown(uint8_t group) const = 0;
  virtual uint32_t ExpForLevel(uint8_t group, uint8_t level) const = 0;
  virtual bool NatureRate(uint8_t nature, int stat, uint8_t& mul, uint8_t& div) const = 0;
  virtual uint8_t MovePP(uint16_t move) const = 0;
  virtual bool ItemKnown(uint16_t item) const = 0;
  virtual bool ItemImportant(uint16_t item) const = 0;
  virtual bool ItemHasName(uint16_t item) const = 0;
  virtual uint16_t ItemHoldEffect(uint16_t item) const = 0;
  virtual const std::array<uint16_t, 256>& GbaCharsJapanese() const = 0;
  virtual const std::array<uint16_t, 256>& GbaCharsWestern() const = 0;
};

const std::array<uint16_t, 256>& GbaTableFor(const GameRules& rules, int xdLanguage);
std::u16string GbaToText(const GameRules& rules, const uint8_t* src, int len, int xdLanguage);
int TextToGba(const GameRules& rules, const std::u16string& text, int xdLanguage, uint8_t* dst);

struct XdMon {
  std::array<uint8_t, xd::save::kPokemonSize> raw{};
  ByteOrder order = ByteOrder::GameCube;

  uint8_t U8(size_t at) const { return raw[at]; }
  uint16_t U16(size_t at) const { return xd::save::Read16(raw.data() + at, order); }
  uint32_t U32(size_t at) const { return xd::save::Read32(raw.data() + at, order); }
  void SetU8(size_t at, uint8_t v) { raw[at] = v; }
  void SetU16(size_t at, uint16_t v);
  void SetU32(size_t at, uint32_t v);
  std::u16string Text(size_t at) const { return xd::save::ReadString(raw.data() + at, 11, order); }

  uint16_t Species() const { return U16(0x00); }
  uint16_t Item() const { return U16(0x02); }
  uint16_t Hp() const { return U16(0x04); }
  uint8_t Level() const { return U8(0x11); }
  uint32_t Pid() const { return U32(0x28); }
  uint32_t OtId() const { return U32(0x24); }
  uint16_t ShadowId() const { return U16(0xBA); }
  bool Egg() const { return (U8(0x1D) & 0x80) != 0; }
  bool AbilityBit() const { return (U8(0x1D) & 0x40) != 0; }
  bool Invalid() const { return (U8(0x1D) & 0x20) != 0; }
};

XdMon EmptyMon(ByteOrder order);

engine::gen3::PlainMon ToGba(const XdMon& mon, const GameRules& rules);
XdMon FromGba(const engine::gen3::PlainMon& gba, const GameRules& rules, ByteOrder order);

void ResetStats(XdMon& mon, const GameRules& rules);

enum class State {
  Empty = 0,
  BadEgg = 1,
  Egg = 2,
  Shadow = 3,
  ItemNotAllowed = 4,
  NoEventFlag = 5,
  Impossible = 6,
  Tradeable = 7,
};
enum class Side { Xd = 0, Gba = 1 };
State TradeState(const XdMon& mon, Side side, int shadowStatus, const GameRules& rules);
const char* StateText(State s);

struct TradeRequest {
  int xdSlot = 0;
  int gbaSlot = 0;
};

struct TradeResult {
  bool ok = false;
  std::string error;
  std::vector<std::string> notes;
  std::vector<uint8_t> xdFile;
  std::vector<uint8_t> gbaFile;
};

struct PartyView {
  std::array<XdMon, 6> mons;
  std::array<State, 6> states{};
  int count = 0;
};
PartyView XdParty(const xd::save::Save& save, const GameRules& rules);
PartyView GbaParty(const engine::gen3::Save& save, const GameRules& rules, ByteOrder order);

TradeResult Trade(const std::vector<uint8_t>& xdFile, const xd::save::Save& xd, const xd::save::ReadReport& report,
                  const engine::gen3::Save& gba, const TradeRequest& request, const GameRules& rules);

}

#endif
