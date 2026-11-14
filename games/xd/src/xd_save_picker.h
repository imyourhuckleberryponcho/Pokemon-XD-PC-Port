#ifndef XD_SAVE_PICKER_H
#define XD_SAVE_PICKER_H

#include <dolphin/pad.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::picker {

enum class Intent { None, Title, NewGame, Continue };
const char* IntentName(Intent intent);
Intent Current();
void ReadIntent();
void SetForRestart(Intent intent);
bool SkipLogos();
void RegisterSkipLogosQuery(bool (*query)());
void Drive(PADStatus* status, int menuId, bool titleOpen, int select);
void Done(const char* why);
bool WantsNewGameNow();
bool WantsContinueNow();

struct Row {
  std::string id;
  std::string name;
  std::filesystem::path root;
  bool active = false;
  std::string key;
  std::string trainer;
  std::string playTime;
  double seconds = 0;
  std::string rules;
  std::string rulesFull;
  std::filesystem::path thumbnail;
};
std::vector<Row> Rows();
bool Possible();
void DecideStartUp();
bool AnsweredThisRun();
void SetAnswered(bool b);
bool SwitchTo(const Row& row);

void ThumbnailAfterSave(const std::filesystem::path& file);
void ThumbnailFrame();
struct Picture {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};
Picture LoadThumbnail(const std::filesystem::path& file);

}

#endif
