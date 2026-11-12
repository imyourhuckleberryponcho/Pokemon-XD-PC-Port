#include <xd_save_write.h>

#include <cstring>

namespace xd::save {

void Write16(uint8_t* p, uint16_t v, ByteOrder order) {
  if (order == ByteOrder::GameCube) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
  } else {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
}

void Write32(uint8_t* p, uint32_t v, ByteOrder order) {
  for (int i = 0; i < 4; i++) {
    p[i] = static_cast<uint8_t>(order == ByteOrder::GameCube ? v >> (24 - 8 * i) : v >> (8 * i));
  }
}

size_t SectionOffsetInSlot(const Save& save, int sectionId) {
  for (const Section& s : save.sections) {
    if (s.id == sectionId) {
      return kSlotHeaderSize + s.offset;
    }
  }
  return 0;
}

std::vector<uint8_t> WriteSaveFile(const std::vector<uint8_t>& file, const Save& save, const ReadReport& report,
                                   bool countAsSave, std::string& error) {
  if (file.size() != kFileSize || save.slotBytes.size() != kSlotSize || save.slot < 0) {
    error = "internal: the save to write isn't complete";
    return {};
  }
  const size_t game = SectionOffsetInSlot(save, 1);
  if (game == 0) {
    error = "the save has no game-data section";
    return {};
  }
  int32_t highest = 0;
  for (const SlotReport& r : report.slots) {
    if (r.written && r.saveCount > highest) {
      highest = r.saveCount;
    }
  }
  if (highest == INT32_MAX) {
    error = "the save counter is at its maximum";
    return {};
  }
  const int32_t counter = highest + 1;
  const int newSlot = (save.slot + 1) % kSlotCount;
  const ByteOrder order = save.order;

  std::vector<uint8_t> slot = save.slotBytes;
  slot[0] = 1;
  slot[1] = 1;
  slot[2] = 1;
  slot[3] = 0;
  Write32(slot.data() + 4, static_cast<uint32_t>(counter), order);
  uint8_t* data = slot.data() + kSlotHeaderSize;
  uint8_t* g = slot.data() + game;
  if (countAsSave) {
    Write32(g + 0x10, Read32(g + 0x10, order) + 1, order);
  }
  uint32_t headerSum = 0;
  for (int i = 0; i < 8; i++) {
    headerSum += slot[i];
  }
  Write32(g + 0x38, headerSum, order);
  EncryptSaveData(data, order);

  std::vector<uint8_t> out = file;
  std::memcpy(out.data() + SlotOffset(newSlot), slot.data(), kSlotSize);
  return out;
}

}
