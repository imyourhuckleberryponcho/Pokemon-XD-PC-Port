#ifndef XD_GEN4_MACHINES_H
#define XD_GEN4_MACHINES_H

#include <mods.h>

#include <filesystem>

namespace xd::gen4::machines {

void Clear();
bool Load(const std::filesystem::path& pack);
void Resolve(const engine::mods::LoadResult& result);
bool Active();

}

#endif
