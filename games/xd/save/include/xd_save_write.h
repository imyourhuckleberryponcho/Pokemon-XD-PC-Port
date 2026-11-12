#ifndef XD_SAVE_WRITE_H
#define XD_SAVE_WRITE_H

#include <xd_save.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::save {

size_t SectionOffsetInSlot(const Save& save, int sectionId);

std::vector<uint8_t> WriteSaveFile(const std::vector<uint8_t>& file, const Save& save, const ReadReport& report,
                                   bool countAsSave, std::string& error);

void Write16(uint8_t* p, uint16_t v, ByteOrder order);
void Write32(uint8_t* p, uint32_t v, ByteOrder order);

}

#endif
