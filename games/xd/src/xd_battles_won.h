#ifndef XD_BATTLES_WON_H
#define XD_BATTLES_WON_H

#include <cstdint>
#include <vector>

namespace xd::battles {

void Frame();

bool Won(unsigned battle);

bool Complete();

std::vector<uint32_t> List();

unsigned Changes();

void Set(const std::vector<uint32_t>& list, bool complete, const char* why);

std::vector<uint32_t> SinceStart();

}

#endif
