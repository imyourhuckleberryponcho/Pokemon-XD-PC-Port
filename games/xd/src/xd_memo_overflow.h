#ifndef XD_MEMO_OVERFLOW_H
#define XD_MEMO_OVERFLOW_H

#include <cstdint>
#include <vector>

namespace xd::memo {

void RegisterSaveExt();

struct Entry {
  uint16_t id = 0;
  uint32_t trainerRnd = 0;
  uint32_t rnd = 0;
};
const std::vector<Entry>& Overflow();
void ClearOverflow();
int TestFill();

}

#endif
