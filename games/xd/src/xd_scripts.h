#ifndef XD_SCRIPTS_H
#define XD_SCRIPTS_H

#include <cstdint>
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
std::string LoadProblems();
void NotifyReplayKept(const char* label, uint32_t battle, int won);

}

#endif
