#include <cstdint>

extern "C" {

int PortXD_MemoAisyouRow(int32_t spriteId, uint16_t* type) {
  if (spriteId != 2000) return 0;
  *type = 18;
  return 1;
}

void* PortXD_get_exp_fight_pokemon_ptr = nullptr;

extern void* pokemon_nakigoe_data[];
extern uint32_t pokemon_nakigoe_data_number;
signed char* pokemonNakigoeDataBiosGetDataAddress(void* data) {
  if (data == nullptr) return nullptr;
  uint16_t index = *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(data) + 0xE);
  if (index >= pokemon_nakigoe_data_number) return nullptr;
  return static_cast<signed char*>(pokemon_nakigoe_data[index]);
}
}
