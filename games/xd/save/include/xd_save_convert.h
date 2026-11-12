#ifndef XD_SAVE_CONVERT_H
#define XD_SAVE_CONVERT_H

#include <xd_save.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::save {

constexpr ByteOrder Other(ByteOrder o) { return o == ByteOrder::GameCube ? ByteOrder::PC : ByteOrder::GameCube; }

bool SwapSaveData(uint8_t* data, ByteOrder from, std::string* error);

enum class SlotResult {
  Unwritten,
  Empty,
  Converted,
  AlreadyTarget,
  Invalid,
};
const char* SlotResultName(SlotResult r);

SlotResult ConvertSlot(uint8_t* slot, ByteOrder to, std::string* error = nullptr);

bool SlotValid(const uint8_t* slot, ByteOrder order);

bool ConvertBanner(uint8_t* file, ByteOrder to);

struct ConvertReport {
  bool bannerOk = false;
  SlotResult slots[kSlotCount] = {SlotResult::Unwritten, SlotResult::Unwritten};
  std::vector<std::string> warnings;
};

bool ConvertFile(std::vector<uint8_t>& file, ByteOrder to, ConvertReport& report, std::string& error);

std::vector<uint8_t> GameViewOf(const std::vector<uint8_t>& stored);

void ApplyGameWrite(std::vector<uint8_t>& stored, size_t offset, const uint8_t* bytes, size_t length,
                    std::vector<std::pair<size_t, size_t>>& changed);

}

#endif
