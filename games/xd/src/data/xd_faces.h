#ifndef XD_FACES_H
#define XD_FACES_H

#include <mods.h>
#include <save_thumbnail.h>

#include <cstdint>
#include <vector>

namespace xd::faces {

void BuildFaces(engine::mods::RowsJob& job);
void BuildBoxIcons(engine::mods::RowsJob& job);
void SetFaceRowsTaken(uint32_t count);

void SetFaceRowsTaken(uint32_t count);

std::vector<uint8_t> TextureFromPicture(const std::vector<uint8_t>& templateFile, const engine::memcard::thumbnail::Picture& picture);
bool DecodeTexture(const std::vector<uint8_t>& file, engine::memcard::thumbnail::Picture& out);

}

#endif
