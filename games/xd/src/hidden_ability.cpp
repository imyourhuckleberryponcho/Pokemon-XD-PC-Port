#include "xd_hidden_ability.h"

#include "xd_forms.h"
#include "xd_gen_rules.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
uint32_t PortXD_RealPokemonTokusei(void* pokemon);
uint8_t pokemonBiosGetFlagAmari(void* pokemon);
void pokemonBiosSetFlagAmari(void* pokemon, uint8_t value);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
uint16_t pokemonDataBiosGetNumPokemon(void* data);
void* pokemonDataBiosGetPtr(uint16_t row);
uint32_t HSD_Rand(void);
}

namespace {
std::map<int, int> sNat, sRow;
constexpr uint8_t kBit = 0x80;
constexpr int kMinGen = 5;
constexpr uint32_t kChance = 20;
uint32_t Chance() {
  static const char* t = std::getenv("XD_HIDDEN_CHANCE");
  return t != nullptr && std::atoi(t) > 0 ? static_cast<uint32_t>(std::atoi(t)) : kChance;
}
}

namespace xd::hidden {

extern "C" int32_t (*(*PortXD_FieldItemUseFunc)(uint32_t item))(uint16_t, int*);
extern "C" int32_t (*PortXD_HiddenItemUse(uint32_t item))(uint16_t, int*);
void SetTables(const std::map<int, int>& byNational, const std::map<int, int>& byRow) {
  sNat = byNational; sRow = byRow;
  if (!sNat.empty() || !sRow.empty()) PortXD_FieldItemUseFunc = PortXD_HiddenItemUse;
}
bool Enabled() { return (!sNat.empty() || !sRow.empty()) && xd::rulegen::Gen() >= kMinGen; }

int OfRow(uint32_t row) {
  if (const auto it = sRow.find(static_cast<int>(row)); it != sRow.end()) return it->second;
  if (sNat.empty() || xd::forms::IsForm(row)) return 0;
  void* data = pokemonDataBiosGetPtr(static_cast<uint16_t>(row));
  if (data == nullptr) return 0;
  const auto it = sNat.find(pokemonDataBiosGetNumPokemon(data));
  return it == sNat.end() ? 0 : it->second;
}

static bool AllHidden() { static const bool on = std::getenv("XD_HIDDEN_ALL") != nullptr && std::getenv("XD_SETTINGS_DIR") != nullptr; return on; }
bool Has(void* pokemon) { return pokemon != nullptr && (AllHidden() || (pokemonBiosGetFlagAmari(pokemon) & kBit) != 0); }

bool Give(void* pokemon, bool on) {
  if (pokemon == nullptr) return false;
  if (on && OfRow(pokemonBiosGetPokemonDataId(pokemon)) == 0) return false;
  const uint8_t f = pokemonBiosGetFlagAmari(pokemon);
  pokemonBiosSetFlagAmari(pokemon, on ? (f | kBit) : (f & ~kBit));
  return true;
}

void MaybeGive(void* pokemon) {
  if (pokemon == nullptr || !Enabled() || OfRow(pokemonBiosGetPokemonDataId(pokemon)) == 0) return;
  if (HSD_Rand() % Chance() != 0) return;
  Give(pokemon, true);
  std::fprintf(stderr, "[hidden] species row %u: hidden ability %d (placeholder rule: 1 in %u)\n", pokemonBiosGetPokemonDataId(pokemon),
               OfRow(pokemonBiosGetPokemonDataId(pokemon)), Chance());
}

}

extern "C" {
uint32_t pokemonGetTokuseiDataId(void* pokemon) {
  if (pokemon != nullptr && xd::hidden::Has(pokemon) && xd::hidden::Enabled()) {
    if (const int id = xd::hidden::OfRow(pokemonBiosGetPokemonDataId(pokemon)); id > 0) return static_cast<uint32_t>(id);
  }
  return PortXD_RealPokemonTokusei(pokemon);
}
}

extern "C" {
void menuPocket2ClosePokemonMenu(int32_t slot);
int32_t menuPocket2OpenPokemonMenu(uint32_t item, int use);
void menuPocket2GetTemochiPokemon(int32_t slot, void** pokemon, void** fightPokemon);
uint8_t pokemonIsDarkPokemon(void* pokemon);
uint8_t windowSearchID(int32_t id);
void winMsgOpen(int32_t a, uint32_t msg, int32_t b, int32_t c);
int32_t winMsgCheck(void);
void winMsgClose(int32_t a);
void GSthreadSwitch(void);
void menuOffScreenSetPriority(int32_t p);
}
namespace {
constexpr uint32_t kBank = 0x7D0u << 20;
void Say(uint32_t id) {
  const uint32_t msg = kBank | id;
  if (windowSearchID(0x4C) == 0) {
    winMsgOpen(2, msg, 1, 0);
  } else {
    winMsgOpen(2, msg, 0, 0);
    do {
      menuOffScreenSetPriority(0);
      GSthreadSwitch();
    } while (winMsgCheck() != 0);
  }
  winMsgClose(1);
}
int32_t UseAbilityPatch(uint16_t item, int* consume) {
  const int32_t slot = menuPocket2OpenPokemonMenu(item, 1);
  if (slot < 0) { menuPocket2ClosePokemonMenu(slot); return 1; }
  void* pokemon = nullptr; void* fp = nullptr;
  menuPocket2GetTemochiPokemon(slot, &pokemon, &fp);
  int32_t result = 1;
  if (pokemon != nullptr && pokemonIsDarkPokemon(pokemon) != 0) {
    Say(xd::hidden::kTextPatchShadow);
  } else if (pokemon == nullptr || xd::hidden::OfRow(pokemonBiosGetPokemonDataId(pokemon)) == 0) {
    Say(xd::hidden::kTextPatchNone);
  } else if (xd::hidden::Has(pokemon)) {
    Say(xd::hidden::kTextPatchAlready);
  } else {
    xd::hidden::Give(pokemon, true);
    std::fprintf(stderr, "[hidden] Ability Patch used: species row %u now has its hidden ability %d\n", pokemonBiosGetPokemonDataId(pokemon),
                 xd::hidden::OfRow(pokemonBiosGetPokemonDataId(pokemon)));
    Say(xd::hidden::kTextPatchDone);
    *consume = 1;
    result = 2;
  }
  menuPocket2ClosePokemonMenu(slot);
  return result;
}
}

extern "C" int32_t (*PortXD_HiddenItemUse(uint32_t item))(uint16_t, int*) {
  if (item == 0 || item != xd::forms::PatchItem() || !xd::hidden::Enabled()) return nullptr;
  return UseAbilityPatch;
}
