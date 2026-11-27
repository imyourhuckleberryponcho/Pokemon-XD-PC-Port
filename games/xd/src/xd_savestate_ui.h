#ifndef XD_SAVESTATE_UI_H
#define XD_SAVESTATE_UI_H

#include <filesystem>
#include <string>

namespace xd::savestate_ui {

constexpr int kSlots = 4;

struct SlotInfo {
  bool used = false;
  std::string when;
  std::string place;
  std::filesystem::path picture;
};

SlotInfo Info(int slot);
void Request(int signedSlot);
bool Offered();
std::string Unavailable();

}

#endif
