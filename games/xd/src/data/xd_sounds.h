#ifndef XD_SOUNDS_H
#define XD_SOUNDS_H

#include <mods.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::sound {

void BuildSounds(engine::mods::RowsJob& job);

long SongNumber(const std::string& name);

void SetTaken(uint32_t sndFiles, uint32_t sndData);

}

#endif
