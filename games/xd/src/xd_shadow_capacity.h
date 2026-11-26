#ifndef XD_SHADOW_CAPACITY_H
#define XD_SHADOW_CAPACITY_H

#include "xd_randomizer.h"

namespace xd::shadowcap {

bool Get(int pool, xd::randomizer::MoreShadowsCapacity& out);
int RealCountOf(const xd::randomizer::MoreShadowsCapacity& cap, int percent);
int RealCount(int pool, int percent);
bool ForCode(const std::string& code, int pool, xd::randomizer::MoreShadowsCapacity& out);

}

extern "C" void PortXD_ReadShadowCapacity(void);

#endif
