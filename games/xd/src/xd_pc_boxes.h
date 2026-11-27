#ifndef XD_PC_BOXES_H
#define XD_PC_BOXES_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xd::pcboxes {

constexpr int kGameBoxes = 8;
constexpr int kMaxBoxes = 32;
constexpr int kSlots = 30;
constexpr size_t kPokemonBytes = 0xC4;
constexpr size_t kNameChars = 10;
constexpr size_t kBoxBytes = kNameChars * 2 + kSlots * kPokemonBytes;

struct Box {
  uint16_t name[kNameChars];
  uint8_t pokemon[kSlots][kPokemonBytes];
};

int Clamp(int boxes);

void Configure(int fromSetup);
int Count();
bool Raise(int boxes);

Box* ExtraBox(int b);
int LastView();
void SetLastView(int box);

std::vector<uint8_t> SaveSection();
bool LoadSection(const uint8_t* data, size_t size, uint16_t version);
void ResetSection();
void Forget();

using NameFn = void (*)(uint16_t* dst, int box1Based);
using ClearFn = void (*)(uint8_t* pokemon, int count);
void SetInitFunctions(NameFn name, ClearFn clear);

void Install();

}

#endif
