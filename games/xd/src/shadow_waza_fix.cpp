#include <cstdint>
#include <cstring>

struct Pokemon;
struct PokemonWaza;
extern "C" PokemonWaza* pokemonBiosGetPokemonWazaPtr(Pokemon* poke, uint32_t wazaNum, uint8_t unk);
extern "C" PokemonWaza* PortXD_ShadowFixWazaPtr(Pokemon* poke, uint32_t wazaNum, uint8_t unk) {
  PokemonWaza* w = pokemonBiosGetPokemonWazaPtr(poke, wazaNum, unk);
  if (w) {
    uint32_t raw;
    std::memcpy(&raw, static_cast<void*>(w), 4);
    const uint32_t mid = (raw >> 16) & 0xFF;
    if ((raw & 0xFF00FFFFu) == 0x01000500u && mid >= 0x64 && mid <= 0x76) {
      const uint8_t fixed[4] = {static_cast<uint8_t>(mid), 0x01, 0x05, 0x00};
      std::memcpy(static_cast<void*>(w), fixed, 4);
    }
  }
  return w;
}
