#pragma once
#include <cstdint>
#include <vector>

#include "pbr_model.h"

namespace pbr {

struct WriteReport {
  long long pobj = 0, reflect_pieces = 0, most_envelopes = 0, display_list_bytes = 0, track_bytes = 0, texture_bytes = 0, archive_bytes = 0;
};

std::vector<uint8_t> hsd_write_archive(const Model& model, WriteReport* report = nullptr);

std::vector<uint32_t> hsd_joint_flags(const Model& model);

}
