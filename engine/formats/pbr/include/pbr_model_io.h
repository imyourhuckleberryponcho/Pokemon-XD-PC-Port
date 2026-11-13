#pragma once
#include <string>

#include "pbr_model.h"

namespace pbr {
Model model_parse(const std::string& text);
Model model_load(const std::string& path);
std::string model_save(const Model& m);
}
