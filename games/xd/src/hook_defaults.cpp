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

uint16_t fightTrainer_GetFightTrainerDataId(void* trainer);
uint16_t fightTrainerDB_GetAiDataId(uint16_t id);
uint8_t fightTrainerAiDB_GetZokuseiCheckFlag(uint16_t id);
uint8_t fightSeqGetTextureZokuseiAry(void* pokemon, int32_t* ary);
int fightTrainerAiCheckTextureZokusei(void* trainer, void* pokemon) {
  int32_t data[4];
  if (fightTrainerAiDB_GetZokuseiCheckFlag(fightTrainerDB_GetAiDataId(fightTrainer_GetFightTrainerDataId(trainer))) == 1) {
    return fightSeqGetTextureZokuseiAry(pokemon, data) >= 1;
  }
  return 1;
}
}
