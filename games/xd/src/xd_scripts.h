#ifndef XD_SCRIPTS_H
#define XD_SCRIPTS_H

#include <string>

namespace engine::mods {
struct LoadResult;
}
namespace engine::script {
class Names;
}

namespace xd::scripts {

engine::script::Names BuildNames(const engine::mods::LoadResult& result);
void Install(const engine::mods::LoadResult& result);
std::string ActiveText();

}

#endif
