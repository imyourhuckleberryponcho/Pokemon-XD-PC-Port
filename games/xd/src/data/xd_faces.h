#ifndef XD_FACES_H
#define XD_FACES_H

#include <mods.h>
#include <save_thumbnail.h>

#include <cstdint>
#include <vector>

namespace xd::faces {

void BuildFaces(engine::mods::RowsJob& job);

std::vector<uint8_t> TextureFromPicture(const std::vector<uint8_t>& templateFile, const engine::memcard::thumbnail::Picture& picture);
bool DecodeTexture(const std::vector<uint8_t>& file, engine::memcard::thumbnail::Picture& out);

}

#endif
