#ifndef XD_SHADOW_CAPACITY_H
#define XD_SHADOW_CAPACITY_H

#include "xd_randomizer.h"

namespace xd::shadowcap {

bool Get(int pool, xd::randomizer::MoreShadowsCapacity& out);
int RealCount(int pool, int percent);

}

extern "C" void PortXD_ReadShadowCapacity(void);

#endif
