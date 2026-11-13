#ifndef XD_SCRIPTS_H
#define XD_SCRIPTS_H

#include <string>

namespace engine::mods {
struct LoadResult;
}

namespace xd::scripts {

void Install(const engine::mods::LoadResult& result);
std::string ActiveText();

}

#endif
