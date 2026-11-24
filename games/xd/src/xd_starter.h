#ifndef XD_STARTER_H
#define XD_STARTER_H

#include "xd_randomizer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xd::starter {

void SetPlan(const xd::randomizer::StarterPlan& plan);
const xd::randomizer::StarterPlan& Plan();
bool Randomized();

std::string SpeciesName(uint32_t species);

void Choose(int index);

inline constexpr const char* kOptChoose = "starter_choose=on";
inline constexpr const char* kOptTwo = "starter_two=on";
inline constexpr bool kLegendaryAllowed = true;
inline constexpr uint32_t kSecondLevel = 10;

struct Candidate {
  uint16_t index = 0;
  uint16_t national = 0;
  uint8_t gen = 3;
  bool legendary = false;
  std::string name;
  uint8_t types[2] = {0, 0};
  std::string typeNames[2];
  uint16_t base[6] = {0, 0, 0, 0, 0, 0};
};
const std::vector<Candidate>& Candidates();
void InvalidateCandidates();
bool IsLegendary(int national);

std::vector<std::string> ChoiceOptions(bool choose, bool two);
bool ChooseWanted(const std::vector<std::string>& options);
bool TwoWanted(const std::vector<std::string>& options);
bool ChooseOn(const std::vector<std::string>& options);
bool TwoOn(const std::vector<std::string>& options);
std::vector<std::string> NamesOf(const std::vector<std::string>& options);
void Answer(uint16_t first, uint16_t second);
std::vector<std::string> TestOptions();
uint16_t TestPick(int slot);

}

extern "C" void PortXD_NewGameCreate(void* save, uint16_t* name);
bool PortXD_OpenStarterPick(void);
bool PortXD_OpenStarterChoose(int two);

#endif
