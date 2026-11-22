#pragma once
#include <mods.h>

#include <string>
#include <string_view>
#include <vector>

namespace xd::common_script {
void Install(const engine::mods::LoadResult& result, const std::vector<engine::mods::ModFolder>& found, std::string_view gameId, std::vector<std::string>& messages);
bool Active();
}
