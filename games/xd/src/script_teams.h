#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xd::teams {

struct Member {
  uint16_t species = 0;
  uint8_t level = 50;
  uint16_t item = 0;
  uint16_t moves[4] = {0, 0, 0, 0};
  uint8_t nature = 0;
  uint8_t ability = 0;
  uint8_t friendship = 0;
  uint8_t iv[6] = {15, 15, 15, 15, 15, 15};
  uint8_t ev[6] = {0, 0, 0, 0, 0, 0};
};

struct Team {
  std::vector<Member> members;
  bool hasAiItems = false;
  uint16_t aiItems[4] = {0, 0, 0, 0};
};

std::string Check(const Team& team);
std::string SetFoeTeam(int slot, const Team& team);
std::string LendTeam(const Team& team);
void Clear();
bool Armed();
bool LendInUse();

}
