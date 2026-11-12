#ifndef XD_GBA_DISC_H
#define XD_GBA_DISC_H

#include <xd_gba.h>
#include <xd_save_names.h>

#include <memory>
#include <string>

namespace xd::gba {

class DiscRules : public GameRules {
public:
  DiscRules();
  ~DiscRules() override;
  bool Load(const std::string& discPath, std::string& error);
  const xd::save::GameNames& Names() const;

  bool SpeciesValid(uint16_t species) const override;
  uint8_t SecondAbility(uint16_t species) const override;
  uint8_t GrowthGroup(uint16_t species) const override;
  std::array<uint16_t, 6> BaseStats(uint16_t species) const override;
  uint16_t NationalDex(uint16_t species) const override;
  uint16_t TradeEvolution(uint16_t species, uint16_t heldItem) const override;
  bool GrowthKnown(uint8_t group) const override;
  uint32_t ExpForLevel(uint8_t group, uint8_t level) const override;
  bool NatureRate(uint8_t nature, int stat, uint8_t& mul, uint8_t& div) const override;
  uint8_t MovePP(uint16_t move) const override;
  bool ItemKnown(uint16_t item) const override;
  bool ItemImportant(uint16_t item) const override;
  bool ItemHasName(uint16_t item) const override;
  uint16_t ItemHoldEffect(uint16_t item) const override;
  const std::array<uint16_t, 256>& GbaCharsJapanese() const override;
  const std::array<uint16_t, 256>& GbaCharsWestern() const override;

  uint16_t SpeciesNumberField(uint16_t species, int which) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
