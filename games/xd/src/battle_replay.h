#ifndef XD_BATTLE_REPLAY_H
#define XD_BATTLE_REPLAY_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xd::replay {

bool Prepare();

bool Watching();
bool KeysScripted();

struct PadSlot;
void PadPoll(void* statusArray, bool titleOpen);

void HandleEvent(const void* sdlEvent);

struct BarView {
  int turn = 1, turns = 0;
  float speed = 1.f;
  bool paused = false, camera = false, finished = false, full = true;
};
void BarFrame();
bool BarGet(BarView* out);
void DrawBar(const BarView& view);

void HookBattles();

uint8_t SelectAction(void* trainer, uint32_t fightType, uint8_t (*real)(void*, uint32_t));
int16_t SelectIrekae(void* trainer, uint32_t a, uint32_t b, uintptr_t c, int16_t (*real)(void*, uint32_t, uint32_t, uintptr_t));

bool SplitOn();
void NoteTurn(uint32_t turn);
void LogicDraw();
void CosmeticDrawn();

void OnlineBegin(bool host, const std::string& rulesText, const std::vector<std::string>& hostTeamHex, const std::vector<std::string>& guestTeamHex, uint32_t seed);
void OnlineRulesKey(const std::string& key);
void OnlineExtras(int protocol, const std::string& previewText);
void OnlineParty(int hostCount, int guestCount);
void OnlineSeed(uint32_t seq, uint32_t seed);
void OnlineSwitchSeed(uint32_t seq, uint32_t seed);
void OnlineChoice(uint32_t seq, const std::string& line);
void OnlineSwitch(uint32_t seq, int result);
void OnlineTurn(uint32_t turn, uint64_t digest);
void OnlineEnd(uint32_t result, bool desync);

void TurnEnd();
void BattleEnd(uint32_t result);

std::string DescribeFile(const std::string& path, std::string* why);

}

#endif
