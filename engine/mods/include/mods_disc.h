#ifndef ENGINE_MODS_DISC_H
#define ENGINE_MODS_DISC_H

#include "mods.h"

namespace engine::mods {

DiscView OpenDiscView();

bool InstallOverlay(const LoadResult& result);

}

#endif
