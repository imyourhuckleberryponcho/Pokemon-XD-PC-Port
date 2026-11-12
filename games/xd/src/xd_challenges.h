#ifndef XD_CHALLENGES_H
#define XD_CHALLENGES_H

#include <string>
#include <vector>

namespace xd::challenges {

struct Rule {
  const char* id;
  const char* label;
  const char* info;
};

const std::vector<Rule>& Rules();
const Rule* Find(const std::string& id);

std::vector<std::string> TestSwitch();

void SetActive(const std::vector<std::string>& ids, const char* why);
bool Active(const char* id);
bool AnyActive();

bool RefusePress(const void* activeWindow, unsigned newButtons, std::string& message);

void Frame();

std::vector<std::string> TakeLog();

std::vector<std::string> State();
void SetState(const std::vector<std::string>& lines, const char* why);

int LevelCapAt(unsigned story, unsigned* bossBattle);
int BattleMaxLevel(unsigned battle, std::string* who);
void LogLevelCaps();

}

#endif
