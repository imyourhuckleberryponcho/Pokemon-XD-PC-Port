#include <cstdint>

extern "C" {

int PortXD_MemoAisyouRow(int32_t spriteId, uint16_t* type) {
  if (spriteId != 2000) return 0;
  *type = 18;
  return 1;
}

void* PortXD_get_exp_fight_pokemon_ptr = nullptr;
}
