#include <xd_gba_disc.h>

#include "common_rel.h"
#include "dol_tables.h"

#include <port_disc.h>

#include <game/pxdvs/app/item/itemData.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/pokemon/pokemonSeikaku.h>
#include <game/pxdvs/app/waza/wazaData.h>

#include <cstring>
#include <vector>

namespace xd::gba {

namespace {

constexpr uint32_t kGbaCodeJapanese = 0x802F2988;
constexpr uint32_t kGbaCodeWestern = 0x802F2B88;
constexpr uint32_t kGrowData = 0x803FEFB0;
constexpr uint32_t kGrowDataNumber = 0x804E83B0;
constexpr uint32_t kGrowDataMaxSize = 0xCA0;

uint32_t Be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

}

struct DiscRules::Impl {
  xd::save::GameNames names;
  XDCommonTables tables{};
  const WazaData* moves = nullptr;
  uint32_t moveCount = 0;
  const uint16_t* itemIndex = nullptr;
  uint32_t itemIndexCount = 0;
  const ItemDataBiosRef* items = nullptr;
  uint32_t itemCount = 0;
  const PokemonSeikakuDataBios* natures = nullptr;
  uint32_t natureCount = 0;
  const PokemonSeikakuRateBios* rates = nullptr;
  uint32_t rateCount = 0;
  std::vector<uint32_t> grow;
  uint32_t growGroups = 0;
  std::array<uint16_t, 256> charsJp{}, charsWest{};

  void* Global(const char* name) const {
    for (uint32_t i = 0; i < tables.numGlobals; i++) {
      if (tables.globals[i].name != nullptr && std::strcmp(tables.globals[i].name, name) == 0 &&
          tables.globals[i].converted) {
        return tables.globals[i].value;
      }
    }
    return nullptr;
  }
  uint32_t Count(const char* name) const {
    const void* v = Global(name);
    return v != nullptr ? *static_cast<const uint32_t*>(v) : 0;
  }
  const PokemonData* Species(uint16_t id) const {
    return tables.pokemon_data != nullptr && id < tables.pokemon_data_number ? &tables.pokemon_data[id] : nullptr;
  }
  const ItemDataBiosRef* Item(uint16_t id) const {
    if (itemIndex == nullptr || items == nullptr || id >= itemIndexCount) {
      return nullptr;
    }
    const uint16_t slot = itemIndex[id];
    return slot < itemCount ? &items[slot] : nullptr;
  }
};

DiscRules::DiscRules() = default;
DiscRules::~DiscRules() = default;

const xd::save::GameNames& DiscRules::Names() const { return impl_->names; }

bool DiscRules::Load(const std::string& discPath, std::string& error) {
  impl_.reset();
  auto impl = std::make_unique<Impl>();
  if (!impl->names.Load(discPath, error)) {
    return false;
  }
  char err[512] = {};
  if (!PortXD_LoadCommonRel(&impl->tables, err, sizeof(err))) {
    error = std::string("common_rel: ") + err;
    return false;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol == nullptr) {
    error = "the disc has no main.dol";
    return false;
  }
  uint8_t buf[0x200];
  for (int t = 0; t < 2; t++) {
    if (!PortDol_Read(dol, dolSize, t == 0 ? kGbaCodeJapanese : kGbaCodeWestern, buf, sizeof(buf))) {
      error = "main.dol: the GBA character tables aren't where the USA game has them";
      return false;
    }
    auto& table = t == 0 ? impl->charsJp : impl->charsWest;
    for (int i = 0; i < 256; i++) {
      table[i] = static_cast<uint16_t>(buf[i * 2] << 8 | buf[i * 2 + 1]);
    }
  }
  uint8_t num[4];
  if (!PortDol_Read(dol, dolSize, kGrowDataNumber, num, 4)) {
    error = "main.dol: no experience table count";
    return false;
  }
  impl->growGroups = Be32(num);
  if (impl->growGroups == 0 || impl->growGroups * 0x194 > kGrowDataMaxSize) {
    error = "main.dol: unexpected experience table size";
    return false;
  }
  std::vector<uint8_t> grow(impl->growGroups * 0x194);
  if (!PortDol_Read(dol, dolSize, kGrowData, grow.data(), static_cast<uint32_t>(grow.size()))) {
    error = "main.dol: experience table not readable";
    return false;
  }
  impl->grow.resize(impl->growGroups * 101);
  for (size_t i = 0; i < impl->grow.size(); i++) {
    impl->grow[i] = Be32(grow.data() + i * 4);
  }
  impl->moves = static_cast<const WazaData*>(impl->Global("waza_data"));
  impl->moveCount = impl->Count("waza_data_number");
  impl->itemIndex = static_cast<const uint16_t*>(impl->Global("item_data_index"));
  impl->itemIndexCount = impl->Count("item_data_index_number");
  impl->items = static_cast<const ItemDataBiosRef*>(impl->Global("item_data_prime"));
  impl->itemCount = impl->Count("item_data_prime_number");
  impl->natures = static_cast<const PokemonSeikakuDataBios*>(impl->Global("pokemon_seikaku_data"));
  impl->natureCount = impl->Count("pokemon_seikaku_data_number");
  impl->rates = static_cast<const PokemonSeikakuRateBios*>(impl->Global("pokemon_seikaku_rate_data"));
  impl->rateCount = impl->Count("pokemon_seikaku_rate_data_number");
  if (impl->tables.pokemon_data == nullptr || impl->moves == nullptr || impl->itemIndex == nullptr ||
      impl->items == nullptr || impl->natures == nullptr || impl->rates == nullptr) {
    error = "common_rel: a table the trade needs (species, moves, items, natures) didn't load";
    return false;
  }
  impl_ = std::move(impl);
  return true;
}

bool DiscRules::SpeciesValid(uint16_t species) const {
  const PokemonData* p = impl_->Species(species);
  return species != 0 && p != nullptr && static_cast<uint32_t>((uintptr_t)p->name) != 0;
}

uint8_t DiscRules::SecondAbility(uint16_t species) const {
  const PokemonData* p = impl_->Species(species);
  return p != nullptr ? p->tokuseiDataId[1] : 0;
}

uint8_t DiscRules::GrowthGroup(uint16_t species) const {
  const PokemonData* p = impl_->Species(species);
  return p != nullptr ? p->growDataId : 0;
}

std::array<uint16_t, 6> DiscRules::BaseStats(uint16_t species) const {
  std::array<uint16_t, 6> s{};
  if (const PokemonData* p = impl_->Species(species)) {
    const BasisStatus& b = p->basisStatus[0];
    s = {b.maxHp, b.phyAtk, b.phyDef, b.speAtk, b.speDef, b.nimbleness};
  }
  return s;
}

uint16_t DiscRules::SpeciesNumberField(uint16_t species, int which) const {
  const PokemonData* p = impl_->Species(species);
  return p == nullptr ? 0 : which == 0 ? p->numPokemon : p->numZukan;
}

uint16_t DiscRules::NationalDex(uint16_t species) const { return SpeciesNumberField(species, 0); }

uint16_t DiscRules::TradeEvolution(uint16_t species, uint16_t heldItem) const {
  const PokemonData* p = impl_->Species(species);
  if (p == nullptr) {
    return 0xFFFF;
  }
  uint16_t result = 0;
  for (const Sinka& s : p->sinka) {
    if (s.kind == 5 || (s.kind == 6 && s.buff == heldItem)) {
      result = s.pokemonDataId;
    }
  }
  return result;
}

bool DiscRules::GrowthKnown(uint8_t group) const { return group < impl_->growGroups; }

uint32_t DiscRules::ExpForLevel(uint8_t group, uint8_t level) const {
  if (group >= impl_->growGroups || level >= 101) {
    return 0;
  }
  return impl_->grow[group * 101u + level];
}

bool DiscRules::NatureRate(uint8_t nature, int stat, uint8_t& mul, uint8_t& div) const {
  if (nature >= impl_->natureCount) {
    mul = 1;
    div = 1;
    return true;
  }
  const PokemonSeikakuDataBios& n = impl_->natures[nature];
  uint8_t id = 0;
  switch (stat) {
  case 1: id = n.phyAtkRateDataId; break;
  case 2: id = n.phyDefRateDataId; break;
  case 3: id = n.speAtkRateDataId; break;
  case 4: id = n.speDefRateDataId; break;
  case 5: id = n.nimblenessRateDataId; break;
  default: mul = div = 1; return true;
  }
  if (id >= impl_->rateCount) {
    return false;
  }
  mul = impl_->rates[id].kake;
  div = impl_->rates[id].waru;
  return true;
}

uint8_t DiscRules::MovePP(uint16_t move) const {
  return impl_->moves != nullptr && move < impl_->moveCount ? impl_->moves[move].pp : 0;
}

bool DiscRules::ItemKnown(uint16_t item) const { return impl_->Item(item) != nullptr; }
bool DiscRules::ItemImportant(uint16_t item) const {
  const ItemDataBiosRef* i = impl_->Item(item);
  return i != nullptr && i->important != 0;
}
bool DiscRules::ItemHasName(uint16_t item) const {
  const ItemDataBiosRef* i = impl_->Item(item);
  return i != nullptr && i->name != 0;
}
uint16_t DiscRules::ItemHoldEffect(uint16_t item) const {
  const ItemDataBiosRef* i = impl_->Item(item);
  return i != nullptr ? i->itemSoubiDataId : 0;
}

const std::array<uint16_t, 256>& DiscRules::GbaCharsJapanese() const { return impl_->charsJp; }
const std::array<uint16_t, 256>& DiscRules::GbaCharsWestern() const { return impl_->charsWest; }

}
